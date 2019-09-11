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

#include "log.h"
#include "thread.h"
#include "linklist.h"
#include "vty.h"
#include "command.h"

#include "ospf6d.h"
#include "ospf6_proto.h"
#include "ospf6_lsa.h"
#include "ospf6_lsdb.h"
#include "ospf6_message.h"
#include "ospf6_route.h"
#include "ospf6_spf.h"

#include "ospf6_top.h"
#include "ospf6_area.h"
#include "ospf6_interface.h"
#include "ospf6_neighbor.h"

#include "ospf6_mdr_flood.h"
#include "ospf6_flood.h"

unsigned char conf_debug_ospf6_flooding;

struct ospf6_lsdb *
ospf6_get_scoped_lsdb (struct ospf6_lsa *lsa)
{
  struct ospf6_lsdb *lsdb = NULL;
  switch (OSPF6_LSA_SCOPE (lsa->header->type))
    {
    case OSPF6_SCOPE_LINKLOCAL:
      lsdb = OSPF6_INTERFACE (lsa->lsdb->data)->lsdb;
      break;
    case OSPF6_SCOPE_AREA:
      lsdb = OSPF6_AREA (lsa->lsdb->data)->lsdb;
      break;
    case OSPF6_SCOPE_AS:
      lsdb = OSPF6_PROCESS (lsa->lsdb->data)->lsdb;
      break;
    default:
      assert (0);
      break;
    }
  return lsdb;
}

struct ospf6_lsdb *
ospf6_get_scoped_lsdb_self (struct ospf6_lsa *lsa)
{
  struct ospf6_lsdb *lsdb_self = NULL;
  switch (OSPF6_LSA_SCOPE (lsa->header->type))
    {
    case OSPF6_SCOPE_LINKLOCAL:
      lsdb_self = OSPF6_INTERFACE (lsa->lsdb->data)->lsdb_self;
      break;
    case OSPF6_SCOPE_AREA:
      lsdb_self = OSPF6_AREA (lsa->lsdb->data)->lsdb_self;
      break;
    case OSPF6_SCOPE_AS:
      lsdb_self = OSPF6_PROCESS (lsa->lsdb->data)->lsdb_self;
      break;
    default:
      assert (0);
      break;
    }
  return lsdb_self;
}

/*
 * Remove lsa from delayed lsa list.
 * Return 0 if lsa was found and removed, non-zero otherwise.
 */
static int
ospf6_remove_delayed_lsa (struct ospf6_lsa *lsa)
{
  struct listnode *node;
  struct ospf6_lsa *delayed_lsa;

  for (ALL_LIST_ELEMENTS_RO (ospf6->delayed_lsa_list, node, delayed_lsa))
    if (delayed_lsa == lsa)
      break;

  if (node)
    {
      list_delete_node (ospf6->delayed_lsa_list, node);
      return 0;
    }

  return -1;
}

/*
 * Simple wrapper function that has the right signature for a list
 * delete callback.
 */
void
ospf6_lsa_list_delete (void *data)
{
  struct ospf6_lsa *lsa = data;

  ospf6_lsa_delete (lsa);
}

/*
 * Delayed lsa callback: check that the thread argument lsa is a valid
 * delayed lsa then originate it.
 */
static int
ospf6_lsa_delayed_originate (struct thread *thread)
{
  struct ospf6_lsa *lsa;

  assert (thread);

  lsa = THREAD_ARG (thread);
  assert (lsa);

  if (ospf6_remove_delayed_lsa (lsa))
    {
      zlog_err ("%s: lsa %s (%p) not in delayed lsa list",
		__func__, lsa->name, lsa);
      return 0;
    }

  if (IS_OSPF6_DEBUG_ORIGINATE_TYPE (lsa->header->type))
    zlog_debug ("%s: originating delayed LSA: %s", __func__, lsa->name);

  lsa->delay = NULL;
  ospf6_lsa_originate (lsa);

  return 0;
}

/*
 * Find a matching lsa in the delayed lsa list.  A matching lsa has
 * the same advertising router, LS type, and LS ID.
 *
 * Returns the corresponding list node if a match is found, NULL
 * otherwise.
 */
static struct listnode
*ospf6_find_matching_delayed_lsa_node (struct ospf6_lsa *lsa)
{
  struct listnode *node;
  struct ospf6_lsa *delayed_lsa;

  for (ALL_LIST_ELEMENTS_RO (ospf6->delayed_lsa_list, node, delayed_lsa))
    if (OSPF6_LSA_IS_SAME (delayed_lsa, lsa))
      return node;

  return NULL;
}

void
ospf6_lsa_originate (struct ospf6_lsa *lsa)
{
  struct ospf6_lsa *old;
  struct ospf6_lsdb *lsdb_self;
  struct listnode *delayed_lsa_node;

  delayed_lsa_node = ospf6_find_matching_delayed_lsa_node (lsa);
  if (delayed_lsa_node)
    {
      struct ospf6_lsa *delayed_lsa = listgetdata (delayed_lsa_node);

      if (! OSPF6_LSA_IS_DIFFER (lsa, delayed_lsa))
	{
	  if (IS_OSPF6_DEBUG_ORIGINATE_TYPE (lsa->header->type))
	    zlog_debug ("%s: Suppress updating LSA "
			"(same LSA already delayed): %s",
			__func__, lsa->name);
	  ospf6_lsa_delete (lsa);
	  return;
	}
      else if (ntohl (lsa->header->seqnum) >=
	       ntohl (delayed_lsa->header->seqnum))
        {
	  /* delayed lsa is different and is superseded by this lsa */
	  if (IS_OSPF6_DEBUG_ORIGINATE_TYPE (lsa->header->type))
	    zlog_debug ("%s: updating delayed LSA %s with %s",
			__func__, delayed_lsa->name, lsa->name);
	  list_delete_node (ospf6->delayed_lsa_list, delayed_lsa_node);
	  ospf6_lsa_delete (delayed_lsa);
	}
    }

  /* find previous LSA */
  old = ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                           lsa->header->adv_router, lsa->lsdb);

  /* if the new LSA does not differ from previous,
     suppress this update of the LSA */
  if (old && ! OSPF6_LSA_IS_DIFFER (lsa, old))
    {
      if (IS_OSPF6_DEBUG_ORIGINATE_TYPE (lsa->header->type))
        zlog_debug ("Suppress updating LSA: %s", lsa->name);
      ospf6_lsa_delete (lsa);
      return;
    }

  if (old)
    {
      long delay_msec;

      delay_msec =
	1000 * ospf6->min_lsa_interval - elapsed_msec (&old->originated);
      if (delay_msec > 0)
	{
	  if (IS_OSPF6_DEBUG_ORIGINATE_TYPE (lsa->header->type))
	    zlog_debug ("%s: delaying LSA %s by %li msec to satisfy "
			"MinLSInterval", __func__, lsa->name, delay_msec);
	  listnode_add (ospf6->delayed_lsa_list, lsa);
	  lsa->delay =
	    thread_add_timer_msec (master, ospf6_lsa_delayed_originate,
				   lsa, delay_msec);
	  return;
	}
    }

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &lsa->originated);

  /* store it in the LSDB for self-originated LSAs */
  lsdb_self = ospf6_get_scoped_lsdb_self (lsa);
  ospf6_lsdb_add (ospf6_lsa_copy (lsa), lsdb_self);

  lsa->refresh = thread_add_timer (master, ospf6_lsa_refresh, lsa,
                                   LS_REFRESH_TIME);

  if (IS_OSPF6_DEBUG_LSA_TYPE (lsa->header->type) ||
      IS_OSPF6_DEBUG_ORIGINATE_TYPE (lsa->header->type))
    {
      zlog_debug ("LSA Originate:");
      ospf6_lsa_header_print (lsa);
    }

  if (old)
    ospf6_flood_clear (old);
  ospf6_flood (NULL, lsa);
  ospf6_install_lsa (lsa);
}

void
ospf6_lsa_originate_process (struct ospf6_lsa *lsa,
                             struct ospf6 *process)
{
  lsa->lsdb = process->lsdb;
  ospf6_lsa_originate (lsa);
}

void
ospf6_lsa_originate_area (struct ospf6_lsa *lsa,
                          struct ospf6_area *oa)
{
  lsa->lsdb = oa->lsdb;
  ospf6_lsa_originate (lsa);
}

void
ospf6_lsa_originate_interface (struct ospf6_lsa *lsa,
                               struct ospf6_interface *oi)
{
  lsa->lsdb = oi->lsdb;
  ospf6_lsa_originate (lsa);
}

void
ospf6_lsa_purge (struct ospf6_lsa *lsa)
{
  struct ospf6_lsa *self;
  struct ospf6_lsdb *lsdb_self;
  struct listnode *delayed_lsa_node;

  /* remove it from the LSDB for self-originated LSAs */
  lsdb_self = ospf6_get_scoped_lsdb_self (lsa);
  self = ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                            lsa->header->adv_router, lsdb_self);
  if (self)
    {
      THREAD_OFF (self->expire);
      THREAD_OFF (self->refresh);
      ospf6_lsdb_remove (self, lsdb_self);
    }

  /* remove any pending, previously delayed LSAs */
  delayed_lsa_node = ospf6_find_matching_delayed_lsa_node (lsa);
  if (delayed_lsa_node)
    {
      struct ospf6_lsa *delayed_lsa = listgetdata (delayed_lsa_node);
      list_delete_node (ospf6->delayed_lsa_list, delayed_lsa_node);
      ospf6_lsa_delete (delayed_lsa);
    }

  ospf6_lsa_premature_aging (lsa);
}


void
ospf6_increment_retrans_count (struct ospf6_lsa *lsa)
{
  /* The LSA must be the original one (see the description
     in ospf6_decrement_retrans_count () below) */
  lsa->retrans_count++;
}

void
ospf6_decrement_retrans_count (struct ospf6_lsa *lsa)
{
  struct ospf6_lsdb *lsdb;
  struct ospf6_lsa *orig;

  /* The LSA must be on the retrans-list of a neighbor. It means
     the "lsa" is a copied one, and we have to decrement the
     retransmission count of the original one (instead of this "lsa"'s).
     In order to find the original LSA, first we have to find
     appropriate LSDB that have the original LSA. */
  lsdb = ospf6_get_scoped_lsdb (lsa);

  /* Find the original LSA of which the retrans_count should be decremented */
  orig = ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                            lsa->header->adv_router, lsdb);
  if (orig)
    {
      orig->retrans_count--;
      assert (orig->retrans_count >= 0);
    }
}

/* RFC2328 section 13.2 Installing LSAs in the database */
void
ospf6_install_lsa (struct ospf6_lsa *lsa)
{
  struct ospf6_lsa *old;
  struct timeval now;
  bool is_maxage;

  if (IS_OSPF6_DEBUG_LSA_TYPE (lsa->header->type) ||
      IS_OSPF6_DEBUG_EXAMIN_TYPE (lsa->header->type))
    zlog_debug ("Install LSA: %s", lsa->name);

  /* Remove the old instance from all neighbors' Link state
     retransmission list (RFC2328 13.2 last paragraph) */
  old = ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                           lsa->header->adv_router, lsa->lsdb);
  if (old)
    {
      THREAD_OFF (old->expire);
      ospf6_flood_clear (old);
    }

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);
  is_maxage = OSPF6_LSA_IS_MAXAGE (lsa);

  if (! is_maxage)
    lsa->expire = thread_add_timer (master, ospf6_lsa_expire, lsa,
                                    MAXAGE + lsa->birth.tv_sec - now.tv_sec);
  else
    lsa->expire = NULL;

  /* actually install */
  lsa->installed = now;
  ospf6_lsdb_add (lsa, lsa->lsdb);

  if (is_maxage)
    {
      /* schedule maxage remover */
      ospf6_maxage_remove (ospf6);
    }

  return;
}

/* RFC2740 section 3.5.2. Sending Link State Update packets */
/* RFC2328 section 13.3 Next step in the flooding procedure */
static void
ospf6_flood_interface (struct ospf6_neighbor *from,
                       struct ospf6_lsa *lsa, struct ospf6_interface *oi)
{
  struct listnode *node, *nnode;
  struct ospf6_neighbor *on;
  struct ospf6_lsa *req;
  int retrans_added = 0;
  int is_debug = 0;

  if (oi->type == OSPF6_IFTYPE_LOOPBACK)
    return;

  if (oi->type == OSPF6_IFTYPE_MDR)
    {
      ospf6_flood_interface_mdr (from, lsa, oi);
      return;
    }

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
      if (on->state < OSPF6_NEIGHBOR_EXCHANGE)
        {
          if (is_debug)
            zlog_debug ("Neighbor state less than ExChange, next neighbor");
          continue;
        }

      /* (b) if neighbor not yet Full, check request-list */
      if (on->state != OSPF6_NEIGHBOR_FULL)
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
                    zlog_debug ("Requesting the same, remove it, next neighbor");
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
            zlog_debug ("Received is from the neighbor, next neighbor");
          continue;
        }

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
  if (retrans_added == 0)
    {
      if (is_debug)
        zlog_debug ("No retransmission scheduled, next interface");
      return;
    }

  /* (3) If the new LSA was received on this interface,
     and it was from DR or BDR, examin next interface */
  if (from && from->ospf6_if == oi &&
      (from->router_id == oi->drouter || from->router_id == oi->bdrouter))
    {
      if (is_debug)
        zlog_debug ("Received is from the I/F's DR or BDR, next interface");
      return;
    }

  /* (4) If the new LSA was received on this interface,
     and the interface state is BDR, examin next interface */
  if (from && from->ospf6_if == oi && oi->state == OSPF6_INTERFACE_BDR)
    {
      if (is_debug)
        zlog_debug ("Received is from the I/F, itself BDR, next interface");
      return;
    }
  if (from && from->ospf6_if == oi)
    SET_FLAG (lsa->flag, OSPF6_LSA_FLOODBACK);

  /* (5) flood the LSA out the interface. */
  if (is_debug)
    zlog_debug ("Schedule flooding for the interface");

  if (oi->type == OSPF6_IFTYPE_BROADCAST ||
      oi->type == OSPF6_IFTYPE_MDR ||
      oi->type == OSPF6_IFTYPE_POINTOMULTIPOINT ||
      oi->type == OSPF6_IFTYPE_NBMA)
    {
      ospf6_lsdb_add (ospf6_lsa_copy (lsa), oi->lsupdate_list);
      oi->thread_send_lsupdate =
        ospf6_send_lsupdate_delayed_msec (master,
                                          ospf6_lsupdate_send_interface, oi,
                                          oi->flood_delay,
                                          oi->thread_send_lsupdate);
    }
  else
    {
      /* reschedule retransmissions to all neighbors */
      for (ALL_LIST_ELEMENTS (oi->neighbor_list, node, nnode, on))
        {
	  ospf6_lsdb_add (ospf6_lsa_copy (lsa), on->lsupdate_list);
          on->thread_send_lsupdate =
            ospf6_send_lsupdate_delayed_msec (master,
                                              ospf6_lsupdate_send_neighbor,
                                              on, oi->flood_delay,
                                              on->thread_send_lsupdate);
        }
    }
}

static void
ospf6_flood_area (struct ospf6_neighbor *from,
                  struct ospf6_lsa *lsa, struct ospf6_area *oa)
{
  struct listnode *node, *nnode;
  struct ospf6_interface *oi;

  for (ALL_LIST_ELEMENTS (oa->if_list, node, nnode, oi))
    {
      if (OSPF6_LSA_SCOPE (lsa->header->type) == OSPF6_SCOPE_LINKLOCAL &&
          oi != OSPF6_INTERFACE (lsa->lsdb->data))
        continue;

#if 0
      if (OSPF6_LSA_SCOPE (lsa->header->type) == OSPF6_SCOPE_AS &&
          ospf6_is_interface_virtual_link (oi))
        continue;
#endif/*0*/

      ospf6_flood_interface (from, lsa, oi);
    }
}

static void
ospf6_flood_process (struct ospf6_neighbor *from,
                     struct ospf6_lsa *lsa, struct ospf6 *process)
{
  struct listnode *node, *nnode;
  struct ospf6_area *oa;

  for (ALL_LIST_ELEMENTS (process->area_list, node, nnode, oa))
    {
      if (OSPF6_LSA_SCOPE (lsa->header->type) == OSPF6_SCOPE_AREA &&
          oa != OSPF6_AREA (lsa->lsdb->data))
        continue;
      if (OSPF6_LSA_SCOPE (lsa->header->type) == OSPF6_SCOPE_LINKLOCAL &&
          oa != OSPF6_INTERFACE (lsa->lsdb->data)->area)
        continue;

      if (ntohs (lsa->header->type) == OSPF6_LSTYPE_AS_EXTERNAL &&
          IS_AREA_STUB (oa))
        continue;

      ospf6_flood_area (from, lsa, oa);
    }
}

void
ospf6_flood (struct ospf6_neighbor *from, struct ospf6_lsa *lsa)
{
  ospf6_flood_process (from, lsa, ospf6);
}

static void
ospf6_flood_clear_interface (struct ospf6_lsa *lsa, struct ospf6_interface *oi)
{
  struct listnode *node, *nnode;
  struct ospf6_neighbor *on;
  struct ospf6_lsa *rem;
  struct ospf6_lsa *update;

  for (ALL_LIST_ELEMENTS (oi->neighbor_list, node, nnode, on))
    {
      rem = ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                               lsa->header->adv_router, on->retrans_list);
      if (rem && ! ospf6_lsa_compare (rem, lsa))
        {
          if (IS_OSPF6_DEBUG_FLOODING ||
              IS_OSPF6_DEBUG_FLOOD_TYPE (lsa->header->type))
            zlog_debug ("Remove %s from retrans_list of %s",
                       rem->name, on->name);
          ospf6_decrement_retrans_count (rem);
          ospf6_lsdb_remove (rem, on->retrans_list);
        }
      //remove stale LSA from neighbor update list
      update = ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                                  lsa->header->adv_router, on->lsupdate_list);
      if (update && ospf6_lsa_compare (update, lsa) == 0)
        {                       //update is a stale lsa
          if (IS_OSPF6_DEBUG_FLOODING)
            zlog_info ("Remove %s from neighbor lsupdate_list of %s",
                       update->name, on->name);
          ospf6_lsdb_remove (update, on->lsupdate_list);
        }
    }
}

static void
ospf6_flood_clear_area (struct ospf6_lsa *lsa, struct ospf6_area *oa)
{
  struct listnode *node, *nnode;
  struct ospf6_interface *oi;
  struct ospf6_lsa *update;

  for (ALL_LIST_ELEMENTS (oa->if_list, node, nnode, oi))
    {
      if (OSPF6_LSA_SCOPE (lsa->header->type) == OSPF6_SCOPE_LINKLOCAL &&
          oi != OSPF6_INTERFACE (lsa->lsdb->data))
        continue;

#if 0
      if (OSPF6_LSA_SCOPE (lsa->header->type) == OSPF6_SCOPE_AS &&
          ospf6_is_interface_virtual_link (oi))
        continue;
#endif/*0*/

      ospf6_flood_clear_interface (lsa, oi);
      //remove stale LSA from interface update list
      update = ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                                  lsa->header->adv_router, oi->lsupdate_list);
      if (update && ospf6_lsa_compare (update, lsa) == 0)
        {                       //update is a stale lsa
          if (IS_OSPF6_DEBUG_FLOODING)
            zlog_info ("Remove %s from interface lsupdate_list",
                       update->name);
          ospf6_lsdb_remove (update, oi->lsupdate_list);
        }
    }
}

static void
ospf6_flood_clear_process (struct ospf6_lsa *lsa, struct ospf6 *process)
{
  struct listnode *node, *nnode;
  struct ospf6_area *oa;

  for (ALL_LIST_ELEMENTS (process->area_list, node, nnode, oa))
    {
      if (OSPF6_LSA_SCOPE (lsa->header->type) == OSPF6_SCOPE_AREA &&
          oa != OSPF6_AREA (lsa->lsdb->data))
        continue;
      if (OSPF6_LSA_SCOPE (lsa->header->type) == OSPF6_SCOPE_LINKLOCAL &&
          oa != OSPF6_INTERFACE (lsa->lsdb->data)->area)
        continue;

      if (ntohs (lsa->header->type) == OSPF6_LSTYPE_AS_EXTERNAL &&
          IS_AREA_STUB (oa))
        continue;

      ospf6_flood_clear_area (lsa, oa);
    }
}

void
ospf6_flood_clear (struct ospf6_lsa *lsa)
{
  ospf6_backupwait_lsa_delete (lsa);
  ospf6_flood_clear_process (lsa, ospf6);
}


/* RFC2328 13.5 (Table 19): Sending link state acknowledgements. */
static void
ospf6_acknowledge_lsa_bdrouter (struct ospf6_lsa *lsa, int ismore_recent,
                                struct ospf6_neighbor *from)
{
  struct ospf6_interface *oi;
  int is_debug = 0;

  if (IS_OSPF6_DEBUG_FLOODING ||
      IS_OSPF6_DEBUG_FLOOD_TYPE (lsa->header->type))
    is_debug++;

  assert (from && from->ospf6_if);
  oi = from->ospf6_if;

  /* LSA has been flood back out receiving interface.
     No acknowledgement sent. */
  if (CHECK_FLAG (lsa->flag, OSPF6_LSA_FLOODBACK))
    {
      if (is_debug)
        zlog_debug ("No acknowledgement (BDR & FloodBack)");
      return;
    }

  /* LSA is more recent than database copy, but was not flooded
     back out receiving interface. Delayed acknowledgement sent
     if advertisement received from Designated Router,
     otherwide do nothing. */
  if (ismore_recent < 0)
    {
      if (oi->drouter == from->router_id)
        {
          if (is_debug)
            zlog_debug ("Delayed acknowledgement (BDR & MoreRecent & from DR)");
          /* Delayed acknowledgement */
          ospf6_lsdb_add (ospf6_lsa_copy (lsa), oi->lsack_list);
          if (oi->thread_send_lsack == NULL)
            {
              // Remove "3" magic number -- send ACK after ackInterval
              if (oi->type == OSPF6_IFTYPE_MDR)
		oi->thread_send_lsack =
                  thread_add_timer_msec (master, ospf6_lsack_send_interface,
                                         oi, oi->mdr.ackInterval);
              else
		oi->thread_send_lsack =
                  thread_add_timer (master, ospf6_lsack_send_interface, oi,
                                    3);
            }
        }
      else
        {
          if (is_debug)
            zlog_debug ("No acknowledgement (BDR & MoreRecent & ! from DR)");
        }
      return;
    }

  /* LSA is a duplicate, and was treated as an implied acknowledgement.
     Delayed acknowledgement sent if advertisement received from
     Designated Router, otherwise do nothing */
  if (CHECK_FLAG (lsa->flag, OSPF6_LSA_DUPLICATE) &&
      CHECK_FLAG (lsa->flag, OSPF6_LSA_IMPLIEDACK))
    {
      if (oi->drouter == from->router_id)
        {
          if (is_debug)
            zlog_debug ("Delayed acknowledgement (BDR & Duplicate & ImpliedAck & from DR)");
          /* Delayed acknowledgement */
          ospf6_lsdb_add (ospf6_lsa_copy (lsa), oi->lsack_list);
          if (oi->thread_send_lsack == NULL)
            {
              // Remove "3" magic number -- send ACK after ackInterval
              if (oi->type == OSPF6_IFTYPE_MDR)
		oi->thread_send_lsack =
                  thread_add_timer_msec (master, ospf6_lsack_send_interface,
                                         oi, oi->mdr.ackInterval);
              else
		oi->thread_send_lsack =
                  thread_add_timer (master, ospf6_lsack_send_interface, oi,
                                    3);
            }
        }
      else
        {
          if (is_debug)
            zlog_debug ("No acknowledgement (BDR & Duplicate & ImpliedAck & ! from DR)");
        }
      return;
    }

  /* LSA is a duplicate, and was not treated as an implied acknowledgement.
     Direct acknowledgement sent */
  if (CHECK_FLAG (lsa->flag, OSPF6_LSA_DUPLICATE) &&
      ! CHECK_FLAG (lsa->flag, OSPF6_LSA_IMPLIEDACK))
    {
      if (is_debug)
        zlog_debug ("Direct acknowledgement (BDR & Duplicate)");
      // This is implementing multicast ACK
      /// Delay by ackInterval for coalescing ACKs
      if (oi->type == OSPF6_IFTYPE_MDR)
        {
          ospf6_lsdb_add (ospf6_lsa_copy (lsa), oi->lsack_list);
          if (oi->thread_send_lsack == NULL)
            oi->thread_send_lsack =
              thread_add_timer_msec (master, ospf6_lsack_send_interface,
                                     oi, oi->mdr.ackInterval);
        }
      else
        {
	  ospf6_lsdb_add (ospf6_lsa_copy (lsa), from->lsack_list);
	  if (from->thread_send_lsack == NULL)
	    from->thread_send_lsack =
	      thread_add_event (master, ospf6_lsack_send_neighbor, from, 0);
        }
      return;
    }

  /* LSA's LS age is equal to Maxage, and there is no current instance
     of the LSA in the link state database, and none of router's
     neighbors are in states Exchange or Loading */
  /* Direct acknowledgement sent, but this case is handled in
     early of ospf6_receive_lsa () */
}

static void
ospf6_acknowledge_lsa_allother (struct ospf6_lsa *lsa, int ismore_recent,
                                struct ospf6_neighbor *from,
                                struct in6_addr *dst)
{
  struct ospf6_interface *oi;
  int is_debug = 0;
  struct ospf6_lsa *lsa_ack;

  if (IS_OSPF6_DEBUG_FLOODING ||
      IS_OSPF6_DEBUG_FLOOD_TYPE (lsa->header->type))
    is_debug++;

  assert (from && from->ospf6_if);
  oi = from->ospf6_if;

  /* LSA has been flood back out receiving interface.
     No acknowledgement sent. */
  if (CHECK_FLAG (lsa->flag, OSPF6_LSA_FLOODBACK))
    {
      if (is_debug)
        zlog_debug ("No acknowledgement (AllOther & FloodBack)");
      return;
    }

  /* LSA is more recent than database copy, but was not flooded
     back out receiving interface. Delayed acknowledgement sent. */
  if (ismore_recent < 0)
    {
      if (is_debug)
        zlog_debug ("Delayed acknowledgement (AllOther & MoreRecent)");
      /* Delayed acknowledgement */
      lsa_ack = ospf6_lsa_copy (lsa);
      quagga_gettime (QUAGGA_CLK_MONOTONIC, &lsa_ack->rxmt_time);
      ospf6_lsdb_add (lsa_ack, oi->lsack_list);
      if (oi->thread_send_lsack == NULL)
        {
          // Remove "3" magic number -- send ACK after ackInterval
          if (oi->type == OSPF6_IFTYPE_MDR)
            oi->thread_send_lsack =
              thread_add_timer_msec (master, ospf6_lsack_send_interface,
                                     oi, oi->mdr.ackInterval);
          else
	    oi->thread_send_lsack =
	      thread_add_timer (master, ospf6_lsack_send_interface, oi, 3);
        }
      return;
    }

  if (oi->type == OSPF6_IFTYPE_MDR)
    {
      ospf6_mdr_acknowledge_lsa_allother (lsa, oi, dst);
      return;
    }

  /* LSA is a duplicate, and was treated as an implied acknowledgement.
     No acknowledgement sent. */
  if (CHECK_FLAG (lsa->flag, OSPF6_LSA_DUPLICATE) &&
      CHECK_FLAG (lsa->flag, OSPF6_LSA_IMPLIEDACK))
    {
      if (is_debug)
        zlog_debug ("No acknowledgement (AllOther & Duplicate & ImpliedAck)");
      return;
    }

  /* LSA is a duplicate, and was not treated as an implied acknowledgement.
     Direct acknowledgement sent */
  if (CHECK_FLAG (lsa->flag, OSPF6_LSA_DUPLICATE) &&
      ! CHECK_FLAG (lsa->flag, OSPF6_LSA_IMPLIEDACK))
    {
      if (is_debug)
        zlog_debug ("Direct acknowledgement (AllOther & Duplicate)");
      ospf6_lsdb_add (ospf6_lsa_copy (lsa), from->lsack_list);
      if (from->thread_send_lsack == NULL)
        from->thread_send_lsack =
          thread_add_event (master, ospf6_lsack_send_neighbor, from, 0);
      return;
    }

  /* LSA's LS age is equal to Maxage, and there is no current instance
     of the LSA in the link state database, and none of router's
     neighbors are in states Exchange or Loading */
  /* Direct acknowledgement sent, but this case is handled in
     early of ospf6_receive_lsa () */
}

static void
ospf6_acknowledge_lsa (struct ospf6_lsa *lsa, int ismore_recent,
                       struct ospf6_neighbor *from, struct in6_addr *dst)
{
  struct ospf6_interface *oi;

  assert (from && from->ospf6_if);
  oi = from->ospf6_if;

  if (oi->state == OSPF6_INTERFACE_BDR)
    ospf6_acknowledge_lsa_bdrouter (lsa, ismore_recent, from);
  else
    ospf6_acknowledge_lsa_allother (lsa, ismore_recent, from, dst);
}

/* RFC2328 section 13 (4):
   if MaxAge LSA and if we have no instance, and no neighbor
   is in states Exchange or Loading
   returns 1 if match this case, else returns 0 */
static int
ospf6_is_maxage_lsa_drop (struct ospf6_lsa *lsa, struct ospf6_neighbor *from)
{
  struct ospf6_neighbor *on;
  struct ospf6_interface *oi;
  struct ospf6_area *oa;
  struct ospf6 *process = NULL;
  struct listnode *i, *j, *k;
  int count = 0;

  if (! OSPF6_LSA_IS_MAXAGE (lsa))
    return 0;

  if (ospf6_lsdb_lookup (lsa->header->type, lsa->header->id,
                         lsa->header->adv_router, lsa->lsdb))
    return 0;

  process = from->ospf6_if->area->ospf6;

  for (ALL_LIST_ELEMENTS_RO (process->area_list, i, oa))
    for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, on))
        if (on->state == OSPF6_NEIGHBOR_EXCHANGE ||
            on->state == OSPF6_NEIGHBOR_LOADING)
          count++;

  if (count == 0)
    return 1;
  return 0;
}

/* RFC2328 section 13 The Flooding Procedure */
void
ospf6_receive_lsa (struct ospf6_lsa_header *lsa_header,
                   struct ospf6_neighbor *from, struct in6_addr *dst)
{
  struct ospf6_lsa *new = NULL, *old = NULL, *rem = NULL;
  int ismore_recent;
  unsigned short cksum, cksum1;
  int is_debug = 0;

  ismore_recent = 1;
  assert (from);

  /* make lsa structure for received lsa */
  new = ospf6_lsa_create (lsa_header);

  if (IS_OSPF6_DEBUG_FLOODING ||
      IS_OSPF6_DEBUG_FLOOD_TYPE (new->header->type))
    {
      is_debug++;
      zlog_debug ("LSA Receive from %s", from->name);
      ospf6_lsa_header_print (new);
    }

  /* (1) LSA Checksum */
  cksum = ntohs (new->header->checksum);
  cksum1 = ntohs (ospf6_lsa_checksum (new->header));
  if (cksum1 != cksum)
    {
      if (is_debug)
        zlog_debug ("Wrong LSA Checksum, discard header %x compute %x",
          cksum, cksum1);
      ospf6_lsa_delete (new);
      return;
    }

  /* (2) Examine the LSA's LS type. 
     RFC2470 3.5.1. Receiving Link State Update packets  */
  if (IS_AREA_STUB (from->ospf6_if->area) &&
      OSPF6_LSA_SCOPE (new->header->type) == OSPF6_SCOPE_AS)
    {
      if (is_debug)
        zlog_debug ("AS-External-LSA (or AS-scope LSA) in stub area, discard");
      ospf6_lsa_delete (new);
      return;
    }

  /* (3) LSA which have reserved scope is discarded
     RFC2470 3.5.1. Receiving Link State Update packets  */
  /* Flooding scope check. LSAs with unknown scope are discarded here.
     Set appropriate LSDB for the LSA */
  switch (OSPF6_LSA_SCOPE (new->header->type))
    {
    case OSPF6_SCOPE_LINKLOCAL:
      new->lsdb = from->ospf6_if->lsdb;
      break;
    case OSPF6_SCOPE_AREA:
      new->lsdb = from->ospf6_if->area->lsdb;
      break;
    case OSPF6_SCOPE_AS:
      new->lsdb = from->ospf6_if->area->ospf6->lsdb;
      break;
    default:
      if (is_debug)
        zlog_debug ("LSA has reserved scope, discard");
      ospf6_lsa_delete (new);
      return;
    }

  /* If LSA was received as multicast, flag it (for later flooding decisions) */
  if (IN6_IS_ADDR_MULTICAST (dst))
    SET_FLAG (new->flag, OSPF6_LSA_RECVMCAST);

  /* (4) if MaxAge LSA and if we have no instance, and no neighbor
         is in states Exchange or Loading */
  if (ospf6_is_maxage_lsa_drop (new, from))
    {
      /* log */
      if (is_debug)
        zlog_debug ("Drop MaxAge LSA with direct acknowledgement.");

      /* a) Acknowledge back to neighbor (Direct acknowledgement, 13.5) */
      if (from->ospf6_if->type == OSPF6_IFTYPE_MDR)
        {
          ospf6_lsdb_add (ospf6_lsa_copy (new), from->ospf6_if->lsack_list);
          if (from->ospf6_if->thread_send_lsack == NULL)
            from->ospf6_if->thread_send_lsack =
              thread_add_timer_msec (master,
                                     ospf6_lsack_send_interface,
                                     from->ospf6_if,
                                     from->ospf6_if->mdr.ackInterval);
        }
      else
        {
	  ospf6_lsdb_add (ospf6_lsa_copy (new), from->lsack_list);
	  if (from->thread_send_lsack == NULL)
	    from->thread_send_lsack =
	      thread_add_event (master, ospf6_lsack_send_neighbor, from, 0);
        }

      /* b) Discard */
      ospf6_lsa_delete (new);
      return;
    }

  /* (5) */
  /* lookup the same database copy in lsdb */
  old = ospf6_lsdb_lookup (new->header->type, new->header->id,
                           new->header->adv_router, new->lsdb);
  if (old)
    {
      ismore_recent = ospf6_lsa_compare (new, old);
      if (ntohl (new->header->seqnum) == ntohl (old->header->seqnum))
        {
          if (is_debug)
            zlog_debug ("Received is duplicated LSA");
          SET_FLAG (new->flag, OSPF6_LSA_DUPLICATE);
        }
    }

  if (from->ospf6_if->type == OSPF6_IFTYPE_MDR)
    ospf6_mdr_neighbor_store_ack (from, new);

  /* if no database copy or received is more recent */
  if (old == NULL || ismore_recent < 0)
    {
      /* in case we have no database copy */
      ismore_recent = -1;

      /* (a) MinLSArrival check */
      if (old && from->state == OSPF6_NEIGHBOR_FULL)
        {
          struct timeval now, res;
	  bool check_minlsarrival = true;

          quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);

	  /* don't check MinLSArrival for LSAs from a neighbor that
	     recently became full; this allows processing router-LSA
	     updates originated just after forming a new adjacency
	     that advertise the new neighbor */
	  if (old->header->adv_router == from->router_id)
	    {
	      timersub (&now, &from->last_changed, &res);
	      assert (res.tv_sec >= 0);
	      if (res.tv_sec < from->ospf6_if->area->ospf6->min_lsa_arrival)
		check_minlsarrival = false;
	    }

	  if (check_minlsarrival)
	    {
	      timersub (&now, &old->installed, &res);
	      assert (res.tv_sec >= 0);
	      if (res.tv_sec < from->ospf6_if->area->ospf6->min_lsa_arrival)
		{
		  if (is_debug)
		    zlog_debug ("LSA can't be updated within MinLSArrival, discard");
		  ospf6_lsa_delete (new);
		  return;   /* examin next lsa */
		}
	    }
        }

      quagga_gettime (QUAGGA_CLK_MONOTONIC, &new->received);

      if (is_debug)
        zlog_debug ("Flood, Install, Possibly acknowledge the received LSA");

      /* (b) immediately flood and (c) remove from all retrans-list */
      /* Prevent self-originated LSA to be flooded. this is to make
      reoriginated instance of the LSA not to be rejected by other routers
      due to MinLSArrival. */
      if (new->header->adv_router != from->ospf6_if->area->ospf6->router_id)
        ospf6_flood (from, new);

      /* (c) Remove the current database copy from all neighbors' Link
             state retransmission lists. */
      /* XXX, flood_clear ? */

      /* (d), installing lsdb, which may cause routing
              table calculation (replacing database copy) */
      ospf6_install_lsa (new);

      /* (e) possibly acknowledge */
      ospf6_acknowledge_lsa (new, ismore_recent, from, dst);

      /* (f) Self Originated LSA, section 13.4 */
      if (new->header->adv_router == from->ospf6_if->area->ospf6->router_id)
        {
          /* Self-originated LSA (newer than ours) is received from
             another router. We have to make a new instance of the LSA
             or have to flush this LSA. */
          if (is_debug)
            {
              zlog_debug ("Newer instance of the self-originated LSA");
              zlog_debug ("Schedule reorigination");
            }
          new->refresh = thread_add_event (master, ospf6_lsa_refresh, new, 0);
        }

      return;
    }

  /* (6) if there is instance on sending neighbor's request list */
  if (ospf6_lsdb_lookup (new->header->type, new->header->id,
                         new->header->adv_router, from->request_list))
    {
      /* if no database copy, should go above state (5) */
      assert (old);

      if (is_debug)
        {
          zlog_debug ("Received is not newer, on the neighbor's request-list");
          zlog_debug ("BadLSReq, discard the received LSA");
        }

      /* BadLSReq */
      thread_add_event (master, bad_lsreq, from, 0);

      ospf6_lsa_delete (new);
      return;
    }

  /* (7) if neither one is more recent */
  if (ismore_recent == 0)
    {
      if (is_debug)
        zlog_debug ("The same instance as database copy (neither recent)");

      if (from->ospf6_if->type == OSPF6_IFTYPE_MDR &&
          old->backupWaitTimer && ospf6_lsa_compare (new, old) == 0)
        {
          struct listnode *node, *nnode;
          u_int32_t *id;
          struct ospf6_neighbor *neigh;

          //remove sender from backupwait list
          ospf6_backupwait_lsa_neighbor_delete (old, from);
          //loop over neighbor neighbors
          //RGO-- enforce that LSA was received as multicast-- otherwise,
          //      cannot assume that sender's neighbors received
          if (IN6_IS_ADDR_MULTICAST (dst))
            {
              for (ALL_LIST_ELEMENTS (from->mdr.rnl, node, nnode, id))
                {
                  if (!old->backupWaitTimer)
                    break;
                  if (*id == from->ospf6_if->area->ospf6->router_id)
                    continue;
                  neigh = ospf6_neighbor_lookup (*id, from->ospf6_if);
                  //remove sender's neighbors from backupwait list
                  if (neigh)
                    ospf6_backupwait_lsa_neighbor_delete (old, neigh);
                }
            }
        }

      /* (a) if on retrans-list, Treat this LSA as an Ack: Implied Ack */
      rem = ospf6_lsdb_lookup (new->header->type, new->header->id,
                               new->header->adv_router, from->retrans_list);
      if (rem)
        {
          if (is_debug)
            {
              zlog_debug ("It is on the neighbor's retrans-list.");
              zlog_debug ("Treat as an Implied acknowledgement");
            }
          SET_FLAG (new->flag, OSPF6_LSA_IMPLIEDACK);
          ospf6_decrement_retrans_count (rem);
          ospf6_lsdb_remove (rem, from->retrans_list);
        }

      if (is_debug)
        zlog_debug ("Possibly acknowledge and then discard");

      /* (b) possibly acknowledge */
      ospf6_acknowledge_lsa (new, ismore_recent, from, dst);

      ospf6_lsa_delete (new);
      return;
    }

  /* (8) previous database copy is more recent */
    {
      assert (old);

      /* If database copy is in 'Seqnumber Wrapping',
         simply discard the received LSA */
      if (OSPF6_LSA_IS_MAXAGE (old) &&
          old->header->seqnum == htonl (MAX_SEQUENCE_NUMBER))
        {
          if (is_debug)
            {
              zlog_debug ("The LSA is in Seqnumber Wrapping");
              zlog_debug ("MaxAge & MaxSeqNum, discard");
            }
          ospf6_lsa_delete (new);
          return;
        }

      // SICDS does not send LSA to non-adjacent neighbor here.
      if (from->ospf6_if->type == OSPF6_IFTYPE_MDR &&
	  from->state < OSPF6_NEIGHBOR_EXCHANGE)
	{
	  if (is_debug)
	    zlog_debug ("MDR does not send LSA to non-adjacent neighbor here.");
	  ospf6_lsa_delete (new);
	  return;
	}

      /* Otherwise, Send database copy of this LSA to this neighbor */
        {
          if (is_debug)
            {
              zlog_debug ("Database copy is more recent.");
              zlog_debug ("Send back directly and then discard");
            }

          /* XXX, MinLSArrival check !? RFC 2328 13 (8) */

	  // suppressing stale LSA responses
	  // when the LSA will be sent pushBack algorithm
	  if (old->backupWaitTimer)
	    {
	      ospf6_lsa_delete (new);
	      return;
	    }

          ospf6_lsdb_add (ospf6_lsa_copy (old), from->lsupdate_list);

	  from->thread_send_lsupdate =
	    ospf6_send_lsupdate_delayed_msec (master,
					      ospf6_lsupdate_send_neighbor,
					      from,
					      from->ospf6_if->flood_delay,
					      from->thread_send_lsupdate);
          ospf6_lsa_delete (new);
          return;
        }
      return;
    }
}


DEFUN (debug_ospf6_flooding,
       debug_ospf6_flooding_cmd,
       "debug ospf6 flooding",
       DEBUG_STR
       OSPF6_STR
       "Debug OSPFv3 flooding function\n"
      )
{
  OSPF6_DEBUG_FLOODING_ON ();
  return CMD_SUCCESS;
}

DEFUN (no_debug_ospf6_flooding,
       no_debug_ospf6_flooding_cmd,
       "no debug ospf6 flooding",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Debug OSPFv3 flooding function\n"
      )
{
  OSPF6_DEBUG_FLOODING_OFF ();
  return CMD_SUCCESS;
}

int
config_write_ospf6_debug_flood (struct vty *vty)
{
  if (IS_OSPF6_DEBUG_FLOODING)
    vty_out (vty, "debug ospf6 flooding%s", VNL);
  return 0;
}

void
install_element_ospf6_debug_flood (void)
{
  install_element (ENABLE_NODE, &debug_ospf6_flooding_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_flooding_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_flooding_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_flooding_cmd);
}





