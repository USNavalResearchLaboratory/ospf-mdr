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

/*
 * Most of the code in this file was written by Richard Ogier
 * or was transcribed from C code written by him.
 * This code implements OSPF-MDR as described in
 * draft-ietf-ospf-manet-mdr-01.txt
 */

#include <zebra.h>

#include "thread.h"
#include "memory.h"
#include "log.h"

#include "ospf6d.h"
#include "ospf6_mdr.h"
#include "ospf6_area.h"
#include "ospf6_flood.h"
#include "ospf6_intra.h"
#include "ospf6_lsa.h"
#include "ospf6_top.h"
#include "ospf6_route.h"
#include "ospf6_spf.h"
#include "ospf6_callbacks.h"

static struct list *ospf6_update_mdr_level_hooks;

static int ospf6_mdr_update_lsa_full (struct ospf6_interface *oi);
static int ospf6_mdr_update_lsa_minimal (struct ospf6_interface *oi);
static int ospf6_mdr_update_lsa_mincost (struct ospf6_interface *oi);

static void q_add (struct list *q, struct ospf6_neighbor *on);
static struct ospf6_neighbor *q_remove (struct list *q);
static void add_tree_node (struct list *L, struct ospf6_neighbor *on,
			   struct tree_node *parent);
static void remove_tree (struct list *L);
static struct tree_node *dfs_next (struct tree_node *u,
				   struct tree_node *root);
static void ospf6_mdr_free_cost_matrix (struct ospf6_interface *oi);
static void ospf6_mdr_create_cost_matrix (struct ospf6_interface *oi);
static int ospf6_mdr_cost (struct ospf6_neighbor *onj,
			   struct ospf6_neighbor *onk);
static bool ospf6_sidcds_lexicographic (int RtrPri_A, int RtrPri_B,
					int DRLevel_A, int DRLevel_B,
					u_int32_t RID_A, u_int32_t RID_B);
static void ospf6_mdr_create_adj_san_matrices (struct ospf6_interface *oi);
static void ospf6_mdr_free_adj_san_matrices (struct ospf6_interface *oi);

static void __attribute__((constructor))
ospf6_mdr_init (void)
{
  assert (ospf6_update_mdr_level_hooks == NULL);
  ospf6_update_mdr_level_hooks = list_new ();
}

static void __attribute__((destructor))
ospf6_mdr_terminate (void)
{
  list_delete (ospf6_update_mdr_level_hooks);
  ospf6_update_mdr_level_hooks = NULL;
}

static void
ospf6_run_update_mdr_level_hooks (struct ospf6_interface *oi)
{
  RUN_HOOKS (ospf6_update_mdr_level_hooks, update_mdr_level_hook_t, oi);
}

int
ospf6_add_update_mdr_level_hook (update_mdr_level_hook_t hook)
{
  int err;

  err = ospf6_add_hook (ospf6_update_mdr_level_hooks, hook);

  if (!err && ospf6 != NULL)
    {
      struct listnode *n1;
      struct ospf6_area *oa;

      for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, n1, oa))
        {
	  struct listnode *n2;
	  struct ospf6_interface *oi;

	  for (ALL_LIST_ELEMENTS_RO (oa->if_list, n2, oi))
	    if (oi->type == OSPF6_IFTYPE_MDR)
	      hook (oi);
	}
    }

  return err;
}

int
ospf6_remove_update_mdr_level_hook (update_mdr_level_hook_t hook)
{
  return ospf6_remove_hook (ospf6_update_mdr_level_hooks, hook);
}

//Determine if node is in CDS
void
ospf6_calculate_mdr (struct ospf6_interface *oi)
{
  struct listnode *j, *k, *u, *v;
  struct ospf6_neighbor *onj, *onk, *onu, *onv;
  struct ospf6_neighbor *max_on = NULL, *max_on2 = NULL, *min_on = NULL;
  struct ospf6_neighbor *max_nbr = NULL;        // RGO
  struct list *q;
  struct list *tree;
  u_int32_t rid = oi->area->ospf6->router_id;
  u_int32_t maxid = 0, maxid2 = 0;
  int max_mdr_level = OSPF6_OTHER, max_mdr_level2 = OSPF6_OTHER;
  u_char max_priority = 1, max_priority2 = 1;
  struct tree_node *tu, *tv, *root;
  int min_hops2;
  bool dr = false, bdr = false;

  // Do not calculate MDRs within hello_interval times TwoHopRefresh.
  if (elapsed_sec (&ospf6->starttime) <
      oi->hello_interval * oi->mdr.TwoHopRefresh)
    {
      bool wait = true;
      if (oi->allow_immediate_hello && listcount (oi->neighbor_list) > 0)
        {
          // don't wait if a full hello has been received from all
          // known neighbors
          wait = false;
          for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
            {
              if (!onj->mdr.Report2Hop)
                {
                  wait = true;
                  break;
                }
            }
        }
      if (wait)
        return;
    }

  tree = list_new ();

  // ######## PHASE 1 #########
  //cost_matrix must be freed at the end of this function
  ospf6_mdr_create_cost_matrix (oi);

  // ###### PHASE 2: MDR Calculation ########

  // First find the largest nbr ID
  // For persistent version, find largest DR level first.
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
    {
      //some intitialization
      // Select dependent neighbors.
      onj->mdr.dependent = false;   //Step 2.1
      onj->mdr.hops = INFTY;
      onj->mdr.hops2 = INFTY;

      if (ospf6_mdr_cost (onj, NULL) != 1)
        continue;               // nbr must be twoway

      //Find Max and 2nd Max Neighbor
      if (ospf6_sidcds_lexicographic (onj->priority, max_priority,
                                      onj->mdr.mdr_level, max_mdr_level,
                                      ntohl (onj->router_id), maxid))
        {
          // previous max neighbor becomes 2nd max neighbor
          maxid2 = maxid;
          max_priority2 = max_priority;
          max_mdr_level2 = max_mdr_level;
          max_on2 = max_on;

          maxid = ntohl (onj->router_id);
          max_mdr_level = onj->mdr.mdr_level;
          max_priority = onj->priority;
          max_on = onj;
        }
      else if (ospf6_sidcds_lexicographic (onj->priority, max_priority2,
                                           onj->mdr.mdr_level, max_mdr_level2,
                                           ntohl (onj->router_id), maxid2))
        {
          maxid2 = ntohl (onj->router_id);
          max_mdr_level2 = onj->mdr.mdr_level;
          max_priority2 = onj->priority;
          max_on2 = onj;
        }
    }

  if (maxid == 0)
    {                           //no neighbors
      oi->mdr.mdr_level = OSPF6_OTHER;
      oi->mdr.nonflooding_mdr = 0;  //not an MDR
      oi->mdr.parent = NULL;
      oi->mdr.bparent = NULL;

      //clean up
      remove_tree (tree);
      ospf6_mdr_free_cost_matrix (oi);
      ospf6_run_update_mdr_level_hooks (oi);
      return;
    }

  //Step 2.2
  if (ospf6_sidcds_lexicographic (oi->priority, max_priority,
                                  oi->mdr.mdr_level, max_mdr_level,
                                  ntohl (rid), maxid))
    {
      oi->mdr.mdr_level = OSPF6_MDR;        //dr = true
      oi->mdr.nonflooding_mdr = 0;  //router is flooding MDR

      // Make all neighbors dependent
      // A dependent neighbor must be an MDR (or BMDR if AdjConn = BI).
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
        {
          // No dependent neighbors if adj reduction is not used.
          if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED)
            break;

          // Select dependent neighbors.
          if (ospf6_mdr_cost (onj, NULL) == 1)
            if (onj->mdr.mdr_level == OSPF6_MDR ||
                (oi->mdr.AdjConnectivity == OSPF6_ADJ_BICONNECTED &&
                 onj->mdr.mdr_level == OSPF6_BMDR))
              onj->mdr.dependent = true;
        }

      oi->mdr.parent = NULL;
      oi->mdr.bparent = NULL;
      //clean up
      remove_tree (tree);
      ospf6_mdr_free_cost_matrix (oi);
      ospf6_run_update_mdr_level_hooks (oi);
      return;                   //I am an MDR
    }

  //Step 2.4
  // Determine if there is a path from on_max to all other nbrs of this node,
  // using only intermediate nodes with larger ID than this node).
  // Use BFS, starting with on_max.
  max_on->mdr.hops = 0;
  add_tree_node (tree, max_on, NULL);
  max_on->mdr.treenode->sec_node = NULL;    // For version 9 BMDR algorithm.
  q = list_new ();
  q_add (q, max_on);            // Add max_on to FIFO.

  while ((onk = q_remove (q)) != NULL)
    {
      // update hops of onk's nbrs
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, u, onu))
        {
          if (ospf6_mdr_cost (onu, NULL) != 1)
            continue;           // nbr must be twoway
          // Cost is from k to u.
          if (ospf6_mdr_cost (onk, onu) != 1)
            continue;
          if (onk->mdr.hops + 1 < onu->mdr.hops)
            {
              onu->mdr.hops = onk->mdr.hops + 1;
              add_tree_node (tree, onu, onk->mdr.treenode);
              // For version 9 BMDR algorithm
              if (onu->mdr.hops == 1)
                onu->mdr.treenode->sec_node = onu;
              else
                onu->mdr.treenode->sec_node = onk->mdr.treenode->sec_node;
              q_add (q, onu);
            }
        }
    }
  list_delete (q);

  //Step 2.6
  // Node is an MDR if any nbr has hops > MDRConstraint.
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, onk))
    {
      if (ospf6_mdr_cost (onk, NULL) != 1)
        continue;               // nbr must be twoway
      if (onk->mdr.hops > oi->mdr.MDRConstraint)
        {
          dr = true;
          // No dependent neighbors if adj reduction is not used.
          if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED)
            break;
          if (onk->mdr.mdr_level == OSPF6_MDR ||
              (oi->mdr.AdjConnectivity == OSPF6_ADJ_BICONNECTED &&
               onk->mdr.mdr_level == OSPF6_BMDR))
            onk->mdr.dependent = true;
        }
    }
  if (dr)
    {
      // max_on is dependent if it is MDR or BMDR.
      // No dependent neighbors if AdjConnectivity is 0.
      if (oi->mdr.AdjConnectivity != OSPF6_ADJ_FULLYCONNECTED &&
          max_on->mdr.mdr_level > OSPF6_OTHER)
        max_on->mdr.dependent = true;
    }

  if (dr)
    oi->mdr.mdr_level = OSPF6_MDR;

  //Step 2.5
  if (!dr && oi->mdr.mdr_level == OSPF6_MDR)
    oi->mdr.mdr_level = OSPF6_BMDR;
  // Otherwise, no change to mdr_level yet.

  //Step 2.7 is not required since MDR calculation will be
  //run again within hello_interval.

  // ###### PHASE 3: Backup MDR Calculation ########

  // Modified to use version 9 BMDR algorithm.
  // ospf6_mdr_cost2() is not used.
  // on->hops2 is used, but is either 0 or INFTY; 0 indicates that
  // two disjoint paths exist to neighbor.

  max_on->mdr.hops2 = 0;
  max_on->mdr.treenode->labeled = 1;        // root is labeled

  // Part (a): Update hops2 by looking at links between nodes
  // u and v on tree that have different second nodes.
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, v, onv))
    {
      if (onv == max_on)
        continue;
      if (!onv->mdr.treenode)
        continue;
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, u, onu))
        {
          if (onu == max_on)
            continue;
          if (!onu->mdr.treenode)
            continue;
          // u and v must have different second nodes.
          if (onu->mdr.treenode->sec_node == onv->mdr.treenode->sec_node)
            continue;

          // u and v must be neighbors, and u must be lex greater than router.
          // ospf6_mdr_cost(onu,onv) does lex comparison of onu and oi.
          if (ospf6_mdr_cost (onu, onv) == 1)
            {
              onv->mdr.hops2 = 0;
              break;            // consider next v
            }
        }
    }

  // Part (b): Find any unlabeled node onk with hops2 = 0, and label it.
  // This divides the unlabeled subtree containing min_on into smaller
  // unlabeled subtrees, one for the parent of min_on if it exists and
  // is unlabeled, and one for each unlabeled child of min_on.
  // Note that dfs_next() has been modified to include
  // the root as an argument.
  while (1)
    {                           // will break when no unlabeled node with finite hops2 exists
      min_hops2 = INFTY;
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, onk))
        {
          if (ospf6_mdr_cost (onk, NULL) != 1)
            continue;           // nbr must be twoway
          if (!onk->mdr.treenode || onk->mdr.treenode->labeled)
            continue;
          if (onk->mdr.hops2 == 0)
            {
              min_hops2 = 0;
              min_on = onk;
              break;            // Disjoint paths exist to min_on = onk.
            }
        }

      if (min_hops2 == INFTY)
        break;                  // Algorithm done

      min_on->mdr.treenode->labeled = 1;

      root = min_on->mdr.treenode->parent;
      // Find root of parent subtree.
      // The root is the first labeled node, or nbr of max_on.
      // The root need not be labeled if it is a nbr of max_on.
      while (root->parent && !root->labeled && root->parent->on != max_on)
        root = root->parent;

      // Iterate thru nodes of parent subtree, using DFS
      for (tu = root; tu; tu = dfs_next (tu, root))
        {
          onu = tu->on;
          if (onu == min_on)
            zlog_err ("Error: onu should not equal min_on");
          // Process links between u and each node in tree rooted at min_on.
          for (tv = min_on->mdr.treenode; tv;
               tv = dfs_next (tv, min_on->mdr.treenode))
            {
              onv = tv->on;
              if (onv == onu)
                zlog_err ("Error: v should not equal u");

              // Process link from u to v to update onv->mdr.hops2
              if (ospf6_mdr_cost (onu, onv) == 1 && onv->mdr.hops2 != 0)
                onv->mdr.hops2 = 0;

              // Process link from v to u to update onu->mdr.hops2
              if (ospf6_mdr_cost (onv, onu) == 1 && onu->mdr.hops2 != 0)
                onu->mdr.hops2 = 0;
            }
        }
    }

  //PHASE 3.3-4
  // Node is a backup DR if any nbr has infinite hops2
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, onk))
    {
      if (ospf6_mdr_cost (onk, NULL) != 1)
        continue;               // nbr must be twoway
      if (onk->mdr.hops2 == INFTY)
        {
          if (!dr)
            bdr = true;         // Router is a BMDR.
          // RGO. Modification for version 07. Backup dependent nbrs
          // are selected only if AdjConn = 2, and must be MDR or BMDR.
          if (!onk->mdr.dependent)
            if (oi->mdr.AdjConnectivity == OSPF6_ADJ_BICONNECTED &&
                onk->mdr.mdr_level >= OSPF6_BMDR)
              onk->mdr.dependent = true;    // onk is a dependent nbr
        }
    }

  //PHASE 3.4
  if (bdr)
    {
      // If AdjConn = 2, max_on is dependent if it is MDR or BMDR.
      if (oi->mdr.AdjConnectivity == 2 && max_on->mdr.mdr_level > OSPF6_OTHER)
        max_on->mdr.dependent = true;
    }

  if (bdr)
    oi->mdr.mdr_level = OSPF6_BMDR;
  if (!dr && !bdr)
    oi->mdr.mdr_level = OSPF6_OTHER;

  //Step 3.5 is not required since MDR calculation will be
  //run again within hello_interval.

  // ###### PHASE 4: Parent Selection ########
  // For an MDR, parent is self and bparent is Rmax (max_on).
  // Backup parent of BMDR is self.
  // For a BMDR and Other, parent is the adjacent MDR neighbor with largest
  // RID, if an adjacent MDR neighbor exists, and is otherwise Rmax.
  // If AdjConn = 1, backup parent of Other is NULL.
  // If AdjConn = 2, backup parent is chosen using the same rules as the
  // parent, except that it must be different from the parent (or NULL).

  if (dr)
    oi->mdr.parent = NULL;          // Parent of MDR is actually self.
  else                          // BMDR or Other
    {
      // Find an adjacent MDR neighbor with max ID, if one exists.
      maxid = 0;
      max_mdr_level = 0;
      max_nbr = NULL;
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
        {
          if (onj->state < OSPF6_NEIGHBOR_EXCHANGE)
            continue;           // consider only adjacent neighbors
          if (onj->mdr.mdr_level < OSPF6_MDR)
            continue;           // consider only MDR neighbors
          if (ospf6_sidcds_lexicographic
              (onj->priority, max_priority, onj->mdr.mdr_level, max_mdr_level,
               ntohl (onj->router_id), maxid))
            {
              maxid = ntohl (onj->router_id);
              max_mdr_level = onj->mdr.mdr_level;
              max_nbr = onj;
            }
        }
      if (maxid != 0)
        oi->mdr.parent = max_nbr;
      else
        oi->mdr.parent = max_on;
    }

  // Initialize backup parent to NULL.
  oi->mdr.bparent = NULL;
  // Backup parent of MDR is Rmax.
  if (dr)
    oi->mdr.bparent = max_on;
  // Backup parent of BMDR is actually self.

  // If AdjConn = 2, MDR Other selects backup parent, using same
  // procedure as for parent, but must not be equal to parent.
  if (!dr && !bdr && oi->mdr.AdjConnectivity == OSPF6_ADJ_BICONNECTED)
    {
      // Find an adjacent MDR or BMDR neighbor with max ID, excluding parent.
      maxid = 0;
      max_mdr_level = 0;
      max_nbr = NULL;
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
        {
          if (onj->state < OSPF6_NEIGHBOR_EXCHANGE)
            continue;           // consider only adjacent neighbors
          if (onj == oi->mdr.parent)
            continue;           // backup parent cannot be parent
          if (onj->mdr.mdr_level < OSPF6_BMDR)
            continue;           // consider only MDR and BMDR neighbors
          if (ospf6_sidcds_lexicographic
              (onj->priority, max_priority, onj->mdr.mdr_level, max_mdr_level,
               ntohl (onj->router_id), maxid))
            {
              maxid = ntohl (onj->router_id);
              max_mdr_level = onj->mdr.mdr_level;
              max_nbr = onj;
            }
        }
      if (maxid != 0)
        oi->mdr.bparent = max_nbr;
      else if (oi->mdr.parent != max_on)
        oi->mdr.bparent = max_on;
      else
        oi->mdr.bparent = max_on2;  // can be NULL
    }
  // ###### PHASE 5: Non-flooding MDR selection ########

  // Step 5.1 already done in Phase 2.
  // Step 5.2. Rmax is max_on.
  // Step 5.3. Run BFS to compute paths from Rmax to the other neighbors,
  // using only intermediate nodes that are MDR neighbors with a
  // smaller router ID than the router itself.
  oi->mdr.nonflooding_mdr = 0;
  if (dr)
    {
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
        onj->mdr.hops = INFTY;
      max_on->mdr.hops = 0;
      q = list_new ();
      q_add (q, max_on);
      while ((onk = q_remove (q)) != NULL)
        {
          for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, u, onu))
            {
              if (ospf6_mdr_cost (onu, NULL) != 1)
                continue;       // nbr must be twoway
              if (oi->mdr.cost_matrix[onk->mdr.cost_matrix_index]
                  [onu->mdr.cost_matrix_index] != 1)
                continue;       // u and k must be neighbors
              if (onk->mdr.hops + 1 < onu->mdr.hops)
                {
                  onu->mdr.hops = onk->mdr.hops + 1;
                  // u can be intermediate node only if its
                  // router ID is smaller than router's.
                  if (onu->mdr.mdr_level == OSPF6_MDR && ntohl (onu->router_id)
                      < ntohl (oi->area->ospf6->router_id))
                    q_add (q, onu);
                }
            }
        }
      list_delete (q);

      // Router is flooding MDR if any nbr has hops > MDRConstraint.
      oi->mdr.nonflooding_mdr = 1;  // initialize to nonflooding
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, onk))
        {
          if (ospf6_mdr_cost (onk, NULL) != 1)
            continue;           // nbr must be twoway
          if (onk->mdr.hops > oi->mdr.MDRConstraint)
            {
              oi->mdr.nonflooding_mdr = 0;  // router is flooding MDR
              break;
            }
        }
    }

  //clean up
  remove_tree (tree);
  ospf6_mdr_free_cost_matrix (oi);
  ospf6_run_update_mdr_level_hooks (oi);
}

//#####################BFS#####################
static void
q_add (struct list *q, struct ospf6_neighbor *on)
{
  listnode_add (q, on);
}

// Removes head of queue and returns neighbor.
// Returns NULL if queue is empty.
static struct ospf6_neighbor *
q_remove (struct list *q)
{
  struct ospf6_neighbor *on;
  if (q->head == NULL)
    return NULL;
  on = (struct ospf6_neighbor *) q->head->data;
  list_delete_node (q, q->head);
  return on;
}

//######################TREE###########################
// Tree node must be added only after its parent has been added.
// Parent tree node is found from its ID via a simple array.
// The index of the tree node indexes the array and matrix.
// Tree nodes must be freed after algorithm is done, using the array.
static void
add_tree_node (struct list *L, struct ospf6_neighbor *on,
               struct tree_node *parent)
{
  struct tree_node *u = XMALLOC (MTYPE_OSPF6_MDR, sizeof (struct tree_node));

  u->on = on;
  on->mdr.treenode = u;

  u->parent = parent;
  u->sec_node = NULL;           // For version 9 BMDR algorithm.
  u->labeled = 0;               // to be set when node is labeled
  u->first_child = NULL;
  u->last_child = NULL;
  u->next_sib = NULL;
  if (parent)
    {
      if (!parent->first_child)
        parent->first_child = u;
      else
        parent->last_child->next_sib = u;
      parent->last_child = u;
    }
  listnode_add (L, u);
}

static void
remove_tree (struct list *L)
{
  struct listnode *node, *nnode;
  struct tree_node *n;
  for (ALL_LIST_ELEMENTS (L, node, nnode, n))
    {
      n->on->mdr.treenode = NULL;
      XFREE (MTYPE_OSPF6_MDR, n);
    }
  list_delete (L);
}

// Finds next node in DFS of unlabeled subtree.
// Labeled nodes define boundary of subtree.
// Search must start at the root of a subtree.
// For version 09, root is now an input, and can be labeled.
// Returns NULL when search is finished.
static struct tree_node *
dfs_next (struct tree_node *u, struct tree_node *root)
{
  struct tree_node *v, *w;
  // Return first unlabeled child, if it exists
  for (v = u->first_child; v != NULL; v = v->next_sib)
    {
      if (!v->labeled)
        return (v);
    }
  // Find an unlabeled sibling, otherwise go to parent and repeat.
  // If parent is NULL or labeled, then root has been reached.
  //for (v = u; v->parent && !(v->parent->labeled); v = v->parent)
  for (v = u; v != root; v = v->parent)
    {
      for (w = v->next_sib; w; w = w->next_sib)
        {
          if (!w->labeled)
            return (w);
        }
    }
  return (NULL);                // DFS is finished.
}

static void
ospf6_mdr_free_cost_matrix (struct ospf6_interface *oi)
{
  u_int i;

  //free matrix
  for (i = 0; i < oi->neighbor_list->count; i++)
    XFREE (MTYPE_OSPF6_MDR, oi->mdr.cost_matrix[i]);
  XFREE (MTYPE_OSPF6_MDR, oi->mdr.cost_matrix);
  oi->mdr.cost_matrix = NULL;
}

static void
ospf6_mdr_create_cost_matrix (struct ospf6_interface *oi)
{
  struct listnode *j, *k, *u;
  u_int32_t *id;
  struct ospf6_neighbor *onj, *onk;
  int count = 0;
  int num_neigh = oi->neighbor_list->count;

  assert (oi->mdr.cost_matrix == NULL);

  //intialize matrix to false
  oi->mdr.cost_matrix = XMALLOC (MTYPE_OSPF6_MDR, sizeof (int *[num_neigh]));
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
    {
      onj->mdr.cost_matrix_index = count;
      oi->mdr.cost_matrix[count] = XCALLOC (MTYPE_OSPF6_MDR,
					sizeof (int[num_neigh]));
      count++;
    }

  //set matrix values
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
    {
      oi->mdr.cost_matrix[onj->mdr.cost_matrix_index][onj->mdr.cost_matrix_index] = 0;
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, onk))
        {
          if (onj == onk)
            continue;           //cost = 0

          if (onj->state < OSPF6_NEIGHBOR_TWOWAY ||
              onk->state < OSPF6_NEIGHBOR_TWOWAY)
            continue;           //cost = 0

          if (!onj->mdr.Report2Hop && !onk->mdr.Report2Hop)
            continue;

          for (ALL_LIST_ELEMENTS_RO (onj->mdr.rnl, u, id))
            {
              if (*id == onk->router_id)
                {
                  oi->mdr.cost_matrix[onj->mdr.cost_matrix_index][onk->mdr.
                                                          cost_matrix_index] =
                    1;
                  break;
                }
            }
        }
    }

  // The above calculation gives an asymmetric matrix.
  // Now make it symmetric depending on Report2Hop.
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
    {
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, onk))
        {
          if (onj == onk)
            continue;           //cost = 0
          if (onj->state < OSPF6_NEIGHBOR_TWOWAY ||
              onk->state < OSPF6_NEIGHBOR_TWOWAY)
            continue;           //cost = 0
          if (!onj->mdr.Report2Hop && !onk->mdr.Report2Hop)
            continue;

          if (onj->mdr.Report2Hop && onk->mdr.Report2Hop)
            {
              // Assumes cost_matrix value is 1 for neighbors, 0 for not.
              oi->mdr.cost_matrix[onj->mdr.cost_matrix_index][onk->mdr.
                                                      cost_matrix_index] =
                oi->mdr.cost_matrix[onk->mdr.cost_matrix_index][onj->mdr.
                                                        cost_matrix_index] =
                oi->mdr.cost_matrix[onj->mdr.cost_matrix_index][onk->mdr.
                                                        cost_matrix_index] *
                oi->mdr.cost_matrix[onk->mdr.cost_matrix_index][onj->mdr.
                                                        cost_matrix_index];
            }
          else if (onj->mdr.Report2Hop && !onk->mdr.Report2Hop)
            {
              oi->mdr.cost_matrix[onk->mdr.cost_matrix_index][onj->mdr.
                                                      cost_matrix_index] =
                oi->mdr.cost_matrix[onj->mdr.cost_matrix_index][onk->mdr.
                                                        cost_matrix_index];
            }
          else if (!onj->mdr.Report2Hop && onk->mdr.Report2Hop)
            {
              oi->mdr.cost_matrix[onj->mdr.cost_matrix_index][onk->mdr.
                                                      cost_matrix_index] =
                oi->mdr.cost_matrix[onk->mdr.cost_matrix_index][onj->mdr.
                                                        cost_matrix_index];
            }
        }
    }
}

static int
ospf6_mdr_cost (struct ospf6_neighbor *onj, struct ospf6_neighbor *onk)
{
  struct ospf6_interface *oi = onj->ospf6_if;

  //###### calculation of cost to my neighbor ######
  //###### 2nd entry must be NULL for this calc ####
  if (onj->state < OSPF6_NEIGHBOR_TWOWAY)
    return 0;                   //not my neighbor
  if (onk == NULL)              //indicates link with current node
    return 1;

  //must be on same interface
  assert (oi == onk->ospf6_if);

  if (ospf6_sidcds_lexicographic (oi->priority, onj->priority,
                                  oi->mdr.mdr_level, onj->mdr.mdr_level,
                                  ntohl (oi->area->ospf6->router_id),
                                  ntohl (onj->router_id)))
    return INFTY;

  return oi->mdr.cost_matrix[onj->mdr.cost_matrix_index][onk->mdr.cost_matrix_index];
}

// True if A > B; compare (RtrPri, MDR Level, RID)
static bool
ospf6_sidcds_lexicographic (int RtrPri_A, int RtrPri_B,
                            int DRLevel_A, int DRLevel_B,
                            u_int32_t RID_A, u_int32_t RID_B)
{
  if (RtrPri_A > RtrPri_B)
    return true;
  if ((RtrPri_A == RtrPri_B) && (DRLevel_A > DRLevel_B))
    return true;
  if ((RtrPri_A == RtrPri_B) && (DRLevel_A == DRLevel_B) && (RID_A > RID_B))
    return true;

  return false;
}

// Return true if a change occured.
bool
ospf6_mdr_set_mdr_level (struct ospf6_neighbor * on,
                         u_int32_t id1, u_int32_t id2)
{
  struct ospf6_interface *oi = on->ospf6_if;
  int old_mdr_level;
  bool changed = false;

  old_mdr_level = on->mdr.mdr_level;
  on->mdr.child = false;

  /*printf("set_mdr_level called, node %d level %d nbr %d level %d dep_sel %d\n",
     ntohl(oi->area->ospf6->router_id), oi->mdr.mdr_level,
     ntohl(on->router_id), on->mdr.mdr_level, on->dependent_selector); */
  if (on->router_id == id1)
    on->mdr.mdr_level = OSPF6_MDR;
  else if (on->router_id == id2)
    on->mdr.mdr_level = OSPF6_BMDR;
  else
    on->mdr.mdr_level = OSPF6_OTHER;
  // Set child even if it is a DR/BDR.
  if (oi->area->ospf6->router_id == id1 || oi->area->ospf6->router_id == id2)
    on->mdr.child = true;
  if (old_mdr_level != on->mdr.mdr_level)
    changed = true;
  // child change does not affect CDS calculation.
  if (on->mdr.mdr_level == OSPF6_OTHER)
    on->mdr.dependent = 0;          // MDR Other cannot be dependent.

  return changed;
}

// ###### Functions related to LSAs  ########

static int
ospf6_mdr_backbone (struct ospf6_neighbor *on)
{
  struct ospf6_interface *oi = on->ospf6_if;
  // If router is using fully connected adjacencies, nbr is backbone
  // iff nbr is using adjacency reduction, indicated by Abit = 0.
  if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED)
    {
      if (on->mdr.Abit == 0)
        return 1;
      else
        return 0;
    }
  // Otherwise, nbr is backbone if condition for needing
  // adjacency is satisfied.
  return need_adjacency (on);
}

// Updates the set of routable neighbors, by checking if a route
// exists to each neighbor. Returns 1 if there is a change.
int
ospf6_mdr_update_routable_neighbors (struct ospf6_interface *oi)
{
  struct listnode *j;
  struct ospf6_neighbor *on;
  struct ospf6_route *route;
  struct prefix prefix;
  int change = 0;

  // There are no routable neighbors if adjacency reduction is not used.
  if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED)
    return 0;

  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, on))
    {
      ospf6_linkstate_prefix (on->router_id, htonl (0), &prefix);

      route = ospf6_route_lookup (&prefix, on->ospf6_if->area->spf_table);
      if (!on->mdr.routable)
        {
          if (route && on->state >= OSPF6_NEIGHBOR_TWOWAY && on->mdr.reverse_2way)
            {
              on->mdr.routable = 1;
              change = 1;
            }
        }
      else if (on->mdr.routable && on->state < OSPF6_NEIGHBOR_TWOWAY)
        {
          on->mdr.routable = 0;
          change = 1;
        }
      // zlog_debug ("Neigh %s state %s routable %d",
      //             on->name, ospf6_neighbor_state_str[on->state], on->mdr.routable);
    }

  return change;
}

int
ospf6_mdr_update_lsa (struct ospf6_interface *oi)
{
  int change = 0;
  int originate = 0;
  change = ospf6_mdr_update_routable_neighbors (oi);

  // The called functions update on->sel_adv and on->adv for
  // each neighbor on, and return 1 if LSA should be originated.
  // on->adv indicates neighbor must be included in router-LSA.

  if (oi->mdr.LSAFullness == OSPF6_LSA_FULLNESS_MINCOST ||
      oi->mdr.LSAFullness == OSPF6_LSA_FULLNESS_MINCOST2PATHS)
    originate = ospf6_mdr_update_lsa_mincost (oi);
  else if (oi->mdr.LSAFullness == OSPF6_LSA_FULLNESS_MIN)
    originate = ospf6_mdr_update_lsa_minimal (oi);
  else if (oi->mdr.LSAFullness == OSPF6_LSA_FULLNESS_FULL)
    originate = ospf6_mdr_update_lsa_full (oi);
  else if (oi->mdr.LSAFullness == OSPF6_LSA_FULLNESS_MDRFULL)
    // For MDR full LSAs, MDR/BMDR generates full LSAs,
    // while Other generates minimal LSAs.
    // This also demonstrates the interoperability between
    // different LSA choices.
    {
      if (oi->mdr.mdr_level == OSPF6_MDR)
        originate = ospf6_mdr_update_lsa_full (oi);
      else
        originate = ospf6_mdr_update_lsa_minimal (oi);
    }

  if (originate)
    ospf6_router_lsa_schedule (oi->area);
  // Run SPF if the set of routable neighbors changed.
  if (change)
    ospf6_spf_schedule (oi->area);
  return originate;
}

static int
ospf6_mdr_update_lsa_full (struct ospf6_interface *oi)
{
  struct listnode *j;
  struct ospf6_neighbor *onj;
  int index, orig = 0;
  int num_neigh = oi->neighbor_list->count;
  int *new_adv = XMALLOC (MTYPE_OSPF6_MDR, num_neigh * sizeof (int));
  index = 0;
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
    {
      // For AdjConn = 0, there are no routable neighbors, and
      // there are no backbone neighbors unless AdjConn > 0
      // for some neighbor.
      onj->mdr.cost_matrix_index = index;
      // With full LSAs, SANL includes all bidirectional
      // neighbors except backbone neighbors.
      if (onj->state >= OSPF6_NEIGHBOR_TWOWAY && !ospf6_mdr_backbone (onj))
        onj->mdr.sel_adv = 1;
      else
        onj->mdr.sel_adv = 0;

      if (onj->mdr.sel_adv && onj->mdr.dependent)
        zlog_err ("Error: nbr is both sel_adv and dependent");
      if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED && onj->mdr.dependent)
        zlog_err ("Error: dependent nbr should not exist "
		  "with full adjacencies");
      if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED && onj->mdr.routable)
        zlog_err ("Error: routable nbr should not exist "
		  "with full adjacencies");

      // Full LSA includes all routable neighbors and Full neighbors.
      if (onj->mdr.routable || onj->state == OSPF6_NEIGHBOR_FULL)
        new_adv[index] = 1;
      else
        new_adv[index] = 0;
      /*printf("node %d nbr %d state %d routable %d index %d %d adv = %d\n",
         ntohl(ospf6->router_id), ntohl(onj->router_id),
         onj->state, onj->mdr.routable, index,
         onj->mdr.cost_matrix_index, new_adv[index]); */

      // Determine if LSA must be originated.
      if (!onj->mdr.adv && new_adv[index])
        orig = 1;
      // Do not originate LSA if nbr was advertised and is still 2-way.
      else if (onj->mdr.adv && onj->state < OSPF6_NEIGHBOR_TWOWAY)
        orig = 1;
      index++;
    }                           // for j
  // If LSA is to be originated, update onj->mdr.adv for each neighbor.
  if (orig)
    {
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
        {
          //printf("node %d nbr %d index %d adv = %d\n",
          //ntohl(ospf6->router_id), ntohl(onj->router_id),
          //onj->mdr.cost_matrix_index, onj->mdr.adv);
          onj->mdr.adv = new_adv[onj->mdr.cost_matrix_index];
        }
    }
  XFREE (MTYPE_OSPF6_MDR, new_adv);
  return orig;
}

// ospf6_mdr_update_lsa_minimal() updates the set of neighbors
// that are advertised in minimal LSAs, and returns 1 if there
// is a change in this set.
static int
ospf6_mdr_update_lsa_minimal (struct ospf6_interface *oi)
{
  struct listnode *j;
  struct ospf6_neighbor *onj;
  int selected_by_j;
  int index, orig = 0;
  int num_neigh = oi->neighbor_list->count;
  int *new_adv = XMALLOC (MTYPE_OSPF6_MDR, num_neigh * sizeof (int));
  index = 0;
  if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED)
    zlog_err ("Error: cannot use minimal LSAs with full adjacencies");
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
    {
      onj->mdr.cost_matrix_index = index;
      // Is the router a selected advertised neighbor of j?
      if (ospf6_mdr_lookup_neighbor (onj->mdr.sanl, ospf6->router_id))
        selected_by_j = 1;
      else
        selected_by_j = 0;

      // SANL is empty for minimal LSAs.
      onj->mdr.sel_adv = 0;

      // Include each Full neighbor, and each routable neighbor that is in
      // SANL, or whose SANL contains the router, or is a backbone neighbor.
      if (onj->state == OSPF6_NEIGHBOR_FULL || (onj->mdr.routable &&
                                                (onj->mdr.sel_adv || selected_by_j
                                                 ||
                                                 ospf6_mdr_backbone (onj))))
        new_adv[index] = 1;
      else
        new_adv[index] = 0;

      // Determine if LSA must be originated.
      if (!onj->mdr.adv && new_adv[index])
        orig = 1;
      // Do not originate LSA if nbr was advertised and is still 2-way.
      else if (onj->mdr.adv && onj->state < OSPF6_NEIGHBOR_TWOWAY)
        orig = 1;
      index++;
    }                           // for j
  // If LSA is to be originated, update onj->mdr.adv for each neighbor.
  if (orig)
    {
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
        onj->mdr.adv = new_adv[onj->mdr.cost_matrix_index];
    }
  XFREE (MTYPE_OSPF6_MDR, new_adv);
  return orig;
}

static int
ospf6_mdr_update_lsa_mincost (struct ospf6_interface *oi)
{
  struct listnode *j, *k, *u;
  struct ospf6_neighbor *onj, *onk, *onu;
  int orig = 0, j_index, k_index, u_index;
  int selected_by_j;
  int new_sel_adv, better_relay;
  int num_neigh = oi->neighbor_list->count;
  int *new_adv = XMALLOC (MTYPE_OSPF6_MDR, num_neigh * sizeof (int));

  // cost_matrix determines which nbrs are nbrs of each other.
  ospf6_mdr_create_cost_matrix (oi);
  // adj_matrix determines which nbr pairs are adjacent.
  // san_matrix determines whether neighbor j
  // includes neighbor k in its SANL.
  ospf6_mdr_create_adj_san_matrices (oi);

  // First update onj->mdr.sel_adv, using new_sel_adv as a temp variable.
  // For each pair of routable nbrs j, k that are not nbrs of each other,
  // find the best intermediate node u to reach from k to j.
  // If link costs are not advertised in Hellos, we assume each
  // link cost is 1.

  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
    {
      new_sel_adv = 0;          // Will be set to 1 if j should be adv.
      j_index = onj->mdr.cost_matrix_index;
      // Is the router a selected advertised neighbor of j?
      if (ospf6_mdr_lookup_neighbor (onj->mdr.sanl, ospf6->router_id))
        selected_by_j = 1;
      else
        selected_by_j = 0;
      // Note: onj->mdr.sel_adv indicates whether router is currently selecting j.
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, onk))
        {
          // Backbone neighbors will be added to adv list later.
          if (onj->state < OSPF6_NEIGHBOR_TWOWAY || ospf6_mdr_backbone (onj))
            break;
          if (onk == onj)
            continue;           // k must not be same as j.
          // k must be bidirectional
          if (onk->state < OSPF6_NEIGHBOR_TWOWAY)
            continue;
          k_index = onk->mdr.cost_matrix_index;
          if (oi->mdr.cost_matrix[j_index][k_index] == 1)
            continue;           // j and k must not be neighbors of each other
          better_relay = 0;
          for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, u, onu))
            {
              if (onu == onk)
                continue;
              if (onu == onj)
                continue;
              if (onu->state < OSPF6_NEIGHBOR_TWOWAY)
                continue;
              u_index = onu->mdr.cost_matrix_index;
              // u must be a neighbor of both j and k
              if (oi->mdr.cost_matrix[u_index][k_index] != 1)
                continue;
              if (oi->mdr.cost_matrix[u_index][j_index] != 1)
                continue;
              // We assume all link costs are 1; otherwise, we would
              // consider link costs here.
              if (oi->mdr.adj_matrix[u_index][j_index] ||
                  ospf6_sidcds_lexicographic (oi->mdr.
                                              san_matrix[j_index][u_index],
                                              selected_by_j,
                                              oi->mdr.
                                              san_matrix[u_index][j_index],
                                              onj->mdr.sel_adv,
                                              ntohl (onu->router_id),
                                              ntohl (ospf6->router_id)))
                {
                  better_relay = 1;
                  break;
                }
            }

          if (better_relay == 0)
            {
              new_sel_adv = 1;
              break;            // Break from k.
            }
        }                       // for k
      onj->mdr.sel_adv = new_sel_adv;

      new_adv[j_index] = 0;
      // Include each Full or routable neighbor that is in SANL,
      // or whose SANL contains the router, or is a backbone neighbor.
      if ((onj->state == OSPF6_NEIGHBOR_FULL || onj->mdr.routable) &&
          (onj->mdr.sel_adv || selected_by_j || ospf6_mdr_backbone (onj)))
        new_adv[j_index] = 1;
      // If adjacency reduction is used, also include each Full neighbor.
      if (oi->mdr.AdjConnectivity != OSPF6_ADJ_FULLYCONNECTED &&
          onj->state == OSPF6_NEIGHBOR_FULL)
        new_adv[j_index] = 1;

      // Determine if LSA must be originated.
      if (!onj->mdr.adv && new_adv[j_index])
        orig = 1;
      // Do not originate LSA if nbr was advertised and is still 2-way.
      else if (onj->mdr.adv && onj->state < OSPF6_NEIGHBOR_TWOWAY)
        orig = 1;
    }                           // for j
  //print_lsa_info(oi);

  // If LSA is to be originated, update onj->mdr.adv for each neighbor.
  if (orig)
    {
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
        onj->mdr.adv = new_adv[onj->mdr.cost_matrix_index];
    }

  XFREE (MTYPE_OSPF6_MDR, new_adv);
  ospf6_mdr_free_cost_matrix (oi);
  ospf6_mdr_free_adj_san_matrices (oi);
  return orig;
}

static void
ospf6_mdr_create_adj_san_matrices (struct ospf6_interface *oi)
{
  struct listnode *j, *k;
  struct ospf6_neighbor *onj, *onk;
  int count = 0, index2;
  int num_neigh = oi->neighbor_list->count;

  assert (oi->mdr.adj_matrix == NULL);
  assert (oi->mdr.san_matrix == NULL);
  oi->mdr.adj_matrix = XMALLOC (MTYPE_OSPF6_MDR, sizeof (int *[num_neigh]));
  oi->mdr.san_matrix = XMALLOC (MTYPE_OSPF6_MDR, sizeof (int *[num_neigh]));

  // Intialize matrices to zero.
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
    {
      //onj->mdr.cost_matrix_index = count; // Done in create_cost_matrix().
      oi->mdr.adj_matrix[count] = XMALLOC (MTYPE_OSPF6_MDR,
				       sizeof (int[num_neigh]));
      oi->mdr.san_matrix[count] = XMALLOC (MTYPE_OSPF6_MDR,
				       sizeof (int[num_neigh]));

      //memset (oi->mdr.cost_matrix[count++], 0, sizeof (int[num_neigh]));
      for (index2 = 0; index2 < num_neigh; index2++)
        {
          oi->mdr.adj_matrix[count][index2] = 0;
          oi->mdr.san_matrix[count][index2] = 0;
        }
      count++;
    }

  // Set appropriate entries of adj_matrix (symmetric).
  // Set appropriate entries of san_matrix (not symmetric).
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, onj))
    {
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, onk))
        {
          if (!oi->mdr.
              cost_matrix[onj->mdr.cost_matrix_index][onk->mdr.cost_matrix_index])
            continue;           // j and k are not neighbors

          // Set san_matrix(j,k) = 1 if j includes k in SANL
          if (ospf6_mdr_lookup_neighbor (onj->mdr.sanl, onk->router_id))
            {
              oi->mdr.san_matrix[onj->mdr.cost_matrix_index][onk->mdr.cost_matrix_index] =
                1;
              //printf("%d san matrix set for %d %d \n", ntohl(ospf6->router_id),
              //ntohl(onj->router_id), ntohl(onk->router_id));
            }

          if (oi->mdr.adj_matrix[onj->mdr.cost_matrix_index][onk->mdr.cost_matrix_index])
            continue;           // adj_matrix(j,k) already set to 1

          // Set adj_matrix(j,k) = adj_matrix(k,j) = 1 if the need_adjacency
          // condition is true for j and k.
          if ((onj->mdr.mdr_level >= OSPF6_BMDR && onk->mdr.mdr_level >= OSPF6_BMDR &&
	       ospf6_mdr_lookup_neighbor (onj->mdr.dnl, onk->router_id)) ||
              (onk->mdr.mdr_level >= OSPF6_BMDR && (onj->drouter == onk->router_id
						|| onj->bdrouter ==
						onk->router_id)))
            {
              oi->mdr.adj_matrix[onj->mdr.cost_matrix_index][onk->mdr.cost_matrix_index] =
                1;
              oi->mdr.adj_matrix[onk->mdr.cost_matrix_index][onj->mdr.cost_matrix_index] =
                1;
              //oi->mdr.san_matrix[onj->mdr.cost_matrix_index][onk->mdr.cost_matrix_index] = 1;
              //oi->mdr.san_matrix[onk->mdr.cost_matrix_index][onj->mdr.cost_matrix_index] = 1;
              //printf("adj matrix set for %d %d \n",
              //ntohl(onj->router_id), ntohl(onk->router_id));
            }
        }
    }
}

static void
ospf6_mdr_free_adj_san_matrices (struct ospf6_interface *oi)
{
  u_int i;

  //free adj matrix
  for (i = 0; i < oi->neighbor_list->count; i++)
    {
      XFREE (MTYPE_OSPF6_MDR, oi->mdr.adj_matrix[i]);
    }
  XFREE (MTYPE_OSPF6_MDR, oi->mdr.adj_matrix);
  oi->mdr.adj_matrix = NULL;
  //free san matrix
  for (i = 0; i < oi->neighbor_list->count; i++)
    XFREE (MTYPE_OSPF6_MDR, oi->mdr.san_matrix[i]);
  XFREE (MTYPE_OSPF6_MDR, oi->mdr.san_matrix);
  oi->mdr.san_matrix = NULL;
}
