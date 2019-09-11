/* -*-  c-file-style: "gnu" -*- */

/*
 * Copyright (c) 2010 The Boeing Company
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

#include <sys/un.h>

#include "zebra.h"
#include "memory.h"
#include "command.h"

#include "ospf6_interface.h"
#include "ospf6_mdr.h"

struct ospf6_interface_mdrsmf {
  bool active;
  char *filename;
  int fd;
  int relay;
  int relay_min_mdr_level;
  unsigned int relay_min_nbr_count;
  int relay_isolated;
};

static unsigned int mdrsmf_data_id;

static void ospf6_smf_update (struct ospf6_interface *oi);
static void ospf6_smf_close (struct ospf6_interface_mdrsmf *mdrsmf);

static int
ospf6_interface_create_mdrsmf (struct ospf6_interface *oi)
{
  struct ospf6_interface_mdrsmf *mdrsmf;
  int err;

  mdrsmf = XCALLOC (MTYPE_OSPF6_IF, sizeof (*mdrsmf));
  mdrsmf->active = false;
  mdrsmf->filename = NULL;
  mdrsmf->fd = -1;
  mdrsmf->relay = -1;
  mdrsmf->relay_min_mdr_level = OSPF6_MDR;
  mdrsmf->relay_min_nbr_count = 2;
  mdrsmf->relay_isolated = 0;

  err = ospf6_add_interface_data (oi, &mdrsmf_data_id, mdrsmf);
  if (err)
    {
      XFREE (MTYPE_OSPF6_IF, mdrsmf);
      return err;
    }

  return 0;
}

static void
ospf6_interface_delete_mdrsmf (struct ospf6_interface *oi)
{
  struct ospf6_interface_mdrsmf *mdrsmf;

  mdrsmf = ospf6_del_interface_data (oi, mdrsmf_data_id);
  if (mdrsmf == NULL)
    return;

  if (mdrsmf->active)
    {
      ospf6_remove_update_mdr_level_hook (ospf6_smf_update);
      mdrsmf->active = false;
    }

  ospf6_smf_close (mdrsmf);

  XFREE (MTYPE_OSPF6_IF, mdrsmf);
}

static int
ospf6_smf_open (struct ospf6_interface_mdrsmf *mdrsmf,
		const char *pathname, struct vty *vty)
{
  int fd;
  struct sockaddr_un addr;
  size_t namelen;

  if ((fd = socket (AF_UNIX, SOCK_DGRAM, 0)) == -1)
    {
      if (vty)
	vty_out (vty, "socket() failed: %s%s", strerror (errno), VNL);
      zlog_err ("%s: socket() failed: %s", __func__, strerror (errno));
      return -1;
    }

  if (fcntl (fd, F_SETFL, O_NONBLOCK) == -1)
    {
      if (vty)
	vty_out (vty, "fcntl() failed: %s%s", strerror (errno), VNL);
      zlog_err ("%s: fcntl() failed: %s", __func__, strerror (errno));
      close (fd);
      return -1;
    }

  memset (&addr, 0, sizeof (addr));
  addr.sun_family = AF_UNIX;
  namelen = strlen (pathname) + 1;
  if (namelen > sizeof (addr.sun_path))
    {
      if (vty)
	vty_out (vty, "path too long: %s%s", pathname, VNL);
      zlog_err ("%s: path too long: %s", __func__, pathname);
      close (fd);
      return -1;
    }
  memcpy (addr.sun_path, pathname, namelen);
  if (connect (fd, (struct sockaddr *)&addr,
	       namelen + sizeof (addr.sun_family)) < 0)
    {
      if (vty)
	vty_out (vty, "connect() failed: %s%s", strerror (errno), VNL);
      zlog_err ("%s: connect() failed: %s", __func__, strerror (errno));
      close (fd);
      return -1;
    }

  if (mdrsmf->filename != pathname)
    {
      if (mdrsmf->filename)
	XFREE (MTYPE_OSPF6_OTHER, mdrsmf->filename);
      mdrsmf->filename = XSTRDUP (MTYPE_OSPF6_OTHER, pathname);
    }
  if (mdrsmf->fd >= 0)
    close (mdrsmf->fd);
  mdrsmf->fd = fd;
  mdrsmf->relay = -1;

  return 0;
}

static void
ospf6_smf_close (struct ospf6_interface_mdrsmf *mdrsmf)
{
  if (mdrsmf->filename)
    {
      XFREE (MTYPE_OSPF6_OTHER, mdrsmf->filename);
      mdrsmf->filename = NULL;
    }

  if (mdrsmf->fd >= 0)
    {
      close (mdrsmf->fd);
      mdrsmf->fd = -1;
    }

  mdrsmf->relay = -1;
}

static void
ospf6_mdrsmf_interface_data (struct vty *vty, struct ospf6_interface **oi,
			     struct ospf6_interface_mdrsmf **mdrsmf)
{
  *oi = ospf6_interface_vtyget (vty);

  *mdrsmf = ospf6_get_interface_data (*oi, mdrsmf_data_id);
  assert (*mdrsmf);
}

DEFUN (ipv6_ospf6_smf_mdr,
       ipv6_ospf6_smf_mdr_cmd,
       "ipv6 ospf6 smf-mdr FILENAME",
       IP6_STR
       OSPF6_STR
       "Tell SMF about the MDR flooding set\n"
       "The filename of the unix domain socket to use for communication\n")
{
  struct ospf6_interface *oi;
  struct ospf6_interface_mdrsmf *mdrsmf;

  ospf6_mdrsmf_interface_data (vty, &oi, &mdrsmf);

  if (!mdrsmf->active)
    {
      int err;

      err = ospf6_add_update_mdr_level_hook (ospf6_smf_update);
      if (err)
	{
	  vty_out (vty, "couldn't add update mdr level hook%s", VNL);
	  return CMD_WARNING;;
	}
      mdrsmf->active = true;
    }

  if (ospf6_smf_open (mdrsmf, argv[0], vty))
    {
      if (mdrsmf->filename)
	XFREE (MTYPE_OSPF6_OTHER, mdrsmf->filename);
      mdrsmf->filename = XSTRDUP (MTYPE_OSPF6_OTHER, argv[0]);
      return CMD_WARNING;
    }

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_smf_mdr,
       no_ipv6_ospf6_smf_mdr_cmd,
       "no ipv6 ospf6 smf-mdr",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Disable telling SMF about the MDR flooding set\n")
{
  struct ospf6_interface *oi;
  struct ospf6_interface_mdrsmf *mdrsmf;

  ospf6_mdrsmf_interface_data (vty, &oi, &mdrsmf);

  if (mdrsmf->active)
    {
      ospf6_remove_update_mdr_level_hook (ospf6_smf_update);
      mdrsmf->active = false;
    }

  ospf6_smf_close (mdrsmf);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_min_smf_relay_mdr_level,
       ipv6_ospf6_min_smf_relay_mdr_level_cmd,
       "ipv6 ospf6 min-smf-relay-mdr-level (MDR|BMDR)",
       IP6_STR
       OSPF6_STR
       "Set the minimum MDR level needed for SMF relaying\n"
       "Require MDR\n"
       "At least BMDR\n")
{
  struct ospf6_interface *oi;
  struct ospf6_interface_mdrsmf *mdrsmf;
  int min_mdr_level;

  ospf6_mdrsmf_interface_data (vty, &oi, &mdrsmf);

  if (strcmp (argv[0], "MDR") == 0)
    min_mdr_level = OSPF6_MDR;
  else if (strcmp (argv[0], "BMDR") == 0)
    min_mdr_level = OSPF6_BMDR;
  else
    {
      vty_out (vty, "unknown mdr level:: %s%s", argv[0], VNL);
      return CMD_WARNING;
    }

  mdrsmf->relay_min_mdr_level = min_mdr_level;
  ospf6_smf_update (oi);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_min_smf_relay_nbr_count,
       ipv6_ospf6_min_smf_relay_nbr_count_cmd,
       "ipv6 ospf6 min-smf-relay-neighbor-count <1-2>",
       IP6_STR
       OSPF6_STR
       "Set the minimum number of neighbors needed for SMF relaying\n"
       "Number of neighbors\n")
{
  struct ospf6_interface *oi;
  struct ospf6_interface_mdrsmf *mdrsmf;

  ospf6_mdrsmf_interface_data (vty, &oi, &mdrsmf);

  mdrsmf->relay_min_nbr_count = strtol (argv[0], NULL, 10);
  ospf6_smf_update (oi);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_smf_relay_isolated,
       ipv6_ospf6_smf_relay_isolated_cmd,
       "ipv6 ospf6 smf-relay-isolated",
       IP6_STR
       OSPF6_STR
       "Enable SMF relaying when isolated (no neighbors)\n")
{
  struct ospf6_interface *oi;
  struct ospf6_interface_mdrsmf *mdrsmf;

  ospf6_mdrsmf_interface_data (vty, &oi, &mdrsmf);

  mdrsmf->relay_isolated = 1;
  ospf6_smf_update (oi);

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_smf_relay_isolated,
       no_ipv6_ospf6_smf_relay_isolated_cmd,
       "no ipv6 ospf6 smf-relay-isolated",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Disable SMF relaying when isolated (no neighbors)\n")
{
  struct ospf6_interface *oi;
  struct ospf6_interface_mdrsmf *mdrsmf;

  ospf6_mdrsmf_interface_data (vty, &oi, &mdrsmf);

  mdrsmf->relay_isolated = 0;
  ospf6_smf_update (oi);

  return CMD_SUCCESS;
}

static void
ospf6_interface_config_write_mdrsmf (struct ospf6_interface *oi,
				     struct vty *vty)
{
  struct ospf6_interface_mdrsmf *mdrsmf;

  mdrsmf = ospf6_get_interface_data (oi, mdrsmf_data_id);
  assert (mdrsmf);

  if (mdrsmf->filename)
    vty_out (vty, " ipv6 ospf6 smf-mdr %s%s", mdrsmf->filename, VNL);

  if (mdrsmf->relay_min_mdr_level != OSPF6_MDR)
    {
      assert (mdrsmf->relay_min_mdr_level == OSPF6_BMDR);
      vty_out (vty, " ipv6 ospf6 min-smf-relay-mdr-level BMDR%s", VNL);
    }

  if (mdrsmf->relay_min_nbr_count != 2)
    vty_out (vty, " ipv6 ospf6 min-smf-relay-neighbor-count %d%s",
	     mdrsmf->relay_min_nbr_count, VNL);

  if (mdrsmf->relay_isolated)
    vty_out (vty, " ipv6 ospf6 smf-relay-isolated%s", VNL);
}

static void
ospf6_smf_update (struct ospf6_interface *oi)
{
  struct ospf6_interface_mdrsmf *mdrsmf;
  int relay;
  const char *cmd;
  size_t cmdlen;
  ssize_t tmp;

  mdrsmf = ospf6_get_interface_data (oi, mdrsmf_data_id);
  assert (mdrsmf);

  if (mdrsmf->fd < 0)
    {
      if (!mdrsmf->filename ||
	  ospf6_smf_open (mdrsmf, mdrsmf->filename, NULL) < 0)
	return;
    }

  /*
    inform SMF which routers are MDRs
    remove leaf nodes from the MDR set
    leaf nodes don't forward in OSPF because OSPF does not forward
    to routers who have already received the LSAs
  */
  if (oi->mdr.mdr_level >= mdrsmf->relay_min_mdr_level &&
      listcount (oi->neighbor_list) >= mdrsmf->relay_min_nbr_count)
    {
      if (IS_OSPF6_DEBUG_INTERFACE)
	zlog_debug ("%s: OSPF (B)MDR and neighbor count %d: smf relay on",
		    __func__, listcount (oi->neighbor_list));
      relay = 1;
    }
  else if (mdrsmf->relay_isolated && listcount (oi->neighbor_list) == 0)
    {
      if (IS_OSPF6_DEBUG_INTERFACE)
	zlog_debug ("%s: OSPF SMF relay isolated: smf relay on", __func__);
      relay = 1;
    }
  else
    {
      if (IS_OSPF6_DEBUG_INTERFACE)
	zlog_debug ("%s: Not OSPF (B)MDR and neighbor count %d; "
		    "Not OSPF SMF relay isolated: smf relay off",
		    __func__, listcount (oi->neighbor_list));
      relay = 0;
    }

  if (relay)
    cmd = "relay on";
  else
    cmd = "relay off";

  if (relay == mdrsmf->relay)
    {
      if (IS_OSPF6_DEBUG_INTERFACE)
	zlog_debug ("%s: OSPF SMF relay status unchanged: smf %s",
		    __func__, cmd);
      return;
    }

  cmdlen = strlen (cmd);
  tmp = write (mdrsmf->fd, cmd, cmdlen);
  if (tmp < 0)
    {
      zlog_err ("%s: write() failed: %s", __func__, strerror (errno));
      close (mdrsmf->fd);
      mdrsmf->fd = -1;
      relay = -1;
    }
  else if ((size_t) tmp != cmdlen)
    {
      zlog_err ("%s: only wrote %zd of %zu bytes", __func__, tmp, cmdlen);
      relay = -1;
    }

  mdrsmf->relay = relay;

  return;
}

static void ospf6_interface_init_mdrsmf (void)
{
  install_element (INTERFACE_NODE, &ipv6_ospf6_smf_mdr_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_smf_mdr_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_min_smf_relay_mdr_level_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_min_smf_relay_nbr_count_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_smf_relay_isolated_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_smf_relay_isolated_cmd);
}

static struct ospf6_interface_operations mdrsmf_ifops = {
  .init = ospf6_interface_init_mdrsmf,
  .create = ospf6_interface_create_mdrsmf,
  .delete = ospf6_interface_delete_mdrsmf,
  .config_write = ospf6_interface_config_write_mdrsmf,
};

OSPF6_INTERFACE_OPERATIONS (mdrsmf_ifops);
