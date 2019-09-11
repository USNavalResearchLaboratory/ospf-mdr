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

#include <zebra.h>

#include "log.h"
#include "thread.h"

#include "ospf6d.h"
#include "ospf6_neighbor.h"
#include "ospf6_lsa.h"
#include "ospf6_interface.h"
#include "ospf6_flood.h"
#include "ospf6_lsdb.h"
#include "ospf6_top.h"
#include "ospf6_mdr_flood.h"

int
ospf6_flood_interface_mdr (struct ospf6_neighbor *from,
                           struct ospf6_lsa *lsa, struct ospf6_interface *oi)
{
  struct listnode *node, *nnode;
  struct ospf6_neighbor *on;
  struct ospf6_lsa *req;
  int retrans_added = 0;
  int is_debug = 0;
  struct list *flood_neighbors = list_new ();
  bool flood_lsa = true;

  if (IS_OSPF6_DEBUG_FLOODING ||
      IS_OSPF6_DEBUG_FLOOD_TYPE (lsa->header->type))
    {
      is_debug++;
      zlog_debug ("Flooding on %s: %s", oi->interface->name, lsa->name);
    }

  /* (1) For each neighbor */
  for (ALL_LIST_ELEMENTS (oi->neighbor_list, node, nnode, on))
    {
      if (is_debug)
        zlog_debug ("To neighbor %s", on->name);

      /* (a) if neighbor state < Exchange, examin next */
      // Consider adjacent and (backup) dependent neighbors.
      // Require all bidirectional neighbors to be covered
      if (on->state < OSPF6_NEIGHBOR_TWOWAY)
        {
          if (is_debug)
            zlog_debug ("Neighbor state less than TwoWay, next neighbor");
          continue;
        }

      if (on->state > OSPF6_NEIGHBOR_TWOWAY &&
	  on->state < OSPF6_NEIGHBOR_FULL && !need_adjacency (on))
	{
	  if (is_debug)
	    zlog_debug ("No longer need adjacency with neighbor %s: "
			"scheduling AdjOK?", on->name);
	  ospf6_neighbor_schedule_adjok (on);
	  continue;
	}

      /* (b) if neighbor not yet Full, check request-list */
      if (on->state >= OSPF6_NEIGHBOR_EXCHANGE &&
          on->state != OSPF6_NEIGHBOR_FULL)
        {
          if (is_debug)
            zlog_debug ("Neighbor not yet Full");

          req = ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                                   lsa->header->adv_router, on->request_list);
          if (req == NULL)
            {
              if (is_debug)
                zlog_debug ("Not on request-list for this neighbor");
              /* fall through */
            }
          else
            {
              /* If new LSA less recent, examin next neighbor */
              if (ospf6_lsa_compare (lsa, req) > 0)
                {
                  if (is_debug)
                    zlog_debug ("Requesting is newer, next neighbor");
                  continue;
                }

              /* If the same instance, delete from request-list and
                 examin next neighbor */
              if (ospf6_lsa_compare (lsa, req) == 0)
                {
                  if (is_debug)
                    zlog_debug
                      ("Requesting the same, remove it, next neighbor");
                  ospf6_lsdb_remove (req, on->request_list);
                  continue;
                }

              /* If the new LSA is more recent, delete from request-list */
              if (ospf6_lsa_compare (lsa, req) < 0)
                {
                  if (is_debug)
                    zlog_debug ("Received is newer, remove requesting");
                  ospf6_lsdb_remove (req, on->request_list);
                  /* fall through */
                }
            }
        }

      /* (c) If the new LSA was received from this neighbor,
         examin next neighbor */
      if (from == on)
        {
          if (is_debug)
            zlog_debug ("LSA was received from neighbor %s, next neighbor",
			on->name);
          continue;
        }

      //Ogierv3 Section 6 Par 3
      /* At this point, we are not positive that the neighbor has
         an up-to-date instance of this new LSA */
      /* However, in the MANET case, we need to:
         i) check whether neighbor sent a multicast ACK for it already
         ii) whether I am an active relay for this originator */
      /* Has LSA been acked previously with multicast ack? */
      if (ospf6_mdr_neighbor_has_acked (on, lsa))
        {                       //Don't add LSA to neighbor's retransmission list
          if (is_debug)
            zlog_debug ("Existing multicast ACK from neighbor %s found "
			"for LSA, next neighbor", on->name);
          continue;             // examine next neighbor: neighbor already acked
        }
      /* Here, checking for coverage of this neighbor on the sender's RNL.
         If not present, I add this to the flood_neighbors list.
         If LSA was received as a unicast, however, can't assume that
         neighbor "on" was covered by the transmission, so still need to
         add to flood_neighbors regardless of from->rnl */
      if (from)
        {
          if (!from->mdr.Report2Hop ||
              (!CHECK_FLAG (lsa->flag, OSPF6_LSA_RECVMCAST)) ||
              !ospf6_mdr_lookup_neighbor (from->mdr.rnl, on->router_id))
            {
              listnode_add (flood_neighbors, on);
            }
        }

      // Retransmit only to adjacent neighbors.
      if (on->state < OSPF6_NEIGHBOR_EXCHANGE)
        continue;

      /* (d) add retrans-list, schedule retransmission */
      if (is_debug)
        zlog_debug ("Add retrans-list of this neighbor");
      ospf6_increment_retrans_count (lsa);

      quagga_gettime (QUAGGA_CLK_MONOTONIC, &lsa->rxmt_time);
      ospf6_lsdb_add (ospf6_lsa_copy (lsa), on->retrans_list);
      on->thread_send_lsupdate =
        ospf6_send_lsupdate_delayed_msec (master,
                                          ospf6_lsupdate_send_neighbor, on,
                                          oi->rxmt_interval * 1000,
                                          on->thread_send_lsupdate);
      retrans_added++;
    }
  /* (2) examin next interface if not added to retrans-list */
  // MDRs can flood an LSA without adding it to the retrans-list
  if (!from && retrans_added == 0)
    {
      /* this LSA is self-originated but there are no adjacent neighbors */
      flood_lsa = false;
      if (is_debug)
        zlog_debug ("Self-originated LSA and no adjacent neighbors");
    }

  //Ogierv3 Section 6 - Remove (3) and (4)

  //Ogierv3 Section 6 - Replace (5)
  //Ogierv3 Forwarding Procedure bullet(a)
  if (from && oi->mdr.mdr_level == OSPF6_MDR)
    {
      //Flood the LSA unless all neighbors are already covered
      if (flood_neighbors->count == 0)
	{
	  flood_lsa = false;
	  if (is_debug)
	    zlog_debug ("All neighbors covered");
	}
      else if (oi->mdr.nonflooding_mdr)
        {
          // Non-flooding MDR acts like a BMDR.
          for (ALL_LIST_ELEMENTS (flood_neighbors, node, nnode, on))
            ospf6_backupwait_lsa_add (lsa, on);
          flood_lsa = false;
	  if (is_debug)
	    zlog_debug ("Router is a non-flooding MDR");
        }
    }
  //Ogierv3 Forwarding Procedure bullet(c)
  else if (from && oi->mdr.mdr_level == OSPF6_BMDR)
    {
      //Don't Flood the LSA now, but backup the uncovered neighbors
      for (ALL_LIST_ELEMENTS (flood_neighbors, node, nnode, on))
        ospf6_backupwait_lsa_add (lsa, on);
      flood_lsa = false;
      if (is_debug)
	zlog_debug ("Router is a BMDR");
    }
  else if (from && oi->mdr.mdr_level == OSPF6_OTHER)
    {
      //OTHER routers do not flood
      flood_lsa = false;
      if (is_debug)
	zlog_debug ("Router is not a MDR/BMDR");
    }

  list_delete (flood_neighbors);

  if (!flood_lsa)
    {
      if (is_debug)
	zlog_debug ("Not flooding LSA %s on interface %s",
		    lsa->name, oi->interface->name);
      return 0;
    }

  if (from && from->ospf6_if == oi)
    SET_FLAG (lsa->flag, OSPF6_LSA_FLOODBACK);

  /* (5) flood the LSA out the interface. */
  if (is_debug)
    zlog_debug ("Schedule flooding for the interface");

  ospf6_lsdb_add (ospf6_lsa_copy (lsa), oi->lsupdate_list);

  oi->thread_send_lsupdate =
    ospf6_send_lsupdate_delayed_msec (master, ospf6_lsupdate_send_interface,
                                      oi, oi->flood_delay,
                                      oi->thread_send_lsupdate);

  return 1;
}

/* RFC 5614: 8.2 */
void
ospf6_mdr_acknowledge_lsa_allother (struct ospf6_lsa *lsa,
				    struct ospf6_interface *oi,
				    struct in6_addr *dst)
{
  int is_debug = 0;
  struct ospf6_lsa *lsa_ack;

  if (IS_OSPF6_DEBUG_FLOODING ||
      IS_OSPF6_DEBUG_FLOOD_TYPE (lsa->header->type))
    is_debug++;

  /* non-duplicate LSAs have already been handled */
  if (!CHECK_FLAG (lsa->flag, OSPF6_LSA_DUPLICATE))
    return;

  //only acknowledge the first arrival of the lsa
  if (IN6_IS_ADDR_MULTICAST (dst))
    return;             //NO ACK

  if (is_debug)
    zlog_debug ("Direct acknowledgement (AllOther & Duplicate)");

  lsa_ack = ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
			       lsa->header->adv_router,
			       oi->lsack_list);
  // Add LSA to lsack_list only if it is not already on list,
  // to keep same transmission time.
  if (!lsa_ack)
    {
      lsa_ack = ospf6_lsa_copy (lsa);
      quagga_gettime (QUAGGA_CLK_MONOTONIC, &lsa_ack->rxmt_time);
      ospf6_lsdb_add (lsa_ack, oi->lsack_list);
    }

  // SICDS sends a multicast ACK immediately if full adjacencies
  // are used, or router is MDR, or biconnected adjacencies are
  // used and router is BMDR.
  if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED ||
      oi->mdr.mdr_level == OSPF6_MDR ||
      (oi->mdr.mdr_level == OSPF6_BMDR &&
       oi->mdr.AdjConnectivity == OSPF6_ADJ_BICONNECTED))
    {
      timerclear (&lsa_ack->rxmt_time); // To send immediately.
      if (oi->thread_send_lsack)
	THREAD_OFF (oi->thread_send_lsack);
      oi->thread_send_lsack =
	thread_add_timer_msec (master, ospf6_lsack_send_interface, oi, 0);
    }
  else if (oi->thread_send_lsack == NULL)
    {
      oi->thread_send_lsack =
	thread_add_timer_msec (master, ospf6_lsack_send_interface,
			       oi, oi->mdr.ackInterval);
    }
}

static void
ospf6_refresh_lsa_backupwait_list (struct ospf6_lsa *lsa)
{
  struct listnode *node, *nnode;
  struct ospf6_neighbor *on;
  struct ospf6_interface *oi;
  struct ospf6_backupwait_neighbor *obn;

  //The neighbor state of backupwait neighbors could have changed
  //remove those backupwait neighbors in a state below EXCHANGE

  if (!lsa->backupwait_neighbor_list)
    return;

  for (ALL_LIST_ELEMENTS (lsa->backupwait_neighbor_list, node, nnode, obn))
    {
      oi = ospf6_interface_lookup_by_ifindex (obn->ifindex);
      on = ospf6_neighbor_lookup (obn->router_id, oi);

      //For SICDS, delete neighbors that are below TWOWAY.
      if (!on || on->state < OSPF6_NEIGHBOR_TWOWAY)
        {                       //backupwait neighbor fell below state TWOWAY
          listnode_delete (lsa->backupwait_neighbor_list, obn);
          free (obn);
        }
    }
  //there are no backupwait neigbors left, cancel the backupwait timer
  if (lsa->backupwait_neighbor_list->count == 0)
    {
      ospf6_backupwait_lsa_delete (lsa);
    }
}

static int
ospf6_backupwait_expiration (struct thread *thread)
{
  struct ospf6_lsa *lsa = (struct ospf6_lsa *) THREAD_ARG (thread);
  struct listnode *node, *nnode, *i;
  struct list *eligible_interfaces;
  struct ospf6_backupwait_neighbor *obn;
  struct ospf6_neighbor *on;
  struct ospf6_interface *oi;
  struct ospf6_lsa *rxmt_lsa;
  struct ospf6_lsa *ack_lsa;

  lsa->backupWaitTimer = NULL;
  ospf6_refresh_lsa_backupwait_list (lsa);
  if (!lsa->backupwait_neighbor_list)
    return 0;

  eligible_interfaces = list_new ();
  for (ALL_LIST_ELEMENTS (lsa->backupwait_neighbor_list, node, nnode, obn))
    {
      //neighbor should exist because backupwait list was just refreshed
      oi = ospf6_interface_lookup_by_ifindex (obn->ifindex);
      on = ospf6_neighbor_lookup (obn->router_id, oi);
      assert (on);
      if (!listnode_lookup (eligible_interfaces, oi))
        listnode_add (eligible_interfaces, oi);

      //BackupWait Timer Expiration 8.1.2.2
      ospf6_backupwait_lsa_neighbor_delete (lsa, on);
    }

  for (ALL_LIST_ELEMENTS_RO (eligible_interfaces, i, oi))
    {
      if (IS_OSPF6_DEBUG_FLOODING)
        zlog_info ("  Add copy of %s to lsupdate_list of %s",
                   lsa->name, oi->interface->name);

      //BackupWait Timer Expiration 8.1.2.1.b
      //if LSA is on ack list, this will count as an implict ack
      //remove LSA from ack list (MANET always on interface ack list)
      ack_lsa =
        ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                           lsa->header->adv_router, oi->lsack_list);
      if (ack_lsa)
        ospf6_lsdb_remove (ack_lsa, oi->lsack_list);

      //BackupWait Timer Expiration 8.1.2.1.c
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
        {
          // Reset rxmt time if LSA is in retrans list.
          rxmt_lsa =
            ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                               lsa->header->adv_router, on->retrans_list);
          if (rxmt_lsa)
	    quagga_gettime (QUAGGA_CLK_MONOTONIC, &rxmt_lsa->rxmt_time);
	}

      //BackupWait Timer Expiration 8.1.2.1.a
      ospf6_lsdb_add (ospf6_lsa_copy (lsa), oi->lsupdate_list);
      //XXX BOEING LSAs after this are gone from the perspective of backupwait
      //with delay equal to 1msec no coalescing takes place
      //with a higher delay backupWaitInterval is effectively increased
      oi->thread_send_lsupdate =
        ospf6_send_lsupdate_delayed_msec (master,
                                          ospf6_lsupdate_send_interface, oi,
                                          1, oi->thread_send_lsupdate);
    }
  list_delete (eligible_interfaces);
  return 0;
}

void
ospf6_backupwait_lsa_add (struct ospf6_lsa *lsa, struct ospf6_neighbor *on)
{
  struct listnode *n;
  struct ospf6_backupwait_neighbor *obn = NULL;

  if (on->ospf6_if->type != OSPF6_IFTYPE_MDR)
    return;

  ospf6_refresh_lsa_backupwait_list (lsa);

  //create the backupwait list and schedule expiration
  if (lsa->backupWaitTimer == NULL)
    {
      unsigned int msec;

      msec = on->ospf6_if->mdr.BackupWaitInterval +
	ospf6_random (on->ospf6_if->mdr.BackupWaitInterval);

      lsa->backupWaitTimer =
        thread_add_timer_msec (master, ospf6_backupwait_expiration, lsa, msec);
      lsa->backupwait_neighbor_list = list_new ();
    }

  //Is this neighbor already on the push back list?
  for (ALL_LIST_ELEMENTS_RO (lsa->backupwait_neighbor_list, n, obn))
    {
      if (on->router_id == obn->router_id &&
          on->ospf6_if->interface->ifindex == obn->ifindex)
        return;                 //already in the list
    }

  //put the backupwait neighbor on the backupwait list
  obn = (struct ospf6_backupwait_neighbor *)
    malloc (sizeof (struct ospf6_backupwait_neighbor));
  obn->router_id = on->router_id;
  obn->ifindex = on->ospf6_if->interface->ifindex;
  listnode_add (lsa->backupwait_neighbor_list, obn);
}

void
ospf6_backupwait_lsa_neighbor_delete (struct ospf6_lsa *lsa,
                                      struct ospf6_neighbor *on)
{
  struct listnode *node, *nnode;
  struct ospf6_backupwait_neighbor *obn;

  //Find backupwait neighbor, if found remove
  for (ALL_LIST_ELEMENTS (lsa->backupwait_neighbor_list, node, nnode, obn))
    {
      if (on->router_id == obn->router_id &&
          on->ospf6_if->interface->ifindex == obn->ifindex)
        {
          listnode_delete (lsa->backupwait_neighbor_list, obn);
          free (obn);
          break;
        }
    }
  //clean out old neighbors and cancel backupwait thread if backupwait
  //no more backupwait neighbors
  ospf6_refresh_lsa_backupwait_list (lsa);
}

void
ospf6_backupwait_lsa_delete (struct ospf6_lsa *lsa)
{
  struct listnode *node, *nnode;
  struct ospf6_backupwait_neighbor *obn;

  //cancel backupwait thread
  THREAD_OFF (lsa->backupWaitTimer);
  lsa->backupWaitTimer = NULL;

  //clean up backupwait neighbor list
  if (lsa->backupwait_neighbor_list)
    {
      for (ALL_LIST_ELEMENTS (lsa->backupwait_neighbor_list, node, nnode, obn))
        free (obn);
      list_delete (lsa->backupwait_neighbor_list);
      lsa->backupwait_neighbor_list = NULL;
    }
}
