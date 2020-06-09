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

#include "memory.h"
#include "if.h"
#include "log.h"
#include "command.h"
#include "thread.h"
#include "prefix.h"
#include "plist.h"

#include "ospf6_lsa.h"
#include "ospf6_lsdb.h"
#include "ospf6_network.h"
#include "ospf6_message.h"
#include "ospf6_route.h"
#include "ospf6_top.h"
#include "ospf6_area.h"
#include "ospf6_interface.h"
#include "ospf6_neighbor.h"
#include "ospf6_intra.h"
#include "ospf6_spf.h"
#include "ospf6d.h"
#include "ospf6_proto.h"
#include "ospf6_mdr_interface.h"
#include "ospf6_flood.h"
#include "ospf6_zebra.h"
#include "ospf6_af.h"
#include "ospf6_private_data.h"

static void
ospf6_interface_state_change (u_char next_state, struct ospf6_interface *oi);
static void
ospf6_interface_cost_change (struct ospf6_interface *oi);
static void
ospf6_interface_mtu_change (struct ospf6_interface *oi);

unsigned char conf_debug_ospf6_interface = 0;

const char *ospf6_interface_state_str[] =
{
  "None",
  "Down",
  "Loopback",
  "Waiting",
  "PointToPoint",
  "DROther",
  "BDR",
  "DR",
  NULL
};

static struct list ospf6_interface_operations_list;
static unsigned int ospf6_interface_init_called;

struct ospf6_interface *
ospf6_interface_lookup_by_ifindex (int ifindex)
{
  struct ospf6_interface *oi;
  struct interface *ifp;

  ifp = if_lookup_by_index (ifindex);
  if (ifp == NULL)
    return (struct ospf6_interface *) NULL;

  oi = (struct ospf6_interface *) ifp->info;
  return oi;
}

/* schedule routing table recalculation */
static void
ospf6_interface_lsdb_hook (struct ospf6_lsa *lsa)
{
  switch (ntohs (lsa->header->type))
    {
      case OSPF6_LSTYPE_LINK:
        if (OSPF6_INTERFACE (lsa->lsdb->data)->state == OSPF6_INTERFACE_DR)
          ospf6_intra_prefix_lsa_schedule_transit (OSPF6_INTERFACE (lsa->lsdb->data));
        ospf6_spf_schedule (OSPF6_INTERFACE (lsa->lsdb->data)->area);
        break;

      default:
        break;
    }
}

static void
ospf6_interface_lsdb_hook_replace (struct ospf6_lsa *old,
				   struct ospf6_lsa *new)
{
  assert (OSPF6_LSA_IS_SAME (old, new));
  ospf6_interface_lsdb_hook (new);
}

/* Create new ospf6 interface structure */
static struct ospf6_interface *
ospf6_interface_create (struct interface *ifp)
{
  struct ospf6_interface *oi;
  struct listnode *node;
  struct ospf6_interface_operations *ops;

  oi = (struct ospf6_interface *)
    XCALLOC (MTYPE_OSPF6_IF, sizeof (struct ospf6_interface));

  if (!oi)
    {
      zlog_err ("Can't malloc ospf6_interface for ifindex %d", ifp->ifindex);
      return (struct ospf6_interface *) NULL;
    }

  oi->area = (struct ospf6_area *) NULL;
  oi->neighbor_list = list_new ();
  oi->neighbor_list->cmp = ospf6_neighbor_cmp;
  oi->linklocal_addr = (struct in6_addr *) NULL;
  oi->transdelay = OSPF6_INTERFACE_TRANSDELAY;
  oi->priority = OSPF6_INTERFACE_PRIORITY;

  oi->hello_interval = OSPF6_INTERFACE_HELLO_INTERVAL;
  oi->dead_interval = OSPF6_INTERFACE_DEAD_INTERVAL;
  oi->rxmt_interval = OSPF6_INTERFACE_RXMT_INTERVAL;
  oi->cost = OSPF6_INTERFACE_COST;
  oi->cost_configured = false;
  oi->state = OSPF6_INTERFACE_DOWN;
  oi->flag = 0;
  oi->mtu_ignore = 0;

  oi->allow_immediate_hello = false;
  oi->initial_immediate_hello_delay = OSPF6_INITIAL_IMMEDIATE_HELLO_DELAY;
  oi->immediate_hello_delay = 0;

  oi->flood_delay = OSPF6_INTERFACE_FLOOD_DELAY;        //msec

  oi->lsupdate_list = ospf6_lsdb_create (oi);
  oi->lsack_list = ospf6_lsdb_create (oi);
  oi->lsdb = ospf6_lsdb_create (oi);
  oi->lsdb->hook_add = ospf6_interface_lsdb_hook;
  oi->lsdb->hook_remove = ospf6_interface_lsdb_hook;
  oi->lsdb->hook_replace = ospf6_interface_lsdb_hook_replace;
  oi->lsdb_self = ospf6_lsdb_create (oi);

  oi->route_connected = OSPF6_ROUTE_TABLE_CREATE (INTERFACE, CONNECTED_ROUTES);
  oi->route_connected->scope = oi;

  /* link both */
  oi->interface = ifp;
  ifp->info = oi;

  ospf6_mdr_interface_create (oi);

  oi->private_data_list = ospf6_private_data_list ();

  for (ALL_LIST_ELEMENTS_RO (&ospf6_interface_operations_list, node, ops))
    {
      int err;

      if (!ops->create)
	continue;

      err = ops->create (oi);
      if (err)
	{
	  zlog_err ("%s: per interface create function %p failed "
		    "for interface %s", __func__, ops->create,
		    oi->interface->name);
	  ospf6_interface_delete (oi);
	  return NULL;
	}
    }

  return oi;
}

static void
ospf6_interface_delete_neighbors (struct ospf6_interface *oi)
{
  struct list *neighbor_list;

  neighbor_list = oi->neighbor_list;
  while (!list_isempty (neighbor_list))
    {
      struct ospf6_neighbor *on;

      on = listnode_head (neighbor_list);
      listnode_delete (neighbor_list, on);
      ospf6_neighbor_delete (on);
    }
}

void
ospf6_interface_delete (struct ospf6_interface *oi)
{
  struct listnode *node;

  ospf6_interface_delete_neighbors (oi);

  for (node = listtail (&ospf6_interface_operations_list);
       node != NULL; node = node->prev)
    {
      struct ospf6_interface_operations *ops;

      ops = listgetdata (node);
      if (ops && ops->delete)
	ops->delete (oi);
    }
  
  list_delete (oi->neighbor_list);

  ospf6_mdr_interface_delete (oi);

  THREAD_OFF (oi->thread_send_hello);
  THREAD_OFF (oi->thread_send_lsupdate);
  THREAD_OFF (oi->thread_send_lsack);
  THREAD_OFF (oi->thread_network_lsa);
  THREAD_OFF (oi->thread_link_lsa);
  THREAD_OFF (oi->thread_intra_prefix_lsa);

  ospf6_lsdb_remove_all (oi->lsdb);
  ospf6_lsdb_remove_all (oi->lsupdate_list);
  ospf6_lsdb_remove_all (oi->lsack_list);

  ospf6_lsdb_delete (oi->lsdb);
  ospf6_lsdb_delete (oi->lsdb_self);

  ospf6_lsdb_delete (oi->lsupdate_list);
  ospf6_lsdb_delete (oi->lsack_list);

  ospf6_route_table_delete (oi->route_connected);

  if (oi->area != NULL)
    {
      listnode_delete (oi->area->if_list, oi);
      oi->area = NULL;
    }

  if (oi->interface != NULL)
    {
      /* cut link */
      oi->interface->info = NULL;
      oi->interface = NULL;
    }

  /* plist_name */
  if (oi->plist_name)
    XFREE (MTYPE_PREFIX_LIST_STR, oi->plist_name);

  if (!list_isempty (oi->private_data_list))
    zlog_err ("%s: possible memory leak: deleting oi->private_data_list "
	      "with %i elements", __func__, listcount(oi->private_data_list));
  list_delete (oi->private_data_list);

  XFREE (MTYPE_OSPF6_IF, oi);
}

static void
ospf6_interface_configure_defaults (struct ospf6_interface *oi)
{
  if (oi->type == OSPF6_IFTYPE_MDR)
    {
      ospf6_mdr_interface_configure_defaults (oi);
      return;
    }

  if (!(oi->config_status & HELLO_INTERVAL_CONFIGURED))
    oi->hello_interval = OSPF6_INTERFACE_HELLO_INTERVAL;

  if (!(oi->config_status & DEAD_INTERVAL_CONFIGURED))
    oi->dead_interval = OSPF6_INTERFACE_DEAD_INTERVAL;

  if (!(oi->config_status & RXMT_INTERVAL_CONFIGURED))
    oi->rxmt_interval = OSPF6_INTERFACE_RXMT_INTERVAL;

  if (!(oi->config_status & LINK_LSA_SUPPRESSION_CONFIGURED))
    oi->LinkLSASuppression = 0;
}

int
ospf6_register_interface_operations (struct ospf6_interface_operations *ops)
{
  struct listnode *node;
  struct ospf6_interface_operations *tmpops;

  for (ALL_LIST_ELEMENTS_RO (&ospf6_interface_operations_list, node, tmpops))
    if (tmpops == ops)
      {
	zlog_err ("%s: per interface operations already registered: %p",
		  __func__, ops);
	return -1;
      }

  listnode_add (&ospf6_interface_operations_list, ops);

  if (ospf6_interface_init_called && ops->init)
    ops->init ();

  if (ops->create && ospf6)
    {
      struct listnode *n;
      struct ospf6_area *oa;

      for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, n, oa))
	{
	  struct listnode *m;
	  struct ospf6_interface *oi;

	  for (ALL_LIST_ELEMENTS_RO (oa->if_list, m, oi))
	    {
	      int err;

	      err = ops->create (oi);
	      if (err)
		zlog_warn ("%s: per interface create function %p failed "
			   "for interface %s", __func__, ops->create,
			   oi->interface->name);
	    }
	}
    }

  return 0;
}

int
ospf6_add_interface_data (struct ospf6_interface *oi,
			  unsigned int *id, void *data)
{
  return ospf6_add_private_data (oi->private_data_list, id, data);
}

void *
ospf6_get_interface_data (struct ospf6_interface *oi, unsigned int id)
{
  return ospf6_get_private_data (oi->private_data_list, id);
}

void *
ospf6_del_interface_data (struct ospf6_interface *oi, unsigned int id)
{
  return ospf6_del_private_data (oi->private_data_list, id);
}

void
ospf6_interface_enable (struct ospf6_interface *oi)
{
  UNSET_FLAG (oi->flag, OSPF6_INTERFACE_DISABLE);

  if (oi->area)
    thread_execute (master, interface_up, oi, 0);
}

static void
__ospf6_interface_disable (struct ospf6_interface *oi)
{
  /* Leave AllSPFRouters */
  if (oi->state > OSPF6_INTERFACE_LOOPBACK)
    ospf6_sso (oi->interface->ifindex, &allspfrouters6, IPV6_LEAVE_GROUP);

  ospf6_interface_state_change (OSPF6_INTERFACE_DOWN, oi);

  /* delete all neighbors */
  ospf6_interface_delete_neighbors (oi);

  ospf6_lsdb_remove_all (oi->lsdb);
  ospf6_lsdb_remove_all (oi->lsdb_self);
  ospf6_lsdb_remove_all (oi->lsupdate_list);
  ospf6_lsdb_remove_all (oi->lsack_list);

  THREAD_OFF (oi->thread_send_hello);
  THREAD_OFF (oi->thread_send_lsupdate);
  THREAD_OFF (oi->thread_send_lsack);
  THREAD_OFF (oi->thread_network_lsa);
  THREAD_OFF (oi->thread_link_lsa);
  THREAD_OFF (oi->thread_intra_prefix_lsa);
}

void
ospf6_interface_disable (struct ospf6_interface *oi)
{
  if (CHECK_FLAG (oi->flag, OSPF6_INTERFACE_DISABLE))
    return;

  SET_FLAG (oi->flag, OSPF6_INTERFACE_DISABLE);

  __ospf6_interface_disable (oi);
}

void
ospf6_interface_if_add (struct interface *ifp)
{
  struct ospf6_interface *oi;

  oi = (struct ospf6_interface *) ifp->info;
  if (oi == NULL)
    return;

  /* interface start */
  if (oi->area && oi->area->ospf6 &&
      !CHECK_FLAG (oi->area->ospf6->flag, OSPF6_DISABLED))
    thread_add_event (master, interface_up, oi, 0);
}

void
ospf6_interface_if_del (struct interface *ifp)
{
  struct ospf6_interface *oi;

  oi = (struct ospf6_interface *) ifp->info;
  if (oi == NULL)
    return;

  /* interface stop */
  if (oi->area)
    thread_execute (master, interface_down, oi, 0);

  if (if_is_transient (ifp))
    ospf6_interface_delete (oi);
}

void
ospf6_interface_update_bandwidth (struct ospf6_interface *oi)
{
  u_int32_t oldcost;

  /* do nothing if an ospf interface cost was configured */
  if (oi->cost_configured)
    return;

  /* update the ospf cost */
  oldcost = oi->cost;

  if (oi->interface->bandwidth > 0)
    {
      unsigned int cost;

      cost = 1000 * oi->area->ospf6->auto_cost_reference_bandwidth /
        oi->interface->bandwidth;
      if (cost < 1)
        cost = 1;
      else if (cost > UINT16_MAX)
        cost = UINT16_MAX;

      oi->cost = cost;
    }
  else
    {
      oi->cost = OSPF6_INTERFACE_COST;
    }

  if (oi->cost != oldcost)
    {
      if (IS_OSPF6_DEBUG_INTERFACE)
        zlog_debug ("Interface %s: new cost: %u",
                    oi->interface->name, oi->cost);

      ospf6_interface_cost_change (oi);
    }
}

static void
ospf6_interface_mtu_change (struct ospf6_interface *oi)
{
  struct listnode *node;
  struct ospf6_neighbor *on;

  /* Try to adjust I/O buffer size with IfMtu */
  if (oi->ifmtu > 0)
    {
      size_t iobuflen;

      // RGO. Sometimes an OSPF packet must exceed MTU, so make iobuflen
      // twice as large as MTU.
      iobuflen = ospf6_iobuf_size (2 * oi->ifmtu);
      if (oi->ifmtu > iobuflen)
        {
	  if (IS_OSPF6_DEBUG_INTERFACE)
	    zlog_debug ("Interface %s: IfMtu is adjusted to I/O "
			"buffer size: %zu.", oi->interface->name, iobuflen);
          oi->ifmtu = iobuflen;
        }
    }

  /* re-establish adjacencies */
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
    {
      THREAD_OFF (on->inactivity_timer);
      thread_add_event (master, inactivity_timer, on, 0);
    }
}

static void
ospf6_interface_update_mtu (struct ospf6_interface *oi)
{
  struct interface *ifp = oi->interface;
  u_int32_t ifmtu = oi->ifmtu;

  /* TODO: should the device MTU be used if it increases and an OSPF
     MTU was not explicitly configured? */
  if (oi->ifmtu == 0)
    {
      oi->ifmtu = ifp->mtu6;
    }
  else if (ifp->mtu6 != 0 && oi->ifmtu > ifp->mtu6)
    {
      if (IS_OSPF6_DEBUG_INTERFACE)
	zlog_debug ("Interface %s: IfMtu cannot go beyond physical mtu (%d)",
		    ifp->name, ifp->mtu6);
      oi->ifmtu = ifp->mtu6;
    }

  if (oi->ifmtu != ifmtu)
    ospf6_interface_mtu_change (oi);
}

void
ospf6_interface_state_update (struct interface *ifp)
{
  struct ospf6_interface *oi;

  oi = (struct ospf6_interface *) ifp->info;
  if (oi == NULL)
    return;
  if (oi->area == NULL)
    return;
  if (oi->area->ospf6 == NULL ||
      CHECK_FLAG (oi->area->ospf6->flag, OSPF6_DISABLED))
    return;

  if (if_is_operative (ifp) && (ospf6_interface_has_linklocal_addr (oi) ||
                         oi->type == OSPF6_IFTYPE_LOOPBACK ||
                         CHECK_FLAG (oi->flag, OSPF6_INTERFACE_PASSIVE)))
    thread_add_event (master, interface_up, oi, 0);
  else
    thread_add_event (master, interface_down, oi, 0);

  return;
}

bool
ospf6_interface_has_linklocal_addr (struct ospf6_interface *oi)
{
  assert (oi->area != NULL);

  if (oi->linklocal_addr == NULL)
    return false;

  if (!IN6_IS_ADDR_LINKLOCAL (oi->linklocal_addr))
    {
      if (IS_OSPF6_DEBUG_INTERFACE)
	{
	  char buf[INET6_ADDRSTRLEN];
	  ospf6_addr2str6 (oi->linklocal_addr, buf, sizeof (buf));
	  zlog_debug ("Invalid link-local address for interface %s: %s",
		      oi->interface->name, buf);
	}
      return false;
    }

  if (ospf6_af_is_ipv4 (oi->area->ospf6) && oi->linklocal_addr_ipv4 == NULL)
    return false;

  return true;
}

static struct ospf6_route *
ospf6_interface_connected_route_add (struct ospf6_interface *oi,
                                     struct prefix *prefix,
                                     bool is_local_host_route)
{
  struct ospf6_route *route;

  /* apply filter */
  if (oi->plist_name)
    {
      struct prefix_list *plist;
      enum prefix_list_type ret;

      plist = prefix_list_lookup (family2afi (prefix->family), oi->plist_name);
      ret = prefix_list_apply (plist, (void *) prefix);
      if (ret == PREFIX_DENY)
        {
          if (IS_OSPF6_DEBUG_INTERFACE)
            {
              char buf[PREFIXSTRLEN];

              prefix2str (prefix, buf, sizeof (buf));
              zlog_debug ("connected prefix %s on %s filtered out by "
                          "prefix-list %s ",
                          buf, oi->interface->name, oi->plist_name);
            }
          return NULL;
        }
    }

  route = ospf6_route_create ();

  if (prefix->family == AF_INET)
    {
      int err;

      err = ospf6_af_prefix_convert4to6 ((struct prefix_ipv6 *) &route->prefix,
                                         (struct prefix_ipv4 *) prefix);
      if (err)
        {
          char buf[PREFIXSTRLEN];

          prefix2str (prefix, buf, sizeof (buf));
          zlog_warn ("%s: error converting connected prefix: %s",
                     __func__, buf);

          ospf6_route_delete (route);

          return NULL;
        }
    }
  else
    {
      memcpy (&route->prefix, prefix, sizeof (struct prefix));
    }

  route->type = OSPF6_DEST_TYPE_NETWORK;
  route->path.origin.adv_router = oi->area->ospf6->router_id;
  route->path.area_id = oi->area->area_id;
  route->path.type = OSPF6_PATH_TYPE_INTRA;
  route->path.metric_type = 1;
  if (!is_local_host_route)
    {
      route->path.cost = oi->cost;
      route->nexthop[0].ifindex = oi->interface->ifindex;
    }
  else
    {
      route->path.prefix_options |= OSPF6_PREFIX_OPTION_LA;
    }

  return ospf6_route_add (route, oi->route_connected);
}

void
ospf6_interface_connected_route_update (struct interface *ifp)
{
  struct ospf6_interface *oi;
  struct connected *c;
  struct listnode *node, *nnode;

  oi = (struct ospf6_interface *) ifp->info;
  if (oi == NULL)
    return;

  /* if area is null, do not make connected-route list */
  if (oi->area == NULL)
    return;

  /* reset linklocal pointer */
  oi->linklocal_addr = NULL;
  oi->linklocal_addr_ipv4 = NULL;

  /* update "route to advertise" interface route table */
  ospf6_route_remove_all (oi->route_connected);

  for (ALL_LIST_ELEMENTS (oi->interface->connected, node, nnode, c))
    {
      bool is_local_host_route;

      if (c->address->prefixlen == 0)
	{
	  if (IS_OSPF6_DEBUG_INTERFACE)
	    {
	      char buf[PREFIXSTRLEN];

	      prefix2str (c->address, buf, sizeof (buf));
	      zlog_debug ("Ignoring address %s on %s: prefix length is zero",
			  buf, oi->interface->name);
	    }
	  continue;
	}

      if (oi->linklocal_addr == NULL)
	{
	  if (c->address->family == AF_INET6 &&
	      IN6_IS_ADDR_LINKLOCAL (&c->address->u.prefix6))
	    oi->linklocal_addr = &c->address->u.prefix6;
	}

      //IPv4 Address Family
      if (ospf6_af_is_ipv4 (oi->area->ospf6))
        {
          if (c->address->family != AF_INET)
	    continue;

          //remove loopback interfaces
          CONTINUE_IF_V4_ADDRESS_LOOPBACK (IS_OSPF6_DEBUG_INTERFACE,
                                           c->address);

          is_local_host_route = (c->address->prefixlen == IPV4_MAX_PREFIXLEN);

	  /*
	   * RFC 5838:
	   *
	   * 2.5. Next-Hop Calculation for IPv4 Unicast and Multicast AFs
	   *
	   * ... the link's IPv4 address will be advertised in the
	   * "link local address" field of the IPv4 instance's
	   * Link-LSA.  This address is placed in the first 32 bits of
	   * the "link local address" field and is used for IPv4
	   * next-hop calculations.  The remaining bits MUST be set to
	   * zero.
	   */
	  /* Set the link local address for use in the Link-LSA */
	  if (oi->linklocal_addr_ipv4 == NULL)
	    oi->linklocal_addr_ipv4 = &c->address->u.prefix4;
        }
      //IPv6 Address Family
      else if (ospf6_af_is_ipv6 (oi->area->ospf6))
        {
          if (c->address->family != AF_INET6)
	    continue;

          CONTINUE_IF_ADDRESS_LINKLOCAL (IS_OSPF6_DEBUG_INTERFACE,
                                         c->address);
          CONTINUE_IF_ADDRESS_UNSPECIFIED (IS_OSPF6_DEBUG_INTERFACE,
                                           c->address);
	  CONTINUE_IF_ADDRESS_LOOPBACK (IS_OSPF6_DEBUG_INTERFACE, c->address);
	  CONTINUE_IF_ADDRESS_V4COMPAT (IS_OSPF6_DEBUG_INTERFACE, c->address);
	  CONTINUE_IF_ADDRESS_V4MAPPED (IS_OSPF6_DEBUG_INTERFACE, c->address);

          is_local_host_route = (c->address->prefixlen == IPV6_MAX_PREFIXLEN);
	}
      else
        {
          continue;
        }

      ospf6_interface_connected_route_add (oi, c->address, is_local_host_route);

      if (c->destination && CONNECTED_PEER (c))
        ospf6_interface_connected_route_add (oi, c->destination, false);
    }

  /* create new Link-LSA */
  ospf6_link_lsa_schedule (oi);
  ospf6_intra_prefix_lsa_schedule_transit (oi);
  ospf6_intra_prefix_lsa_schedule_stub (oi->area);
}

bool
ospf6_interface_prefix_is_connected (struct ospf6_interface *oi,
				     struct prefix *prefix)
{
  struct ospf6_route *route;

  route = ospf6_route_lookup (prefix, oi->route_connected);
  if (route != NULL)
    return true;

  return false;
}

bool
ospf6_area_prefix_is_connected (struct ospf6_area *oa, struct prefix *prefix)
{
  struct ospf6_interface *oi;
  struct listnode *node;

  for (ALL_LIST_ELEMENTS_RO (oa->if_list, node, oi))
    {
      if (ospf6_interface_prefix_is_connected (oi, prefix))
	return true;
    }

  return false;
}

bool
ospf6_prefix_is_connected (struct ospf6 *o, struct prefix *prefix)
{
  struct ospf6_area *oa;
  struct listnode *node;

  for (ALL_LIST_ELEMENTS_RO (o->area_list, node, oa))
    {
      if (ospf6_area_prefix_is_connected (oa, prefix))
	return true;
    }

  return false;
}

static void
ospf6_interface_state_change (u_char next_state, struct ospf6_interface *oi)
{
  u_char prev_state;

  prev_state = oi->state;
  oi->state = next_state;

  if (prev_state == next_state)
    return;

  /* log */
  if (IS_OSPF6_DEBUG_INTERFACE)
    {
      zlog_debug ("Interface state change %s: %s -> %s", oi->interface->name,
		  ospf6_interface_state_str[prev_state],
		  ospf6_interface_state_str[next_state]);
    }

  if ((prev_state == OSPF6_INTERFACE_DR ||
       prev_state == OSPF6_INTERFACE_BDR) &&
      (next_state != OSPF6_INTERFACE_DR &&
       next_state != OSPF6_INTERFACE_BDR))
    ospf6_sso (oi->interface->ifindex, &alldrouters6, IPV6_LEAVE_GROUP);
  if ((prev_state != OSPF6_INTERFACE_DR &&
       prev_state != OSPF6_INTERFACE_BDR) &&
      (next_state == OSPF6_INTERFACE_DR ||
       next_state == OSPF6_INTERFACE_BDR))
    ospf6_sso (oi->interface->ifindex, &alldrouters6, IPV6_JOIN_GROUP);

  ospf6_router_lsa_schedule (oi->area);
  if (next_state == OSPF6_INTERFACE_DOWN)
    {
      ospf6_network_lsa_execute (oi);
      ospf6_intra_prefix_lsa_execute_transit (oi);
      ospf6_intra_prefix_lsa_schedule_stub (oi->area);
    }
  else if (prev_state == OSPF6_INTERFACE_DR ||
           next_state == OSPF6_INTERFACE_DR)
    {
      ospf6_network_lsa_schedule (oi);
      ospf6_intra_prefix_lsa_schedule_transit (oi);
      ospf6_intra_prefix_lsa_schedule_stub (oi->area);
    }
}


/* DR Election, RFC2328 section 9.4 */

#define IS_ELIGIBLE(n) \
  ((n)->state >= OSPF6_NEIGHBOR_TWOWAY && (n)->priority != 0)

static struct ospf6_neighbor *
better_bdrouter (struct ospf6_neighbor *a, struct ospf6_neighbor *b)
{
  if ((a == NULL || ! IS_ELIGIBLE (a) || a->drouter == a->router_id) &&
      (b == NULL || ! IS_ELIGIBLE (b) || b->drouter == b->router_id))
    return NULL;
  else if (a == NULL || ! IS_ELIGIBLE (a) || a->drouter == a->router_id)
    return b;
  else if (b == NULL || ! IS_ELIGIBLE (b) || b->drouter == b->router_id)
    return a;

  if (a->bdrouter == a->router_id && b->bdrouter != b->router_id)
    return a;
  if (a->bdrouter != a->router_id && b->bdrouter == b->router_id)
    return b;

  if (a->priority > b->priority)
    return a;
  if (a->priority < b->priority)
    return b;

  if (ntohl (a->router_id) > ntohl (b->router_id))
    return a;
  if (ntohl (a->router_id) < ntohl (b->router_id))
    return b;

  zlog_warn ("Router-ID duplicate ?");
  return a;
}

static struct ospf6_neighbor *
better_drouter (struct ospf6_neighbor *a, struct ospf6_neighbor *b)
{
  if ((a == NULL || ! IS_ELIGIBLE (a) || a->drouter != a->router_id) &&
      (b == NULL || ! IS_ELIGIBLE (b) || b->drouter != b->router_id))
    return NULL;
  else if (a == NULL || ! IS_ELIGIBLE (a) || a->drouter != a->router_id)
    return b;
  else if (b == NULL || ! IS_ELIGIBLE (b) || b->drouter != b->router_id)
    return a;

  if (a->drouter == a->router_id && b->drouter != b->router_id)
    return a;
  if (a->drouter != a->router_id && b->drouter == b->router_id)
    return b;

  if (a->priority > b->priority)
    return a;
  if (a->priority < b->priority)
    return b;

  if (ntohl (a->router_id) > ntohl (b->router_id))
    return a;
  if (ntohl (a->router_id) < ntohl (b->router_id))
    return b;

  zlog_warn ("Router-ID duplicate ?");
  return a;
}

static u_char
dr_election (struct ospf6_interface *oi)
{
  struct listnode *node, *nnode;
  struct ospf6_neighbor *on, *drouter, *bdrouter, myself;
  struct ospf6_neighbor *best_drouter, *best_bdrouter;
  u_char next_state = 0;

  drouter = bdrouter = NULL;
  best_drouter = best_bdrouter = NULL;

  /* pseudo neighbor myself, including noting current DR/BDR (1) */
  memset (&myself, 0, sizeof (myself));
  ospf6_id2str (oi->area->ospf6->router_id, myself.name, sizeof (myself.name));
  myself.state = OSPF6_NEIGHBOR_TWOWAY;
  myself.drouter = oi->drouter;
  myself.bdrouter = oi->bdrouter;
  myself.priority = oi->priority;
  myself.router_id = oi->area->ospf6->router_id;

  /* Electing BDR (2) */
  for (ALL_LIST_ELEMENTS (oi->neighbor_list, node, nnode, on))
    bdrouter = better_bdrouter (bdrouter, on);
  
  best_bdrouter = bdrouter;
  bdrouter = better_bdrouter (best_bdrouter, &myself);

  /* Electing DR (3) */
  for (ALL_LIST_ELEMENTS (oi->neighbor_list, node, nnode, on))
    drouter = better_drouter (drouter, on);

  best_drouter = drouter;
  drouter = better_drouter (best_drouter, &myself);
  if (drouter == NULL)
    drouter = bdrouter;

  /* the router itself is newly/no longer DR/BDR (4) */
  if ((drouter == &myself && myself.drouter != myself.router_id) ||
      (drouter != &myself && myself.drouter == myself.router_id) ||
      (bdrouter == &myself && myself.bdrouter != myself.router_id) ||
      (bdrouter != &myself && myself.bdrouter == myself.router_id))
    {
      myself.drouter = (drouter ? drouter->router_id : htonl (0));
      myself.bdrouter = (bdrouter ? bdrouter->router_id : htonl (0));

      /* compatible to Electing BDR (2) */
      bdrouter = better_bdrouter (best_bdrouter, &myself);

      /* compatible to Electing DR (3) */
      drouter = better_drouter (best_drouter, &myself);
      if (drouter == NULL)
        drouter = bdrouter;
    }

  /* Set interface state accordingly (5) */
  if (drouter && drouter == &myself)
    next_state = OSPF6_INTERFACE_DR;
  else if (bdrouter && bdrouter == &myself)
    next_state = OSPF6_INTERFACE_BDR;
  else
    next_state = OSPF6_INTERFACE_DROTHER;

  /* If NBMA, schedule Start for each neighbor having priority of 0 (6) */
  /* XXX */

  /* If DR or BDR change, invoke AdjOK? for each neighbor (7) */
  /* RFC 2328 section 12.4. Originating LSAs (3) will be handled
     accordingly after AdjOK */
  if (oi->drouter != (drouter ? drouter->router_id : htonl (0)) ||
      oi->bdrouter != (bdrouter ? bdrouter->router_id : htonl (0)))
    {
      if (IS_OSPF6_DEBUG_INTERFACE)
        zlog_debug ("DR Election on %s: DR: %s BDR: %s", oi->interface->name,
		    (drouter ? drouter->name : "0.0.0.0"),
		    (bdrouter ? bdrouter->name : "0.0.0.0"));

      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
        {
          if (on->state < OSPF6_NEIGHBOR_TWOWAY)
            continue;
          /* Schedule AdjOK. */
	  ospf6_neighbor_schedule_adjok (on);
        }
    }

  oi->drouter = (drouter ? drouter->router_id : htonl (0));
  oi->bdrouter = (bdrouter ? bdrouter->router_id : htonl (0));
  return next_state;
}


/* Interface State Machine */
int
interface_up (struct thread *thread)
{
  struct ospf6_interface *oi;
  u_char state;

  oi = (struct ospf6_interface *) THREAD_ARG (thread);
  assert (oi && oi->interface);

  if (IS_OSPF6_DEBUG_INTERFACE)
    zlog_debug ("Interface Event %s: [InterfaceUp]",
		oi->interface->name);

  /* check physical interface is up */
  if (! if_is_operative (oi->interface))
    {
      if (IS_OSPF6_DEBUG_INTERFACE)
        zlog_debug ("Interface %s is down, can't execute [InterfaceUp]",
		    oi->interface->name);
      return 0;
    }

  /* update interface type (if needed) */
  if (oi->type == OSPF6_IFTYPE_NONE)
    {
      if (if_is_broadcast (oi->interface))
        oi->type = OSPF6_IFTYPE_BROADCAST;
      else if (if_is_pointopoint (oi->interface))
        oi->type = OSPF6_IFTYPE_POINTOPOINT;
      else if (if_is_loopback (oi->interface))
        oi->type = OSPF6_IFTYPE_LOOPBACK;
      else
        oi->type = OSPF6_IFTYPE_BROADCAST;
    }

  /* update interface bandwidth (if needed) */
  ospf6_interface_update_bandwidth (oi);

  /* update interface mtu (if needed) */
  ospf6_interface_update_mtu (oi);

  /* if already enabled, do nothing */
  if (oi->state > OSPF6_INTERFACE_DOWN)
    {
      if (IS_OSPF6_DEBUG_INTERFACE)
        zlog_debug ("Interface %s already enabled",
		    oi->interface->name);
      return 0;
    }

  /* Update interface route */
  ospf6_interface_connected_route_update (oi->interface);

  if (oi->type != OSPF6_IFTYPE_LOOPBACK &&
      !CHECK_FLAG (oi->flag, OSPF6_INTERFACE_PASSIVE))
    {
      if (!ospf6_interface_has_linklocal_addr (oi))
	{
	  if (IS_OSPF6_DEBUG_INTERFACE)
	    zlog_debug ("Interface %s can't execute [InterfaceUp]: "
			"no link-local address", oi->interface->name);
	  return 0;
	}
    }

  /* decide next interface state */
  if (CHECK_FLAG (oi->flag, OSPF6_INTERFACE_PASSIVE))
    {
      state = OSPF6_INTERFACE_LOOPBACK;
    }
  else
    {
      switch (oi->type)
	{
	case OSPF6_IFTYPE_LOOPBACK:
	  state = OSPF6_INTERFACE_LOOPBACK;
	  break;

	case OSPF6_IFTYPE_POINTOPOINT:
	case OSPF6_IFTYPE_POINTOMULTIPOINT:
	case OSPF6_IFTYPE_MDR:
	  state = OSPF6_INTERFACE_POINTTOPOINT;
	  break;

	case OSPF6_IFTYPE_BROADCAST:
	case OSPF6_IFTYPE_NBMA:
	  if (oi->priority == 0)
	    state = OSPF6_INTERFACE_DROTHER;
	  else
	    state = OSPF6_INTERFACE_WAITING;
	  break;

	default:
	  state = OSPF6_INTERFACE_NONE;
	  break;
	}
    }

  ospf6_interface_state_change (state, oi);
  if (state == OSPF6_INTERFACE_WAITING)
    thread_add_timer (master, wait_timer, oi, oi->dead_interval);

  /* Schedule Hello */
  if (state > OSPF6_INTERFACE_LOOPBACK)
    {
      /* Join AllSPFRouters */
      ospf6_sso (oi->interface->ifindex, &allspfrouters6, IPV6_JOIN_GROUP);

      THREAD_OFF (oi->thread_send_hello);
      oi->thread_send_hello =
	thread_add_event (master, ospf6_hello_send, oi, 0);
    }

  return 0;
}

int
wait_timer (struct thread *thread)
{
  struct ospf6_interface *oi;

  oi = (struct ospf6_interface *) THREAD_ARG (thread);
  assert (oi && oi->interface);

  if (IS_OSPF6_DEBUG_INTERFACE)
    zlog_debug ("Interface Event %s: [WaitTimer]",
		oi->interface->name);

  if (oi->state == OSPF6_INTERFACE_WAITING)
    ospf6_interface_state_change (dr_election (oi), oi);

  return 0;
}

int
backup_seen (struct thread *thread)
{
  struct ospf6_interface *oi;

  oi = (struct ospf6_interface *) THREAD_ARG (thread);
  assert (oi && oi->interface);

  if (IS_OSPF6_DEBUG_INTERFACE)
    zlog_debug ("Interface Event %s: [BackupSeen]",
		oi->interface->name);

  if (oi->state == OSPF6_INTERFACE_WAITING)
    ospf6_interface_state_change (dr_election (oi), oi);

  return 0;
}

int
neighbor_change (struct thread *thread)
{
  struct ospf6_interface *oi;

  oi = (struct ospf6_interface *) THREAD_ARG (thread);
  assert (oi && oi->interface);

  if (IS_OSPF6_DEBUG_INTERFACE)
    zlog_debug ("Interface Event %s: [NeighborChange]",
		oi->interface->name);

  if (oi->state == OSPF6_INTERFACE_DROTHER ||
      oi->state == OSPF6_INTERFACE_BDR ||
      oi->state == OSPF6_INTERFACE_DR)
    ospf6_interface_state_change (dr_election (oi), oi);

  return 0;
}

int
interface_down (struct thread *thread)
{
  struct ospf6_interface *oi;

  oi = (struct ospf6_interface *) THREAD_ARG (thread);
  assert (oi && oi->interface);

  if (IS_OSPF6_DEBUG_INTERFACE)
    zlog_debug ("Interface Event %s: [InterfaceDown]",
		oi->interface->name);

  __ospf6_interface_disable (oi);

  return 0;
}


/* show specified interface structure */
static int
ospf6_interface_show (struct vty *vty, struct interface *ifp)
{
  struct ospf6_interface *oi;
  struct connected *c;
  struct prefix *p;
  struct listnode *i;
  char strbuf[PREFIXSTRLEN], drouter[16], bdrouter[16];
  const char *updown[3] = {"down", "up", NULL};
  const char *type;
  struct timeval res, now;
  char duration[32];
  struct ospf6_lsa *lsa;

  /* check physical interface type */
  if (if_is_loopback (ifp))
    type = "LOOPBACK";
  else if (if_is_broadcast (ifp))
    type = "BROADCAST";
  else if (if_is_pointopoint (ifp))
    type = "POINTOPOINT";
  else
    type = "UNKNOWN";

  vty_out (vty, "%s is %s, type %s%s",
           ifp->name, updown[if_is_operative (ifp)], type,
	   VNL);
  vty_out (vty, "  Interface ID: %d%s", ifp->ifindex, VNL);

  if (ifp->info == NULL)
    {
      vty_out (vty, "   OSPF not enabled on this interface%s", VNL);
      return 0;
    }
  else
    oi = (struct ospf6_interface *) ifp->info;

  if (oi->type == OSPF6_IFTYPE_BROADCAST)
    type = "BROADCAST";
  else if (oi->type == OSPF6_IFTYPE_LOOPBACK)
    type = "LOOPBACK";
  else if (oi->type == OSPF6_IFTYPE_NBMA)
    type = "NBMA";
  else if (oi->type == OSPF6_IFTYPE_POINTOMULTIPOINT)
    type = "POINT TO MULTIPOINT";
  else if (oi->type == OSPF6_IFTYPE_MDR)
    type = "OSPF MANET MDR";
  else if (oi->type == OSPF6_IFTYPE_POINTOPOINT)
    type = "POINT TO POINT";
  else
    type = "UNKNOWN";
  vty_out (vty, "  OSPF6 type %s%s", type, VTY_NEWLINE);

  if (oi->type == OSPF6_IFTYPE_MDR)
    ospf6_mdr_interface_show (vty, oi);

  vty_out (vty, "  Internet Address:%s", VNL);

  for (ALL_LIST_ELEMENTS_RO (ifp->connected, i, c))
    {
      bool peer = false;
      char peerbuf[PREFIXSTRLEN];

      p = c->address;
      prefix2str (p, strbuf, sizeof (strbuf));

      if (c->destination && CONNECTED_PEER (c))
        {
          peer = true;
          prefix2str (c->destination, peerbuf, sizeof (peerbuf));
        }

      switch (p->family)
        {
        case AF_INET:
          vty_out (vty, "    inet : %s", strbuf);
          break;
        case AF_INET6:
          vty_out (vty, "    inet6: %s", strbuf);
          break;
        default:
          vty_out (vty, "    ???  : %s", strbuf);
          break;
        }

      if (peer)
        vty_out (vty, " peer %s", peerbuf);

      vty_out (vty, VNL);
    }

  if (oi->area)
    {
      if (oi->ifmtu)
        snprintf(strbuf, sizeof (strbuf), "%u", oi->ifmtu);
      else
        snprintf(strbuf, sizeof (strbuf), "not set");
      vty_out (vty, "  Interface MTU %s (autodetect: %u)%s",
	       strbuf, ifp->mtu6, VNL);
      vty_out (vty, "  MTU mismatch detection: %s%s", oi->mtu_ignore ?
	       "disabled" : "enabled", VNL);
      ospf6_id2str (oi->area->area_id, strbuf, sizeof (strbuf));
      vty_out (vty, "  Area ID %s, Cost %hu%s", strbuf, oi->cost,
	       VNL);
    }
  else
    vty_out (vty, "  Not Attached to Area%s", VNL);

  vty_out (vty, "  State %s, Transmit Delay %d sec, Priority %d%s",
           ospf6_interface_state_str[oi->state],
           oi->transdelay, oi->priority,
	   VNL);
  vty_out (vty, "  Timer intervals configured:%s", VNL);
  vty_out (vty, "   Hello %d, Dead %d, Retransmit %d%s",
           oi->hello_interval, oi->dead_interval, oi->rxmt_interval,
	   VNL);

  ospf6_id2str (oi->drouter, drouter, sizeof (drouter));
  ospf6_id2str (oi->bdrouter, bdrouter, sizeof (bdrouter));
  vty_out (vty, "  DR: %s BDR: %s%s", drouter, bdrouter, VNL);

  vty_out (vty, "  Number of I/F scoped LSAs is %u%s",
           oi->lsdb->count, VNL);

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);

  timerclear (&res);
  if (oi->thread_send_lsupdate)
    timersub (&oi->thread_send_lsupdate->u.sands, &now, &res);
  timerstring (&res, duration, sizeof (duration));
  vty_out (vty, "    %d Pending LSAs for LSUpdate in Time %s [thread %s]%s",
           oi->lsupdate_list->count, duration,
           (oi->thread_send_lsupdate ? "on" : "off"),
           VNL);
  for (lsa = ospf6_lsdb_head (oi->lsupdate_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    vty_out (vty, "      %s%s", lsa->name, VNL);

  timerclear (&res);
  if (oi->thread_send_lsack)
    timersub (&oi->thread_send_lsack->u.sands, &now, &res);
  timerstring (&res, duration, sizeof (duration));
  vty_out (vty, "    %d Pending LSAs for LSAck in Time %s [thread %s]%s",
           oi->lsack_list->count, duration,
           (oi->thread_send_lsack ? "on" : "off"),
           VNL);
  for (lsa = ospf6_lsdb_head (oi->lsack_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    vty_out (vty, "      %s%s", lsa->name, VNL);

  return 0;
}

/* show interface */
DEFUN (show_ipv6_ospf6_interface,
       show_ipv6_ospf6_interface_ifname_cmd,
       "show ipv6 ospf6 interface IFNAME",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       INTERFACE_STR
       IFNAME_STR
       )
{
  struct interface *ifp;
  struct listnode *i;

  if (argc)
    {
      ifp = if_lookup_by_name (argv[0]);
      if (ifp == NULL)
        {
          vty_out (vty, "No such Interface: %s%s", argv[0],
                   VNL);
          return CMD_WARNING;
        }
      ospf6_interface_show (vty, ifp);
    }
  else
    {
      for (ALL_LIST_ELEMENTS_RO (iflist, i, ifp))
        ospf6_interface_show (vty, ifp);
    }

  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_interface,
       show_ipv6_ospf6_interface_cmd,
       "show ipv6 ospf6 interface",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       INTERFACE_STR
       )

DEFUN (show_ipv6_ospf6_interface_ifname_prefix,
       show_ipv6_ospf6_interface_ifname_prefix_cmd,
       "show ipv6 ospf6 interface IFNAME prefix",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       INTERFACE_STR
       IFNAME_STR
       "Display connected prefixes to advertise\n"
       )
{
  struct interface *ifp;
  struct ospf6_interface *oi;

  ifp = if_lookup_by_name (argv[0]);
  if (ifp == NULL)
    {
      vty_out (vty, "No such Interface: %s%s", argv[0], VNL);
      return CMD_WARNING;
    }

  oi = ifp->info;
  if (oi == NULL)
    {
      vty_out (vty, "OSPFv3 is not enabled on %s%s", argv[0], VNL);
      return CMD_WARNING;
    }

  argc--;
  argv++;
  ospf6_route_table_show (vty, argc, argv, oi->route_connected);

  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_interface_ifname_prefix,
       show_ipv6_ospf6_interface_ifname_prefix_detail_cmd,
       "show ipv6 ospf6 interface IFNAME prefix (X:X::X:X|X:X::X:X/M|A.B.C.D|A.B.C.D/M|detail)",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       INTERFACE_STR
       IFNAME_STR
       "Display connected prefixes to advertise\n"
       OSPF6_ROUTE_ADDRESS_STR
       OSPF6_ROUTE_PREFIX_STR
       OSPF6_ROUTE_ADDRESS_STR
       OSPF6_ROUTE_PREFIX_STR
       "Display details of the prefixes\n"
       )

ALIAS (show_ipv6_ospf6_interface_ifname_prefix,
       show_ipv6_ospf6_interface_ifname_prefix_match_cmd,
       "show ipv6 ospf6 interface IFNAME prefix (X:X::X:X/M|A.B.C.D/M) (match|detail)",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       INTERFACE_STR
       IFNAME_STR
       "Display connected prefixes to advertise\n"
       OSPF6_ROUTE_PREFIX_STR
       OSPF6_ROUTE_PREFIX_STR
       OSPF6_ROUTE_MATCH_STR
       "Display details of the prefixes\n"
       )

DEFUN (show_ipv6_ospf6_interface_prefix,
       show_ipv6_ospf6_interface_prefix_cmd,
       "show ipv6 ospf6 interface prefix",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       INTERFACE_STR
       "Display connected prefixes to advertise\n"
       )
{
  struct listnode *i;
  struct ospf6_interface *oi;
  struct interface *ifp;

  for (ALL_LIST_ELEMENTS_RO (iflist, i, ifp))
    {
      oi = (struct ospf6_interface *) ifp->info;
      if (oi == NULL)
        continue;

      ospf6_route_table_show (vty, argc, argv, oi->route_connected);
    }

  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_interface_prefix,
       show_ipv6_ospf6_interface_prefix_detail_cmd,
       "show ipv6 ospf6 interface prefix (X:X::X:X|X:X::X:X/M|A.B.C.D|A.B.C.D/M|detail)",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       INTERFACE_STR
       "Display connected prefixes to advertise\n"
       OSPF6_ROUTE_ADDRESS_STR
       OSPF6_ROUTE_PREFIX_STR
       OSPF6_ROUTE_ADDRESS_STR
       OSPF6_ROUTE_PREFIX_STR
       "Display details of the prefixes\n"
       )

ALIAS (show_ipv6_ospf6_interface_prefix,
       show_ipv6_ospf6_interface_prefix_match_cmd,
       "show ipv6 ospf6 interface prefix (X:X::X:X/M|A.B.C.D/M) (match|detail)",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       INTERFACE_STR
       "Display connected prefixes to advertise\n"
       OSPF6_ROUTE_PREFIX_STR
       OSPF6_ROUTE_PREFIX_STR
       OSPF6_ROUTE_MATCH_STR
       "Display details of the prefixes\n"
       )

struct ospf6_interface *
ospf6_interface_get (struct interface *ifp)
{
  struct ospf6_interface *oi;

  oi = ifp->info;
  if (oi == NULL)
    oi = ospf6_interface_create (ifp);
  assert (oi);

  return oi;
}

struct ospf6_interface *
ospf6_interface_vtyget (struct vty *vty)
{
  struct interface *ifp;

  ifp = vty->index;
  assert (ifp);

  return ospf6_interface_get (ifp);
}

/* interface variable set command */
DEFUN (ipv6_ospf6_ifmtu,
       ipv6_ospf6_ifmtu_cmd,
       "ipv6 ospf6 ifmtu <1-65535>",
       IP6_STR
       OSPF6_STR
       "Interface MTU\n"
       "OSPFv3 Interface MTU\n"
       )
{
  struct ospf6_interface *oi;
  struct interface *ifp;
  unsigned int ifmtu;
  u_int32_t prev_ifmtu;
  int r;

  oi = ospf6_interface_vtyget (vty);
  ifp = oi->interface;
  assert (ifp);

  ifmtu = strtol (argv[0], NULL, 10);

  if (oi->ifmtu == ifmtu)
    return CMD_SUCCESS;

  prev_ifmtu = oi->ifmtu;
  oi->ifmtu = ifmtu;

  if (ifp->mtu6 != 0 && ifp->mtu6 < ifmtu)
    {
      vty_out (vty, "Limiting OSPF MTU for interface %s to device MTU: %u%s",
               ifp->name, ifp->mtu6, VNL);
      oi->ifmtu = ifp->mtu6;
      r = CMD_WARNING;
    }
  else
    {
      r = CMD_SUCCESS;
    }

  if (oi->ifmtu != prev_ifmtu)
    ospf6_interface_mtu_change (oi);

  return r;
}

DEFUN (no_ipv6_ospf6_ifmtu,
       no_ipv6_ospf6_ifmtu_cmd,
       "no ipv6 ospf6 ifmtu",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Interface MTU\n"
       )
{
  struct ospf6_interface *oi;
  struct interface *ifp;

  oi = ospf6_interface_vtyget (vty);
  ifp = oi->interface;
  assert (ifp);

  if (oi->ifmtu == ifp->mtu6)
    return CMD_SUCCESS;

  oi->ifmtu = ifp->mtu6;

  ospf6_interface_mtu_change (oi);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_cost,
       ipv6_ospf6_cost_cmd,
       "ipv6 ospf6 cost <1-65535>",
       IP6_STR
       OSPF6_STR
       "Interface cost\n"
       "Outgoing metric of this interface\n"
       )
{
  struct ospf6_interface *oi;
  unsigned long int lcost;

  oi = ospf6_interface_vtyget (vty);

  lcost = strtol (argv[0], NULL, 10);

  if (lcost > UINT32_MAX)
    {
      vty_out (vty, "Cost %ld is out of range%s", lcost, VNL);
      return CMD_WARNING;
    }
  
  oi->cost_configured = true;
  if (oi->cost == lcost)
    return CMD_SUCCESS;
  
  oi->cost = lcost;
  
  ospf6_interface_cost_change (oi);

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_cost,
       no_ipv6_ospf6_cost_cmd,
       "no ipv6 ospf6 cost",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Interface cost\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->cost_configured = false;
  ospf6_interface_update_bandwidth (oi);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_hellointerval,
       ipv6_ospf6_hellointerval_cmd,
       "ipv6 ospf6 hello-interval <1-65535>",
       IP6_STR
       OSPF6_STR
       "Interval time of Hello packets\n"
       SECONDS_STR
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->hello_interval = strtol (argv[0], NULL, 10);
  oi->config_status |= HELLO_INTERVAL_CONFIGURED;

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_flooddelay,
       ipv6_ospf6_flooddelay_cmd,
       "ipv6 ospf6 flood-delay <1-65535>",
       IP6_STR
       OSPF6_STR
       "Time in msec to coalesce LSAs before sending\n"
       SECONDS_STR
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->flood_delay = strtol (argv[0], NULL, 10);
  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_link_lsa_suppression,
       ipv6_ospf6_link_lsa_suppression_cmd,
       "ipv6 ospf6 link-lsa-suppression",
       IP6_STR
       OSPF6_STR
       "Enable link-LSA suppression\n")
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->LinkLSASuppression = 1;
  oi->config_status |= LINK_LSA_SUPPRESSION_CONFIGURED;

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_link_lsa_suppression,
       no_ipv6_ospf6_link_lsa_suppression_cmd,
       "no ipv6 ospf6 link-lsa-suppression",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Disable link-LSA suppression\n")
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->LinkLSASuppression = 0;
  oi->config_status |= LINK_LSA_SUPPRESSION_CONFIGURED;

  return CMD_SUCCESS;
}

/* interface variable set command */
DEFUN (ipv6_ospf6_deadinterval,
       ipv6_ospf6_deadinterval_cmd,
       "ipv6 ospf6 dead-interval <1-65535>",
       IP6_STR
       OSPF6_STR
       "Interval time after which a neighbor is declared down\n"
       SECONDS_STR
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->dead_interval = strtol (argv[0], NULL, 10);
  oi->config_status |= DEAD_INTERVAL_CONFIGURED;

  return CMD_SUCCESS;
}

/* interface variable set command */
DEFUN (ipv6_ospf6_transmitdelay,
       ipv6_ospf6_transmitdelay_cmd,
       "ipv6 ospf6 transmit-delay <1-3600>",
       IP6_STR
       OSPF6_STR
       "Transmit delay of this interface\n"
       SECONDS_STR
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->transdelay = strtol (argv[0], NULL, 10);
  return CMD_SUCCESS;
}

/* interface variable set command */
DEFUN (ipv6_ospf6_retransmitinterval,
       ipv6_ospf6_retransmitinterval_cmd,
       "ipv6 ospf6 retransmit-interval <1-65535>",
       IP6_STR
       OSPF6_STR
       "Time between retransmitting lost link state advertisements\n"
       SECONDS_STR
       )
{
  struct ospf6_interface *oi;
  long tmp;

  oi = ospf6_interface_vtyget (vty);

  tmp = strtol (argv[0], NULL, 10);
  if (1000 * tmp < oi->mdr.ackInterval)
    {
      vty_out (vty, "ERROR: ack interval cannot exceed retransmit interval%s",
	       VNL);
      return CMD_WARNING;
    }

  oi->rxmt_interval = tmp;
  oi->config_status |= RXMT_INTERVAL_CONFIGURED;

  return CMD_SUCCESS;
}

/* interface variable set command */
DEFUN (ipv6_ospf6_priority,
       ipv6_ospf6_priority_cmd,
       "ipv6 ospf6 priority <0-255>",
       IP6_STR
       OSPF6_STR
       "Router priority\n"
       "Priority value\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->priority = strtol (argv[0], NULL, 10);

  if (oi->area)
    ospf6_interface_state_change (dr_election (oi), oi);

  return CMD_SUCCESS;
}

DEFUN_DEPRECATED (ipv6_ospf6_instance,
       ipv6_ospf6_instance_cmd,
       "ipv6 ospf6 instance-id <0-255>",
       IP6_STR
       OSPF6_STR
       "Configure OSPFv3 Instance ID\n"
       "Instance ID value\n"
       )
{
  uint8_t instance_id;

  if (ospf6 == NULL)
    ospf6 = ospf6_create ();
  assert (ospf6);

  vty_out (vty, "WARNING: configure instance-id under "
	   "'router ospf6' instead%s", VNL);

  instance_id = atoi (argv[0]);

  if (ospf6->instance_id == instance_id)
    return CMD_SUCCESS;

  if (!CHECK_FLAG (ospf6->flag, OSPF6_DISABLED))
    {
      vty_out (vty, "Cannot assign instance ID %u: "
	       "OSPFv3 instance %u already enabled%s",
	       instance_id, ospf6->instance_id, VNL);
      return CMD_WARNING;
    }

  if (ospf6->instance_id != 0)
    vty_out (vty, "Changing OSPFv3 Instance ID from %u to %u%s",
	     ospf6->instance_id, instance_id, VNL);

  ospf6->instance_id = instance_id;

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_passive,
       ipv6_ospf6_passive_cmd,
       "ipv6 ospf6 passive",
       IP6_STR
       OSPF6_STR
       "passive interface, No adjacency will be formed on this interface\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  if (CHECK_FLAG (oi->flag, OSPF6_INTERFACE_PASSIVE))
    return CMD_SUCCESS;

  SET_FLAG (oi->flag, OSPF6_INTERFACE_PASSIVE);

  if (oi->state > OSPF6_INTERFACE_DOWN)
    {
      thread_execute (master, interface_down, oi, 0);
      thread_execute (master, interface_up, oi, 0);
    }

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_passive,
       no_ipv6_ospf6_passive_cmd,
       "no ipv6 ospf6 passive",
       NO_STR
       IP6_STR
       OSPF6_STR
       "passive interface: No Adjacency will be formed on this I/F\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  if (!CHECK_FLAG (oi->flag, OSPF6_INTERFACE_PASSIVE))
    return CMD_SUCCESS;

  UNSET_FLAG (oi->flag, OSPF6_INTERFACE_PASSIVE);

  if (oi->state > OSPF6_INTERFACE_DOWN)
    {
      thread_execute (master, interface_down, oi, 0);
      thread_execute (master, interface_up, oi, 0);
    }

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_mtu_ignore,
       ipv6_ospf6_mtu_ignore_cmd,
       "ipv6 ospf6 mtu-ignore",
       IP6_STR
       OSPF6_STR
       "Ignore MTU mismatch on this interface\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->mtu_ignore = 1;

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_mtu_ignore,
       no_ipv6_ospf6_mtu_ignore_cmd,
       "no ipv6 ospf6 mtu-ignore",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Ignore MTU mismatch on this interface\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->mtu_ignore = 0;

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_allow_immediate_hello,
       ipv6_ospf6_allow_immediate_hello_cmd,
       "ipv6 ospf6 allow-immediate-hello",
       IP6_STR
       OSPF6_STR
       "Allow sending an immediate reply Hello "
       "when a new neighbor is discovered\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->allow_immediate_hello = true;
  oi->config_status |= ALLOW_IMMEDIATE_HELLO_CONFIGURED;

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_allow_immediate_hello,
       no_ipv6_ospf6_allow_immediate_hello_cmd,
       "no ipv6 ospf6 allow-immediate-hello",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Allow sending an immediate reply Hello "
       "when a new neighbor is discovered\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->allow_immediate_hello = false;
  oi->config_status |= ALLOW_IMMEDIATE_HELLO_CONFIGURED;

  return CMD_SUCCESS;
}

static void
ospf6_interface_set_relax_neighbor_inactivity (struct ospf6_interface *oi,
                                               bool enable)
{
  oi->relax_neighbor_inactivity = enable;
}

DEFUN (ipv6_ospf6_relax_neighbor_inactivity,
       ipv6_ospf6_relax_neighbor_inactivity_cmd,
       "ipv6 ospf6 relax-neighbor-inactivity",
       IP6_STR
       OSPF6_STR
       "Enable relaxed neighbor inactivity\n")
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  ospf6_interface_set_relax_neighbor_inactivity (oi, true);

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_relax_neighbor_inactivity,
       no_ipv6_ospf6_relax_neighbor_inactivity_cmd,
       "no ipv6 ospf6 relax-neighbor-inactivity",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Disable relaxed neighbor inactivity\n")
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  ospf6_interface_set_relax_neighbor_inactivity (oi, false);

  return CMD_SUCCESS;
}

static void
ospf6_interface_set_adjacency_formation_limit (struct ospf6_interface *oi,
                                               unsigned int limit)
{
  oi->adjacency_formation_limit = limit;
}

DEFUN (ipv6_ospf6_adjacency_formation_limit,
       ipv6_ospf6_adjacency_formation_limit_cmd,
       "ipv6 ospf6 adjacency-formation-limit <1-65535>",
       IP6_STR
       OSPF6_STR
       "Limit the number of adjacencies formed concurrently\n")
{
  struct ospf6_interface *oi;
  unsigned int limit;

  oi = ospf6_interface_vtyget (vty);

  limit = strtol (argv[0], NULL, 10);
  ospf6_interface_set_adjacency_formation_limit (oi, limit);

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_adjacency_formation_limit,
       no_ipv6_ospf6_adjacency_formation_limit_cmd,
       "no ipv6 ospf6 adjacency-formation-limit",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Do not limit the number of adjacencies formed concurrently\n")
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  ospf6_interface_set_adjacency_formation_limit (oi, 0);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_advertise_prefix_list,
       ipv6_ospf6_advertise_prefix_list_cmd,
       "ipv6 ospf6 advertise prefix-list WORD",
       IP6_STR
       OSPF6_STR
       "Advertising options\n"
       "Filter prefix using prefix-list\n"
       "Prefix list name\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  if (oi->plist_name)
    XFREE (MTYPE_PREFIX_LIST_STR, oi->plist_name);
  oi->plist_name = XSTRDUP (MTYPE_PREFIX_LIST_STR, argv[0]);

  ospf6_interface_connected_route_update (oi->interface);

  if (oi->area)
    {
      ospf6_link_lsa_schedule (oi);
      if (oi->state == OSPF6_INTERFACE_DR)
        {
          ospf6_network_lsa_schedule (oi);
          ospf6_intra_prefix_lsa_schedule_transit (oi);
        }
      ospf6_intra_prefix_lsa_schedule_stub (oi->area);
    }

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_network,
       ipv6_ospf6_network_cmd,
       "ipv6 ospf6 network (broadcast|non-broadcast|point-to-multipoint|point-to-point|loopback|manet-designated-router)",
       "IPv6 Information\n"
       "OSPF6 interface commands\n"
       "Network type\n"
       "Specify OSPF6 broadcast multi-access network\n"
       "Specify OSPF6 NBMA network\n"
       "Specify OSPF6 point-to-multipoint network\n"
       "Specify OSPF6 point-to-point network\n"
       "Specify OSPF6 loopback\n"
       "Specify OSPF6 manet-designated-router (MDR) network\n"
       )
{
  struct ospf6_interface *oi;
  size_t arglen;
  u_char type;

  oi = ospf6_interface_vtyget (vty);

  arglen = strlen (argv[0]);

  if (strncmp (argv[0], "broadcast", arglen) == 0)
    type = OSPF6_IFTYPE_BROADCAST;
  else if (strncmp (argv[0], "non-broadcast", arglen) == 0)
    type = OSPF6_IFTYPE_NBMA;
  else if (strncmp (argv[0], "point-to-multipoint", arglen) == 0)
    type = OSPF6_IFTYPE_POINTOMULTIPOINT;
  else if (strncmp (argv[0], "point-to-point", arglen) == 0)
    type = OSPF6_IFTYPE_POINTOPOINT;
  else if (strncmp (argv[0], "loopback", arglen) == 0)
    type = OSPF6_IFTYPE_LOOPBACK;
  else if (strncmp (argv[0], "manet-designated-router", arglen) == 0)
    type = OSPF6_IFTYPE_MDR;
  else
    assert (0);

  if (type == oi->type)
    return CMD_SUCCESS;

  oi->type = type;

  ospf6_interface_configure_defaults (oi);

  if (oi->state > OSPF6_INTERFACE_DOWN)
    {
      thread_execute (master, interface_down, oi, 0);
      thread_execute (master, interface_up, oi, 0);
    }

  return CMD_SUCCESS;
}

ALIAS (ipv6_ospf6_network,
       ospf6_network_cmd,
       "ospf6 network (broadcast|non-broadcast|point-to-multipoint|point-to-point|loopback|manet-designated-router)",
       "OSPF interface commands\n"
       "Network type\n"
       "Specify OSPF6 broadcast multi-access network\n"
       "Specify OSPF6 NBMA network\n"
       "Specify OSPF6 point-to-multipoint network\n"
       "Specify OSPF6 point-to-point network\n"
       "Specify OSPF6 loopback\n"
       "Specify OSPF6 manet-designated-router (MDR) network\n"
       )

DEFUN (no_ipv6_ospf6_network,
       no_ipv6_ospf6_network_cmd,
       "no ipv6 ospf6 network",
       NO_STR
       "IP Information\n" "OSPF6 interface commands\n" "Network type\n")
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->type = OSPF6_IFTYPE_NONE;

  return CMD_SUCCESS;
}

ALIAS (no_ipv6_ospf6_network,
       no_ospf6_network_cmd,
       "no ospf6 network",
       NO_STR "OSPF6 interface commands\n" "Network type\n")
DEFUN (no_ipv6_ospf6_advertise_prefix_list,
       no_ipv6_ospf6_advertise_prefix_list_cmd,
       "no ipv6 ospf6 advertise prefix-list",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Advertising options\n"
       "Filter prefix using prefix-list\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  if (oi->plist_name)
    {
      XFREE (MTYPE_PREFIX_LIST_STR, oi->plist_name);
      oi->plist_name = NULL;
    }

  ospf6_interface_connected_route_update (oi->interface);

  if (oi->area)
    {
      ospf6_link_lsa_schedule (oi);
      if (oi->state == OSPF6_INTERFACE_DR)
        {
          ospf6_network_lsa_schedule (oi);
          ospf6_intra_prefix_lsa_schedule_transit (oi);
        }
      ospf6_intra_prefix_lsa_schedule_stub (oi->area);
    }

  return CMD_SUCCESS;
}

static void
ospf6_interface_cost_change (struct ospf6_interface *oi)
{
  struct listnode *node;
  struct ospf6_interface_operations *ops;

  if (oi->area)
    {
      /* update cost held in route_connected list in ospf6_interface */
      ospf6_interface_connected_route_update (oi->interface);

      /* execute LSA hooks */
      ospf6_link_lsa_schedule (oi);
      ospf6_router_lsa_schedule (oi->area);
      ospf6_network_lsa_schedule (oi);
      ospf6_intra_prefix_lsa_schedule_transit (oi);
      ospf6_intra_prefix_lsa_schedule_stub (oi->area);
    }

  for (ALL_LIST_ELEMENTS_RO (&ospf6_interface_operations_list, node, ops))
    {
      if (ops->cost_update)
	ops->cost_update (oi);
    }
}

static int
config_write_ospf6_interface (struct vty *vty)
{
  struct listnode *i;
  struct ospf6_interface *oi;
  struct interface *ifp;

  for (ALL_LIST_ELEMENTS_RO (iflist, i, ifp))
    {
      struct listnode *node;
      struct ospf6_interface_operations *ops;

      oi = (struct ospf6_interface *) ifp->info;
      if (oi == NULL)
        continue;

      vty_out (vty, "interface %s%s",
               oi->interface->name, VNL);
      if (ifp->desc)
        vty_out (vty, " description %s%s", ifp->desc, VNL);

      if (oi->ifmtu && ifp->mtu6 != oi->ifmtu)
        vty_out (vty, " ipv6 ospf6 ifmtu %d%s", oi->ifmtu, VNL);

      if (oi->cost_configured && oi->cost != OSPF6_INTERFACE_COST)
        vty_out (vty, " ipv6 ospf6 cost %d%s",
                 oi->cost, VNL);

      if ((oi->type != OSPF6_IFTYPE_MDR &&
           oi->hello_interval != OSPF6_INTERFACE_HELLO_INTERVAL) ||
          (oi->type == OSPF6_IFTYPE_MDR &&
           oi->hello_interval != OSPF6_MDR_HELLO_INTERVAL))
        vty_out (vty, " ipv6 ospf6 hello-interval %d%s",
                 oi->hello_interval, VNL);

      if ((oi->type != OSPF6_IFTYPE_MDR &&
           oi->dead_interval != OSPF6_INTERFACE_DEAD_INTERVAL) ||
          (oi->type == OSPF6_IFTYPE_MDR &&
           oi->dead_interval != OSPF6_MDR_DEAD_INTERVAL))
        vty_out (vty, " ipv6 ospf6 dead-interval %d%s",
                 oi->dead_interval, VNL);

      if ((oi->type != OSPF6_IFTYPE_MDR &&
           oi->rxmt_interval != OSPF6_INTERFACE_RXMT_INTERVAL) ||
          (oi->type == OSPF6_IFTYPE_MDR &&
           oi->rxmt_interval != OSPF6_MDR_RXMT_INTERVAL))
        vty_out (vty, " ipv6 ospf6 retransmit-interval %d%s",
                 oi->rxmt_interval, VNL);

      if (oi->priority != OSPF6_INTERFACE_PRIORITY)
        vty_out (vty, " ipv6 ospf6 priority %d%s",
                 oi->priority, VNL);

      if (oi->transdelay != OSPF6_INTERFACE_TRANSDELAY)
        vty_out (vty, " ipv6 ospf6 transmit-delay %d%s",
                 oi->transdelay, VNL);

      switch (oi->type)
        {
        case OSPF6_IFTYPE_BROADCAST:
          if (oi && !if_is_broadcast (oi->interface))
            vty_out (vty, " ipv6 ospf6 network broadcast%s", VNL);
          break;
        case OSPF6_IFTYPE_NBMA:
          vty_out (vty, " ipv6 ospf6 network non-broadcast%s", VNL);
          break;
        case OSPF6_IFTYPE_POINTOMULTIPOINT:
          vty_out (vty, " ipv6 ospf6 network point-to-multipoint%s", VNL);
          break;
        case OSPF6_IFTYPE_POINTOPOINT:
          if (!oi || !if_is_pointopoint (oi->interface))
            vty_out (vty, " ipv6 ospf6 network point-to-point %s", VNL);
          break;
        case OSPF6_IFTYPE_LOOPBACK:
          vty_out (vty, " ipv6 ospf6 network loopback%s", VNL);
          break;
        case OSPF6_IFTYPE_MDR:
	  ospf6_mdr_interface_config_write (vty, oi);
          break;
	}

      if (oi->flood_delay != OSPF6_INTERFACE_FLOOD_DELAY)
        vty_out (vty, " ipv6 ospf6 flood-delay %d%s", oi->flood_delay, VNL);

      if (oi->type != OSPF6_IFTYPE_MDR && oi->LinkLSASuppression)
	vty_out (vty, " ipv6 ospf6 link-lsa-suppression%s", VNL);
      else if (oi->type == OSPF6_IFTYPE_MDR && !oi->LinkLSASuppression)
	vty_out (vty, " no ipv6 ospf6 link-lsa-suppression%s", VNL);

      if (oi->plist_name)
        vty_out (vty, " ipv6 ospf6 advertise prefix-list %s%s",
                 oi->plist_name, VNL);

      if (CHECK_FLAG (oi->flag, OSPF6_INTERFACE_PASSIVE))
        vty_out (vty, " ipv6 ospf6 passive%s", VNL);

      if (oi->mtu_ignore)
        vty_out (vty, " ipv6 ospf6 mtu-ignore%s", VNL);

      if (oi->allow_immediate_hello)
        vty_out (vty, " ipv6 ospf6 allow-immediate-hello%s", VNL);

      if (oi->relax_neighbor_inactivity)
        vty_out (vty, " ipv6 ospf6 relax-neighbor-inactivity%s", VNL);

      if (oi->adjacency_formation_limit > 0)
        vty_out (vty, " ipv6 ospf6 adjacency-formation-limit %u%s",
                 oi->adjacency_formation_limit, VNL);

      for (ALL_LIST_ELEMENTS_RO (&ospf6_interface_operations_list, node, ops))
	if (ops && ops->config_write)
	  ops->config_write (oi, vty);

      vty_out (vty, "!%s", VNL);
    }
  return 0;
}

static struct cmd_node interface_node =
{
  INTERFACE_NODE,
  "%s(config-if)# ",
  1 /* VTYSH */
};

void
ospf6_interface_init (void)
{
  struct listnode *node;
  struct ospf6_interface_operations *ops;

  /* Install interface node. */
  install_node (&interface_node, config_write_ospf6_interface);

  install_element (VIEW_NODE, &show_ipv6_ospf6_interface_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_interface_prefix_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_interface_prefix_detail_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_interface_prefix_match_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_interface_ifname_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_interface_ifname_prefix_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_interface_ifname_prefix_detail_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_interface_ifname_prefix_match_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_interface_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_interface_prefix_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_interface_prefix_detail_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_interface_prefix_match_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_interface_ifname_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_interface_ifname_prefix_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_interface_ifname_prefix_detail_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_interface_ifname_prefix_match_cmd);

  install_element (CONFIG_NODE, &interface_cmd);
  install_default (INTERFACE_NODE);
  install_element (INTERFACE_NODE, &interface_desc_cmd);
  install_element (INTERFACE_NODE, &no_interface_desc_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_cost_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_cost_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_ifmtu_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_ifmtu_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_deadinterval_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_hellointerval_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_priority_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_retransmitinterval_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_transmitdelay_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_instance_cmd);

  install_element (INTERFACE_NODE, &ipv6_ospf6_passive_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_passive_cmd);

  install_element (INTERFACE_NODE, &ipv6_ospf6_mtu_ignore_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_mtu_ignore_cmd);

  install_element (INTERFACE_NODE, &ipv6_ospf6_allow_immediate_hello_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_allow_immediate_hello_cmd);

  install_element (INTERFACE_NODE,
                   &ipv6_ospf6_relax_neighbor_inactivity_cmd);
  install_element (INTERFACE_NODE,
                   &no_ipv6_ospf6_relax_neighbor_inactivity_cmd);

  install_element (INTERFACE_NODE,
                   &ipv6_ospf6_adjacency_formation_limit_cmd);
  install_element (INTERFACE_NODE,
                   &no_ipv6_ospf6_adjacency_formation_limit_cmd);

  install_element (INTERFACE_NODE, &ipv6_ospf6_advertise_prefix_list_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_advertise_prefix_list_cmd);

  install_element (INTERFACE_NODE, &ipv6_ospf6_network_cmd);
  install_element (INTERFACE_NODE, &ospf6_network_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_network_cmd);
  install_element (INTERFACE_NODE, &no_ospf6_network_cmd);

  install_element (INTERFACE_NODE, &ipv6_ospf6_flooddelay_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_link_lsa_suppression_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_link_lsa_suppression_cmd);

  ospf6_mdr_interface_init ();

  for (ALL_LIST_ELEMENTS_RO (&ospf6_interface_operations_list, node, ops))
    if (ops && ops->init)
      ops->init ();

  ospf6_interface_init_called++;
}

void
ospf6_interface_terminate (void)
{
  struct listnode *node;
  struct interface *ifp;

  for (ALL_LIST_ELEMENTS_RO (iflist, node, ifp))
    {
      struct ospf6_interface *oi;

      oi = ifp->info;
      if (oi != NULL)
	ospf6_interface_delete (oi);
    }

  list_delete_all_node (&ospf6_interface_operations_list);
}

DEFUN (debug_ospf6_interface,
       debug_ospf6_interface_cmd,
       "debug ospf6 interface",
       DEBUG_STR
       OSPF6_STR
       "Debug OSPFv3 Interface\n"
      )
{
  OSPF6_DEBUG_INTERFACE_ON ();
  return CMD_SUCCESS;
}

DEFUN (no_debug_ospf6_interface,
       no_debug_ospf6_interface_cmd,
       "no debug ospf6 interface",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Debug OSPFv3 Interface\n"
      )
{
  OSPF6_DEBUG_INTERFACE_OFF ();
  return CMD_SUCCESS;
}

int
config_write_ospf6_debug_interface (struct vty *vty)
{
  if (IS_OSPF6_DEBUG_INTERFACE)
    vty_out (vty, "debug ospf6 interface%s", VNL);
  return 0;
}

void
install_element_ospf6_debug_interface (void)
{
  install_element (ENABLE_NODE, &debug_ospf6_interface_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_interface_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_interface_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_interface_cmd);
}
