/* -*-  c-file-style: "gnu" -*- */

/*
 * Copyright (c) 2009-2010 The Boeing Company
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

#include "zebra.h"
#include "zclient.h"
#include "log.h"

#include "ospf6_zebra.h"
#include "ospf6_neighbor.h"
#include "ospf6_interface.h"
#include "ospf6_message.h"
#include "ospf6_af.h"
#include "ospf6d.h"
#include "ospf6_zebra_linkmetrics.h"
#include "ospf6_callbacks.h"

#include "zebra_linkmetrics.h"
#include "lmgenl.h"

static struct list *ospf6_linkmetrics_hooks;
static struct list *ospf6_linkstatus_hooks;

static void __attribute__((constructor))
ospf6_zebra_linkmetrics_init (void)
{
  assert (ospf6_linkmetrics_hooks == NULL);
  ospf6_linkmetrics_hooks = list_new ();

  assert (ospf6_linkstatus_hooks == NULL);
  ospf6_linkstatus_hooks = list_new ();
}

static void __attribute__((destructor))
ospf6_zebra_linkmetrics_terminate (void)
{
  list_delete (ospf6_linkmetrics_hooks);
  ospf6_linkmetrics_hooks = NULL;

  list_delete (ospf6_linkstatus_hooks);
  ospf6_linkstatus_hooks = NULL;
}

static void
ospf6_run_linkmetrics_hooks (struct ospf6_neighbor *on,
                             struct zebra_linkmetrics *metrics)
{
  RUN_HOOKS (ospf6_linkmetrics_hooks, linkmetrics_hook_t, on, metrics);
}

static void
ospf6_run_linkstatus_hooks (struct ospf6_interface *oi,
			    struct ospf6_neighbor *on,
                            struct zebra_linkstatus *status)
{
  RUN_HOOKS (ospf6_linkstatus_hooks, linkstatus_hook_t, oi, on, status);
}

int
ospf6_add_linkmetrics_hook (linkmetrics_hook_t hook)
{
  return ospf6_add_hook (ospf6_linkmetrics_hooks, hook);
}

int
ospf6_add_linkstatus_hook (linkstatus_hook_t hook)
{
  return ospf6_add_hook (ospf6_linkstatus_hooks, hook);
}

int
ospf6_remove_linkmetrics_hook (linkmetrics_hook_t hook)
{
  return ospf6_remove_hook (ospf6_linkmetrics_hooks, hook);
}

int
ospf6_remove_linkstatus_hook (linkstatus_hook_t hook)
{
  return ospf6_remove_hook (ospf6_linkstatus_hooks, hook);
}

void
ospf6_zebra_update_linkmetrics (struct ospf6_neighbor *on,
				struct zebra_linkmetrics *linkmetrics)
{
  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    zlog_debug ("%s: updating link metrics for neighbor %s",
		__func__, on->name);

  ospf6_run_linkmetrics_hooks (on, linkmetrics);
}

static struct ospf6_neighbor *
ospf6_neighbor_lookup_by_ifaddr (struct in6_addr *linklocal_addr,
                                 struct ospf6_interface *oi)
{
  struct ospf6_neighbor *on;
  bool addr_unspecified;
  bool addr_linklocal;

  addr_unspecified = IN6_IS_ADDR_UNSPECIFIED (linklocal_addr);
  addr_linklocal =
    !addr_unspecified && IN6_IS_ADDR_LINKLOCAL (linklocal_addr);

  if (!addr_unspecified && !addr_linklocal)
    {
      char buf[INET6_ADDRSTRLEN];
      ospf6_addr2str6 (linklocal_addr, buf, sizeof (buf));
      zlog_err ("%s: invalid link-local address: %s", __func__, buf);
      return NULL;
    }

  if (oi->type == OSPF6_IFTYPE_POINTOPOINT &&
      listcount (oi->neighbor_list) == 1)
    {
      on = listgetdata (listhead (oi->neighbor_list));
      if (addr_unspecified ||
          IN6_ARE_ADDR_EQUAL (&on->linklocal_addr, linklocal_addr))
        return on;
    }
  else if (!addr_unspecified)
    {
      struct listnode *n;

      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, n, on))
        {
          if (IN6_ARE_ADDR_EQUAL (&on->linklocal_addr, linklocal_addr))
            return on;
        }
    }

  return NULL;
}

int
ospf6_zebra_linkmetrics (int command, struct zclient *zclient,
			 zebra_size_t length)
{
  struct zebra_linkmetrics metrics;
  struct ospf6_interface *oi;
  struct ospf6_neighbor *on;

  assert (command == ZEBRA_LINKMETRICS_METRICS);

  if (zapi_read_linkmetrics (&metrics, zclient->ibuf, length))
    {
      zlog_err ("%s: zapi_read_linkmetrics() failed", __func__);
      return -1;
    }

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    {
      zlog_debug ("%s: received link metrics update", __func__);
      zebra_linkmetrics_logdebug (&metrics);
    }

  oi = ospf6_interface_lookup_by_ifindex (metrics.ifindex);
  if (oi == NULL)
    {
      zlog_err ("%s: unknown interface index: %d",
		__func__, metrics.ifindex);
      return -1;
    }

  on = ospf6_neighbor_lookup_by_ifaddr (&metrics.nbr_addr6, oi);
  if (on == NULL)
    {
      if (IS_OSPF6_DEBUG_ZEBRA (RECV))
	{
	  char lladdrstr[INET6_ADDRSTRLEN];
	  ospf6_addr2str6 (&metrics.nbr_addr6,
			   lladdrstr, sizeof (lladdrstr));
          zlog_debug ("%s: neighbor %s not found for link metrics update "
                      "on interface %s",
                      __func__, lladdrstr, oi->interface->name);
	}
      return -1;
    }

  ospf6_zebra_update_linkmetrics (on, &metrics);

  return 0;
}

int
ospf6_zebra_linkstatus (int command, struct zclient *zclient,
			zebra_size_t length)
{
  struct zebra_linkstatus status;
  struct ospf6_interface *oi;
  struct ospf6_neighbor *on;

  assert (command == ZEBRA_LINKMETRICS_STATUS);

  if (zapi_read_linkstatus (&status, zclient->ibuf, length))
    {
      zlog_err ("%s: zapi_read_linkstatus() failed", __func__);
      return -1;
    }

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    {
      zlog_debug ("%s: received link status update", __func__);
      zebra_linkstatus_logdebug (&status);
    }

  oi = ospf6_interface_lookup_by_ifindex (status.ifindex);
  if (oi == NULL)
    {
      zlog_err ("%s: unknown interface index: %d",
		__func__, status.ifindex);
      return -1;
    }

  /* neighbor can be unknown only for STATUS_UP events */
  on = ospf6_neighbor_lookup_by_ifaddr (&status.nbr_addr6, oi);
  if (on == NULL && status.status != LM_STATUS_UP)
    {
      char lladdrstr[INET6_ADDRSTRLEN];

      ospf6_addr2str6 (&status.nbr_addr6,
                       lladdrstr, sizeof (lladdrstr));
      zlog_debug ("%s: neighbor %s not found for link status %s update "
                  "on interface %s", __func__, lladdrstr,
                  status.status ? "up" : "down",  oi->interface->name);
      return -1;
    }

  ospf6_run_linkstatus_hooks (oi, on, &status);

  return 0;
}
