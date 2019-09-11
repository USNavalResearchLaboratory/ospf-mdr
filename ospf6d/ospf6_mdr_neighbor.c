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

#include "zebra.h"

#include "command.h"
#include "memory.h"

#include "ospf6d.h"
#include "ospf6_af.h"
#include "ospf6_area.h"
#include "ospf6_interface.h"
#include "ospf6_neighbor.h"
#include "ospf6_lsa.h"
#include "ospf6_lsdb.h"
#include "ospf6_mdr.h"
#include "ospf6_mdr_neighbor.h"

static struct ospf6_lnl_element *
ospf6_mdr_lookup_lnl_element (struct ospf6_neighbor *on)
{
  struct listnode *node;
  struct ospf6_interface *oi = on->ospf6_if;
  struct ospf6_lnl_element *lnl_element = NULL;

  for (ALL_LIST_ELEMENTS_RO (oi->mdr.lnl, node, lnl_element))
    if (on->router_id == lnl_element->id)
      return lnl_element;
  return NULL;
}

void
ospf6_mdr_neighbor_create (struct ospf6_neighbor *on)
{
  struct ospf6_lnl_element *lnl_element;

  lnl_element = ospf6_mdr_lookup_lnl_element (on);
  if (lnl_element)
    ospf6_mdr_delete_lnl_element (on->ospf6_if, lnl_element);
  on->mdr.rnl = list_new ();
  on->mdr.dnl = list_new ();
  on->mdr.sanl = list_new ();
  on->mdr.Report2Hop = false;
  on->mdr.reverse_2way = false;
  on->mdr.dependent = false;
  on->mdr.dependent_selector = false;
  on->mdr.routable = false;
  on->mdr.adv = false;
  on->mdr.sel_adv = false;
  on->mdr.list_type = 0;
  on->mdr.consec_hellos = 0;

  on->mdr.ack_list = ospf6_lsdb_create (on);
}

static void
ospf6_mdr_delete_neighbor_list (struct list *n_list)
{
  struct listnode *node, *nnode;
  u_int32_t *id;

  for (ALL_LIST_ELEMENTS (n_list, node, nnode, id))
    XFREE (MTYPE_OSPF6_MDR, id);
  list_delete (n_list);
}

//HNL Functions
static void
ospf6_mdr_add_lnl_element (struct ospf6_neighbor *on)
{
  struct ospf6_interface *oi = on->ospf6_if;
  struct ospf6_lnl_element *lnl_element;

  lnl_element = ospf6_mdr_lookup_lnl_element (on);

  if (lnl_element)
    {
      lnl_element->hsn = oi->mdr.hsn;
      return;
    }

  lnl_element = XMALLOC (MTYPE_OSPF6_MDR, sizeof (struct ospf6_lnl_element));
  lnl_element->id = on->router_id;
  lnl_element->hsn = oi->mdr.hsn;
  listnode_add (oi->mdr.lnl, lnl_element);
}

void
ospf6_mdr_neighbor_delete (struct ospf6_neighbor *on)
{
  if (on->ospf6_if->type == OSPF6_IFTYPE_MDR)
    {
      ospf6_mdr_add_lnl_element (on);
      ospf6_mdr_set_mdr_level (on, 0, 0);       //important for statistics gathering
    }
  ospf6_mdr_delete_neighbor_list (on->mdr.rnl);
  ospf6_mdr_delete_neighbor_list (on->mdr.dnl);
  ospf6_mdr_delete_neighbor_list (on->mdr.sanl);

  THREAD_OFF (on->mdr.thread_ack_list_expire);
  ospf6_lsdb_remove_all (on->mdr.ack_list);
  ospf6_lsdb_delete (on->mdr.ack_list);
}

void
ospf6_mdr_neighbor_state_change (struct ospf6_neighbor *on,
				 u_char prev_state, u_char next_state)
{
  struct ospf6_interface *oi = on->ospf6_if;

  // If neighbor goes from bidirectional to non-bidirectional,
  // do MDR calculation and update adjacencies and LSA.
  if (prev_state >= OSPF6_NEIGHBOR_TWOWAY &&
      next_state < OSPF6_NEIGHBOR_TWOWAY)
    {
      ospf6_calculate_mdr (oi);
      ospf6_update_adjacencies (oi);
      ospf6_mdr_update_lsa (oi);
    }
  // If neighbor becomes bidirectional or Full, update LSA.
  else if ((prev_state < OSPF6_NEIGHBOR_TWOWAY &&
	    next_state >= OSPF6_NEIGHBOR_TWOWAY) ||
	   next_state == OSPF6_NEIGHBOR_FULL)
    {
      ospf6_mdr_update_lsa (oi);
    }
}

/*
 *keep_adjacency() is used to decide whether an existing adjacency
 * should be kept vs. torn down. The condition is less strict than
 * need_adjacency(), for hysteresis and adjacency stability.
 * (The condition is equivalent to the need_adjacency() condition for an
 * OSPF broadcast network, i.e., at least one endpoint must be DR/BDR.)
 */
int
keep_adjacency (struct ospf6_neighbor *on)
{
  struct ospf6_interface *oi = on->ospf6_if;

  if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED)
    return 1;

  if (oi->type == OSPF6_IFTYPE_MDR)
    {
      if (on->mdr.Abit)
        return 1;              // Neighbor is using full-topology adjacencies.
      if (oi->mdr.mdr_level == OSPF6_MDR || oi->mdr.mdr_level == OSPF6_BMDR ||
          on->mdr.mdr_level == OSPF6_MDR || on->mdr.mdr_level == OSPF6_BMDR)
        return 1;
      else
        return 0;
    }

  return 1;
}

int
ospf6_mdr_neighbor_need_adjacency (struct ospf6_neighbor *on)
{
  struct ospf6_interface *oi = on->ospf6_if;
  u_int32_t bp_rid = 0, p_rid = 0;

  if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED)
    return 1;
  if (on->mdr.Abit)
    return 1;           // Neighbor is using full-topology adjacencies.

  if (oi->mdr.mdr_level >= OSPF6_BMDR && on->mdr.mdr_level >= OSPF6_BMDR &&
      (on->mdr.dependent || on->mdr.dependent_selector))
    return 1;

  // Form adjacency between child and parent.
  // The condition must be symmetric: child and parent must agree.
  if (oi->mdr.mdr_level >= OSPF6_BMDR && on->mdr.child == true)
    return 1;

  if (on->mdr.mdr_level >= OSPF6_BMDR)
    {
      if (oi->mdr.parent)
	p_rid = oi->mdr.parent->router_id;
      if (oi->mdr.bparent)
	bp_rid = oi->mdr.bparent->router_id;
      if (on->router_id == p_rid)
	return 1;
      if (on->router_id == bp_rid)
	return 1;       // backup parent is adjacent for biconnected
    }

  return 0;
}

static void
ospf6_neighbor_mdrdetails(struct vty *vty, struct ospf6_neighbor *on)
{
  struct listnode *node;
  u_int32_t *rid;
  char ridstr[INET_ADDRSTRLEN];

  assert (on->ospf6_if->type == OSPF6_IFTYPE_MDR);

  vty_out (vty, "Neighbor %s%s", on->name, VNL);

#define VTYINDENT "  "

  vty_out (vty, VTYINDENT "Neighbor Hello Sequence Number (NHSN): %u%s",
	   on->mdr.hsn, VNL);
  vty_out (vty, VTYINDENT "A-bit: %u%s", on->mdr.Abit ? 1 : 0, VNL);
  vty_out (vty, VTYINDENT "FullHelloRcvd: %u%s", on->mdr.Report2Hop ? 1 : 0, VNL);

  vty_out (vty, VTYINDENT "Neighbor's MDR Level: %i%s", on->mdr.mdr_level, VNL);

  ospf6_id2str (on->drouter, ridstr, sizeof (ridstr));
  vty_out (vty, VTYINDENT "Neighbor's Parent: %s%s", ridstr, VNL);

  ospf6_id2str (on->bdrouter, ridstr, sizeof (ridstr));
  vty_out (vty, VTYINDENT "Neighbor's Backup Parent: %s%s", ridstr, VNL);

  vty_out (vty, VTYINDENT "Child: %u%s", on->mdr.child ? 1 : 0, VNL);
  vty_out (vty, VTYINDENT "Dependent Neighbor: %u%s",
	   on->mdr.dependent ? 1 : 0, VNL);
  vty_out (vty, VTYINDENT "Dependent Selector: %u%s",
	   on->mdr.dependent_selector ? 1 : 0, VNL);
  vty_out (vty, VTYINDENT "Advertised Neighbor: %u%s", on->mdr.adv ? 1 : 0, VNL);
  vty_out (vty, VTYINDENT "Selected Advertised Neighbor (SAN): %u%s",
	   on->mdr.sel_adv ? 1 : 0, VNL);
  vty_out (vty, VTYINDENT "Routable: %u%s", on->mdr.routable ? 1 : 0, VNL);

  vty_out (vty, VTYINDENT "Neighbor's Bidirectional Neighbor Set (BNS):%s",
	   VNL);
  for (ALL_LIST_ELEMENTS_RO (on->mdr.rnl, node, rid))
    {
      ospf6_id2str (*rid, ridstr, sizeof (ridstr));
      vty_out (vty, VTYINDENT VTYINDENT "%s%s", ridstr, VNL);
    }

  vty_out (vty, VTYINDENT "Neighbor's Dependent Neighbor Set (DNS):%s", VNL);
  for (ALL_LIST_ELEMENTS_RO (on->mdr.dnl, node, rid))
    {
      ospf6_id2str (*rid, ridstr, sizeof (ridstr));
      vty_out (vty, VTYINDENT VTYINDENT "%s%s", ridstr, VNL);
    }

  vty_out (vty, VTYINDENT "Neighbor's Selected Advertised Neighbor Set "
	   "(SANS):%s", VNL);
  for (ALL_LIST_ELEMENTS_RO (on->mdr.sanl, node, rid))
    {
      ospf6_id2str (*rid, ridstr, sizeof (ridstr));
      vty_out (vty, VTYINDENT VTYINDENT "%s%s", ridstr, VNL);
    }

  vty_out (vty, VTYINDENT "Neighbor's Link Metrics: UNIMPLEMENTED%s", VNL);

#undef VTYINDENT
}

DEFUN (show_ipv6_ospf6_neighbor_mdr,
       show_ipv6_ospf6_neighbor_mdr_cmd,
       "show ipv6 ospf6 neighbor mdrdetail [A.B.C.D]",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Neighbor list\n"
       "MDR details\n"
       "Specify Router-ID as IPv4 address notation\n")
{
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  struct ospf6_neighbor *on;
  struct listnode *i, *j, *k;
  u_int32_t routerid;
  int numnbr = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  if (argc && ospf6_str2id (argv[0], &routerid))
    {
      vty_out (vty, "invalid router-id: '%s'%s", argv[0], VNL);
      return CMD_SUCCESS;
    }

  for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, i, oa))
    for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
      {
	if (oi->type != OSPF6_IFTYPE_MDR)
	  continue;

	for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, on))
	  {
	    if (argc && on->router_id != routerid)
	      continue;

	    if (numnbr)
	      vty_out (vty, "%s", VNL);

	    ospf6_neighbor_mdrdetails(vty, on);

	    numnbr++;
	  }
      }

  if (!numnbr)
    {
      if (argc)
	vty_out (vty, "neighbor %s not found%s", argv[0], VNL);
      else
	vty_out (vty, "no neighbors found%s", VNL);
    }

  return CMD_SUCCESS;
}

void
ospf6_mdr_neighbor_init (void)
{
  install_element (ENABLE_NODE, &show_ipv6_ospf6_neighbor_mdr_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_neighbor_mdr_cmd);
}

static struct ospf6_lsa *
ospf6_mdr_neighbor_lookup_ack (struct ospf6_neighbor *on,
			       struct ospf6_lsa *lsa)
{
  return ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
			    lsa->header->adv_router, on->mdr.ack_list);
}

static int
ospf6_mdr_neighbor_ack_list_expire (struct thread *thread)
{
  struct ospf6_neighbor *on;
  struct timeval now;
  int ack_cache_timeout;
  struct ospf6_lsa *ack;
  unsigned int remaining;

  on = THREAD_ARG (thread);
  on->mdr.thread_ack_list_expire = NULL;

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);
  ack_cache_timeout = on->ospf6_if->mdr.ack_cache_timeout;
  remaining = 0;

  for (ack = ospf6_lsdb_head (on->mdr.ack_list); ack != NULL;
       ack = ospf6_lsdb_next (ack))
    {
      if (timersub_sec (&now, &ack->received) > ack_cache_timeout)
	{
	  assert (ack->lock == 2);
	  ospf6_lsdb_remove (ack, on->mdr.ack_list);
	}
      else
	{
	  remaining++;
	}
  }

  if (remaining > 0)
    on->mdr.thread_ack_list_expire =
      thread_add_timer (master, ospf6_mdr_neighbor_ack_list_expire,
			on, ack_cache_timeout);

  return 0;
}

// Section 3.4.3 bullet 2
void
ospf6_mdr_neighbor_store_ack (struct ospf6_neighbor *on,
			      struct ospf6_lsa *lsa)
{
  struct ospf6_lsa *ack;

  assert (on->ospf6_if->type == OSPF6_IFTYPE_MDR);

  ack = ospf6_mdr_neighbor_lookup_ack (on, lsa);
  if (ack == NULL || ospf6_lsa_compare (lsa, ack) < 0)
    {
      /* no existing ack or this ack is for a more recent lsa */
      ack = ospf6_lsa_create_headeronly (lsa->header);
      quagga_gettime (QUAGGA_CLK_MONOTONIC, &ack->received);

      ospf6_lsdb_add (ack, on->mdr.ack_list);

      if (on->mdr.thread_ack_list_expire == NULL)
	on->mdr.thread_ack_list_expire =
	  thread_add_timer (master, ospf6_mdr_neighbor_ack_list_expire,
			    on, on->ospf6_if->mdr.ack_cache_timeout);
    }
}

bool
ospf6_mdr_neighbor_has_acked (struct ospf6_neighbor *on, struct ospf6_lsa *lsa)
{
  struct ospf6_lsa *ack;

  assert (on->ospf6_if->type == OSPF6_IFTYPE_MDR);

  ack = ospf6_mdr_neighbor_lookup_ack (on, lsa);

  if (ack != NULL && ospf6_lsa_compare (ack, lsa) <= 0)
    return true;

  return false;
}

void
ospf6_mdr_delete_all_neighbors (struct list *n_list)
{
  struct listnode *node, *nnode;
  u_int32_t *neigh;

  for (ALL_LIST_ELEMENTS (n_list, node, nnode, neigh))
    XFREE (MTYPE_OSPF6_MDR, neigh);
  list_delete_all_node (n_list);
}

void
ospf6_mdr_add_neighbor (struct list *n_list, u_int32_t id)
{
  u_int32_t *neigh = XMALLOC (MTYPE_OSPF6_MDR, sizeof (u_int32_t));
  *neigh = id;
  listnode_add (n_list, neigh);
}

// ZZZ
bool
ospf6_mdr_lookup_neighbor (struct list *n_list, u_int32_t id)
{
  struct listnode *n;
  u_int32_t *neigh_id;

  for (ALL_LIST_ELEMENTS_RO (n_list, n, neigh_id))
    if (id == *neigh_id)
      return true;
  return false;
}

// Return true if list is changed.
bool
ospf6_mdr_delete_neighbor (struct list * n_list, u_int32_t id)
{
  struct listnode *node, *nnode;
  u_int32_t *neigh_id;
  bool changed = false;

  for (ALL_LIST_ELEMENTS (n_list, node, nnode, neigh_id))
    {
      if (id == *neigh_id)
        {
          XFREE (MTYPE_OSPF6_MDR, neigh_id);
	  list_delete_node (n_list, node);
          changed = true;
        }
    }
  return changed;
}

void
ospf6_mdr_delete_lnl_element (struct ospf6_interface *oi,
                              struct ospf6_lnl_element *lnl_element)
{
  listnode_delete (oi->mdr.lnl, lnl_element);
  XFREE (MTYPE_OSPF6_MDR, lnl_element);
}
