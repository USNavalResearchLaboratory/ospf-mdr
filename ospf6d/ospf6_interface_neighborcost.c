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

#include "zebra.h"
#include "command.h"
#include "memory.h"

#include "ospf6d.h"
#include "ospf6_af.h"
#include "ospf6_interface.h"
#include "ospf6_neighbor.h"
#include "ospf6_interface_neighbor_metric.h"

static const char *neighborcost_name = "neighbor-cost";
static unsigned int neighborcost_nbrmetric_id;

struct neighbor_cost_entry {
  u_int32_t router_id;
  u_int32_t cost;
};

struct ospf6_interface_neighborcost {
  struct list *neighbor_cost_list;
};

static void
ospf6_interface_config_write_neighborcost (struct ospf6_interface *oi,
					   struct vty *vty);
static void
ospf6_interface_cost_update_neighborcost (struct ospf6_interface *oi);
static int
ospf6_neighbor_create_neighborcost (struct ospf6_neighbor *on);
static int
ospf6_interface_neighborcost_update (struct ospf6_interface *oi,
				     u_int32_t router_id, u_int16_t newcost);

static void
delete_neighbor_cost_entry (void *entry)
{
  XFREE (MTYPE_OSPF6_OTHER, entry);
}

static struct listnode *
ospf6_interface_neighborcost_lookup (struct list *neighbor_cost_list,
				     u_int32_t router_id)
{
  struct listnode *node;
  struct neighbor_cost_entry *entry;

  for (ALL_LIST_ELEMENTS_RO (neighbor_cost_list, node, entry))
    if (entry->router_id == router_id)
      return node;

  return NULL;
}

static void
ospf6_interface_delete_neighborcost (struct ospf6_interface *oi)
{
  struct ospf6_interface_neighborcost *inc;

  inc = ospf6_interface_neighbor_metric_data (oi, neighborcost_nbrmetric_id);
  assert (inc);

  list_delete (inc->neighbor_cost_list);

  XFREE (MTYPE_OSPF6_IF, inc);
}

static int
ospf6_interface_register_neighborcost (struct ospf6_interface *oi,
				       struct vty *vty)
{
  int err;
  struct ospf6_interface_neighborcost *inc;
  struct ospf6_interface_neighbor_metric_params nbrmetric_params = {
    .name = neighborcost_name,
    .delete = ospf6_interface_delete_neighborcost,
    .config_write = ospf6_interface_config_write_neighborcost,
    .cost_update = ospf6_interface_cost_update_neighborcost,
    .nbrops = {
      .create = ospf6_neighbor_create_neighborcost,
    },
  };

  inc = XCALLOC (MTYPE_OSPF6_IF, sizeof (*inc));
  inc->neighbor_cost_list = list_new ();
  inc->neighbor_cost_list->del = delete_neighbor_cost_entry;

  nbrmetric_params.data = inc;
  err =
    ospf6_interface_register_neighbor_metric (oi, &neighborcost_nbrmetric_id,
					      &nbrmetric_params, vty);
  if (err)
    {
      vty_out (vty, "could not register neighbor metric %s on interface %s%s",
	       nbrmetric_params.name, oi->interface->name, VNL);
      list_delete (inc->neighbor_cost_list);
      XFREE (MTYPE_OSPF6_IF, inc);
      return -1;
    }

  return 0;
}

DEFUN (ipv6_ospf6_neighbor_cost,
       ipv6_ospf6_neighbor_cost_cmd,
       "ipv6 ospf6 neighbor-cost A.B.C.D <1-65535>",
       IP6_STR
       OSPF6_STR
       "Neighbor cost metric\n"
       "Specify Router-ID as IPv4 address notation\n"
       "Outgoing metric for this neighbor\n")
{
  struct ospf6_interface *oi;
  int registered, err;
  u_int32_t router_id;
  u_int16_t newcost;

  oi = ospf6_interface_vtyget (vty);

  err = ospf6_str2id (argv[0], &router_id);
  if (err)
    {
      vty_out (vty, "invalid router-id: %s%s", argv[0], VNL);
      return CMD_WARNING;
    }

  newcost = strtol (argv[1], NULL, 10);

  registered =
    ospf6_interface_neighbor_metric_registered (oi,
						neighborcost_nbrmetric_id);
  if (!registered)
    {
      err = ospf6_interface_register_neighborcost (oi, vty);
      if (err)
	return CMD_WARNING;
    }

  err = ospf6_interface_enable_neighbor_metric (oi,
						neighborcost_nbrmetric_id);
  if (err)
    {
      vty_out (vty, "could not enable neighbor metric %s on interface %s%s",
	       neighborcost_name, oi->interface->name, VNL);
      return CMD_WARNING;
    }

  err = ospf6_interface_neighborcost_update (oi, router_id, newcost);
  if (err)
    {
      vty_out (vty, "updating neighbor cost metric failed for %s "
	       "on interface %s%s", argv[0], oi->interface->name, VNL);
      return CMD_WARNING;
    }

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_neighbor_cost,
       no_ipv6_ospf6_neighbor_cost_cmd,
       "no ipv6 ospf6 neighbor-cost [A.B.C.D]",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Neighbor cost metric\n"
       "Specify Router-ID as IPv4 address notation\n")
{
  struct ospf6_interface *oi;
  int enabled, err = 0;
  u_int32_t router_id;
  struct ospf6_interface_neighborcost *inc;

  oi = ospf6_interface_vtyget (vty);

  if (argc == 1)
    {
      err = ospf6_str2id (argv[0], &router_id);
      if (err)
	{
	  vty_out (vty, "invalid router-id: %s%s", argv[0], VNL);
	  return CMD_WARNING;
	}
    }

  enabled =
    ospf6_interface_neighbor_metric_enabled (oi, neighborcost_nbrmetric_id);
  if (!enabled)
    {
      vty_out (vty, "%s is not enabled for interface %s%s",
	       neighborcost_name, oi->interface->name, VNL);
      return CMD_WARNING;
    }

  inc = ospf6_interface_neighbor_metric_data (oi,
					      neighborcost_nbrmetric_id);
  assert (inc);

  if (argc == 0)
    {
      list_delete_all_node (inc->neighbor_cost_list);
      err +=
	ospf6_interface_reset_neighbor_metric (oi,
					       neighborcost_nbrmetric_id);
    }
  else
    {
      struct listnode *node;

      node = ospf6_interface_neighborcost_lookup (inc->neighbor_cost_list,
						  router_id);
      if (node != NULL)
	{
	  struct neighbor_cost_entry *entry;
	  struct ospf6_neighbor *on;

	  entry = listgetdata (node);
	  delete_neighbor_cost_entry (entry);
	  list_delete_node (inc->neighbor_cost_list, node);

	  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
	    if (on->router_id == router_id)
	      {
		err += ospf6_interface_update_neighbor_metric (on, oi->cost,
							       neighborcost_nbrmetric_id);
		break;
	      }
	}
      else
	{
	  vty_out (vty, "no neighbor cost found for neighbor %s%s",
		   argv[0], VNL);
	}
    }

  if (list_isempty (inc->neighbor_cost_list))
    err +=
      ospf6_interface_disable_neighbor_metric (oi,
					       neighborcost_nbrmetric_id);

  if (err)
    return CMD_WARNING;

  return CMD_SUCCESS;
}

static int
ospf6_interface_neighborcost_update (struct ospf6_interface *oi,
				     u_int32_t router_id, u_int16_t newcost)
{
  struct ospf6_interface_neighborcost *inc;
  int err = 0;
  struct listnode *node;
  struct neighbor_cost_entry *entry;
  struct ospf6_neighbor *on;

  inc = ospf6_interface_neighbor_metric_data (oi, neighborcost_nbrmetric_id);
  assert (inc);

  node = ospf6_interface_neighborcost_lookup (inc->neighbor_cost_list,
					      router_id);
  if (node != NULL)
    {
      entry = listgetdata (node);
      entry->cost = newcost;
    }
  else
    {
      entry = XMALLOC (MTYPE_OSPF6_OTHER, sizeof (*entry));
      entry->router_id = router_id;
      entry->cost = newcost;
      listnode_add (inc->neighbor_cost_list, entry);
    }

  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
    if (on->router_id == router_id)
      {
	err +=
	  ospf6_interface_update_neighbor_metric (on, newcost,
						  neighborcost_nbrmetric_id);
	break;
      }

  return err;
}

static int
ospf6_neighbor_create_neighborcost (struct ospf6_neighbor *on)
{
  struct ospf6_interface_neighborcost *inc;
  struct listnode *node;
  int err = 0;

  inc = ospf6_interface_neighbor_metric_data (on->ospf6_if,
					      neighborcost_nbrmetric_id);
  assert (inc);

  node = ospf6_interface_neighborcost_lookup (inc->neighbor_cost_list,
					      on->router_id);
  if (node != NULL)
    {
      struct neighbor_cost_entry *entry;

      entry = listgetdata (node);
      err +=
	ospf6_interface_update_neighbor_metric (on, entry->cost,
						neighborcost_nbrmetric_id);
    }

  return err;
}

static void
ospf6_interface_config_write_neighborcost (struct ospf6_interface *oi,
					   struct vty *vty)
{
  struct ospf6_interface_neighborcost *inc;
  struct listnode *node;
  struct neighbor_cost_entry *entry;

  inc = ospf6_interface_neighbor_metric_data (oi, neighborcost_nbrmetric_id);
  assert (inc);

  for (ALL_LIST_ELEMENTS_RO (inc->neighbor_cost_list, node, entry))
    {
      char router_id[INET_ADDRSTRLEN];

      ospf6_id2str (entry->router_id, router_id, sizeof (router_id));
      vty_out (vty, " ipv6 ospf6 neighbor-cost %s %u%s",
	       router_id, entry->cost, VNL);
    }
}

static void
ospf6_interface_cost_update_neighborcost (struct ospf6_interface *oi)
{
  struct ospf6_interface_neighborcost *inc;
  struct listnode *node;
  struct ospf6_neighbor *on;

  inc = ospf6_interface_neighbor_metric_data (oi, neighborcost_nbrmetric_id);
  assert (inc);

  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
    {
      struct listnode *n;
      int err;

      n = ospf6_interface_neighborcost_lookup (inc->neighbor_cost_list,
					       on->router_id);
      if (n != NULL)
	continue;		/* neighbor has a configured cost */

      err = ospf6_interface_update_neighbor_metric (on, oi->cost,
						    neighborcost_nbrmetric_id);
      if (err)
	zlog_warn ("could not update cost for neighbor %s", on->name);
    }
}

static void
ospf6_interface_init_neighborcost (void)
{
  install_element (INTERFACE_NODE, &ipv6_ospf6_neighbor_cost_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_neighbor_cost_cmd);
}

static struct ospf6_interface_operations neighborcost_ifops = {
  .init = ospf6_interface_init_neighborcost,
};

OSPF6_INTERFACE_OPERATIONS (neighborcost_ifops);
