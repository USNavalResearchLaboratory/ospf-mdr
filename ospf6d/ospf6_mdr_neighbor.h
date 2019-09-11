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

#ifndef OSPF6_MDR_NEIGHBOR_H
#define OSPF6_MDR_NEIGHBOR_H

#include <stdbool.h>

#include "zebra.h"

#include "linklist.h"

struct ospf6_mdr_neighbor
{
  struct ospf6_lsdb *ack_list;
  struct thread *thread_ack_list_expire;

  bool routable;
  bool dependent;
  bool dependent_selector;
  bool adv;                  // advertised neighbor
  bool sel_adv;              // selected advertised neighbor
  bool Abit;                 // A-bit from hello TLV
  struct list *rnl;             // List of bidirectional neighbor router IDs.
  struct list *dnl;             // List of dependent neighbor router IDs.
  struct list *sanl;            // List of selected adv neighbors.
  int list_type;
  struct ospf6_neighbor *parent;
  int hops;
  int hops2;
  struct tree_node *treenode;
  bool child;
  bool Report2Hop;
  bool reverse_2way;
  int mdr_level;
  int cost_matrix_index;
  u_int16_t hsn;
  u_int16_t changed_hsn;
  int consec_hellos;  // For neighbor acceptance
};

struct ospf6_lnl_element
{
  u_int32_t id;
  u_int16_t hsn;
};

struct ospf6_neighbor;
struct ospf6_header;
struct ospf6_lls_header;
struct ospf6_lsa_header;
struct ospf6_interface;
struct ospf6_lsa;

extern void ospf6_mdr_neighbor_init (void);
extern void ospf6_mdr_neighbor_create (struct ospf6_neighbor *on);
extern void ospf6_mdr_neighbor_delete (struct ospf6_neighbor *on);
extern void ospf6_mdr_neighbor_state_change (struct ospf6_neighbor *on,
					     u_char prev_state,
					     u_char next_state);

extern bool ospf6_mdr_lookup_neighbor (struct list *, u_int32_t);
extern int keep_adjacency (struct ospf6_neighbor *);
extern int ospf6_mdr_neighbor_need_adjacency (struct ospf6_neighbor *on);
extern int ospf6_neighbor_hello_recv (struct ospf6_neighbor *on,
				      struct ospf6_header *oh,
				      struct ospf6_lls_header *lls);
extern void ospf6_neighbor_state_change (u_char, struct ospf6_neighbor *);
extern void ospf6_mdr_delete_lnl_element (struct ospf6_interface *,
					  struct ospf6_lnl_element *);
extern bool ospf6_mdr_delete_neighbor (struct list *, u_int32_t);
extern void ospf6_mdr_add_neighbor (struct list *, u_int32_t);
extern void ospf6_mdr_delete_all_neighbors (struct list *);
extern void ospf6_mdr_neighbor_store_ack (struct ospf6_neighbor *on,
					  struct ospf6_lsa *lsa);
extern bool ospf6_mdr_neighbor_has_acked (struct ospf6_neighbor *on,
					  struct ospf6_lsa *lsa);

#endif	/* OSPF6_MDR_NEIGHBOR_H */
