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
#include "memory.h"
#include "thread.h"
#include "linklist.h"
#include "vty.h"
#include "command.h"

#include "ospf6_proto.h"
#include "ospf6_lsa.h"
#include "ospf6_lsdb.h"
#include "ospf6_message.h"
#include "ospf6_top.h"
#include "ospf6_area.h"
#include "ospf6_interface.h"
#include "ospf6_neighbor.h"
#include "ospf6_intra.h"
#include "ospf6_flood.h"
#include "ospf6d.h"
#include "ospf6_route.h"
#include "ospf6_spf.h"
#include "ospf6_af.h"
#include "ospf6d.h"
#include "ospf6_mdr_neighbor.h"
#include "ospf6_private_data.h"

#define OSPF6_INACTIVITY_TIMER_MARGIN_MSEC 100

struct ospf6_interface_neighbor {
  struct list *neighbor_operations_list;
};

static unsigned int neighbor_data_id;

unsigned char conf_debug_ospf6_neighbor = 0;

const char *ospf6_neighbor_state_str[] =
{ "None", "Down", "Attempt", "Init", "Twoway", "ExStart", "ExChange",
  "Loading", "Full", NULL };

int
ospf6_neighbor_cmp (void *va, void *vb)
{
  struct ospf6_neighbor *ona = (struct ospf6_neighbor *) va;
  struct ospf6_neighbor *onb = (struct ospf6_neighbor *) vb;
  return (ntohl (ona->router_id) < ntohl (onb->router_id) ? -1 : 1);
}

struct ospf6_neighbor *
ospf6_neighbor_lookup (u_int32_t router_id,
                       struct ospf6_interface *oi)
{
  struct listnode *n;
  struct ospf6_neighbor *on;

  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, n, on))
    if (on->router_id == router_id)
      return on;
  
  return (struct ospf6_neighbor *) NULL;
}

/* create ospf6_neighbor */
struct ospf6_neighbor *
ospf6_neighbor_create (u_int32_t router_id, struct ospf6_interface *oi)
{
  struct ospf6_neighbor *on;
  char buf[16];
  struct ospf6_interface_neighbor *ifn;
  struct listnode *node;
  struct ospf6_neighbor_operations *ops;

  on = (struct ospf6_neighbor *)
    XCALLOC (MTYPE_OSPF6_NEIGHBOR, sizeof (struct ospf6_neighbor));

  ospf6_id2str (router_id, buf, sizeof (buf));
  snprintf (on->name, sizeof (on->name), "%s%%%s",
            buf, oi->interface->name);
  on->ospf6_if = oi;
  on->state = OSPF6_NEIGHBOR_DOWN;
  quagga_gettime (QUAGGA_CLK_MONOTONIC, &on->last_changed);
  on->router_id = router_id;

  on->summary_list = ospf6_lsdb_create (on);
  on->request_list = ospf6_lsdb_create (on);
  on->retrans_list = ospf6_lsdb_create (on);

  on->dbdesc_list = ospf6_lsdb_create (on);
  on->lsreq_list = ospf6_lsdb_create (on);
  on->lsupdate_list = ospf6_lsdb_create (on);
  on->lsack_list = ospf6_lsdb_create (on);

  on->cost = oi->cost;

  ospf6_mdr_neighbor_create (on);

  on->private_data_list = ospf6_private_data_list ();

  ifn = ospf6_get_interface_data (oi, neighbor_data_id);
  assert (ifn);

  for (ALL_LIST_ELEMENTS_RO (ifn->neighbor_operations_list, node, ops))
    {
      int err;

      if (!ops->create)
	continue;

      err = ops->create (on);
      if (err)
	{
	  zlog_err ("%s: per neighbor create function %p failed "
		    "for neighbor %s", __func__, ops->create, on->name);
	  for (node = node->prev; node != NULL; node = node->prev)
	    {
	      ops = listgetdata (node);
	      if (ops && ops->delete)
		ops->delete (on);
	    }
	  ospf6_neighbor_delete (on);
	  return NULL;
	}
    }

  listnode_add_sort (oi->neighbor_list, on);
  return on;
}

void
ospf6_neighbor_delete (struct ospf6_neighbor *on)
{
  struct ospf6_lsa *lsa;
  struct ospf6_interface_neighbor *ifn;
  struct listnode *node;

  ospf6_neighbor_state_change (OSPF6_NEIGHBOR_DOWN, on);

  ospf6_lsdb_remove_all (on->summary_list);
  ospf6_lsdb_remove_all (on->request_list);
  for (lsa = ospf6_lsdb_head (on->retrans_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    {
      ospf6_decrement_retrans_count (lsa);
      ospf6_lsdb_remove (lsa, on->retrans_list);
    }

  ospf6_lsdb_remove_all (on->dbdesc_list);
  ospf6_lsdb_remove_all (on->lsreq_list);
  ospf6_lsdb_remove_all (on->lsupdate_list);
  ospf6_lsdb_remove_all (on->lsack_list);

  ospf6_lsdb_delete (on->summary_list);
  ospf6_lsdb_delete (on->request_list);
  ospf6_lsdb_delete (on->retrans_list);

  ospf6_lsdb_delete (on->dbdesc_list);
  ospf6_lsdb_delete (on->lsreq_list);
  ospf6_lsdb_delete (on->lsupdate_list);
  ospf6_lsdb_delete (on->lsack_list);

  ospf6_mdr_neighbor_delete (on);

  THREAD_OFF (on->inactivity_timer);

  THREAD_OFF (on->thread_send_dbdesc);
  THREAD_OFF (on->thread_send_lsreq);
  THREAD_OFF (on->thread_send_lsupdate);
  THREAD_OFF (on->thread_send_lsack);

  THREAD_OFF (on->thread_adjok);

  ifn = ospf6_get_interface_data (on->ospf6_if, neighbor_data_id);
  assert (ifn);

  for (node = listtail (ifn->neighbor_operations_list);
       node != NULL; node = node->prev)
    {
      struct ospf6_neighbor_operations *ops;

      ops = listgetdata (node);
      if (ops && ops->delete)
	ops->delete (on);
    }

  list_delete (on->private_data_list);

  XFREE (MTYPE_OSPF6_NEIGHBOR, on);
}

int
ospf6_add_neighbor_data (struct ospf6_neighbor *on,
			 unsigned int *id, void *data)
{
  return ospf6_add_private_data (on->private_data_list, id, data);
}

void *
ospf6_get_neighbor_data (struct ospf6_neighbor *on, unsigned int id)
{
  return ospf6_get_private_data (on->private_data_list, id);
}

void *
ospf6_del_neighbor_data (struct ospf6_neighbor *on, unsigned int id)
{
  return ospf6_del_private_data (on->private_data_list, id);
}

int
ospf6_register_neighbor_operations (struct ospf6_interface *oi,
				    struct ospf6_neighbor_operations *ops)
{
  struct ospf6_interface_neighbor *ifn;
  struct listnode *node;
  struct ospf6_neighbor_operations *tmpops;

  ifn = ospf6_get_interface_data (oi, neighbor_data_id);
  assert (ifn);

  for (ALL_LIST_ELEMENTS_RO (ifn->neighbor_operations_list, node, tmpops))
    if (tmpops == ops)
      {
	zlog_err ("%s: per neighbor operations already registered: %p",
		  __func__, ops);
	return -1;
      }

  listnode_add (ifn->neighbor_operations_list, ops);

  if (ops->create)
    {
      struct listnode *node;
      struct ospf6_neighbor *on;

      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
	{
	  int err;

	  err = ops->create (on);
	  if (err)
	    zlog_warn ("%s: per neighbor create function %p failed "
		       "for neighbor %s", __func__, ops->create, on->name);
	}
    }

  return 0;
}

int
ospf6_remove_neighbor_operations (struct ospf6_interface *oi,
				  struct ospf6_neighbor_operations *ops)
{
  struct ospf6_interface_neighbor *ifn;
  struct listnode *node;
  struct ospf6_neighbor_operations *tmpops;

  if (neighbor_data_id == 0)
    return 0;

  ifn = ospf6_get_interface_data (oi, neighbor_data_id);
  assert (ifn);

  for (ALL_LIST_ELEMENTS_RO (ifn->neighbor_operations_list, node, tmpops))
    if (tmpops == ops)
      break;

  if (node == NULL)
    {
      zlog_err ("%s: per neighbor operations not found: %p", __func__, ops);
      return -1;
    }

  list_delete_node (ifn->neighbor_operations_list, node);

  if (ops->remove)
    ops->remove (oi, ops);

  return 0;
}

/* A helper function that invokes all hello_recv callbacks registered
 * for the interface
 */
int
ospf6_neighbor_hello_recv (struct ospf6_neighbor *on,
			   struct ospf6_header *oh,
			   struct ospf6_lls_header *lls)
{
  struct ospf6_interface_neighbor *ifn;
  struct listnode *node;
  struct ospf6_neighbor_operations *ops;

  ifn = ospf6_get_interface_data (on->ospf6_if, neighbor_data_id);
  assert (ifn);

  for (ALL_LIST_ELEMENTS_RO (ifn->neighbor_operations_list, node, ops))
    {
      int err;

      if (!ops->hello_recv)
	continue;

      err = ops->hello_recv (on, oh, lls);
      if (err)
	return err;
    }

  return 0;
}

static unsigned int
ospf6_interface_adjacency_formation_count (struct ospf6_interface *oi)
{
  unsigned int count;
  struct listnode *node;
  struct ospf6_neighbor *on;

  count = 0;
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
    {
      if (on->state > OSPF6_NEIGHBOR_TWOWAY &&
          on->state < OSPF6_NEIGHBOR_FULL)
        count++;
    }

  return count;
}

void
ospf6_neighbor_state_change (u_char next_state, struct ospf6_neighbor *on)
{
  u_char prev_state;
  struct ospf6_interface *oi = on->ospf6_if;
  struct ospf6_interface_neighbor *ifn;
  struct listnode *node;
  struct ospf6_neighbor_operations *ops;

  prev_state = on->state;
  on->state = next_state;

  if (prev_state == next_state)
    return;

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &on->last_changed);

  if (oi->allow_immediate_hello)
    {
      /* reset the immediate hello delay if immediate hellos are
       * active and the neighbor state is increasing (assume the
       * immediate hellos are effective)
       */
      if (oi->immediate_hello_delay > 0 &&
	  prev_state < OSPF6_NEIGHBOR_TWOWAY && next_state > prev_state)
	oi->immediate_hello_delay = 0;
    }

  /* log */
  if (IS_OSPF6_DEBUG_NEIGHBOR (STATE))
    {
      zlog_debug ("Neighbor state change %s: [%s]->[%s]", on->name,
		  ospf6_neighbor_state_str[prev_state],
		  ospf6_neighbor_state_str[next_state]);
    }

  if (oi->type == OSPF6_IFTYPE_MDR)
    {
      ospf6_mdr_neighbor_state_change (on, prev_state, next_state);
    }
  else
  if (prev_state == OSPF6_NEIGHBOR_FULL || next_state == OSPF6_NEIGHBOR_FULL)
    {
      ospf6_router_lsa_schedule (oi->area);
      if (oi->state == OSPF6_INTERFACE_DR)
        {
          ospf6_network_lsa_schedule (oi);
          ospf6_intra_prefix_lsa_schedule_transit (oi);
        }
      ospf6_intra_prefix_lsa_schedule_stub (oi->area);
    }

  if ((prev_state == OSPF6_NEIGHBOR_EXCHANGE ||
       prev_state == OSPF6_NEIGHBOR_LOADING) &&
      (next_state != OSPF6_NEIGHBOR_EXCHANGE &&
       next_state != OSPF6_NEIGHBOR_LOADING))
    ospf6_maxage_remove (oi->area->ospf6);

  ifn = ospf6_get_interface_data (oi, neighbor_data_id);
  assert (ifn);

  for (ALL_LIST_ELEMENTS_RO (ifn->neighbor_operations_list, node, ops))
    {
      if (!ops->state_change)
	continue;
      ops->state_change (on, prev_state);
    }

  if (oi->adjacency_formation_limit > 0 &&
      next_state == OSPF6_NEIGHBOR_FULL &&
      ospf6_interface_adjacency_formation_count (oi) <
      oi->adjacency_formation_limit)
    {
      struct ospf6_neighbor *on2;
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on2))
        {
          if (on2->state == OSPF6_NEIGHBOR_TWOWAY && need_adjacency (on2))
            {
              ospf6_neighbor_exstart (on2);
              break;
            }
        }
    }
}

/* RFC2328 section 10.4 */
int
need_adjacency (struct ospf6_neighbor *on)
{
 if (on->ospf6_if->type == OSPF6_IFTYPE_MDR)
   return ospf6_mdr_neighbor_need_adjacency (on);

  if (on->ospf6_if->state == OSPF6_INTERFACE_POINTTOPOINT ||
      on->ospf6_if->state == OSPF6_INTERFACE_DR ||
      on->ospf6_if->state == OSPF6_INTERFACE_BDR)
    return 1;

  if (on->ospf6_if->drouter == on->router_id ||
      on->ospf6_if->bdrouter == on->router_id)
    return 1;

  return 0;
}

void
ospf6_neighbor_schedule_inactivity (struct ospf6_neighbor *on)
{
  unsigned int msec;

  msec =
    1000 * on->ospf6_if->dead_interval + OSPF6_INACTIVITY_TIMER_MARGIN_MSEC;

  THREAD_OFF (on->inactivity_timer);
  on->inactivity_timer =
    thread_add_timer_msec (master, inactivity_timer, on, msec);
}

int
hello_received (struct thread *thread)
{
  struct ospf6_neighbor *on;

  on = (struct ospf6_neighbor *) THREAD_ARG (thread);
  assert (on);

  if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    zlog_debug ("Neighbor Event %s: *HelloReceived*", on->name);

  /* reset Inactivity Timer */
  ospf6_neighbor_schedule_inactivity (on);

  if (on->ospf6_if->allow_immediate_hello && on->state < OSPF6_NEIGHBOR_TWOWAY)
    ospf6_schedule_immediate_hello (on->ospf6_if);

  if (on->state <= OSPF6_NEIGHBOR_DOWN &&
      (on->ospf6_if->type != OSPF6_IFTYPE_MDR ||
       on->mdr.consec_hellos >= on->ospf6_if->mdr.consec_hello_threshold))
    ospf6_neighbor_state_change (OSPF6_NEIGHBOR_INIT, on);

  return 0;
}

int
twoway_received (struct thread *thread)
{
  struct ospf6_neighbor *on;

  on = (struct ospf6_neighbor *) THREAD_ARG (thread);
  assert (on);

  if (on->state > OSPF6_NEIGHBOR_INIT)
    return 0;

  if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    zlog_debug ("Neighbor Event %s: *2Way-Received*", on->name);

  thread_add_event (master, neighbor_change, on->ospf6_if, 0);

  if (on->ospf6_if->type == OSPF6_IFTYPE_MDR)
    {
      // must be run before calculate CDS, so neighbors are in correct state
      // RGO.  Require state to be INIT before changing to TWOWAY.
      // This is necessary if multiple consecutive Hellos are required
      // for changing state from DOWN to INIT in hello_received().
      // ospf6_neighbor_state_change (OSPF6_NEIGHBOR_TWOWAY, on);
      if (on->state == OSPF6_NEIGHBOR_INIT)     // For consecutive_hellos
        ospf6_neighbor_state_change (OSPF6_NEIGHBOR_TWOWAY, on);
      return 0;
    }

  if (! need_adjacency (on))
    {
      ospf6_neighbor_state_change (OSPF6_NEIGHBOR_TWOWAY, on);
      return 0;
    }

  ospf6_neighbor_exstart (on);

  return 0;
}

static void
__ospf6_neighbor_exstart (struct ospf6_neighbor *on, u_int32_t dbdesc_seqnum)
{
  ospf6_neighbor_state_change (OSPF6_NEIGHBOR_EXSTART, on);
  SET_FLAG (on->dbdesc_bits, OSPF6_DBDESC_MSBIT);
  SET_FLAG (on->dbdesc_bits, OSPF6_DBDESC_MBIT);
  SET_FLAG (on->dbdesc_bits, OSPF6_DBDESC_IBIT);

  on->dbdesc_seqnum = dbdesc_seqnum;

  THREAD_OFF (on->thread_send_dbdesc);
  on->thread_send_dbdesc =
    thread_add_event (master, ospf6_dbdesc_send, on, 0);
}

void
ospf6_neighbor_exstart (struct ospf6_neighbor *on)
{
  struct ospf6_interface *oi;
  struct timeval tv;

  oi = on->ospf6_if;
  if (oi->adjacency_formation_limit > 0)
    {
      unsigned int count;

      count = ospf6_interface_adjacency_formation_count (oi);
      if (count >= oi->adjacency_formation_limit)
        {
          if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
            zlog_debug ("Deferring ExStart for neighbor %s: "
                        "%u partial adjacencies for interface %s",
                        on->name, count, oi->interface->name);
          return;
        }
    }

  /* the initial sequence number for DbDesc */
  if (quagga_gettime (QUAGGA_CLK_MONOTONIC, &tv) < 0)
    tv.tv_sec = 1;

  __ospf6_neighbor_exstart (on, tv.tv_sec);
}

int
negotiation_done (struct thread *thread)
{
  struct ospf6_neighbor *on;
  struct ospf6_lsa *lsa;

  on = (struct ospf6_neighbor *) THREAD_ARG (thread);
  assert (on);

  if (on->state != OSPF6_NEIGHBOR_EXSTART)
    return 0;

  if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    zlog_debug ("Neighbor Event %s: *NegotiationDone*", on->name);

  /* clear ls-list */
  ospf6_lsdb_remove_all (on->summary_list);
  ospf6_lsdb_remove_all (on->request_list);
  for (lsa = ospf6_lsdb_head (on->retrans_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    {
      ospf6_decrement_retrans_count (lsa);
      ospf6_lsdb_remove (lsa, on->retrans_list);
    }

  /* Interface scoped LSAs */
  for (lsa = ospf6_lsdb_head (on->ospf6_if->lsdb); lsa;
       lsa = ospf6_lsdb_next (lsa))
    {
      if (OSPF6_LSA_IS_MAXAGE (lsa))
        {
          quagga_gettime (QUAGGA_CLK_MONOTONIC, &lsa->rxmt_time);
          ospf6_increment_retrans_count (lsa);
          ospf6_lsdb_add (ospf6_lsa_copy (lsa), on->retrans_list);
        }
      else
        ospf6_lsdb_add (ospf6_lsa_copy (lsa), on->summary_list);
    }

  /* Area scoped LSAs */
  for (lsa = ospf6_lsdb_head (on->ospf6_if->area->lsdb); lsa;
       lsa = ospf6_lsdb_next (lsa))
    {
      if (OSPF6_LSA_IS_MAXAGE (lsa))
        {
          quagga_gettime (QUAGGA_CLK_MONOTONIC, &lsa->rxmt_time);
          ospf6_increment_retrans_count (lsa);
          ospf6_lsdb_add (ospf6_lsa_copy (lsa), on->retrans_list);
        }
      else
        ospf6_lsdb_add (ospf6_lsa_copy (lsa), on->summary_list);
    }

  /* AS scoped LSAs */
  for (lsa = ospf6_lsdb_head (on->ospf6_if->area->ospf6->lsdb); lsa;
       lsa = ospf6_lsdb_next (lsa))
    {
      if (OSPF6_LSA_IS_MAXAGE (lsa))
        {
          quagga_gettime (QUAGGA_CLK_MONOTONIC, &lsa->rxmt_time);
          ospf6_increment_retrans_count (lsa);
          ospf6_lsdb_add (ospf6_lsa_copy (lsa), on->retrans_list);
        }
      else
        ospf6_lsdb_add (ospf6_lsa_copy (lsa), on->summary_list);
    }

  UNSET_FLAG (on->dbdesc_bits, OSPF6_DBDESC_IBIT);
  ospf6_neighbor_state_change (OSPF6_NEIGHBOR_EXCHANGE, on);

  return 0;
}

int
exchange_done (struct thread *thread)
{
  struct ospf6_neighbor *on;

  on = (struct ospf6_neighbor *) THREAD_ARG (thread);
  assert (on);

  if (on->state != OSPF6_NEIGHBOR_EXCHANGE)
    return 0;

  if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    zlog_debug ("Neighbor Event %s: *ExchangeDone*", on->name);

  THREAD_OFF (on->thread_send_dbdesc);
  ospf6_lsdb_remove_all (on->dbdesc_list);

/* XXX
  thread_add_timer (master, ospf6_neighbor_last_dbdesc_release, on,
                    on->ospf6_if->dead_interval);
*/

  if (on->request_list->count == 0)
    ospf6_neighbor_state_change (OSPF6_NEIGHBOR_FULL, on);
  else
    ospf6_neighbor_state_change (OSPF6_NEIGHBOR_LOADING, on);

  return 0;
}

int
loading_done (struct thread *thread)
{
  struct ospf6_neighbor *on;

  on = (struct ospf6_neighbor *) THREAD_ARG (thread);
  assert (on);

  if (on->state != OSPF6_NEIGHBOR_LOADING)
    return 0;

  if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    zlog_debug ("Neighbor Event %s: *LoadingDone*", on->name);

  assert (on->request_list->count == 0);

  ospf6_neighbor_state_change (OSPF6_NEIGHBOR_FULL, on);

  return 0;
}

int
adj_ok (struct thread *thread)
{
  struct ospf6_neighbor *on;
  struct ospf6_lsa *lsa;

  on = (struct ospf6_neighbor *) THREAD_ARG (thread);
  assert (on);

  THREAD_OFF (on->thread_adjok);

  if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    zlog_debug ("Neighbor Event %s: *AdjOK?*", on->name);

  if (on->state == OSPF6_NEIGHBOR_TWOWAY && need_adjacency (on))
    {
      ospf6_neighbor_exstart (on);
    }
  else if (on->state >= OSPF6_NEIGHBOR_EXSTART &&
           ! need_adjacency (on))
    {
      ospf6_neighbor_state_change (OSPF6_NEIGHBOR_TWOWAY, on);
      ospf6_lsdb_remove_all (on->summary_list);
      ospf6_lsdb_remove_all (on->request_list);
      for (lsa = ospf6_lsdb_head (on->retrans_list); lsa;
           lsa = ospf6_lsdb_next (lsa))
        {
          ospf6_decrement_retrans_count (lsa);
          ospf6_lsdb_remove (lsa, on->retrans_list);
        }
    }

  return 0;
}

void
ospf6_neighbor_schedule_adjok (struct ospf6_neighbor *on)
{
  if (on->thread_adjok == NULL)
    on->thread_adjok = thread_add_event (master, adj_ok, on, 0);
}

int
seqnumber_mismatch (struct thread *thread)
{
  struct ospf6_neighbor *on;
  struct ospf6_lsa *lsa;

  on = (struct ospf6_neighbor *) THREAD_ARG (thread);
  assert (on);

  if (on->state < OSPF6_NEIGHBOR_EXCHANGE)
    return 0;

  if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    zlog_debug ("Neighbor Event %s: *SeqNumberMismatch*", on->name);

  ospf6_lsdb_remove_all (on->summary_list);
  ospf6_lsdb_remove_all (on->request_list);
  for (lsa = ospf6_lsdb_head (on->retrans_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    {
      ospf6_decrement_retrans_count (lsa);
      ospf6_lsdb_remove (lsa, on->retrans_list);
    }

  /* For event SeqNumberMismatch the DD sequence number is incremented */
  __ospf6_neighbor_exstart (on, on->dbdesc_seqnum + 1);

  return 0;
}

int
bad_lsreq (struct thread *thread)
{
  struct ospf6_neighbor *on;
  struct ospf6_lsa *lsa;

  on = (struct ospf6_neighbor *) THREAD_ARG (thread);
  assert (on);

  if (on->state < OSPF6_NEIGHBOR_EXCHANGE)
    return 0;

  if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    zlog_debug ("Neighbor Event %s: *BadLSReq*", on->name);

  ospf6_lsdb_remove_all (on->summary_list);
  ospf6_lsdb_remove_all (on->request_list);
  for (lsa = ospf6_lsdb_head (on->retrans_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    {
      ospf6_decrement_retrans_count (lsa);
      ospf6_lsdb_remove (lsa, on->retrans_list);
    }

  /* The action for event BadLSReq is the same as SeqNumberMismatch */
  __ospf6_neighbor_exstart (on, on->dbdesc_seqnum + 1);

  return 0;
}

int
oneway_received (struct thread *thread)
{
  struct ospf6_neighbor *on;
  struct ospf6_lsa *lsa;

  on = (struct ospf6_neighbor *) THREAD_ARG (thread);
  assert (on);

  if (on->state < OSPF6_NEIGHBOR_TWOWAY)
    return 0;

  if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    zlog_debug ("Neighbor Event %s: *1Way-Received*", on->name);

  ospf6_neighbor_state_change (OSPF6_NEIGHBOR_INIT, on);
  thread_add_event (master, neighbor_change, on->ospf6_if, 0);

  ospf6_lsdb_remove_all (on->summary_list);
  ospf6_lsdb_remove_all (on->request_list);
  for (lsa = ospf6_lsdb_head (on->retrans_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    {
      ospf6_decrement_retrans_count (lsa);
      ospf6_lsdb_remove (lsa, on->retrans_list);
    }

  THREAD_OFF (on->thread_send_dbdesc);
  THREAD_OFF (on->thread_send_lsreq);
  THREAD_OFF (on->thread_send_lsupdate);
  THREAD_OFF (on->thread_send_lsack);

  return 0;
}

int
inactivity_timer (struct thread *thread)
{
  struct ospf6_neighbor *on;

  on = (struct ospf6_neighbor *) THREAD_ARG (thread);
  assert (on);

  if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    zlog_debug ("Neighbor Event %s: *InactivityTimer*", on->name);

  on->inactivity_timer = NULL;
  on->drouter = on->prev_drouter = 0;
  on->bdrouter = on->prev_bdrouter = 0;

  ospf6_neighbor_state_change (OSPF6_NEIGHBOR_DOWN, on);
  thread_add_event (master, neighbor_change, on->ospf6_if, 0);

  listnode_delete (on->ospf6_if->neighbor_list, on);
  ospf6_neighbor_delete (on);

  return 0;
}



/* vty functions */
/* show neighbor structure */
static void
ospf6_neighbor_show (struct vty *vty, struct ospf6_neighbor *on)
{
  char router_id[16];
  char duration[16];
  struct timeval now, res;
  char nstate[16];
  char deadtime[16];
  long h, m, s;

  /* Router-ID (Name) */
  ospf6_id2str (on->router_id, router_id, sizeof (router_id));
#ifdef HAVE_GETNAMEINFO
  {
  }
#endif /*HAVE_GETNAMEINFO*/

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);

  /* Dead time */
  h = m = s = 0;
  if (on->inactivity_timer)
    {
      s = on->inactivity_timer->u.sands.tv_sec - recent_relative_time().tv_sec;
      h = s / 3600;
      s -= h * 3600;
      m = s / 60;
      s -= m * 60;
    }
  snprintf (deadtime, sizeof (deadtime), "%02ld:%02ld:%02ld", h, m, s);

  /* Neighbor State */
  if (on->ospf6_if->type == OSPF6_IFTYPE_POINTOMULTIPOINT ||
      on->ospf6_if->type == OSPF6_IFTYPE_MDR ||
      on->ospf6_if->type == OSPF6_IFTYPE_POINTOPOINT)
    snprintf (nstate, sizeof (nstate), "PointToPoint");
  else
    {
      if (on->router_id == on->drouter)
        snprintf (nstate, sizeof (nstate), "DR");
      else if (on->router_id == on->bdrouter)
        snprintf (nstate, sizeof (nstate), "BDR");
      else
        snprintf (nstate, sizeof (nstate), "DROther");
    }

  /* Duration */
  timersub (&now, &on->last_changed, &res);
  timerstring (&res, duration, sizeof (duration));

  /*
  vty_out (vty, "%-15s %3d %11s %6s/%-12s %11s %s[%s]%s",
           "Neighbor ID", "Pri", "DeadTime", "State", "", "Duration",
           "I/F", "State", VNL);
  */

  vty_out (vty, "%-15s %3d %11s %6s/%-12s %11s %s[%s]%s",
           router_id, on->priority, deadtime,
           ospf6_neighbor_state_str[on->state], nstate, duration,
           on->ospf6_if->interface->name,
           ospf6_interface_state_str[on->ospf6_if->state], VNL);
}

static void
ospf6_neighbor_show_drchoice (struct vty *vty, struct ospf6_neighbor *on)
{
  char router_id[16];
  char drouter[16], bdrouter[16];
  char duration[16];
  struct timeval now, res;

/*
    vty_out (vty, "%-15s %6s/%-11s %-15s %-15s %s[%s]%s",
             "RouterID", "State", "Duration", "DR", "BDR", "I/F",
             "State", VNL);
*/

  ospf6_id2str (on->router_id, router_id, sizeof (router_id));
  ospf6_id2str (on->drouter, drouter, sizeof (drouter));
  ospf6_id2str (on->bdrouter, bdrouter, sizeof (bdrouter));

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);
  timersub (&now, &on->last_changed, &res);
  timerstring (&res, duration, sizeof (duration));

  vty_out (vty, "%-15s %6s/%-11s %-15s %-15s %s[%s]%s",
           router_id, ospf6_neighbor_state_str[on->state],
           duration, drouter, bdrouter, on->ospf6_if->interface->name,
           ospf6_interface_state_str[on->ospf6_if->state],
           VNL);
}

static void
ospf6_neighbor_show_detail (struct vty *vty, struct ospf6_neighbor *on)
{
  char drouter[16], bdrouter[16];
  char linklocal_addr[64], duration[32];
  struct timeval now, res;
  struct ospf6_lsa *lsa;

  ospf6_addr2str6 (&on->linklocal_addr, linklocal_addr,
		   sizeof (linklocal_addr));
  ospf6_id2str (on->drouter, drouter, sizeof (drouter));
  ospf6_id2str (on->bdrouter, bdrouter, sizeof (bdrouter));

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);
  timersub (&now, &on->last_changed, &res);
  timerstring (&res, duration, sizeof (duration));

  vty_out (vty, " Neighbor %s%s", on->name,
           VNL);
  vty_out (vty, "    Area %s via interface %s (ifindex %d) metric %u%s",
           on->ospf6_if->area->name,
           on->ospf6_if->interface->name,
           on->ospf6_if->interface->ifindex,
	   on->cost,
           VNL);
  vty_out (vty, "    His IfIndex: %d Link-local address: %s%s",
           on->ifindex, linklocal_addr,
           VNL);
  vty_out (vty, "    State %s for a duration of %s%s",
           ospf6_neighbor_state_str[on->state], duration,
           VNL);
  vty_out (vty, "    His choice of DR/BDR %s/%s, Priority %d%s",
           drouter, bdrouter, on->priority,
           VNL);
  vty_out (vty, "    DbDesc status: %s%s%s SeqNum: %#lx%s",
           (CHECK_FLAG (on->dbdesc_bits, OSPF6_DBDESC_IBIT) ? "Initial " : ""),
           (CHECK_FLAG (on->dbdesc_bits, OSPF6_DBDESC_MBIT) ? "More " : ""),
           (CHECK_FLAG (on->dbdesc_bits, OSPF6_DBDESC_MSBIT) ?
            "Master" : "Slave"), (u_long) ntohl (on->dbdesc_seqnum),
           VNL);

  vty_out (vty, "    Summary-List: %d LSAs%s", on->summary_list->count,
           VNL);
  for (lsa = ospf6_lsdb_head (on->summary_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    vty_out (vty, "      %s%s", lsa->name, VNL);

  vty_out (vty, "    Request-List: %d LSAs%s", on->request_list->count,
           VNL);
  for (lsa = ospf6_lsdb_head (on->request_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    vty_out (vty, "      %s%s", lsa->name, VNL);

  vty_out (vty, "    Retrans-List: %d LSAs%s", on->retrans_list->count,
           VNL);
  for (lsa = ospf6_lsdb_head (on->retrans_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    vty_out (vty, "      %s%s", lsa->name, VNL);

  timerclear (&res);
  if (on->thread_send_dbdesc)
    timersub (&on->thread_send_dbdesc->u.sands, &now, &res);
  timerstring (&res, duration, sizeof (duration));
  vty_out (vty, "    %d Pending LSAs for DbDesc in Time %s [thread %s]%s",
           on->dbdesc_list->count, duration,
           (on->thread_send_dbdesc ? "on" : "off"),
           VNL);
  for (lsa = ospf6_lsdb_head (on->dbdesc_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    vty_out (vty, "      %s%s", lsa->name, VNL);

  timerclear (&res);
  if (on->thread_send_lsreq)
    timersub (&on->thread_send_lsreq->u.sands, &now, &res);
  timerstring (&res, duration, sizeof (duration));
  vty_out (vty, "    %d Pending LSAs for LSReq in Time %s [thread %s]%s",
           on->lsreq_list->count, duration,
           (on->thread_send_lsreq ? "on" : "off"),
           VNL);
  for (lsa = ospf6_lsdb_head (on->lsreq_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    vty_out (vty, "      %s%s", lsa->name, VNL);

  timerclear (&res);
  if (on->thread_send_lsupdate)
    timersub (&on->thread_send_lsupdate->u.sands, &now, &res);
  timerstring (&res, duration, sizeof (duration));
  vty_out (vty, "    %d Pending LSAs for LSUpdate in Time %s [thread %s]%s",
           on->lsupdate_list->count, duration,
           (on->thread_send_lsupdate ? "on" : "off"),
           VNL);
  for (lsa = ospf6_lsdb_head (on->lsupdate_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    vty_out (vty, "      %s%s", lsa->name, VNL);

  timerclear (&res);
  if (on->thread_send_lsack)
    timersub (&on->thread_send_lsack->u.sands, &now, &res);
  timerstring (&res, duration, sizeof (duration));
  vty_out (vty, "    %d Pending LSAs for LSAck in Time %s [thread %s]%s",
           on->lsack_list->count, duration,
           (on->thread_send_lsack ? "on" : "off"),
           VNL);
  for (lsa = ospf6_lsdb_head (on->lsack_list); lsa;
       lsa = ospf6_lsdb_next (lsa))
    vty_out (vty, "      %s%s", lsa->name, VNL);

}

DEFUN (show_ipv6_ospf6_neighbor,
       show_ipv6_ospf6_neighbor_cmd,
       "show ipv6 ospf6 neighbor",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Neighbor list\n"
      )
{
  struct ospf6_neighbor *on;
  struct ospf6_interface *oi;
  struct ospf6_area *oa;
  struct listnode *i, *j, *k;
  void (*showfunc) (struct vty *, struct ospf6_neighbor *);

  OSPF6_CMD_CHECK_RUNNING ();
  showfunc = ospf6_neighbor_show;

  if (argc)
    {
      if (! strncmp (argv[0], "de", 2))
        showfunc = ospf6_neighbor_show_detail;
      else if (! strncmp (argv[0], "dr", 2))
        showfunc = ospf6_neighbor_show_drchoice;
    }

  if (showfunc == ospf6_neighbor_show)
    vty_out (vty, "%-15s %3s %11s %6s/%-12s %11s %s[%s]%s",
             "Neighbor ID", "Pri", "DeadTime", "State", "IfState", "Duration",
             "I/F", "State", VNL);
  else if (showfunc == ospf6_neighbor_show_drchoice)
    vty_out (vty, "%-15s %6s/%-11s %-15s %-15s %s[%s]%s",
             "RouterID", "State", "Duration", "DR", "BDR", "I/F",
             "State", VNL);

  for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, i, oa))
    for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, on))
        (*showfunc) (vty, on);

  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_neighbor,
       show_ipv6_ospf6_neighbor_detail_cmd,
       "show ipv6 ospf6 neighbor (detail|drchoice)",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Neighbor list\n"
       "Display details\n"
       "Display DR choices\n"
      )

DEFUN (show_ipv6_ospf6_neighbor_one,
       show_ipv6_ospf6_neighbor_one_cmd,
       "show ipv6 ospf6 neighbor A.B.C.D",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Neighbor list\n"
       "Specify Router-ID as IPv4 address notation\n"
      )
{
  struct ospf6_neighbor *on;
  struct ospf6_interface *oi;
  struct ospf6_area *oa;
  struct listnode *i, *j, *k;
  void (*showfunc) (struct vty *, struct ospf6_neighbor *);
  u_int32_t router_id;

  OSPF6_CMD_CHECK_RUNNING ();
  showfunc = ospf6_neighbor_show_detail;

  if (ospf6_str2id (argv[0], &router_id))
    {
      vty_out (vty, "Router-ID is not parsable: %s%s", argv[0],
               VNL);
      return CMD_SUCCESS;
    }

  for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, i, oa))
    for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, on))
	if (on->router_id == router_id)
	  {
	    (*showfunc) (vty, on);
	    return CMD_SUCCESS;
	  }

  vty_out (vty, "Neighbor %s not found%s", argv[0], VNL);

  return CMD_SUCCESS;
}

DEFUN (show_ipv6_ospf6_neighbor_cost,
       show_ipv6_ospf6_neighbor_cost_cmd,
       "show ipv6 ospf6 neighbor-cost [A.B.C.D]",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Neighbor cost\n"
       "Optional router-id in dotted quad notation\n")
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
      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, on))
	{
	  if (argc && on->router_id != routerid)
	    continue;

	  vty_out (vty, "neighbor %s cost: %u%s", on->name, on->cost, VNL);
	  numnbr++;
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
ospf6_neighbor_init (void)
{
  install_element (VIEW_NODE, &show_ipv6_ospf6_neighbor_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_neighbor_detail_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_neighbor_one_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_neighbor_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_neighbor_detail_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_neighbor_one_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_neighbor_cost_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_neighbor_cost_cmd);

  ospf6_mdr_neighbor_init ();
}

DEFUN (debug_ospf6_neighbor,
       debug_ospf6_neighbor_cmd,
       "debug ospf6 neighbor",
       DEBUG_STR
       OSPF6_STR
       "Debug OSPFv3 Neighbor\n"
      )
{
  unsigned char level = 0;
  if (argc)
    {
      if (! strncmp (argv[0], "s", 1))
        level = OSPF6_DEBUG_NEIGHBOR_STATE;
      if (! strncmp (argv[0], "e", 1))
        level = OSPF6_DEBUG_NEIGHBOR_EVENT;
    }
  else
    level = OSPF6_DEBUG_NEIGHBOR_STATE | OSPF6_DEBUG_NEIGHBOR_EVENT;

  OSPF6_DEBUG_NEIGHBOR_ON (level);
  return CMD_SUCCESS;
}

ALIAS (debug_ospf6_neighbor,
       debug_ospf6_neighbor_detail_cmd,
       "debug ospf6 neighbor (state|event)",
       DEBUG_STR
       OSPF6_STR
       "Debug OSPFv3 Neighbor\n"
       "Debug OSPFv3 Neighbor State Change\n"
       "Debug OSPFv3 Neighbor Event\n"
      )

DEFUN (no_debug_ospf6_neighbor,
       no_debug_ospf6_neighbor_cmd,
       "no debug ospf6 neighbor",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Debug OSPFv3 Neighbor\n"
      )
{
  unsigned char level = 0;
  if (argc)
    {
      if (! strncmp (argv[0], "s", 1))
        level = OSPF6_DEBUG_NEIGHBOR_STATE;
      if (! strncmp (argv[0], "e", 1))
        level = OSPF6_DEBUG_NEIGHBOR_EVENT;
    }
  else
    level = OSPF6_DEBUG_NEIGHBOR_STATE | OSPF6_DEBUG_NEIGHBOR_EVENT;

  OSPF6_DEBUG_NEIGHBOR_OFF (level);
  return CMD_SUCCESS;
}

ALIAS (no_debug_ospf6_neighbor,
       no_debug_ospf6_neighbor_detail_cmd,
       "no debug ospf6 neighbor (state|event)",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Debug OSPFv3 Neighbor\n"
       "Debug OSPFv3 Neighbor State Change\n"
       "Debug OSPFv3 Neighbor Event\n"
      )

int
config_write_ospf6_debug_neighbor (struct vty *vty)
{
  if (IS_OSPF6_DEBUG_NEIGHBOR (STATE) &&
      IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    vty_out (vty, "debug ospf6 neighbor%s", VNL);
  else if (IS_OSPF6_DEBUG_NEIGHBOR (STATE))
    vty_out (vty, "debug ospf6 neighbor state%s", VNL);
  else if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    vty_out (vty, "debug ospf6 neighbor event%s", VNL);
  return 0;
}

void
install_element_ospf6_debug_neighbor (void)
{
  install_element (ENABLE_NODE, &debug_ospf6_neighbor_cmd);
  install_element (ENABLE_NODE, &debug_ospf6_neighbor_detail_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_neighbor_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_neighbor_detail_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_neighbor_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_neighbor_detail_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_neighbor_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_neighbor_detail_cmd);
}

static int
ospf6_interface_create_neighbor (struct ospf6_interface *oi)
{
  struct ospf6_interface_neighbor *ifn;
  int err;

  ifn = XCALLOC (MTYPE_OSPF6_IF, sizeof (*ifn));
  ifn->neighbor_operations_list = list_new ();

  err = ospf6_add_interface_data (oi, &neighbor_data_id, ifn);
  if (err)
    {
      list_delete (ifn->neighbor_operations_list);
      XFREE (MTYPE_OSPF6_IF, ifn);
      return err;
    }

  return 0;
}

static void
ospf6_interface_delete_neighbor (struct ospf6_interface *oi)
{
  struct ospf6_interface_neighbor *ifn;
  struct listnode *node, *nnode;
  struct ospf6_neighbor_operations *ops;

  ifn = ospf6_get_interface_data (oi, neighbor_data_id);
  if (ifn == NULL)
    return;

  for (ALL_LIST_ELEMENTS (ifn->neighbor_operations_list, node, nnode, ops))
    {
      int err;

      err = ospf6_remove_neighbor_operations (oi, ops);
      if (err)
	zlog_err ("%s: error removing neighbor operations %p", __func__, ops);
    }

  ospf6_del_interface_data (oi, neighbor_data_id);

  list_delete (ifn->neighbor_operations_list);
  XFREE (MTYPE_OSPF6_IF, ifn);
}

static struct ospf6_interface_operations neighbor_ifops = {
  .create = ospf6_interface_create_neighbor,
  .delete = ospf6_interface_delete_neighbor,
};

OSPF6_INTERFACE_OPERATIONS (neighbor_ifops);
