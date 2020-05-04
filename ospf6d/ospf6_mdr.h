/* -*-  c-file-style: "gnu" -*- */

/*
 * Copyright (c) 2005-2010 The Boeing Company
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

#ifndef OSPF6_MDR_H
#define OSPF6_MDR_H

#include <stdbool.h>

#include "ospf6_interface.h"
#include "ospf6_neighbor.h"

#define INFTY 10000

struct tree_node
{
  struct ospf6_neighbor *on;
  int labeled;                  // indicates node is labeled
  struct tree_node *parent;     // parent in tree
  // sec_node needed for version 9 BMDR algorithm
  struct ospf6_neighbor *sec_node;      // second node on path to this node
  struct tree_node *first_child;        // pointer to first child of this node
  struct tree_node *last_child; // pointer to last child of this node
  struct tree_node *next_sib;   // pointer to next child of same parent
  // next_sib points to another node at the same hop level as this node
  // next_sib is NULL if this is the last child of the parent
};

bool ospf6_mdr_set_mdr_level (struct ospf6_neighbor *on,
			      u_int32_t id1, u_int32_t id2);
void ospf6_calculate_mdr (struct ospf6_interface *oi);
int ospf6_mdr_update_lsa (struct ospf6_interface *oi);
int ospf6_mdr_update_routable_neighbors (struct ospf6_interface *oi);

/**
 * A MDR level callback
 *
 * A MDR level callback function is called whenever the MDR level
 * changes and is passed a pointer to the affected ospf interface
 * structure.
 */
typedef void (*update_mdr_level_hook_t) (struct ospf6_interface *);

/**
 * Add a MDR level callback.
 *
 * @param hook The callback function to be added.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int ospf6_add_update_mdr_level_hook (update_mdr_level_hook_t hook);

/**
 * Remove a MDR level callback.
 *
 * @param hook The callback function to be removed.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int ospf6_remove_update_mdr_level_hook (update_mdr_level_hook_t hook);

#endif /* OSPF6_MDR_H */
