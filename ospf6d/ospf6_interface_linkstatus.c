/* -*- Mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2015 The Boeing Company
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

#include "lib/zebra.h"
#include "lib/memory.h"
#include "lib/lmgenl.h"
#include "lib/log.h"
#include "lib/thread.h"
#include "lib/command.h"

#include "zebra/zserv_linkmetrics.h"

#include "ospf6_interface.h"
#include "ospf6_zebra.h"
#include "ospf6_zebra_linkmetrics.h"
#include "ospf6_af.h"
#include "ospf6_neighbor.h"
#include "ospf6d.h"

struct ospf6_interface_linkstatus_config {
  bool enabled;
};

struct ospf6_interface_linkstatus_stats {
  unsigned long status_up_count;
  unsigned long status_down_count;
  unsigned long status_unknown_count;
};

struct ospf6_interface_linkstatus {
  struct ospf6_interface_linkstatus_config config;
  struct ospf6_interface_linkstatus_stats stats;
};

static const struct ospf6_interface_linkstatus_config
linkstatus_config_default = {
  .enabled = true,
};

static unsigned int linkstatus_data_id;

static void
ospf6_linkstatus_update (struct ospf6_interface *oi,
			 struct ospf6_neighbor *on,
                         struct zebra_linkstatus *status)
{
  struct ospf6_interface_linkstatus *ils;

  ils = ospf6_get_interface_data (oi, linkstatus_data_id);
  if (ils == NULL || !ils->config.enabled)
    {
      if (IS_OSPF6_DEBUG_ZEBRA (RECV))
	{
	  char lladdrstr[INET6_ADDRSTRLEN];

	  ospf6_addr2str6 (&status->nbr_addr6, lladdrstr, sizeof (lladdrstr));
	  zlog_debug ("%s: ignoring link status update on interface %s "
		      "for %s %s", __func__, oi->interface->name, lladdrstr,
                      status->status ? "up" : "down");
	}

      return;
    }

  switch (status->status)
    {
    case LM_STATUS_UP:
      ils->stats.status_up_count++;
      if (on == NULL)
        {
          /* Expedite the Hello mechanism to find the new neighbor
             that just came up */
          if (conf_debug_ospf6_zebra)
            {
              char lladdrstr[INET6_ADDRSTRLEN];

              ospf6_addr2str6 (&status->nbr_addr6,
                               lladdrstr, sizeof (lladdrstr));
              zlog_debug ("%s: Expediting Hello mechanism due to reception "
                          "of link status UP message on interface %s for %s",
                          __func__, oi->interface->name, lladdrstr);
            }

          THREAD_OFF (oi->thread_send_hello);
          oi->thread_send_hello =
            thread_add_event (master, ospf6_hello_send, oi, 0);
        }
      break;

    case LM_STATUS_DOWN:
      ils->stats.status_down_count++;
      if (on != NULL)
        {
          if (conf_debug_ospf6_zebra)
            {
              zlog_debug ("%s: removing neighbor %s: link status down",
                          __func__, on->name);
            }

          THREAD_OFF (on->inactivity_timer);
          thread_add_event (master, inactivity_timer, on, 0);
        }
      break;

    default:
      ils->stats.status_unknown_count++;
      {
	char lladdrstr[INET6_ADDRSTRLEN];

	ospf6_addr2str6 (&status->nbr_addr6, lladdrstr, sizeof (lladdrstr));
	zlog_err ("%s: ignoring link status for neighbor %s "
		  "on interface %s for %s: status 0x%x", __func__, on->name,
		  oi->interface->name, lladdrstr, status->status);
      }
      break;
    }
}

static int
ospf6_interface_linkstatus_enable (struct vty *vty, bool val)
{
  struct ospf6_interface *oi;
  struct ospf6_interface_linkstatus *ils;

  oi = ospf6_interface_vtyget (vty);
  ils = ospf6_get_interface_data (oi, linkstatus_data_id);
  if (ils == NULL)
    {
      vty_out (vty, "ERROR: cross-layer link status information not "
               "supported for interface %s%s", oi->interface->name, VNL);
      return CMD_WARNING;
    }

  ils->config.enabled = val;

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_linkstatus,
       ipv6_ospf6_linkstatus_cmd,
       "ipv6 ospf6 link-status",
       IP6_STR
       OSPF6_STR
       "Enable using cross-layer link status information\n")
{
  return ospf6_interface_linkstatus_enable (vty, true);
}

DEFUN (no_ipv6_ospf6_linkstatus,
       no_ipv6_ospf6_linkstatus_cmd,
       "no ipv6 ospf6 link-status",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Disable using cross-layer link status information\n")
{
  return ospf6_interface_linkstatus_enable (vty, false);
}

static unsigned int
ospf6_interface_linkstatus_show (struct vty *vty, struct ospf6_interface *oi)
{
  struct ospf6_interface_linkstatus *ils;

  ils = ospf6_get_interface_data (oi, linkstatus_data_id);
  if (ils == NULL)
    return 0;

  if (ils->config.enabled)
    {
      vty_out (vty, "cross-layer link status statistics for interface %s:%s",
               oi->interface->name, VNL);
      vty_out (vty, "  link status up count: %lu%s",
               ils->stats.status_up_count, VNL);
      vty_out (vty, "  link status down count: %lu%s",
               ils->stats.status_down_count, VNL);
      vty_out (vty, "  link status unknown count: %lu%s",
               ils->stats.status_unknown_count, VNL);
    }
  else
    {
      vty_out (vty, "cross-layer link status information not enabled "
               "for interface %s%s", oi->interface->name, VNL);
   }

  return 1;
}

DEFUN (show_ipv6_ospf6_linkstatus,
       show_ipv6_ospf6_linkstatus_cmd,
       "show ipv6 ospf6 link-status [IFNAME]",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Cross-layer link status information\n"
       IFNAME_STR)
{
  unsigned int num;

  num = 0;
  if (argc)
    {
      struct interface *ifp;
      struct ospf6_interface *oi;

      ifp = if_lookup_by_name (argv[0]);
      if (ifp != NULL)
        oi = ifp->info;
      else
        oi = NULL;

      if (oi == NULL)
        {
          vty_out (vty, "Unknown OSPF interface: %s%s", argv[0], VNL);
          return CMD_WARNING;
        }

      num += ospf6_interface_linkstatus_show (vty, oi);
    }
  else
    {
      struct listnode *n;
      struct interface *ifp;

      for (ALL_LIST_ELEMENTS_RO (iflist, n, ifp))
        {
          struct ospf6_interface *oi;

          oi = ifp->info;
          if (oi == NULL)
            continue;

          num += ospf6_interface_linkstatus_show (vty, oi);
        }
    }

  if (num == 0)
    {
      vty_out (vty, "No cross-layer link status information found%s", VNL);
    }

  return CMD_SUCCESS;
}

static void
ospf6_interface_init_linkstatus (void)
{
  int err;

  install_element (INTERFACE_NODE, &ipv6_ospf6_linkstatus_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_linkstatus_cmd);

  install_element (ENABLE_NODE, &show_ipv6_ospf6_linkstatus_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_linkstatus_cmd);

  err = ospf6_add_linkstatus_hook (ospf6_linkstatus_update);
  if (err)
    {
      zlog_err ("%s: error adding link status callback", __func__);
    }
}

static void
ospf6_interface_linkstatus_init (struct ospf6_interface_linkstatus *ils)
{
  ils->config = linkstatus_config_default;
}

static int
ospf6_interface_create_linkstatus (struct ospf6_interface *oi)
{
  struct ospf6_interface_linkstatus *ils;
  int err;

  ils = XCALLOC (MTYPE_OSPF6_IF, sizeof (*ils));
  ospf6_interface_linkstatus_init (ils);

  err = ospf6_add_interface_data (oi, &linkstatus_data_id, ils);
  if (err)
    {
      XFREE (MTYPE_OSPF6_IF, ils);
      return err;
    }

  return 0;
}

static void
ospf6_interface_delete_linkstatus (struct ospf6_interface *oi)
{
  struct ospf6_interface_linkstatus *ils;

  ils = ospf6_del_interface_data (oi, linkstatus_data_id);
  if (ils != NULL)
    XFREE (MTYPE_OSPF6_IF, ils);
}

static void
ospf6_interface_config_write_linkstatus (struct ospf6_interface *oi,
                                         struct vty *vty)
{
  struct ospf6_interface_linkstatus *ils;

  ils = ospf6_get_interface_data (oi, linkstatus_data_id);
  assert (ils);

  if (ils->config.enabled != linkstatus_config_default.enabled)
    {
      vty_out (vty, " %sipv6 ospf6 link-status%s",
               ils->config.enabled ? "" : "no ", VNL);
    }
}

static struct ospf6_interface_operations linkstatus_ifops = {
  .init = ospf6_interface_init_linkstatus,
  .create = ospf6_interface_create_linkstatus,
  .delete = ospf6_interface_delete_linkstatus,
  .config_write = ospf6_interface_config_write_linkstatus,
};

OSPF6_INTERFACE_OPERATIONS (linkstatus_ifops);
