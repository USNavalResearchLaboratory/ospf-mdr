/*
 * Copyright (C) 2003 Yasuhiro Ohara
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

#include <zebra.h>

#include "log.h"
#include "vty.h"
#include "command.h"
#include "prefix.h"
#include "stream.h"
#include "zclient.h"
#include "memory.h"

#include "ospf6_proto.h"
#include "ospf6_top.h"
#include "ospf6_interface.h"
#include "ospf6_route.h"
#include "ospf6_lsa.h"
#include "ospf6_lsdb.h"
#include "ospf6_asbr.h"
#include "ospf6_zebra.h"
#include "ospf6d.h"
#include "ospf6_neighbor.h"
#include "ospf6_intra.h"
#include "ospf6_area.h"
#include "ospf6_af.h"
#include "ospf6_zebra_linkmetrics.h"

unsigned char conf_debug_ospf6_zebra = 0;

/* information about zebra. */
struct zclient *zclient = NULL;

struct in_addr router_id_zebra;

/* Router-id update message from zebra. */
static int
ospf6_router_id_update_zebra (int command, struct zclient *zclient,
			      zebra_size_t length)
{
  struct prefix router_id;
  struct ospf6 *o = ospf6;

  zebra_router_id_update_read(zclient->ibuf,&router_id);
  router_id_zebra = router_id.u.prefix4;

  if (o == NULL)
    return 0;

  if (o->router_id  == 0)
    o->router_id = (u_int32_t) router_id_zebra.s_addr;

  return 0;
}

/* redistribute function */
void
ospf6_zebra_redistribute (int type)
{
  if (zclient->redist[type])
    return;
  zclient->redist[type] = 1;
  if (zclient->sock > 0)
    zebra_redistribute_send (ZEBRA_REDISTRIBUTE_ADD, zclient, type);
}

void
ospf6_zebra_no_redistribute (int type)
{
  if (! zclient->redist[type])
    return;
  zclient->redist[type] = 0;
  if (zclient->sock > 0)
    zebra_redistribute_send (ZEBRA_REDISTRIBUTE_DELETE, zclient, type);
}

/* Inteface addition message from zebra. */
static int
ospf6_zebra_if_add (int command, struct zclient *zclient, zebra_size_t length)
{
  struct interface *ifp;

  ifp = zebra_interface_add_read (zclient->ibuf);
  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    zlog_debug ("Zebra Interface add: %s index %d mtu %d",
		ifp->name, ifp->ifindex, ifp->mtu6);
  ospf6_interface_if_add (ifp);
  return 0;
}

static int
ospf6_zebra_if_del (int command, struct zclient *zclient, zebra_size_t length)
{
  struct interface *ifp;

  if (!(ifp = zebra_interface_state_read(zclient->ibuf)))
    return 0;

  if (if_is_operative (ifp))
    zlog_warn ("Zebra: got delete of %s, but interface is still up", ifp->name);

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    zlog_debug ("Zebra Interface delete: %s index %d mtu %d",
		ifp->name, ifp->ifindex, ifp->mtu6);

  ospf6_interface_if_del (ifp);

  ifp->ifindex = IFINDEX_INTERNAL;

  if (if_is_transient (ifp))
    if_delete (ifp);

  return 0;
}

static int
ospf6_zebra_if_state_update (int command, struct zclient *zclient,
                             zebra_size_t length)
{
  struct interface *ifp;

  ifp = zebra_interface_state_read (zclient->ibuf);
  if (ifp == NULL)
    return 0;
  
  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    zlog_debug ("Zebra Interface state change: "
                "%s index %d flags %llx metric %d mtu %d",
		ifp->name, ifp->ifindex, (unsigned long long)ifp->flags, 
		ifp->metric, ifp->mtu6);

  ospf6_interface_state_update (ifp);
  return 0;
}

static int
ospf6_zebra_if_address_update_add (int command, struct zclient *zclient,
                                   zebra_size_t length)
{
  struct connected *c;
  char buf[128];

  c = zebra_interface_address_read (ZEBRA_INTERFACE_ADDRESS_ADD, zclient->ibuf);
  if (c == NULL)
    return 0;

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    zlog_debug ("Zebra Interface address add: %s %5s %s/%d",
		c->ifp->name, prefix_family_str (c->address),
		inet_ntop (c->address->family, &c->address->u.prefix,
			   buf, sizeof (buf)), c->address->prefixlen);

  if (c->address->family == AF_INET6 || c->address->family == AF_INET)
    {
      struct ospf6_interface *oi;
      bool before, after;

      oi = c->ifp->info;
      before = oi && oi->area && ospf6_interface_has_linklocal_addr (oi);

      ospf6_interface_connected_route_update (c->ifp);

      after = oi && oi->area && ospf6_interface_has_linklocal_addr (oi);

      if (after != before)
        ospf6_interface_state_update (c->ifp);
    }

  return 0;
}

static int
ospf6_zebra_if_address_update_delete (int command, struct zclient *zclient,
                               zebra_size_t length)
{
  struct connected *c;
  char buf[128];

  c = zebra_interface_address_read (ZEBRA_INTERFACE_ADDRESS_DELETE, zclient->ibuf);
  if (c == NULL)
    return 0;

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    zlog_debug ("Zebra Interface address delete: %s %5s %s/%d",
		c->ifp->name, prefix_family_str (c->address),
		inet_ntop (c->address->family, &c->address->u.prefix,
			   buf, sizeof (buf)), c->address->prefixlen);

  if (c->address->family == AF_INET6 || c->address->family == AF_INET)
    {
      struct ospf6_interface *oi;
      bool before, after;

      oi = c->ifp->info;
      before = oi && oi->area && ospf6_interface_has_linklocal_addr (oi);

      ospf6_interface_connected_route_update (c->ifp);

      after = oi && oi->area && ospf6_interface_has_linklocal_addr (oi);

      if (after != before)
        ospf6_interface_state_update (c->ifp);
    }

  connected_free (c);

  return 0;
}

static int
ospf6_zebra_read_ipv4 (int command, struct zclient *zclient,
                       zebra_size_t length)
{
  struct stream *s;
  struct zapi_ipv4 api;
  struct prefix_ipv4 p4;
  unsigned long ifindex;
  struct prefix_ipv6 p;
  struct in6_addr *nexthop;
  int err;

  if (!ospf6_af_is_ipv4 (ospf6))
    return 0;

  s = zclient->ibuf;
  ifindex = 0;
  nexthop = NULL;

  /* Type, flags, message. */
  memset (&api, 0, sizeof (api));
  api.type = stream_getc (s);
  api.flags = stream_getc (s);
  api.message = stream_getc (s);
  api.nexthop_num = 0;

  /* IPv4 prefix. */
  memset (&p4, 0, sizeof (struct prefix_ipv4));
  p4.family = AF_INET;
  p4.prefixlen = stream_getc (s);
  stream_get (&p4.prefix, s, PSIZE (p4.prefixlen));

  /* Convert to an IPv6 prefix */
  err = ospf6_af_prefix_convert4to6 (&p, &p4);
  if (err)
    {
      char buf[PREFIXSTRLEN];

      prefix2str ((struct prefix *)&p4, buf, sizeof(buf));
      zlog_warn ("%s: error converting prefix: %s", __func__, buf);

      return 0;
    }

  err = ospf6_af_validate_prefix (ospf6, &p.prefix, p.prefixlen, true);
  if (err)
    {
      if (IS_OSPF6_DEBUG_ZEBRA (RECV))
	{
	  char buf[PREFIXSTRLEN];

	  prefix2str ((struct prefix *)&p, buf, sizeof(buf));
	  zlog_warn ("%s: ignoring prefix %s: "
		     "address family incompatibility", __func__, buf);
	}

      return 0;
    }

  /* Nexthop, ifindex, distance, metric. */
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_NEXTHOP))
    {
      int i;

      api.nexthop_num = stream_getc (s);

      nexthop = XMALLOC (MTYPE_OSPF6_OTHER,
			 api.nexthop_num * sizeof (struct in6_addr));
      for (i = 0; i < api.nexthop_num; i++)
	{
	  struct in_addr nexthop4;

	  nexthop4.s_addr = stream_get_ipv4 (s);
	  ospf6_af_address_convert4to6 (&nexthop[i], &nexthop4);
	}
    }
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_IFINDEX))
    {
      api.ifindex_num = stream_getc (s);
      ifindex = stream_getl (s);
    }
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_DISTANCE))
    api.distance = stream_getc (s);
  else
    api.distance = 0;
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_METRIC))
    api.metric = stream_getl (s);
  else
    api.metric = 0;

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    {
      char prefixstr[128], nexthopstr[128];
      ospf6_prefix2str (ospf6, (struct prefix *) &p,
			prefixstr, sizeof (prefixstr));
      if (nexthop)
        ospf6_addr2str (ospf6, nexthop, nexthopstr, sizeof (nexthopstr));
      else
        snprintf (nexthopstr, sizeof (nexthopstr), "0.0.0.0");

      zlog_debug ("Zebra Receive route %s: %s %s nexthop %s ifindex %ld",
                  (command == ZEBRA_IPV4_ROUTE_ADD ? "add" : "delete"),
                  zebra_route_string (api.type), prefixstr, nexthopstr,
                  ifindex);
    }

  if (command == ZEBRA_IPV4_ROUTE_ADD)
    ospf6_asbr_redistribute_add (api.type, ifindex, (struct prefix *) &p,
                                 api.nexthop_num, nexthop, api.metric);
  else
    ospf6_asbr_redistribute_remove (api.type, ifindex, (struct prefix *) &p);

  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_NEXTHOP))
    XFREE (MTYPE_OSPF6_OTHER, nexthop);

  return 0;
}

static int
ospf6_zebra_read_ipv6 (int command, struct zclient *zclient,
                       zebra_size_t length)
{
  struct stream *s;
  struct zapi_ipv6 api;
  unsigned long ifindex;
  struct prefix_ipv6 p;
  struct in6_addr *nexthop;
  int err;

  if (!ospf6_af_is_ipv6 (ospf6))
    return 0;

  s = zclient->ibuf;
  ifindex = 0;
  nexthop = NULL;
  memset (&api, 0, sizeof (api));

  /* Type, flags, message. */
  api.type = stream_getc (s);
  api.flags = stream_getc (s);
  api.message = stream_getc (s);

  /* IPv6 prefix. */
  memset (&p, 0, sizeof (struct prefix_ipv6));
  p.family = AF_INET6;
  p.prefixlen = stream_getc (s);
  stream_get (&p.prefix, s, PSIZE (p.prefixlen));

  err = ospf6_af_validate_prefix (ospf6, &p.prefix, p.prefixlen, true);
  if (err)
    {
      if (IS_OSPF6_DEBUG_ZEBRA (RECV))
	{
	  char buf[PREFIXSTRLEN];

	  prefix2str ((struct prefix *)&p, buf, sizeof(buf));
	  zlog_warn ("%s: ignoring prefix %s: "
		     "address family incompatibility", __func__, buf);
	}

      return 0;
    }

  /* Nexthop, ifindex, distance, metric. */
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_NEXTHOP))
    {
      api.nexthop_num = stream_getc (s);
      nexthop = (struct in6_addr *)
        malloc (api.nexthop_num * sizeof (struct in6_addr));
      stream_get (nexthop, s, api.nexthop_num * sizeof (struct in6_addr));
    }
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_IFINDEX))
    {
      api.ifindex_num = stream_getc (s);
      ifindex = stream_getl (s);
    }
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_DISTANCE))
    api.distance = stream_getc (s);
  else
    api.distance = 0;
  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_METRIC))
    api.metric = stream_getl (s);
  else
    api.metric = 0;

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    {
      char prefixstr[128], nexthopstr[128];
      ospf6_prefix2str (ospf6, (struct prefix *)&p,
			prefixstr, sizeof (prefixstr));
      if (nexthop)
        ospf6_addr2str (ospf6, nexthop, nexthopstr, sizeof (nexthopstr));
      else
        snprintf (nexthopstr, sizeof (nexthopstr), "::");

      zlog_debug ("Zebra Receive route %s: %s %s nexthop %s ifindex %ld",
		  (command == ZEBRA_IPV6_ROUTE_ADD ? "add" : "delete"),
		  zebra_route_string(api.type), prefixstr, nexthopstr, ifindex);
    }
 
  if (command == ZEBRA_IPV6_ROUTE_ADD)
    ospf6_asbr_redistribute_add (api.type, ifindex, (struct prefix *) &p,
                                 api.nexthop_num, nexthop, api.metric);
  else
    ospf6_asbr_redistribute_remove (api.type, ifindex, (struct prefix *) &p);

  if (CHECK_FLAG (api.message, ZAPI_MESSAGE_NEXTHOP))
    free (nexthop);

  return 0;
}

typedef enum
{
  NONE,
  ROUTE_ADD,
  ROUTE_REMOVE,
} ospf6_zebra_route_update_t;

static void
__ospf6_zebra_route_update (ospf6_zebra_route_update_t update,
                            struct ospf6_route *route)
{
  bool af_is_ipv4;
  struct prefix prefix;
  struct prefix *p;
  unsigned int i;
  unsigned int nhcount;
  u_char message;
#if 0
  u_char distance;
#endif
  u_char flags;
  int psize;
  struct stream *s;

  af_is_ipv4 = ospf6_af_is_ipv4 (ospf6);

  if (af_is_ipv4)
    {
      int err;

      err = ospf6_af_prefix_convert6to4 ((struct prefix_ipv4 *)&prefix,
                                         (struct prefix_ipv6 *)&route->prefix);
      if (err)
        {
          char buf[PREFIXSTRLEN];

          prefix2str (&route->prefix, buf, sizeof (buf));
	  zlog_warn ("%s: error converting destination prefix: %s",
		     __func__, buf);
          return;
        }

      p = &prefix;
    }
  else
    {
      p = &route->prefix;
    }

  for (i = 0; i < OSPF6_MULTI_PATH_LIMIT &&
         ospf6_nexthop_is_set (&route->nexthop[i]); i++)
    {
      /* nothing */
    }
  nhcount = i;

  if (nhcount == 0)
    {
      if (IS_OSPF6_DEBUG_ZEBRA (SEND))
        zlog_debug ("  No nexthop, ignore");
      return;
    }

  message = 0;
  flags = 0;

  /* OSPF pass nexthop and metric */
  SET_FLAG (message, ZAPI_MESSAGE_NEXTHOP);
  SET_FLAG (message, ZAPI_MESSAGE_METRIC);

#if 0
  /* Distance value. */
  SET_FLAG (message, ZAPI_MESSAGE_DISTANCE);
#endif

  /* Make packet. */
  s = zclient->obuf;
  stream_reset (s);

  /* Put command, type, flags, message. */
  if (af_is_ipv4)
    {
      if (update == ROUTE_ADD)
        zclient_create_header (s, ZEBRA_IPV4_ROUTE_ADD);
      else
        zclient_create_header (s, ZEBRA_IPV4_ROUTE_DELETE);
    }
  else
    {
      if (update == ROUTE_ADD)
        zclient_create_header (s, ZEBRA_IPV6_ROUTE_ADD);
      else
        zclient_create_header (s, ZEBRA_IPV6_ROUTE_DELETE);
    }

  stream_putc (s, ZEBRA_ROUTE_OSPF6);
  stream_putc (s, flags);
  stream_putc (s, message);
  stream_putw (s, SAFI_UNICAST);

  /* Put prefix information. */
  psize = PSIZE (p->prefixlen);
  stream_putc (s, p->prefixlen);
  stream_write (s, &p->u.prefix, psize);

  /* Nexthop count. */
  stream_putc (s, nhcount);

  /* Nexthop, ifindex, distance and metric information. */
  for (i = 0; i < nhcount; i++)
    {
      struct in_addr nhaddr;
      void *addr;
      size_t addrsize;
      bool directly_connected;

      directly_connected = af_is_ipv4 &&
        ospf6_route_directly_connected (&route->prefix, &route->nexthop[i]);

      if (!IN6_IS_ADDR_UNSPECIFIED (&route->nexthop[i].address))
        {
          if (af_is_ipv4)
            {
              int err;

              err = ospf6_af_address_convert6to4 (&nhaddr,
                                                  &route->nexthop[i].address);
              if (err)
                {
                  char buf[INET6_ADDRSTRLEN];

                  ospf6_addr2str6 (&route->nexthop[i].address,
                                   buf, sizeof (buf));
                  zlog_warn ("%s: error converting nexthop address: %s",
                             __func__, buf);
                  continue;
                }

              assert (nhaddr.s_addr != INADDR_ANY);
              addr = &nhaddr;
              addrsize = sizeof (nhaddr);
            }
          else
            {
              addr = &route->nexthop[i].address;
              addrsize = sizeof (route->nexthop[i].address);
            }
        }
      else
        {
          addr = NULL;
          addrsize = 0;
        }

      if (directly_connected || addr == NULL)
        {
          assert (route->nexthop[i].ifindex != 0);

	  if (IS_OSPF6_DEBUG_ZEBRA (SEND))
	    {
	      zlog_debug ("  nexthop: %s(%u)",
                          ifindex2ifname (route->nexthop[i].ifindex),
			  route->nexthop[i].ifindex);
	    }

          stream_putc (s, ZEBRA_NEXTHOP_IFINDEX);
          stream_putl (s, route->nexthop[i].ifindex);
        }
      else if (route->nexthop[i].ifindex != 0)
        {
          assert (addr != NULL);

	  if (IS_OSPF6_DEBUG_ZEBRA (SEND))
	    {
              char buf[INET6_ADDRSTRLEN];

	      ospf6_addr2str (ospf6, &route->nexthop[i].address,
			      buf, sizeof (buf));
	      zlog_debug ("  nexthop: %s%%%s(%u)", buf,
                          ifindex2ifname (route->nexthop[i].ifindex),
			  route->nexthop[i].ifindex);
	    }

          if (af_is_ipv4)
            stream_putc (s, ZEBRA_NEXTHOP_IPV4_IFINDEX);
          else
            stream_putc (s, ZEBRA_NEXTHOP_IPV6_IFINDEX);

          stream_write (s, addr, addrsize);
          stream_putl (s, route->nexthop[i].ifindex);
        }
      else
        {
          assert (addr != NULL);

	  if (IS_OSPF6_DEBUG_ZEBRA (SEND))
	    {
              char buf[INET6_ADDRSTRLEN];

	      ospf6_addr2str (ospf6, &route->nexthop[i].address,
			      buf, sizeof (buf));
	      zlog_debug ("  nexthop: %s", buf);
	    }

          if (af_is_ipv4)
            stream_putc (s, ZEBRA_NEXTHOP_IPV4);
          else
            stream_putc (s, ZEBRA_NEXTHOP_IPV6);

          stream_write (s, addr, addrsize);
        }
    }

#if 0
  if (CHECK_FLAG (message, ZAPI_MESSAGE_DISTANCE))
    stream_putc (s, distance);
#endif

  if (CHECK_FLAG (message, ZAPI_MESSAGE_METRIC))
    {
      u_int32_t metric;

      if (route->path.metric_type == 2)
        metric = route->path.cost_e2;
      else
        metric = route->path.cost;

      if (IS_OSPF6_DEBUG_ZEBRA (SEND))
        zlog_debug ("  metric: %u", metric);

      stream_putl (s, metric);
    }

  stream_putw_at (s, 0, stream_get_endp (s));

  zclient_send_message (zclient);
}

static void
ospf6_zebra_route_update (ospf6_zebra_route_update_t update,
                          struct ospf6_route *request)
{
  assert (update == ROUTE_ADD || update == ROUTE_REMOVE);

  if (IS_OSPF6_DEBUG_ZEBRA (SEND))
    {
      char buf[PREFIXSTRLEN];

      ospf6_prefix2str (ospf6, &request->prefix, buf, sizeof (buf));
      zlog_debug ("Send %s route: %s",
		  (update == ROUTE_REMOVE ? "remove" : "add"), buf);
    }

  if (zclient->sock < 0)
    {
      if (IS_OSPF6_DEBUG_ZEBRA (SEND))
        zlog_debug ("  Not connected to Zebra");
      return;
    }

  if (request->path.origin.adv_router == ospf6->router_id &&
      (request->path.type == OSPF6_PATH_TYPE_EXTERNAL1 ||
       request->path.type == OSPF6_PATH_TYPE_EXTERNAL2))
    {
      if (IS_OSPF6_DEBUG_ZEBRA (SEND))
        zlog_debug ("  Ignore self-originated external route");
      return;
    }

  /* If removing is the best path and if there's another path,
     treat this request as add the secondary path */
  if (update == ROUTE_REMOVE && ospf6_route_is_best (request) &&
      request->next && ospf6_route_is_same (request, request->next))
    {
      if (IS_OSPF6_DEBUG_ZEBRA (SEND))
        zlog_debug ("  Best-path removal resulted Secondary addition");
      update = ROUTE_ADD;
      request = request->next;
    }

  /* Only the best path will be sent to zebra. */
  if (! ospf6_route_is_best (request))
    {
      /* this is not preferred best route, ignore */
      if (IS_OSPF6_DEBUG_ZEBRA (SEND))
        zlog_debug ("  Ignore non-best route");
      return;
    }

  __ospf6_zebra_route_update (update, request);
}

void
ospf6_zebra_route_update_add (struct ospf6_route *request)
{
  ospf6_zebra_route_update (ROUTE_ADD, request);
}

void
ospf6_zebra_route_update_remove (struct ospf6_route *request)
{
  ospf6_zebra_route_update (ROUTE_REMOVE, request);
}

void
ospf6_zebra_init (void)
{
  /* Allocate zebra structure. */
  zclient = zclient_new ();
  zclient_init (zclient, ZEBRA_ROUTE_OSPF6);
  zclient->router_id_update = ospf6_router_id_update_zebra;
  zclient->interface_add = ospf6_zebra_if_add;
  zclient->interface_delete = ospf6_zebra_if_del;
  zclient->interface_up = ospf6_zebra_if_state_update;
  zclient->interface_down = ospf6_zebra_if_state_update;
  zclient->interface_address_add = ospf6_zebra_if_address_update_add;
  zclient->interface_address_delete = ospf6_zebra_if_address_update_delete;
  zclient->ipv4_route_add = ospf6_zebra_read_ipv4;
  zclient->ipv4_route_delete = ospf6_zebra_read_ipv4;
  zclient->ipv6_route_add = ospf6_zebra_read_ipv6;
  zclient->ipv6_route_delete = ospf6_zebra_read_ipv6;
  zclient->linkmetrics_subscribe = 1; /* XXX this could be made configurable */
  zclient->linkmetrics = ospf6_zebra_linkmetrics;
  zclient->linkstatus = ospf6_zebra_linkstatus;

  /* redistribute connected route by default */
  /* ospf6_zebra_redistribute (ZEBRA_ROUTE_CONNECT); */

  return;
}

/* Debug */

DEFUN (debug_ospf6_zebra_sendrecv,
       debug_ospf6_zebra_sendrecv_cmd,
       "debug ospf6 zebra (send|recv)",
       DEBUG_STR
       OSPF6_STR
       "Debug connection between zebra\n"
       "Debug Sending zebra\n"
       "Debug Receiving zebra\n"
      )
{
  unsigned char level = 0;

  if (argc)
    {
      if (! strncmp (argv[0], "s", 1))
        level = OSPF6_DEBUG_ZEBRA_SEND;
      else if (! strncmp (argv[0], "r", 1))
        level = OSPF6_DEBUG_ZEBRA_RECV;
    }
  else
    level = OSPF6_DEBUG_ZEBRA_SEND | OSPF6_DEBUG_ZEBRA_RECV;

  OSPF6_DEBUG_ZEBRA_ON (level);
  return CMD_SUCCESS;
}

ALIAS (debug_ospf6_zebra_sendrecv,
       debug_ospf6_zebra_cmd,
       "debug ospf6 zebra",
       DEBUG_STR
       OSPF6_STR
       "Debug connection between zebra\n"
      )


DEFUN (no_debug_ospf6_zebra_sendrecv,
       no_debug_ospf6_zebra_sendrecv_cmd,
       "no debug ospf6 zebra (send|recv)",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Debug connection between zebra\n"
       "Debug Sending zebra\n"
       "Debug Receiving zebra\n"
      )
{
  unsigned char level = 0;

  if (argc)
    {
      if (! strncmp (argv[0], "s", 1))
        level = OSPF6_DEBUG_ZEBRA_SEND;
      else if (! strncmp (argv[0], "r", 1))
        level = OSPF6_DEBUG_ZEBRA_RECV;
    }
  else
    level = OSPF6_DEBUG_ZEBRA_SEND | OSPF6_DEBUG_ZEBRA_RECV;

  OSPF6_DEBUG_ZEBRA_OFF (level);
  return CMD_SUCCESS;
}

ALIAS (no_debug_ospf6_zebra_sendrecv,
       no_debug_ospf6_zebra_cmd,
       "no debug ospf6 zebra",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Debug connection between zebra\n"
      )

int
config_write_ospf6_debug_zebra (struct vty *vty)
{
  if (IS_OSPF6_DEBUG_ZEBRA (SEND) && IS_OSPF6_DEBUG_ZEBRA (RECV))
    vty_out (vty, "debug ospf6 zebra%s", VNL);
  else
    {
      if (IS_OSPF6_DEBUG_ZEBRA (SEND))
        vty_out (vty, "debug ospf6 zebra send%s", VNL);
      if (IS_OSPF6_DEBUG_ZEBRA (RECV))
        vty_out (vty, "debug ospf6 zebra recv%s", VNL);
    }
  return 0;
}

void
install_element_ospf6_debug_zebra (void)
{
  install_element (ENABLE_NODE, &debug_ospf6_zebra_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_zebra_cmd);
  install_element (ENABLE_NODE, &debug_ospf6_zebra_sendrecv_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_zebra_sendrecv_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_zebra_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_zebra_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_zebra_sendrecv_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_zebra_sendrecv_cmd);
}


