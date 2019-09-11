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

static struct list ospf6_linkmetrics_hooks;
static struct list ospf6_linkstatus_hooks;

static void
ospf6_run_linkmetrics_hooks (struct ospf6_neighbor *on,
			     zebra_linkmetrics_t *linkmetrics)
{
  RUN_HOOKS(&ospf6_linkmetrics_hooks, linkmetrics_hook_t, on, linkmetrics);
}

static void
ospf6_run_linkstatus_hooks (struct ospf6_interface *oi,
			    struct ospf6_neighbor *on,
			    zebra_linkstatus_t *linkstatus)
{
  RUN_HOOKS(&ospf6_linkstatus_hooks, linkstatus_hook_t, oi, on, linkstatus);
}

int
ospf6_add_linkmetrics_hook (linkmetrics_hook_t hook)
{
  return ospf6_add_hook (&ospf6_linkmetrics_hooks, hook);
}

int
ospf6_add_linkstatus_hook (linkstatus_hook_t hook)
{
  return ospf6_add_hook (&ospf6_linkstatus_hooks, hook);
}

int
ospf6_remove_linkmetrics_hook (linkmetrics_hook_t hook)
{
  return ospf6_remove_hook (&ospf6_linkmetrics_hooks, hook);
}

int
ospf6_remove_linkstatus_hook (linkstatus_hook_t hook)
{
  return ospf6_remove_hook (&ospf6_linkstatus_hooks, hook);
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
  struct listnode *n;
  struct ospf6_neighbor *on;

  if (!IN6_IS_ADDR_LINKLOCAL (linklocal_addr))
    {
      char buf[INET6_ADDRSTRLEN];
      ospf6_addr2str6 (linklocal_addr, buf, sizeof (buf));
      zlog_err ("%s: invalid link-local address: %s", __func__, buf);
      return NULL;
    }

  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, n, on))
    if (IN6_ARE_ADDR_EQUAL (&on->linklocal_addr, linklocal_addr))
      return on;

  if (conf_debug_ospf6_neighbor)
    {
      char buf[INET6_ADDRSTRLEN];
      ospf6_addr2str6 (linklocal_addr, buf, sizeof (buf));
      zlog_debug ("%s: no neighbor found for link-local address %s",
		  __func__, buf);
    }

  return NULL;
}

int
ospf6_zebra_linkmetrics (int command, struct zclient *zclient,
			 zebra_size_t length)
{
  zebra_linkmetrics_t linkmetrics;
  struct ospf6_interface *oi;
  struct ospf6_neighbor *on;

  assert (command == ZEBRA_LINKMETRICS_METRICS);

  if (zapi_read_linkmetrics (&linkmetrics, zclient->ibuf, length))
    {
      zlog_err ("%s: zapi_read_linkmetrics() failed", __func__);
      return -1;
    }

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    {
      zlog_debug ("%s: received link metrics update", __func__);
      zebra_linkmetrics_logdebug (&linkmetrics);
    }

  oi = ospf6_interface_lookup_by_ifindex (linkmetrics.ifindex);
  if (oi == NULL)
    {
      zlog_err ("%s: unknown interface index: %d",
		__func__, linkmetrics.ifindex);
      return -1;
    }

  if (!IN6_IS_ADDR_LINKLOCAL (&linkmetrics.linklocal_addr))
    {
      if (IS_OSPF6_DEBUG_ZEBRA (RECV))
        {
          char lladdrstr[INET6_ADDRSTRLEN];
          ospf6_addr2str6 (&linkmetrics.linklocal_addr,
                           lladdrstr, sizeof (lladdrstr));
          zlog_err ("%s: non-link-local neighbor address: %s",
                    __func__, lladdrstr);
        }
      return -1;
    }

  on = ospf6_neighbor_lookup_by_ifaddr (&linkmetrics.linklocal_addr, oi);
  if (on == NULL)
    {
      if (IS_OSPF6_DEBUG_ZEBRA (RECV))
	{
	  char lladdrstr[INET6_ADDRSTRLEN];
	  ospf6_addr2str6 (&linkmetrics.linklocal_addr,
			   lladdrstr, sizeof (lladdrstr));
	  zlog_err ("%s: neighbor not found for ipv6 link-local address %s",
		    __func__, lladdrstr);
	}
      return -1;
    }

  ospf6_zebra_update_linkmetrics (on, &linkmetrics);

  return 0;
}

int
ospf6_zebra_linkstatus (int command, struct zclient *zclient,
			zebra_size_t length)
{
  zebra_linkstatus_t linkstatus;
  struct ospf6_interface *oi;
  struct ospf6_neighbor *on;

  assert (command == ZEBRA_LINKMETRICS_STATUS);

  if (zapi_read_linkstatus (&linkstatus, zclient->ibuf, length))
    {
      zlog_err ("%s: zapi_read_linkmetrics() failed", __func__);
      return -1;
    }

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    {
      zlog_debug ("%s: received link status update", __func__);
      zebra_linkstatus_logdebug (&linkstatus);
    }

  oi = ospf6_interface_lookup_by_ifindex (linkstatus.ifindex);
  if (oi == NULL)
    {
      zlog_err ("%s: unknown interface index: %d",
		__func__, linkstatus.ifindex);
      return -1;
    }

  if (!IN6_IS_ADDR_LINKLOCAL (&linkstatus.linklocal_addr))
    {
      if (IS_OSPF6_DEBUG_ZEBRA (RECV))
        {
          char lladdrstr[INET6_ADDRSTRLEN];
          ospf6_addr2str6 (&linkstatus.linklocal_addr,
                           lladdrstr, sizeof (lladdrstr));
          zlog_err ("%s: non-link-local neighbor address: %s",
                    __func__, lladdrstr);
        }
      return -1;
    }

  /* neighbor can be unknown only for LINKMETRICS_STATUS_UP events */
  on = ospf6_neighbor_lookup_by_ifaddr (&linkstatus.linklocal_addr, oi);
  if (on == NULL && linkstatus.status != LM_STATUS_UP)
    {
      char statusstr[40], lladdrstr[INET6_ADDRSTRLEN];

      zebra_linkstatus_string (statusstr, sizeof (statusstr),
			       linkstatus.status);
      ospf6_addr2str6 (&linkstatus.linklocal_addr,
		       lladdrstr, sizeof (lladdrstr));
      zlog_debug ("%s: neighbor not found for link status update "
		  "on interface %s from %s: %s",
		  __func__, oi->interface->name, lladdrstr, statusstr);
      return -1;
    }

  ospf6_run_linkstatus_hooks (oi, on, &linkstatus);

  return 0;
}
