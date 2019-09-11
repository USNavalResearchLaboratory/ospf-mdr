/* -*-  c-file-style: "gnu" -*- */

/*
 * Copyright (c) 2011 The Boeing Company
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <stdbool.h>

#include "zebra.h"

#include "thread.h"
#include "linklist.h"
#include "memory.h"
#include "command.h"

#include "ospf6_af.h"
#include "ospf6_area.h"
#include "ospf6_lsa.h"
#include "ospf6_lsdb.h"
#include "ospf6_proto.h"
#include "ospf6_route.h"
#include "ospf6_intra.h"
#include "ospf6_spf.h"
#include "ospf6d.h"

typedef enum {
  UNIDIRECTIONAL = 1,
  BIDIRECTIONAL,
} linklog_type_t;

struct ospf6_sdt_linklog {
  unsigned int interval;
  char filename[PATH_MAX];
  FILE *file;
  linklog_type_t linktype;
  bool connected;
};

struct ospf6_sdt_pathlog {
  unsigned int interval;
  char filename[PATH_MAX];
  FILE *file;
  u_int32_t src_router_id;
  struct prefix dst_prefix;
  bool connected;
};

struct ospf6_sdt_area {
  struct thread *linklog_thread;
  struct ospf6_sdt_linklog llog;

  struct thread *pathlog_thread;
  struct ospf6_sdt_pathlog plog;
};

static unsigned int sdt_area_data_id;

static int
ospf6_sdt_area_timestampstr (char *buf, size_t bufsize)
{
  struct timeval tv;
  struct tm tm;
  size_t len;

  if (gettimeofday (&tv, NULL))
    {
      zlog_warn ("%s: gettimeofday() failed", __func__);
      return -1;
    }

  if (gmtime_r (&tv.tv_sec, &tm) == NULL)
    {
      zlog_warn ("%s: gmtime_r() failed", __func__);
      return -1;
    }

  len = strftime (buf, bufsize, "%T", &tm);
  if (len == 0)
    {
      zlog_warn ("%s: timestampstr() failed", __func__);
      return -1;
    }

  if (bufsize - len == 1)
    {
      zlog_warn ("%s: time stamp string exceeds bufsize", __func__);
      return -1;
    }

  buf += len;
  bufsize -= len;

  if (snprintf (buf, bufsize, ".%06ld", tv.tv_usec) >= (int) bufsize)
    {
      zlog_warn ("%s: snprintf() failed", __func__);
      return -1;
    }

  return 0;
}

static bool
ospf6_sdt_connected (struct ospf6_area *oa, u_int32_t router_id)
{
  struct prefix prefix;
  struct ospf6_route *route;

  /* check if connected to given source router */
  ospf6_linkstate_prefix (router_id, htonl (0), &prefix);
  route = ospf6_route_lookup (&prefix, oa->spf_table);

  return route ? true : false;
}

static void
ospf6_sdt_loglink (FILE *file, u_int32_t rid1, u_int32_t rid2)
{
  char r1str[INET_ADDRSTRLEN], r2str[INET_ADDRSTRLEN];

  ospf6_id2str (rid1, r1str, sizeof (r1str));
  ospf6_id2str (rid2, r2str, sizeof (r2str));

  fprintf (file, "%s -> %s\n", r1str, r2str);
}

static bool
ospf6_sdt_linklist_lookup (u_int32_t adv_router_id,
			   u_int32_t neighbor_router_id,
			   struct list *tmplist)
{
  struct listnode *node;
  struct {
    u_int32_t adv_router_id;
    u_int32_t neighbor_router_id;
  } *tmp;

  for (ALL_LIST_ELEMENTS_RO (tmplist, node, tmp))
    {
      if (tmp->adv_router_id == neighbor_router_id &&
	  tmp->neighbor_router_id == adv_router_id)
	break;
    }

  if (node)
    {
      XFREE (MTYPE_TMP, tmp);
      list_delete_node (tmplist, node);

      return true;
    }

  tmp = XMALLOC (MTYPE_TMP, sizeof (*tmp));
  tmp->adv_router_id = adv_router_id;
  tmp->neighbor_router_id = neighbor_router_id;
  listnode_add (tmplist, tmp);

  return false;
}

static void
ospf6_sdt_loglink_routerid (u_int32_t adv_router_id,
			    u_int32_t neighbor_router_id,
			    FILE *file, linklog_type_t linktype,
			    struct list *tmplist)
{
  bool loglink;

  switch (linktype)
    {
    case UNIDIRECTIONAL:
      loglink = true;
      break;

    case BIDIRECTIONAL:
      loglink = ospf6_sdt_linklist_lookup (adv_router_id,
					   neighbor_router_id, tmplist);
      break;

    default:
      assert (0);
      break;
    }

  if (loglink)
    ospf6_sdt_loglink (file, adv_router_id, neighbor_router_id);
}

static void
ospf6_sdt_loglink_process_routerlsa (struct ospf6_lsa *lsa, FILE *file,
				     linklog_type_t linktype,
				     struct list *tmplist)
{
  u_int32_t adv_router_id;
  void *end;
  struct ospf6_router_lsdesc *lsdesc;

  assert (OSPF6_LSA_IS_TYPE (ROUTER, lsa));
  if (linktype == BIDIRECTIONAL)
    assert (tmplist);

  adv_router_id = lsa->header->adv_router;

  lsdesc = (struct ospf6_router_lsdesc *)
    (OSPF6_LSA_HEADER_END (lsa->header) + sizeof (struct ospf6_router_lsa));
  end = OSPF6_LSA_END (lsa->header);

  while (end >= (void *) (lsdesc + 1))
    {
      u_int32_t neighbor_router_id = lsdesc->neighbor_router_id;

      if (neighbor_router_id != adv_router_id)
	ospf6_sdt_loglink_routerid (adv_router_id, neighbor_router_id,
				    file, linktype, tmplist);

      lsdesc++;
    }
}

static void
ospf6_sdt_loglink_process_networklsa (struct ospf6_lsa *lsa, FILE *file,
				      linklog_type_t linktype,
				      struct list *tmplist)
{
  u_int32_t adv_router_id;
  void *end;
  struct ospf6_network_lsdesc *lsdesc;

  assert (OSPF6_LSA_IS_TYPE (NETWORK, lsa));
  if (linktype == BIDIRECTIONAL)
    assert (tmplist);

  adv_router_id = lsa->header->adv_router;

  lsdesc = (struct ospf6_network_lsdesc *)
    (OSPF6_LSA_HEADER_END (lsa->header) + sizeof (struct ospf6_network_lsa));
  end = OSPF6_LSA_END (lsa->header);

  while (end >= (void *) (lsdesc + 1))
    {
      u_int32_t router_id = lsdesc->router_id;

      if (router_id != adv_router_id)
	ospf6_sdt_loglink_routerid (adv_router_id, router_id,
				    file, linktype, tmplist);

      lsdesc++;
    }
}

static int
ospf6_sdt_area_linklog (struct ospf6_area *oa, struct ospf6_sdt_linklog *llog)
{
  char timestr[16];
  u_int16_t type;
  struct ospf6_lsa *lsa;
  struct list *tmplist = NULL;

  if (!llog->file)
    {
      zlog_warn ("%s: no linklog output file", __func__);
      return -1;
    }

  if (ospf6_sdt_area_timestampstr (timestr, sizeof (timestr)))
    return -1;

  if (llog->linktype == BIDIRECTIONAL)
    tmplist = list_new ();

  fprintf (llog->file, "Routing-Links List: %s\n", timestr);

  /* for all network-LSAs, add a link from the DR to every router
     included in the LSA */
  type = ntohs (OSPF6_LSTYPE_NETWORK);
  for (lsa = ospf6_lsdb_type_head (type, oa->lsdb);
       lsa; lsa = ospf6_lsdb_type_next (type, lsa))
    {
      if (OSPF6_LSA_IS_MAXAGE (lsa))
	continue;

      if (llog->connected &&
	  !ospf6_sdt_connected (oa, lsa->header->adv_router))
	continue;

      ospf6_sdt_loglink_process_networklsa (lsa, llog->file,
					    llog->linktype, tmplist);
    }

  type = ntohs (OSPF6_LSTYPE_ROUTER);
  for (lsa = ospf6_lsdb_type_head (type, oa->lsdb);
       lsa; lsa = ospf6_lsdb_type_next (type, lsa))
    {
      if (OSPF6_LSA_IS_MAXAGE (lsa))
	continue;

      if (llog->connected &&
	  !ospf6_sdt_connected (oa, lsa->header->adv_router))
	continue;

      ospf6_sdt_loglink_process_routerlsa (lsa, llog->file,
					   llog->linktype, tmplist);
    }

  fprintf (llog->file, "End of Routing-Links List.\n");
  fflush (llog->file);

  if (tmplist)
    {
      struct listnode *node;
      void *data;

      for (ALL_LIST_ELEMENTS_RO (tmplist, node, data))
	XFREE (MTYPE_TMP, data);

      list_delete (tmplist);
    }

  return 0;
}

static int
ospf6_sdt_area_linklog_timer (struct thread *thread)
{
  struct ospf6_area *oa;
  struct ospf6_sdt_area *sdt;

  oa = THREAD_ARG (thread);
  assert (oa);

  sdt = ospf6_area_get_data (oa, sdt_area_data_id);
  assert (sdt);

  ospf6_sdt_area_linklog (oa, &sdt->llog);

  sdt->linklog_thread =
    thread_add_timer (master, ospf6_sdt_area_linklog_timer,
		      oa, sdt->llog.interval);

  return 0;
}

static void
ospf6_sdt_area_start_linklog (struct ospf6_area *oa, unsigned int interval,
			      const char *filename, FILE *file,
			      linklog_type_t linktype, bool connected)
{
  struct ospf6_sdt_area *sdt;

  sdt = ospf6_area_get_data (oa, sdt_area_data_id);
  assert (sdt);

  sdt->llog.interval = interval;
  strncpy(sdt->llog.filename, filename, sizeof (sdt->llog.filename));
  sdt->llog.filename[sizeof (sdt->llog.filename) - 1] = '\0';
  sdt->llog.file = file;
  sdt->llog.linktype = linktype;
  sdt->llog.connected = connected;

  THREAD_TIMER_ON (master, sdt->linklog_thread,
		   ospf6_sdt_area_linklog_timer, oa, 0);

  return;
}

static void
ospf6_sdt_area_stop_linklog (struct ospf6_sdt_area *sdt)
{
  if (sdt->linklog_thread)
    THREAD_TIMER_OFF (sdt->linklog_thread);

  sdt->llog.interval = 0;
  sdt->llog.filename[0] = '\0';
  if (sdt->llog.file)
    {
      fclose (sdt->llog.file);
      sdt->llog.file = NULL;
    }
  sdt->llog.linktype = 0;
  sdt->llog.connected = 0;

  return;
}

static FILE *
ospf6_sdt_open (struct vty *vty, const char *path)
{
  int fd;
  FILE *file;

  fd = open (path, O_RDWR | O_NONBLOCK | O_APPEND | O_CREAT, 0644);
  if (fd < 0)
    {
      vty_out (vty, "opening '%s' failed: %s%s", path, strerror (errno), VNL);
      return NULL;
    }

  file = fdopen (fd, "a");
  if (file == NULL)
    {
      vty_out (vty, "fdopen() failed: %s%s", strerror (errno), VNL);
      close (fd);
    }

  return file;
}

DEFUN (area_loglinks,
       area_loglinks_cmd,
       "area (A.B.C.D|<0-4294967295>) loglinks (unidirectional|bidirectional) "
       "to-file FILENAME interval <1-255> (all|connected)",
       "OSPF area parameters\n"
       OSPF6_AREAID_DOT_STR
       OSPF6_AREAID_VAL_STR
       "Enable logging links\n"
       "Unidirectional links (all links)\n"
       "Bidirectional links (only links with a known reverse link)\n"
       "Filename to log links to\n"
       "Filename\n"
       "Minimum time between logging links\n"
       "Seconds\n"
       "Log all links\n"
       "Only log links if a route exists to the advertising router\n")
{
  struct ospf6 *o;
  u_int32_t area_id;
  struct ospf6_area *oa;
  linklog_type_t linktype;
  FILE *file;
  unsigned int interval;
  bool connected;
  size_t arglen;
  struct ospf6_sdt_area *sdt;

  o = (struct ospf6 *) vty->index;
  ospf6_str2id (argv[0], &area_id);
  oa = ospf6_area_get (area_id, o);
  assert (oa);

  arglen = strlen (argv[1]);
  if (strncmp (argv[1], "unidirectional", arglen) == 0)
    linktype = UNIDIRECTIONAL;
  else if (strncmp (argv[1], "bidirectional", arglen) == 0)
    linktype = BIDIRECTIONAL;
  else
    {
      vty_out (vty, "%s: unknown link type: %s%s",
               __func__, argv[1],  VNL);
      return CMD_WARNING;
    }

  interval = strtol (argv[3], NULL, 10);

  arglen = strlen (argv[4]);
  if (strncmp (argv[4], "all", arglen) == 0)
    connected = false;
  else if (strncmp (argv[4], "connected", arglen) == 0)
    connected = true;
  else
    {
      vty_out (vty, "%s: unknown option: %s%s",
               __func__, argv[4],  VNL);
      return CMD_WARNING;
    }

  file = ospf6_sdt_open (vty, argv[2]);
  if (file == NULL)
    return CMD_WARNING;

  sdt = ospf6_area_get_data (oa, sdt_area_data_id);
  assert (sdt);
  ospf6_sdt_area_stop_linklog (sdt);
  ospf6_sdt_area_start_linklog (oa, interval, argv[2], file,
				linktype, connected);

  return CMD_SUCCESS;
}

DEFUN (no_area_loglinks,
       no_area_loglinks_cmd,
       "no area (A.B.C.D|<0-4294967295>) loglinks",
       NO_STR
       "OSPF area parameters\n"
       OSPF6_AREAID_DOT_STR
       OSPF6_AREAID_VAL_STR
       "disable logging links\n")
{
  struct ospf6 *o;
  u_int32_t area_id;
  struct ospf6_area *oa;
  struct ospf6_sdt_area *sdt;

  o = (struct ospf6 *) vty->index;
  ospf6_str2id (argv[0], &area_id);
  oa = ospf6_area_get (area_id, o);
  assert (oa);

  sdt = ospf6_area_get_data (oa, sdt_area_data_id);
  assert (sdt);
  ospf6_sdt_area_stop_linklog (sdt);

  return CMD_SUCCESS;
}

static int
ospf6_sdt_area_pathlog (struct ospf6_area *oa, struct ospf6_sdt_pathlog *plog)
{
  char timestr[16];
  bool logpath = true;
  struct ospf6_route *dstroute;

  if (!plog->file)
    {
      zlog_warn ("%s: no pathlog output file", __func__);
      return -1;
    }

  if (ospf6_sdt_area_timestampstr (timestr, sizeof (timestr)))
    return -1;

  fprintf (plog->file, "Routing-Links List: %s\n", timestr);

  if (plog->connected && !ospf6_sdt_connected (oa, plog->src_router_id))
    logpath = false;

  /* find route to the destination prefix */
  dstroute = ospf6_route_lookup_bestmatch (&plog->dst_prefix,
					   oa->ospf6->route_table);
  if (dstroute == NULL)
    {
      char buf[PREFIXSTRLEN];

      ospf6_prefix2str (oa->ospf6, &plog->dst_prefix,
			buf, sizeof (buf));
      zlog_warn ("%s: no route to destination prefix %s", __func__, buf);
      logpath = false;
    }

  if (logpath)
    {
      struct ospf6_route *route;
      struct ospf6_route_table *spf_table;
      unsigned char tmp_debug_ospf6_spf;
      struct prefix prefix;

      /* disable debugging and calculate spf tree from src_router_id */
      tmp_debug_ospf6_spf = conf_debug_ospf6_spf;
      conf_debug_ospf6_spf = 0;
      spf_table = OSPF6_ROUTE_TABLE_CREATE (NONE, SPF_RESULTS);
      ospf6_spf_calculation (plog->src_router_id, spf_table, oa);
      conf_debug_ospf6_spf = tmp_debug_ospf6_spf;

      /* see show_ipv6_ospf6_simulate_spf_tree_root_cmd */

      /* find the destination in the spf tree */
      ospf6_linkstate_prefix (dstroute->path.origin.adv_router,
			      htonl (0), &prefix);
      route = ospf6_route_lookup (&prefix, spf_table);
      if (route)
        {
	  struct ospf6_vertex *v;

	  /* print the path (traverse back to the root of the spf tree) */
	  for (v = (struct ospf6_vertex *) route->route_option;
	       v->parent; v = v->parent)
	    {
	      u_int32_t adv_router_id, neighbor_router_id;

	      adv_router_id = v->lsa->header->adv_router;
	      neighbor_router_id = v->parent->lsa->header->adv_router;

	      if (neighbor_router_id != adv_router_id)
		ospf6_sdt_loglink (plog->file, adv_router_id,
				   neighbor_router_id);
	    }
	}
      else
        {
	  zlog_err ("%s: no route found to destination in area %s",
		    __func__, oa->name);
	}

      ospf6_spf_table_finish (spf_table);
      ospf6_route_table_delete (spf_table);
    }

  fprintf (plog->file, "End of Routing-Links List.\n");
  fflush (plog->file);

  return 0;
}

static int
ospf6_sdt_area_pathlog_timer (struct thread *thread)
{
  struct ospf6_area *oa;
  struct ospf6_sdt_area *sdt;

  oa = THREAD_ARG (thread);
  assert (oa);

  sdt = ospf6_area_get_data (oa, sdt_area_data_id);
  assert (sdt);

  ospf6_sdt_area_pathlog (oa, &sdt->plog);

  sdt->pathlog_thread =
    thread_add_timer (master, ospf6_sdt_area_pathlog_timer,
		      oa, sdt->plog.interval);

  return 0;
}

static void
ospf6_sdt_area_start_pathlog (struct ospf6_area *oa, unsigned int interval,
			      const char *filename, FILE *file,
			      u_int32_t src_router_id,
			      struct prefix *dst_prefix, bool connected)
{
  struct ospf6_sdt_area *sdt;

  sdt = ospf6_area_get_data (oa, sdt_area_data_id);
  assert (sdt);

  sdt->plog.interval = interval;
  strncpy(sdt->plog.filename, filename, sizeof (sdt->plog.filename));
  sdt->plog.filename[sizeof (sdt->plog.filename) - 1] = '\0';
  sdt->plog.file = file;
  sdt->plog.src_router_id = src_router_id;
  sdt->plog.dst_prefix = *dst_prefix;
  sdt->plog.connected = connected;

  THREAD_TIMER_ON (master, sdt->pathlog_thread,
                   ospf6_sdt_area_pathlog_timer, oa, 0);

  return;
}

static void
ospf6_sdt_area_stop_pathlog (struct ospf6_sdt_area *sdt)
{
  if (sdt->pathlog_thread)
    THREAD_TIMER_OFF (sdt->pathlog_thread);

  sdt->plog.interval = 0;
  sdt->plog.filename[0] = '\0';
  if (sdt->plog.file)
    {
      fclose (sdt->plog.file);
      sdt->plog.file = NULL;
    }
  sdt->plog.src_router_id = 0;
  memset (&sdt->plog.dst_prefix, 0, sizeof (sdt->plog.dst_prefix));
  sdt->plog.connected = 0;

  return;
}

DEFUN (area_logpath,
       area_logpath_cmd,
       "area (A.B.C.D|<0-4294967295>) logpath from A.B.C.D to "
       "(A.B.C.D[/M]|X:X::X:X[/M]) to-file FILENAME interval <1-255> "
       "(always|connected)",
       "OSPF area parameters\n"
       OSPF6_AREAID_DOT_STR
       OSPF6_AREAID_VAL_STR
       "Enable logging path\n"
       "From source router-id\n"
       OSPF6_ROUTER_ID_STR
       "To destination address/prefix\n"
       "IPv4 Address/Prefix\n"
       "IPv6 Address/Prefix\n"
       "Filename to log path to\n"
       "Filename\n"
       "minimum time between logging path\n"
       "Seconds\n"
       "Always log path\n"
       "Only log path if a route exists to the source router\n")
{
  struct ospf6 *o;
  u_int32_t area_id;
  struct ospf6_area *oa;
  u_int32_t src_router_id;
  struct prefix dst_prefix;
  FILE *file;
  unsigned int interval;
  bool connected;
  size_t arglen;
  struct ospf6_sdt_area *sdt;

  o = (struct ospf6 *) vty->index;
  ospf6_str2id (argv[0], &area_id);
  oa = ospf6_area_get (area_id, o);
  assert (oa);

  if (ospf6_str2id (argv[1], &src_router_id))
    {
      vty_out (vty, "%s: invalid source router-id: '%s'%s",
	       __func__, argv[1], VNL);
      return CMD_WARNING;
    }

  if (!ospf6_str2prefix (o, argv[2], &dst_prefix))
    {
      vty_out (vty, "%s: invalid destination prefix: '%s'%s",
	       __func__, argv[2], VNL);
      return CMD_WARNING;
    }

  interval = strtol (argv[4], NULL, 10);

  arglen = strlen (argv[5]);
  if (strncmp (argv[5], "always", arglen) == 0)
    connected = false;
  else if (strncmp (argv[5], "connected", arglen) == 0)
    connected = true;
  else
    {
      vty_out (vty, "%s: unknown option: %s%s",
               __func__, argv[5],  VNL);
      return CMD_WARNING;
    }

  file = ospf6_sdt_open (vty, argv[3]);
  if (file == NULL)
    return CMD_WARNING;

  sdt = ospf6_area_get_data (oa, sdt_area_data_id);
  assert (sdt);
  ospf6_sdt_area_stop_pathlog (sdt);
  ospf6_sdt_area_start_pathlog (oa, interval, argv[3], file,
				src_router_id, &dst_prefix, connected);

  return CMD_SUCCESS;
}

DEFUN (no_area_logpath,
       no_area_logpath_cmd,
       "no area (A.B.C.D|<0-4294967295>) logpath",
       NO_STR
       "OSPF area parameters\n"
       OSPF6_AREAID_DOT_STR
       OSPF6_AREAID_VAL_STR
       "disable logging path\n")
{
  struct ospf6 *o;
  u_int32_t area_id;
  struct ospf6_area *oa;
  struct ospf6_sdt_area *sdt;

  o = (struct ospf6 *) vty->index;
  ospf6_str2id (argv[0], &area_id);
  oa = ospf6_area_get (area_id, o);
  assert (oa);

  sdt = ospf6_area_get_data (oa, sdt_area_data_id);
  assert (sdt);
  ospf6_sdt_area_stop_pathlog (sdt);

  return CMD_SUCCESS;
}

static void
ospf6_sdt_area_config_write (struct ospf6_area *oa, struct vty *vty)
{
  struct ospf6_sdt_area *sdt;

  sdt = ospf6_area_get_data (oa, sdt_area_data_id);
  assert (sdt);

  if (sdt->linklog_thread)
    {
      const char *dirstr, *connstr;

      switch (sdt->llog.linktype)
	{
	case UNIDIRECTIONAL:
	  dirstr = "unidirectional";
	  break;

	case BIDIRECTIONAL:
	  dirstr = "bidirectional";
	  break;

	default:
	  assert (0);
	  break;
	}

      if (sdt->llog.connected)
	connstr = "connected";
      else
	connstr = "all";

      vty_out (vty, " area %s loglinks %s to-file %s interval %u %s%s",
	       oa->name, dirstr, sdt->llog.filename,
	       sdt->llog.interval, connstr, VNL);
    }

  if (sdt->pathlog_thread)
    {
      char srcstr[INET_ADDRSTRLEN];
      char dststr[PREFIXSTRLEN];
      const char *connstr;

      ospf6_id2str (sdt->plog.src_router_id, srcstr, sizeof (srcstr));
      ospf6_prefix2str (oa->ospf6, &sdt->plog.dst_prefix,
			dststr, sizeof (dststr));

      if (sdt->llog.connected)
	connstr = "connected";
      else
	connstr = "always";

      vty_out (vty, " area %s logpath from %s to %s to-file %s "
	       "interval %u %s%s", oa->name, srcstr, dststr,
	       sdt->plog.filename, sdt->plog.interval, connstr, VNL);
    }
}

static int
ospf6_sdt_area_create (struct ospf6_area *oa)
{
  struct ospf6_sdt_area *sdt;
  int err;

  sdt = XCALLOC (MTYPE_OSPF6_AREA, sizeof (*sdt));

  err = ospf6_area_add_data (oa, &sdt_area_data_id, sdt);
  if (err)
    {
      XFREE (MTYPE_OSPF6_AREA, sdt);
      return err;
    }

  return 0;
}

static void
ospf6_sdt_area_delete (struct ospf6_area *oa)
{
  struct ospf6_sdt_area *sdt;

  sdt = ospf6_area_del_data (oa, sdt_area_data_id);
  if (sdt == NULL)
    return;

  ospf6_sdt_area_stop_linklog (sdt);
  ospf6_sdt_area_stop_pathlog (sdt);

  XFREE (MTYPE_OSPF6_AREA, sdt);
}

static void
ospf6_sdt_area_init (void)
{
  install_element (OSPF6_NODE, &area_loglinks_cmd);
  install_element (OSPF6_NODE, &no_area_loglinks_cmd);

  install_element (OSPF6_NODE, &area_logpath_cmd);
  install_element (OSPF6_NODE, &no_area_logpath_cmd);
}

static struct ospf6_area_operations ospf6_sdt_area_ops = {
  .init = ospf6_sdt_area_init,
  .create = ospf6_sdt_area_create,
  .delete = ospf6_sdt_area_delete,
  .config_write = ospf6_sdt_area_config_write,
};

OSPF6_AREA_OPERATIONS (ospf6_sdt_area_ops);
