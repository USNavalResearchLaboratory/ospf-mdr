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
#include "linklist.h"
#include "thread.h"
#include "memory.h"
#include "if.h"
#include "prefix.h"
#include "table.h"
#include "vty.h"
#include "command.h"

#include "ospf6_proto.h"
#include "ospf6_message.h"
#include "ospf6_route.h"
#include "ospf6_lsa.h"
#include "ospf6_lsdb.h"

#include "ospf6_top.h"
#include "ospf6_area.h"
#include "ospf6_interface.h"
#include "ospf6_neighbor.h"
#include "ospf6_intra.h"
#include "ospf6_asbr.h"
#include "ospf6_abr.h"
#include "ospf6_flood.h"
#include "ospf6d.h"
#include "ospf6_mdr.h"
#include "ospf6_af.h"
#include "ospf6_flood.h"

#define LSA_SCHEDULE_DELAY_MSEC 100

unsigned char conf_debug_ospf6_brouter = 0;
u_int32_t conf_debug_ospf6_brouter_specific_router_id;
u_int32_t conf_debug_ospf6_brouter_specific_area_id;

/******************************/
/* RFC2740 3.4.3.1 Router-LSA */
/******************************/

static int
ospf6_router_lsa_show (struct vty *vty, struct ospf6_lsa *lsa)
{
  char *start, *end, *current;
  char buf[32], name[32], bits[16], options[32];
  struct ospf6_router_lsa *router_lsa;
  struct ospf6_router_lsdesc *lsdesc;

  router_lsa = (struct ospf6_router_lsa *)
    ((char *) lsa->header + sizeof (struct ospf6_lsa_header));

  ospf6_capability_printbuf (router_lsa->bits, bits, sizeof (bits));
  ospf6_options_printbuf (router_lsa->options, options, sizeof (options));
  vty_out (vty, "    Bits: %s Options: %s%s", bits, options, VNL);

  start = (char *) router_lsa + sizeof (struct ospf6_router_lsa);
  end = (char *) lsa->header + ntohs (lsa->header->length);
  for (current = start; current + sizeof (struct ospf6_router_lsdesc) <= end;
       current += sizeof (struct ospf6_router_lsdesc))
    {
      lsdesc = (struct ospf6_router_lsdesc *) current;

      if (lsdesc->type == OSPF6_ROUTER_LSDESC_POINTTOPOINT)
        snprintf (name, sizeof (name), "Point-To-Point");
      else if (lsdesc->type == OSPF6_ROUTER_LSDESC_TRANSIT_NETWORK)
        snprintf (name, sizeof (name), "Transit-Network");
      else if (lsdesc->type == OSPF6_ROUTER_LSDESC_STUB_NETWORK)
        snprintf (name, sizeof (name), "Stub-Network");
      else if (lsdesc->type == OSPF6_ROUTER_LSDESC_VIRTUAL_LINK)
        snprintf (name, sizeof (name), "Virtual-Link");
      else
        snprintf (name, sizeof (name), "Unknown (%#x)", lsdesc->type);

      vty_out (vty, "    Type: %s Metric: %d%s",
               name, ntohs (lsdesc->metric), VNL);
      vty_out (vty, "    Interface ID: %s%s",
               ospf6_id2str (lsdesc->interface_id,
			     buf, sizeof (buf)), VNL);
      vty_out (vty, "    Neighbor Interface ID: %s%s",
               ospf6_id2str (lsdesc->neighbor_interface_id,
			     buf, sizeof (buf)), VNL);
      vty_out (vty, "    Neighbor Router ID: %s%s",
               ospf6_id2str (lsdesc->neighbor_router_id,
			     buf, sizeof (buf)), VNL);
    }
  return 0;
}

static int
ospf6_router_lsa_originate (struct thread *thread)
{
  struct ospf6_area *oa;

  char buffer [OSPF6_MAX_LSASIZE];
  struct ospf6_lsa_header *lsa_header;
  struct ospf6_lsa *lsa;
  struct ospf6_lsa *old;
  u_int32_t link_state_id = 0;
  struct listnode *node, *nnode;
  struct listnode *j;
  struct ospf6_interface *oi;
  struct ospf6_neighbor *on, *drouter = NULL;
  struct ospf6_router_lsa *router_lsa;
  struct ospf6_router_lsdesc *lsdesc;
  u_int16_t type;
  u_int32_t router;
  int count;

  oa = (struct ospf6_area *) THREAD_ARG (thread);
  oa->thread_router_lsa = NULL;

  old = ospf6_lsdb_lookup (htons (OSPF6_LSTYPE_ROUTER), htonl (0),
                           oa->ospf6->router_id, oa->lsdb);
  if (old != NULL)
    {
      long delay_msec;

      delay_msec = 1000 * oa->ospf6->min_lsa_interval -
        elapsed_msec (&old->originated);
      if (delay_msec > 0)
        {
          if (IS_OSPF6_DEBUG_ORIGINATE (ROUTER))
            zlog_debug ("Delaying Router-LSA origination for area %s by %li "
                        "msec to satisfy MinLSInterval", oa->name, delay_msec);
          oa->thread_router_lsa =
            thread_add_timer_msec (master, ospf6_router_lsa_originate,
                                   oa, delay_msec);
          return 0;
        }
    }

  if (IS_OSPF6_DEBUG_ORIGINATE (ROUTER))
    zlog_debug ("Originate Router-LSA for Area %s", oa->name);

  memset (buffer, 0, sizeof (buffer));
  lsa_header = (struct ospf6_lsa_header *) buffer;
  router_lsa = (struct ospf6_router_lsa *)
    ((caddr_t) lsa_header + sizeof (struct ospf6_lsa_header));

  OSPF6_OPT_SET (router_lsa->options, OSPF6_OPT_V6, 2);
  OSPF6_OPT_SET (router_lsa->options, OSPF6_OPT_E, 2);
  OSPF6_OPT_CLEAR (router_lsa->options, OSPF6_OPT_MC, 2);
  OSPF6_OPT_CLEAR (router_lsa->options, OSPF6_OPT_N, 2);
  OSPF6_OPT_SET (router_lsa->options, OSPF6_OPT_R, 2);
  OSPF6_OPT_CLEAR (router_lsa->options, OSPF6_OPT_DC, 2);

  OSPF6_OPT_SET (router_lsa->options, OSPF6_OPT_AF, 1);
  OSPF6_OPT_CLEAR (router_lsa->options, OSPF6_OPT_L, 1);

  if (ospf6_is_router_abr (ospf6))
    SET_FLAG (router_lsa->bits, OSPF6_ROUTER_BIT_B);
  else
    UNSET_FLAG (router_lsa->bits, OSPF6_ROUTER_BIT_B);
  if (ospf6_asbr_is_asbr (ospf6))
    SET_FLAG (router_lsa->bits, OSPF6_ROUTER_BIT_E);
  else
    UNSET_FLAG (router_lsa->bits, OSPF6_ROUTER_BIT_E);
  UNSET_FLAG (router_lsa->bits, OSPF6_ROUTER_BIT_V);
  UNSET_FLAG (router_lsa->bits, OSPF6_ROUTER_BIT_W);

  /* describe links for each interfaces */
  lsdesc = (struct ospf6_router_lsdesc *)
    ((caddr_t) router_lsa + sizeof (struct ospf6_router_lsa));

  for (ALL_LIST_ELEMENTS (oa->if_list, node, nnode, oi))
    {
      /* Interfaces in state Down or Loopback are not described */
      if (oi->state == OSPF6_INTERFACE_DOWN ||
          oi->state == OSPF6_INTERFACE_LOOPBACK)
        continue;

      /* Nor are interfaces without any full adjacencies described */
      //MDR may include non-adjacent neighbors in LSA
      //except when fully connected adjacencies are used
      if (!(oi->type == OSPF6_IFTYPE_MDR &&
            oi->mdr.AdjConnectivity > OSPF6_ADJ_FULLYCONNECTED))
        {
          //If there are 0 neighbors in state FULL then go to next interface
	  count = 0;
	  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, on))
	    if (on->state == OSPF6_NEIGHBOR_FULL)
	      count++;
	  if (count == 0)
	    continue;
        }

      /* Multiple Router-LSA instance according to size limit setting */
      if ( (oa->router_lsa_size_limit != 0)
	   && ((unsigned int) ((caddr_t) lsdesc +
			       sizeof (struct ospf6_router_lsdesc) -
			       (caddr_t) buffer) > oa->router_lsa_size_limit))
        {
          if ((caddr_t) lsdesc == (caddr_t) router_lsa +
                                  sizeof (struct ospf6_router_lsa))
            {
              if (IS_OSPF6_DEBUG_ORIGINATE (ROUTER))
                zlog_debug ("Size limit setting for Router-LSA too short");
              return 0;
            }

          /* Fill LSA Header */
          lsa_header->age = 0;
          lsa_header->type = htons (OSPF6_LSTYPE_ROUTER);
          lsa_header->id = htonl (link_state_id);
          lsa_header->adv_router = oa->ospf6->router_id;
          lsa_header->seqnum =
            ospf6_new_ls_seqnum (lsa_header->type, lsa_header->id,
                                 lsa_header->adv_router, oa->lsdb);
          lsa_header->length = htons ((caddr_t) lsdesc - (caddr_t) buffer);

          /* LSA checksum */
          ospf6_lsa_checksum (lsa_header);

          /* create LSA */
          lsa = ospf6_lsa_create (lsa_header);

          /* Originate */
          ospf6_lsa_originate_area (lsa, oa);

          /* Reset setting for consecutive origination */
          memset ((caddr_t) router_lsa + sizeof (struct ospf6_router_lsa),
                  0, (caddr_t) lsdesc - (caddr_t) router_lsa);
          lsdesc = (struct ospf6_router_lsdesc *)
            ((caddr_t) router_lsa + sizeof (struct ospf6_router_lsa));
          link_state_id ++;
        }

      /* Point-to-Point interfaces */
      if (oi->type == OSPF6_IFTYPE_POINTOPOINT ||
          oi->type == OSPF6_IFTYPE_MDR ||
          oi->type == OSPF6_IFTYPE_POINTOMULTIPOINT)
        {
          for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, on))
            {
              if (oi->type == OSPF6_IFTYPE_MDR)
                {
                  if (!on->mdr.adv)
                    continue;
                }
              else if (on->state != OSPF6_NEIGHBOR_FULL)
		continue;

              lsdesc->type = OSPF6_ROUTER_LSDESC_POINTTOPOINT;
              lsdesc->metric = htons (on->cost);
              lsdesc->interface_id = htonl (oi->interface->ifindex);
              lsdesc->neighbor_interface_id = htonl (on->ifindex);
              lsdesc->neighbor_router_id = on->router_id;

              lsdesc++;
            }
        }

      /* Broadcast and NBMA interfaces */
      if (oi->type == OSPF6_IFTYPE_BROADCAST || oi->type == OSPF6_IFTYPE_NBMA)
        {
          /* If this router is not DR,
             and If this router not fully adjacent with DR,
             this interface is not transit yet: ignore. */
          if (oi->state != OSPF6_INTERFACE_DR)
            {
              drouter = ospf6_neighbor_lookup (oi->drouter, oi);
              if (drouter == NULL || drouter->state != OSPF6_NEIGHBOR_FULL)
                continue;
            }

          lsdesc->type = OSPF6_ROUTER_LSDESC_TRANSIT_NETWORK;
          lsdesc->metric = htons (oi->cost);
          lsdesc->interface_id = htonl (oi->interface->ifindex);
          if (oi->state != OSPF6_INTERFACE_DR)
            {
              lsdesc->neighbor_interface_id = htonl (drouter->ifindex);
              lsdesc->neighbor_router_id = drouter->router_id;
            }
          else
            {
              lsdesc->neighbor_interface_id = htonl (oi->interface->ifindex);
              lsdesc->neighbor_router_id = oi->area->ospf6->router_id;
            }

          lsdesc++;
        }

      /* Virtual links */
        /* xxx */
      /* Point-to-Multipoint interfaces */
        /* xxx */
    }

  if ((caddr_t) lsdesc != (caddr_t) router_lsa +
                          sizeof (struct ospf6_router_lsa))
    {
      /* Fill LSA Header */
      lsa_header->age = 0;
      lsa_header->type = htons (OSPF6_LSTYPE_ROUTER);
      lsa_header->id = htonl (link_state_id);
      lsa_header->adv_router = oa->ospf6->router_id;
      lsa_header->seqnum =
        ospf6_new_ls_seqnum (lsa_header->type, lsa_header->id,
                             lsa_header->adv_router, oa->lsdb);
      lsa_header->length = htons ((caddr_t) lsdesc - (caddr_t) buffer);

      /* LSA checksum */
      ospf6_lsa_checksum (lsa_header);

      /* create LSA */
      lsa = ospf6_lsa_create (lsa_header);

      /* Originate */
      ospf6_lsa_originate_area (lsa, oa);

      link_state_id ++;
    }
  else
    {
      if (IS_OSPF6_DEBUG_ORIGINATE (ROUTER))
        zlog_debug ("Nothing to describe in Router-LSA, suppress");
    }

  /* Do premature-aging of rest, undesired Router-LSAs */
  type = ntohs (OSPF6_LSTYPE_ROUTER);
  router = oa->ospf6->router_id;
  for (lsa = ospf6_lsdb_type_router_head (type, router, oa->lsdb); lsa;
       lsa = ospf6_lsdb_type_router_next (type, router, lsa))
    {
      if (ntohl (lsa->header->id) < link_state_id)
        continue;
      ospf6_lsa_purge (lsa);
    }

  return 0;
}

void
ospf6_router_lsa_schedule (struct ospf6_area *oa)
{
  if (!oa->thread_router_lsa)
    {
      oa->thread_router_lsa =
        thread_add_timer_msec (master, ospf6_router_lsa_originate,
                               oa, LSA_SCHEDULE_DELAY_MSEC);
    }
}

/*******************************/
/* RFC2740 3.4.3.2 Network-LSA */
/*******************************/

static int
ospf6_network_lsa_show (struct vty *vty, struct ospf6_lsa *lsa)
{
  char *start, *end, *current;
  struct ospf6_network_lsa *network_lsa;
  struct ospf6_network_lsdesc *lsdesc;
  char buf[128], options[32];

  network_lsa = (struct ospf6_network_lsa *)
    ((caddr_t) lsa->header + sizeof (struct ospf6_lsa_header));

  ospf6_options_printbuf (network_lsa->options, options, sizeof (options));
  vty_out (vty, "     Options: %s%s", options, VNL);

  start = (char *) network_lsa + sizeof (struct ospf6_network_lsa);
  end = (char *) lsa->header + ntohs (lsa->header->length);
  for (current = start; current + sizeof (struct ospf6_network_lsdesc) <= end;
       current += sizeof (struct ospf6_network_lsdesc))
    {
      lsdesc = (struct ospf6_network_lsdesc *) current;
      ospf6_id2str (lsdesc->router_id, buf, sizeof (buf));
      vty_out (vty, "     Attached Router: %s%s", buf, VNL);
    }
  return 0;
}

static int
ospf6_network_lsa_originate (struct thread *thread)
{
  struct ospf6_interface *oi;

  char buffer [OSPF6_MAX_LSASIZE];
  struct ospf6_lsa_header *lsa_header;

  int count;
  struct ospf6_lsa *old, *lsa;
  struct ospf6_network_lsa *network_lsa;
  struct ospf6_network_lsdesc *lsdesc;
  struct ospf6_neighbor *on;
  struct ospf6_link_lsa *link_lsa;
  struct listnode *i;
  u_int16_t type;

  oi = (struct ospf6_interface *) THREAD_ARG (thread);
  oi->thread_network_lsa = NULL;

  /* The interface must be enabled until here. A Network-LSA of a
     disabled interface (but was once enabled) should be flushed
     by ospf6_lsa_refresh (), and does not come here. */
  assert (oi->area);

  old = ospf6_lsdb_lookup (htons (OSPF6_LSTYPE_NETWORK),
                           htonl (oi->interface->ifindex),
                           oi->area->ospf6->router_id, oi->area->lsdb);

  /* Do not originate Network-LSA if not DR */
  if (oi->state != OSPF6_INTERFACE_DR)
    {
      if (old)
        ospf6_lsa_purge (old);
      return 0;
    }

  if (old != NULL)
    {
      long delay_msec;

      delay_msec = 1000 * oi->area->ospf6->min_lsa_interval -
        elapsed_msec (&old->originated);
      if (delay_msec > 0)
        {
          if (IS_OSPF6_DEBUG_ORIGINATE (NETWORK))
            zlog_debug ("Delaying Network-LSA origination for interface %s "
                        "by %li msec to satisfy MinLSInterval",
                        oi->interface->name, delay_msec);
          oi->thread_network_lsa =
            thread_add_timer_msec (master, ospf6_network_lsa_originate,
                                   oi, delay_msec);
          return 0;
        }
    }

  if (IS_OSPF6_DEBUG_ORIGINATE (NETWORK))
    zlog_debug ("Originate Network-LSA for Interface %s", oi->interface->name);

  /* If none of neighbor is adjacent to us */
  count = 0;
  
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, i, on))
    if (on->state == OSPF6_NEIGHBOR_FULL)
      count++;
  
  if (count == 0)
    {
      if (IS_OSPF6_DEBUG_ORIGINATE (NETWORK))
        zlog_debug ("Interface stub, ignore");
      if (old)
        ospf6_lsa_purge (old);
      return 0;
    }

  /* prepare buffer */
  memset (buffer, 0, sizeof (buffer));
  lsa_header = (struct ospf6_lsa_header *) buffer;
  network_lsa = (struct ospf6_network_lsa *)
    ((caddr_t) lsa_header + sizeof (struct ospf6_lsa_header));

  /* Collect the interface's Link-LSAs to describe
     network's optional capabilities */
  type = htons (OSPF6_LSTYPE_LINK);
  for (lsa = ospf6_lsdb_type_head (type, oi->lsdb); lsa;
       lsa = ospf6_lsdb_type_next (type, lsa))
    {
      link_lsa = (struct ospf6_link_lsa *)
        ((caddr_t) lsa->header + sizeof (struct ospf6_lsa_header));
      network_lsa->options[0] |= link_lsa->options[0];
      network_lsa->options[1] |= link_lsa->options[1];
      network_lsa->options[2] |= link_lsa->options[2];
    }

  lsdesc = (struct ospf6_network_lsdesc *)
    ((caddr_t) network_lsa + sizeof (struct ospf6_network_lsa));

  /* set Link Description to the router itself */
  lsdesc->router_id = oi->area->ospf6->router_id;
  lsdesc++;

  /* Walk through the neighbors */
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, i, on))
    {
      if (on->state != OSPF6_NEIGHBOR_FULL)
        continue;

      /* set this neighbor's Router-ID to LSA */
      lsdesc->router_id = on->router_id;
      lsdesc++;
    }

  /* Fill LSA Header */
  lsa_header->age = 0;
  lsa_header->type = htons (OSPF6_LSTYPE_NETWORK);
  lsa_header->id = htonl (oi->interface->ifindex);
  lsa_header->adv_router = oi->area->ospf6->router_id;
  lsa_header->seqnum =
    ospf6_new_ls_seqnum (lsa_header->type, lsa_header->id,
                         lsa_header->adv_router, oi->area->lsdb);
  lsa_header->length = htons ((caddr_t) lsdesc - (caddr_t) buffer);

  /* LSA checksum */
  ospf6_lsa_checksum (lsa_header);

  /* create LSA */
  lsa = ospf6_lsa_create (lsa_header);

  /* Originate */
  ospf6_lsa_originate_area (lsa, oi->area);

  return 0;
}

void
ospf6_network_lsa_schedule (struct ospf6_interface *oi)
{
  if (!oi->thread_network_lsa)
    {
      oi->thread_network_lsa =
        thread_add_timer_msec (master, ospf6_network_lsa_originate,
                               oi, LSA_SCHEDULE_DELAY_MSEC);
    }
}

void
ospf6_network_lsa_execute (struct ospf6_interface *oi)
{
  THREAD_OFF (oi->thread_network_lsa);
  thread_execute (master, ospf6_network_lsa_originate, oi, 0);
}

/****************************/
/* RFC2740 3.4.3.6 Link-LSA */
/****************************/

static int
ospf6_link_lsa_show (struct vty *vty, struct ospf6_lsa *lsa)
{
  char *start, *end, *current;
  struct ospf6_link_lsa *link_lsa;
  int prefixnum;
  char buf[128], options[32];
  struct ospf6_prefix *prefix;
  const char *p, *la, *nu;
  struct in6_addr in6;

  link_lsa = (struct ospf6_link_lsa *)
    ((caddr_t) lsa->header + sizeof (struct ospf6_lsa_header));

  ospf6_options_printbuf (link_lsa->options, options, sizeof (options));
  ospf6_addr2str (ospf6, &link_lsa->linklocal_addr, buf, sizeof (buf));
  prefixnum = ntohl (link_lsa->prefix_num);

  vty_out (vty, "     Priority: %d Options: %s%s",
           link_lsa->priority, options, VNL);
  vty_out (vty, "     LinkLocal Address: %s%s", buf, VNL);
  vty_out (vty, "     Number of Prefix: %d%s", prefixnum, VNL);

  start = (char *) link_lsa + sizeof (struct ospf6_link_lsa);
  end = (char *) lsa->header + ntohs (lsa->header->length); 
  for (current = start; current < end; current += OSPF6_PREFIX_SIZE (prefix))
    {
      prefix = (struct ospf6_prefix *) current;
      if (prefix->prefix_length == 0 ||
          current + OSPF6_PREFIX_SIZE (prefix) > end)
        break;

      p = (CHECK_FLAG (prefix->prefix_options, OSPF6_PREFIX_OPTION_P) ?
           "P" : "--");
      la = (CHECK_FLAG (prefix->prefix_options, OSPF6_PREFIX_OPTION_LA) ?
           "LA" : "--");
      nu = (CHECK_FLAG (prefix->prefix_options, OSPF6_PREFIX_OPTION_NU) ?
           "NU" : "--");
      vty_out (vty, "     Prefix Options: %s|**|%s|%s%s", p, la, nu, VNL);

      memset (&in6, 0, sizeof (in6));
      memcpy (&in6, OSPF6_PREFIX_BODY (prefix),
              OSPF6_PREFIX_SPACE (prefix->prefix_length));
      ospf6_addr2str (ospf6, &in6, buf, sizeof (buf));
      vty_out (vty, "     Prefix: %s/%d%s",
               buf, ospf6_af_prefixlen6 (ospf6, prefix->prefix_length), VNL);
    }

  return 0;
}

static int
ospf6_link_lsa_originate (struct thread *thread)
{
  struct ospf6_interface *oi;

  char buffer[OSPF6_MAX_LSASIZE];
  struct ospf6_lsa_header *lsa_header;
  struct ospf6_lsa *old, *lsa;

  struct ospf6_link_lsa *link_lsa;
  struct ospf6_prefix *op;
  int suppress, af_is_ipv4;

  oi = (struct ospf6_interface *) THREAD_ARG (thread);
  oi->thread_link_lsa = NULL;

  assert (oi->area);

  /*
   * RFC 5340: 4.4.3.8. Link-LSAs
   *
   * ...
   * If LinkLSASuppression is configured for the interface and the
   * interface type is not broadcast or NBMA, origination of the
   * link-LSA may be suppressed. ...
   */
  if (oi->LinkLSASuppression && oi->type != OSPF6_IFTYPE_BROADCAST &&
      oi->type != OSPF6_IFTYPE_NBMA)
    suppress = 1;
  else
    suppress = 0;

  af_is_ipv4 = ospf6_af_is_ipv4 (oi->area->ospf6);

  /* find previous LSA */
  old = ospf6_lsdb_lookup (htons (OSPF6_LSTYPE_LINK),
                           htonl (oi->interface->ifindex),
                           oi->area->ospf6->router_id, oi->lsdb);

  /*
   * For IPv4 AFs, a link-LSA that includes the link's IPv4 address in
   * the link-local address field needs to be originated even if
   * link-LSA suppression is enabled (for nexthop calculation
   * purposes, see Section 2.5 of RFC 5838)
   */
  if (CHECK_FLAG (oi->flag, OSPF6_INTERFACE_DISABLE) ||
      CHECK_FLAG (oi->flag, OSPF6_INTERFACE_PASSIVE) ||
      oi->type == OSPF6_IFTYPE_LOOPBACK ||
      oi->type == OSPF6_IFTYPE_VIRTUALLINK ||
      (suppress && !af_is_ipv4))
    {
      if (old)
        ospf6_lsa_purge (old);
      return 0;
    }

  if (old != NULL)
    {
      long delay_msec;

      delay_msec = 1000 * oi->area->ospf6->min_lsa_interval -
        elapsed_msec (&old->originated);
      if (delay_msec > 0)
        {
          if (IS_OSPF6_DEBUG_ORIGINATE (LINK))
            zlog_debug ("Delaying Link-LSA origination for interface %s "
                        "by %li msec to satisfy MinLSInterval",
                        oi->interface->name, delay_msec);
          oi->thread_link_lsa =
            thread_add_timer_msec (master, ospf6_link_lsa_originate,
                                   oi, delay_msec);
          return 0;
        }
    }

  if (IS_OSPF6_DEBUG_ORIGINATE (LINK))
    zlog_debug ("Originate Link-LSA for Interface %s", oi->interface->name);

  /* can't make Link-LSA if linklocal address not set */
  if (!ospf6_interface_has_linklocal_addr (oi))
    {
      if (IS_OSPF6_DEBUG_ORIGINATE (LINK))
        zlog_debug ("No Linklocal address on %s, defer originating",
                   oi->interface->name);
      if (old)
        ospf6_lsa_purge (old);
      return 0;
    }

  /* prepare buffer */
  memset (buffer, 0, sizeof (buffer));
  lsa_header = (struct ospf6_lsa_header *) buffer;
  link_lsa = (struct ospf6_link_lsa *)
    ((caddr_t) lsa_header + sizeof (struct ospf6_lsa_header));

  /* Fill Link-LSA */
  link_lsa->priority = oi->priority;
  memcpy (link_lsa->options, oi->area->options, 3);

  if (af_is_ipv4)
    {
      /*
       * RFC 5838:
       *
       * 2.5. Next-Hop Calculation for IPv4 Unicast and Multicast AFs
       *
       * ... the link's IPv4 address will be advertised in the "link local
       * address" field of the IPv4 instance's Link-LSA.  This address is
       * placed in the first 32 bits of the "link local address" field and
       * is used for IPv4 next-hop calculations.  The remaining bits MUST
       * be set to zero.
       */
      ospf6_af_address_convert4to6 (&link_lsa->linklocal_addr,
                                    oi->linklocal_addr_ipv4);
    }
  else
    {
      memcpy (&link_lsa->linklocal_addr, oi->linklocal_addr,
              sizeof (struct in6_addr));
    }

  op = (struct ospf6_prefix *)
    ((caddr_t) link_lsa + sizeof (struct ospf6_link_lsa));

  if (!suppress)
    {
      struct ospf6_route *route;
      unsigned int num_prefixes = 0;

      /* connected prefix to advertise */
      for (route = ospf6_route_head (oi->route_connected); route;
	   route = ospf6_route_next (route))
	{
          if ((char *) op + sizeof (*op) +
              OSPF6_PREFIX_SPACE (route->prefix.prefixlen) >
              buffer + sizeof (buffer))
            {
              zlog_warn ("Only including %u of %u prefixes in Link-LSA "
                         "for interface %s", num_prefixes,
                         oi->route_connected->count, oi->interface->name);
              break;
            }

	  op->prefix_length = route->prefix.prefixlen;
	  op->prefix_options = route->path.prefix_options;
	  op->prefix_metric = htons (0);
	  memcpy (OSPF6_PREFIX_BODY (op), &route->prefix.u.prefix6,
		  OSPF6_PREFIX_SPACE (op->prefix_length));
          num_prefixes++;
	  op = OSPF6_PREFIX_NEXT (op);
          assert ((char *) op <= buffer + sizeof (buffer));
	}

      link_lsa->prefix_num = htonl (num_prefixes);
    }

  /* Fill LSA Header */
  lsa_header->age = 0;
  lsa_header->type = htons (OSPF6_LSTYPE_LINK);
  lsa_header->id = htonl (oi->interface->ifindex);
  lsa_header->adv_router = oi->area->ospf6->router_id;
  lsa_header->seqnum =
    ospf6_new_ls_seqnum (lsa_header->type, lsa_header->id,
                         lsa_header->adv_router, oi->lsdb);
  lsa_header->length = htons ((caddr_t) op - (caddr_t) buffer);

  /* LSA checksum */
  ospf6_lsa_checksum (lsa_header);

  /* create LSA */
  lsa = ospf6_lsa_create (lsa_header);

  /* Originate */
  ospf6_lsa_originate_interface (lsa, oi);

  return 0;
}

void
ospf6_link_lsa_schedule (struct ospf6_interface *oi)
{
  if (!oi->thread_link_lsa)
    {
      oi->thread_link_lsa =
        thread_add_timer_msec (master, ospf6_link_lsa_originate,
                               oi, LSA_SCHEDULE_DELAY_MSEC);
    }
}

/*****************************************/
/* RFC2740 3.4.3.7 Intra-Area-Prefix-LSA */
/*****************************************/

static int
ospf6_intra_prefix_lsa_show (struct vty *vty, struct ospf6_lsa *lsa)
{
  char *start, *end, *current;
  struct ospf6_intra_prefix_lsa *intra_prefix_lsa;
  int prefixnum;
  char buf[128];
  struct ospf6_prefix *prefix;
  char id[16], adv_router[16];
  const char *p, *la, *nu;
  struct in6_addr in6;

  intra_prefix_lsa = (struct ospf6_intra_prefix_lsa *)
    ((caddr_t) lsa->header + sizeof (struct ospf6_lsa_header));

  prefixnum = ntohs (intra_prefix_lsa->prefix_num);

  vty_out (vty, "     Number of Prefix: %d%s", prefixnum, VNL);

  ospf6_id2str (intra_prefix_lsa->ref_id, id, sizeof (id));
  ospf6_id2str (intra_prefix_lsa->ref_adv_router,
		adv_router, sizeof (adv_router));
  vty_out (vty, "     Reference: %s Id: %s Adv: %s%s",
           ospf6_lstype_name (intra_prefix_lsa->ref_type), id, adv_router,
           VNL);

  start = (char *) intra_prefix_lsa + sizeof (struct ospf6_intra_prefix_lsa);
  end = (char *) lsa->header + ntohs (lsa->header->length); 
  for (current = start; current < end; current += OSPF6_PREFIX_SIZE (prefix))
    {
      prefix = (struct ospf6_prefix *) current;
      if (prefix->prefix_length == 0 ||
          current + OSPF6_PREFIX_SIZE (prefix) > end)
        break;

      p = (CHECK_FLAG (prefix->prefix_options, OSPF6_PREFIX_OPTION_P) ?
           "P" : "--");
      la = (CHECK_FLAG (prefix->prefix_options, OSPF6_PREFIX_OPTION_LA) ?
           "LA" : "--");
      nu = (CHECK_FLAG (prefix->prefix_options, OSPF6_PREFIX_OPTION_NU) ?
           "NU" : "--");
      vty_out (vty, "     Prefix Options: %s|**|%s|%s%s", p, la, nu, VNL);

      memset (&in6, 0, sizeof (in6));
      memcpy (&in6, OSPF6_PREFIX_BODY (prefix),
              OSPF6_PREFIX_SPACE (prefix->prefix_length));
      ospf6_addr2str (ospf6, &in6, buf, sizeof (buf));
      vty_out (vty, "     Prefix: %s/%d%s",
               buf, ospf6_af_prefixlen6 (ospf6, prefix->prefix_length), VNL);
    }

  return 0;
}

static int
ospf6_intra_prefix_lsa_originate_stub (struct thread *thread)
{
  struct ospf6_area *oa;

  char buffer[OSPF6_MAX_LSASIZE];
  struct ospf6_lsa_header *lsa_header;
  struct ospf6_lsa *old, *lsa;

  struct ospf6_intra_prefix_lsa *intra_prefix_lsa;
  struct ospf6_interface *oi;
  struct ospf6_neighbor *on;
  struct ospf6_route *route;
  struct ospf6_route_table *route_advertise;
  struct ospf6_prefix *op;
  struct listnode *i, *j;
  int full_count = 0;
  unsigned short prefix_num = 0;

  oa = (struct ospf6_area *) THREAD_ARG (thread);
  oa->thread_intra_prefix_lsa = NULL;

  /* find previous LSA */
  old = ospf6_lsdb_lookup (htons (OSPF6_LSTYPE_INTRA_PREFIX),
                           htonl (0), oa->ospf6->router_id, oa->lsdb);

  if (! IS_AREA_ENABLED (oa))
    {
      if (old)
        ospf6_lsa_purge (old);
      return 0;
    }

  if (old != NULL)
    {
      long delay_msec;

      delay_msec = 1000 * oa->ospf6->min_lsa_interval -
        elapsed_msec (&old->originated);
      if (delay_msec > 0)
        {
          if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
            zlog_debug ("Delaying Intra-Area-Prefix-LSA (stub) origination "
                        "for area %s by %li msec to satisfy MinLSInterval",
                        oa->name, delay_msec);
          oa->thread_intra_prefix_lsa =
            thread_add_timer_msec (master,
                                   ospf6_intra_prefix_lsa_originate_stub,
                                   oa, delay_msec);
          return 0;
        }
    }

  if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
    zlog_debug ("Originate Intra-Area-Prefix-LSA for area %s's stub prefix",
               oa->name);

  route_advertise = ospf6_route_table_create (0, 0);

  for (ALL_LIST_ELEMENTS_RO (oa->if_list, i, oi))
    {
      if (oi->state == OSPF6_INTERFACE_DOWN)
        {
          if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
            zlog_debug ("  Interface %s is down, ignore", oi->interface->name);
          continue;
        }

      full_count = 0;

      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, on))
        if (on->state == OSPF6_NEIGHBOR_FULL)
          full_count++;

      if (oi->state != OSPF6_INTERFACE_LOOPBACK &&
          oi->state != OSPF6_INTERFACE_POINTTOPOINT &&
          full_count != 0)
        {
          if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
            zlog_debug ("  Interface %s is not stub, ignore",
                       oi->interface->name);
          continue;
        }

      if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
        zlog_debug ("  Interface %s:", oi->interface->name);

      /* connected prefix to advertise */
      for (route = ospf6_route_head (oi->route_connected); route;
           route = ospf6_route_best_next (route))
        {
          struct ospf6_route *route_new;
          u_int8_t prefix_length, prefix_options;

          prefix_options = route->path.prefix_options;

	  //RFC 2740 3.4.3.7 Bullet 5 --
          if (oi->type == OSPF6_IFTYPE_MDR ||
              oi->type == OSPF6_IFTYPE_POINTOMULTIPOINT)
	    {
	      prefix_options |= OSPF6_PREFIX_OPTION_LA;
	      if (ospf6_af_is_ipv4 (oa->ospf6) && oa->ospf6->af_interop)
		prefix_length = 32;
	      else
		prefix_length = 128;
	    }
          else
            {
              prefix_length = route->prefix.prefixlen;
            }

	  if (ospf6_af_validate_prefix (oa->ospf6, &route->prefix.u.prefix6,
					route->prefix.prefixlen, false))
	    {
	      if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
		{
		  char buf[PREFIXSTRLEN];

                  ospf6_prefix2str (oa->ospf6, &route->prefix,
                                    buf, sizeof (buf));
                  zlog_debug ("    ignore %s", buf);
		}
	      continue;
	    }

          if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
            {
              char buf[PREFIXSTRLEN];

              ospf6_prefix2str (oa->ospf6, &route->prefix, buf, sizeof (buf));
              zlog_debug ("    include %s", buf);
            }

          route_new = ospf6_route_copy (route);
          route_new->prefix.prefixlen = prefix_length;
          route_new->path.prefix_options = prefix_options;
          //must add mask application here because it was removed
          //in ospf6_interface.c, ospf6_interface_connected_route_update()
          apply_mask (&route_new->prefix);

          ospf6_route_add (route_new, route_advertise);
	}
    }

  /* prepare buffer */
  memset (buffer, 0, sizeof (buffer));
  lsa_header = (struct ospf6_lsa_header *) buffer;
  intra_prefix_lsa = (struct ospf6_intra_prefix_lsa *)
    ((caddr_t) lsa_header + sizeof (struct ospf6_lsa_header));

  /* Fill Intra-Area-Prefix-LSA */
  intra_prefix_lsa->ref_type = htons (OSPF6_LSTYPE_ROUTER);
  intra_prefix_lsa->ref_id = htonl (0);
  intra_prefix_lsa->ref_adv_router = oa->ospf6->router_id;

  prefix_num = 0;
  op = (struct ospf6_prefix *)
    ((caddr_t) intra_prefix_lsa + sizeof (struct ospf6_intra_prefix_lsa));

  for (route = ospf6_route_head (route_advertise); route;
       route = ospf6_route_best_next (route))
    {
      if ((char *) op + sizeof (*op) +
          OSPF6_PREFIX_SPACE (route->prefix.prefixlen) >
          buffer + sizeof (buffer))
        {
          zlog_warn ("Only including %u of %u prefixes in "
                     "Intra-Area-Prefix-LSA for stub interfaces",
                     prefix_num, route_advertise->count);
          break;
        }

      op->prefix_length = route->prefix.prefixlen;
      op->prefix_options = route->path.prefix_options;
      op->prefix_metric = htons (route->path.cost);
      memcpy (OSPF6_PREFIX_BODY (op), &route->prefix.u.prefix6,
              OSPF6_PREFIX_SPACE (op->prefix_length));
      op = OSPF6_PREFIX_NEXT (op);
      prefix_num++;
      assert ((char *) op <= buffer + sizeof (buffer));
    }

  ospf6_route_table_delete (route_advertise);

  if (prefix_num == 0 && old)
    ospf6_lsa_purge (old);

  if (prefix_num == 0)
    {
      if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
        zlog_debug ("Quit to Advertise Intra-Prefix: no route to advertise");
      return 0;
    }

  intra_prefix_lsa->prefix_num = htons (prefix_num);

  /* Fill LSA Header */
  lsa_header->age = 0;
  lsa_header->type = htons (OSPF6_LSTYPE_INTRA_PREFIX);
  lsa_header->id = htonl (0);
  lsa_header->adv_router = oa->ospf6->router_id;
  lsa_header->seqnum =
    ospf6_new_ls_seqnum (lsa_header->type, lsa_header->id,
                         lsa_header->adv_router, oa->lsdb);
  lsa_header->length = htons ((caddr_t) op - (caddr_t) lsa_header);

  /* LSA checksum */
  ospf6_lsa_checksum (lsa_header);

  /* create LSA */
  lsa = ospf6_lsa_create (lsa_header);

  /* Originate */
  ospf6_lsa_originate_area (lsa, oa);

  return 0;
}

void
ospf6_intra_prefix_lsa_schedule_stub (struct ospf6_area *oa)
{
  if (!oa->thread_intra_prefix_lsa)
    {
      oa->thread_intra_prefix_lsa =
        thread_add_timer_msec (master,
                               ospf6_intra_prefix_lsa_originate_stub,
                               oa, LSA_SCHEDULE_DELAY_MSEC);
    }
}

static int
ospf6_intra_prefix_lsa_originate_transit (struct thread *thread)
{
  struct ospf6_interface *oi;

  char buffer[OSPF6_MAX_LSASIZE];
  struct ospf6_lsa_header *lsa_header;
  struct ospf6_lsa *old, *lsa;

  struct ospf6_intra_prefix_lsa *intra_prefix_lsa;
  struct ospf6_neighbor *on;
  struct ospf6_route *route;
  struct ospf6_prefix *op;
  struct listnode *i;
  int full_count = 0;
  unsigned short prefix_num = 0;
  struct ospf6_route_table *route_advertise;
  struct ospf6_link_lsa *link_lsa;
  char *start, *end, *current;
  u_int16_t type;

  oi = (struct ospf6_interface *) THREAD_ARG (thread);
  oi->thread_intra_prefix_lsa = NULL;

  assert (oi->area);

  /* find previous LSA */
  old = ospf6_lsdb_lookup (htons (OSPF6_LSTYPE_INTRA_PREFIX),
                           htonl (oi->interface->ifindex),
                           oi->area->ospf6->router_id, oi->area->lsdb);

  if (CHECK_FLAG (oi->flag, OSPF6_INTERFACE_DISABLE))
    {
      if (old)
        ospf6_lsa_purge (old);
      return 0;
    }

  if (old != NULL)
    {
      long delay_msec;

      delay_msec = 1000 * oi->area->ospf6->min_lsa_interval -
        elapsed_msec (&old->originated);
      if (delay_msec > 0)
        {
          if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
            zlog_debug ("Delaying Intra-Area-Prefix-LSA origination for "
                        "interface %s by %li msec to satisfy MinLSInterval",
                        oi->interface->name, delay_msec);
          oi->thread_intra_prefix_lsa =
            thread_add_timer_msec (master,
                                   ospf6_intra_prefix_lsa_originate_transit,
                                   oi, delay_msec);
          return 0;
        }
    }

  if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
    zlog_debug ("Originate Intra-Area-Prefix-LSA for interface %s's prefix",
               oi->interface->name);

  /* prepare buffer */
  memset (buffer, 0, sizeof (buffer));
  lsa_header = (struct ospf6_lsa_header *) buffer;
  intra_prefix_lsa = (struct ospf6_intra_prefix_lsa *)
    ((caddr_t) lsa_header + sizeof (struct ospf6_lsa_header));

  /* Fill Intra-Area-Prefix-LSA */
  intra_prefix_lsa->ref_type = htons (OSPF6_LSTYPE_NETWORK);
  intra_prefix_lsa->ref_id = htonl (oi->interface->ifindex);
  intra_prefix_lsa->ref_adv_router = oi->area->ospf6->router_id;

  if (oi->state != OSPF6_INTERFACE_DR)
    {
      if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
        zlog_debug ("  Interface is not DR");
      if (old)
        ospf6_lsa_purge (old);
      return 0;
    }

  full_count = 0;
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, i, on))
    if (on->state == OSPF6_NEIGHBOR_FULL)
      full_count++;
  
  if (full_count == 0)
    {
      if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
        zlog_debug ("  Interface is stub");
      if (old)
        ospf6_lsa_purge (old);
      return 0;
    }

  /* connected prefix to advertise */
  route_advertise = ospf6_route_table_create (0, 0);

  type = ntohs (OSPF6_LSTYPE_LINK);
  for (lsa = ospf6_lsdb_type_head (type, oi->lsdb); lsa;
       lsa = ospf6_lsdb_type_next (type, lsa))
    {
      if (OSPF6_LSA_IS_MAXAGE (lsa))
        continue;

      if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
        zlog_debug ("  include prefix from %s", lsa->name);

      if (lsa->header->adv_router != oi->area->ospf6->router_id)
        {
          on = ospf6_neighbor_lookup (lsa->header->adv_router, oi);
          if (on == NULL || on->state != OSPF6_NEIGHBOR_FULL)
            {
              if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
                zlog_debug ("    Neighbor not found or not Full, ignore");
              continue;
            }
        }

      link_lsa = (struct ospf6_link_lsa *)
        ((caddr_t) lsa->header + sizeof (struct ospf6_lsa_header));

      prefix_num = (unsigned short) ntohl (link_lsa->prefix_num);
      start = (char *) link_lsa + sizeof (struct ospf6_link_lsa);
      end = (char *) lsa->header + ntohs (lsa->header->length); 
      for (current = start; current < end && prefix_num;
           current += OSPF6_PREFIX_SIZE (op))
        {
          op = (struct ospf6_prefix *) current;
          if (op->prefix_length == 0 ||
              current + OSPF6_PREFIX_SIZE (op) > end)
            break;

          route = ospf6_route_create ();

          route->type = OSPF6_DEST_TYPE_NETWORK;
          route->prefix.family = AF_INET6;
          route->prefix.prefixlen = op->prefix_length;
          memset (&route->prefix.u.prefix6, 0, sizeof (struct in6_addr));
          memcpy (&route->prefix.u.prefix6, OSPF6_PREFIX_BODY (op),
                  OSPF6_PREFIX_SPACE (op->prefix_length));
          //must add mask application here because it was removed
          //in ospf6_interface.c, ospf6_interface_connected_route_update()
	  // ospf6_prefix_apply_mask (op);
          apply_mask (&route->prefix);

          route->path.origin.type = lsa->header->type;
          route->path.origin.id = lsa->header->id;
          route->path.origin.adv_router = lsa->header->adv_router;
          route->path.options[0] = link_lsa->options[0];
          route->path.options[1] = link_lsa->options[1];
          route->path.options[2] = link_lsa->options[2];
          route->path.prefix_options = op->prefix_options;
          route->path.area_id = oi->area->area_id;
          route->path.type = OSPF6_PATH_TYPE_INTRA;

          if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
            {
              char buf[PREFIXSTRLEN];

              ospf6_prefix2str (oi->area->ospf6, &route->prefix,
				buf, sizeof (buf));
              zlog_debug ("    include %s", buf);
            }

          ospf6_route_add (route, route_advertise);
          prefix_num--;
        }
      if (current != end && IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
        zlog_debug ("Trailing garbage in %s", lsa->name);
    }

  op = (struct ospf6_prefix *)
    ((caddr_t) intra_prefix_lsa + sizeof (struct ospf6_intra_prefix_lsa));

  prefix_num = 0;
  for (route = ospf6_route_head (route_advertise); route;
       route = ospf6_route_best_next (route))
    {
      if ((char *) op + sizeof (*op) +
          OSPF6_PREFIX_SPACE (route->prefix.prefixlen) >
          buffer + sizeof (buffer))
        {
          zlog_warn ("Only including %u of %u prefixes in "
                     "Intra-Area-Prefix-LSA for interface %s", prefix_num,
                     route_advertise->count, oi->interface->name);
          break;
        }

      op->prefix_length = route->prefix.prefixlen;
      op->prefix_options = route->path.prefix_options;
      op->prefix_metric = htons (0);
      memcpy (OSPF6_PREFIX_BODY (op), &route->prefix.u.prefix6,
              OSPF6_PREFIX_SPACE (op->prefix_length));
      op = OSPF6_PREFIX_NEXT (op);
      prefix_num++;
      assert ((char *) op <= buffer + sizeof (buffer));
    }

  ospf6_route_table_delete (route_advertise);

  if (prefix_num == 0)
    {
      if (IS_OSPF6_DEBUG_ORIGINATE (INTRA_PREFIX))
        zlog_debug ("Quit to Advertise Intra-Prefix: no route to advertise");
      return 0;
    }

  intra_prefix_lsa->prefix_num = htons (prefix_num);

  /* Fill LSA Header */
  lsa_header->age = 0;
  lsa_header->type = htons (OSPF6_LSTYPE_INTRA_PREFIX);
  lsa_header->id = htonl (oi->interface->ifindex);
  lsa_header->adv_router = oi->area->ospf6->router_id;
  lsa_header->seqnum =
    ospf6_new_ls_seqnum (lsa_header->type, lsa_header->id,
                         lsa_header->adv_router, oi->area->lsdb);
  lsa_header->length = htons ((caddr_t) op - (caddr_t) lsa_header);

  /* LSA checksum */
  ospf6_lsa_checksum (lsa_header);

  /* create LSA */
  lsa = ospf6_lsa_create (lsa_header);

  /* Originate */
  ospf6_lsa_originate_area (lsa, oi->area);

  return 0;
}

void
ospf6_intra_prefix_lsa_schedule_transit (struct ospf6_interface *oi)
{
  if (!oi->thread_intra_prefix_lsa)
    {
      oi->thread_intra_prefix_lsa =
        thread_add_timer_msec (master,
                               ospf6_intra_prefix_lsa_originate_transit,
                               oi,  LSA_SCHEDULE_DELAY_MSEC);
    }
}

void
ospf6_intra_prefix_lsa_execute_transit (struct ospf6_interface *oi)
{
  THREAD_OFF (oi->thread_intra_prefix_lsa);
  thread_execute (master, ospf6_intra_prefix_lsa_originate_transit, oi, 0);
}

static unsigned int
__ospf6_intra_prefix_lsa_add (struct ospf6_lsa *lsa)
{
  struct ospf6_area *oa;
  struct ospf6_intra_prefix_lsa *intra_prefix_lsa;
  struct prefix ls_prefix;
  struct ospf6_route *route, *ls_entry;
  int i, prefix_num;
  struct ospf6_prefix *op;
  char *start, *current, *end;
  unsigned int numadded = 0;

  if (OSPF6_LSA_IS_MAXAGE (lsa))
    return numadded;

  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
    zlog_debug ("%s found", lsa->name);

  oa = OSPF6_AREA (lsa->lsdb->data);

  intra_prefix_lsa = (struct ospf6_intra_prefix_lsa *)
    OSPF6_LSA_HEADER_END (lsa->header);
  if (intra_prefix_lsa->ref_type == htons (OSPF6_LSTYPE_ROUTER))
    ospf6_linkstate_prefix (intra_prefix_lsa->ref_adv_router,
                            htonl (0), &ls_prefix);
  else if (intra_prefix_lsa->ref_type == htons (OSPF6_LSTYPE_NETWORK))
    ospf6_linkstate_prefix (intra_prefix_lsa->ref_adv_router,
                            intra_prefix_lsa->ref_id, &ls_prefix);
  else
    {
      if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
        zlog_debug ("Unknown reference LS-type: %#hx",
		    ntohs (intra_prefix_lsa->ref_type));
      return numadded;
    }

  ls_entry = ospf6_route_lookup (&ls_prefix, oa->spf_table);
  if (ls_entry == NULL)
    {
      if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
        {
	  char buf[64];
          ospf6_linkstate_prefix2str (&ls_prefix, buf, sizeof (buf));
          zlog_debug ("LS entry does not exist: %s", buf);
        }
      return numadded;
    }

  prefix_num = ntohs (intra_prefix_lsa->prefix_num);
  start = (caddr_t) intra_prefix_lsa +
          sizeof (struct ospf6_intra_prefix_lsa);
  end = OSPF6_LSA_END (lsa->header);
  for (current = start; current < end; current += OSPF6_PREFIX_SIZE (op))
    {
      struct prefix prefix = {
	.family = AF_INET6,
      };

      op = (struct ospf6_prefix *) current;
      if (prefix_num == 0)
        break;
      if (end < current + OSPF6_PREFIX_SIZE (op))
        break;
      prefix_num--;

      ospf6_prefix_in6_addr (&prefix.u.prefix6, op);
      prefix.prefixlen = op->prefix_length;

      /* check prefix address family */
      if (ospf6_af_validate_prefix (oa->ospf6,
				    &prefix.u.prefix6, prefix.prefixlen, false))
	{
	  char buf[PREFIXSTRLEN];

	  ospf6_prefix2str (oa->ospf6, &prefix, buf, sizeof (buf));
	  zlog_warn ("%s: ignoring prefix %s in lsa %s: "
		     "address family incompatibility",
		     __func__, buf, lsa->name);

	  /* ignore this prefix */
	  continue;
	}

      /* check if this prefix is connected */
      if (ospf6_area_prefix_is_connected (oa, &prefix))
	{
	  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
	    {
	      char buf[PREFIXSTRLEN];

	      ospf6_prefix2str (oa->ospf6, &prefix, buf, sizeof (buf));
	      zlog_debug ("%s: ignoring prefix %s in lsa %s: "
			  "prefix is connected", __func__, buf, lsa->name);
	    }

	  /* ignore this prefix */
	  continue;
	}

      route = ospf6_route_create ();

      route->prefix = prefix;

      route->type = OSPF6_DEST_TYPE_NETWORK;
      route->path.origin.type = lsa->header->type;
      route->path.origin.id = lsa->header->id;
      route->path.origin.adv_router = lsa->header->adv_router;
      route->path.prefix_options = op->prefix_options;
      route->path.area_id = oa->area_id;
      route->path.type = OSPF6_PATH_TYPE_INTRA;
      route->path.metric_type = 1;
      route->path.cost = ls_entry->path.cost + ntohs (op->prefix_metric);

      if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
	{
	  char buf[PREFIXSTRLEN];
	  ospf6_prefix2str (oa->ospf6, &route->prefix, buf, sizeof (buf));
	  zlog_debug ("route %s", buf);
	}

      for (i = 0; i < OSPF6_MULTI_PATH_LIMIT &&
             ospf6_nexthop_is_set (&ls_entry->nexthop[i]); i++)
	{
	  ospf6_nexthop_copy (&route->nexthop[i], &ls_entry->nexthop[i]);

	  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
	    {
	      char nexthop[INET6_ADDRSTRLEN];
              unsigned int ifindex;
	      ospf6_addr2str (oa->ospf6, &route->nexthop[i].address,
			      nexthop, sizeof (nexthop));
              ifindex = route->nexthop[i].ifindex;
              zlog_debug ("  nexthop %s%%%s(%u)", nexthop,
                          ifindex2ifname (ifindex), ifindex);
	    }
	}

      if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
        {
	  char buf[PREFIXSTRLEN];
          ospf6_prefix2str (oa->ospf6, &route->prefix, buf, sizeof (buf));
          zlog_debug ("  add %s", buf);
        }

      ospf6_route_add (route, oa->route_table);
      numadded++;
    }

  if (current != end && IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
    zlog_debug ("Trailing garbage ignored");

  return numadded;
}

void
ospf6_intra_prefix_lsa_add (struct ospf6_lsa *lsa)
{
  __ospf6_intra_prefix_lsa_add (lsa);
}

static unsigned int
__ospf6_intra_prefix_lsa_remove (struct ospf6_lsa *lsa,
			void (*remove_route) (struct ospf6_route *route,
					      struct ospf6_route_table *table))
{
  struct ospf6_area *oa;
  struct ospf6_intra_prefix_lsa *intra_prefix_lsa;
  struct ospf6_route *route;
  int prefix_num;
  struct ospf6_prefix *op;
  char *start, *current, *end;
  unsigned int numremoved = 0;

  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
    zlog_debug ("%s disappearing", lsa->name);

  oa = OSPF6_AREA (lsa->lsdb->data);

  intra_prefix_lsa = (struct ospf6_intra_prefix_lsa *)
    OSPF6_LSA_HEADER_END (lsa->header);

  prefix_num = ntohs (intra_prefix_lsa->prefix_num);
  start = (caddr_t) intra_prefix_lsa +
          sizeof (struct ospf6_intra_prefix_lsa);
  end = OSPF6_LSA_END (lsa->header);
  for (current = start; current < end; current += OSPF6_PREFIX_SIZE (op))
    {
      struct prefix prefix = {
	.family = AF_INET6,
      };

      op = (struct ospf6_prefix *) current;
      if (prefix_num == 0)
        break;
      if (end < current + OSPF6_PREFIX_SIZE (op))
        break;
      prefix_num--;

      ospf6_prefix_in6_addr (&prefix.u.prefix6, op);
      prefix.prefixlen = op->prefix_length;

      /* check prefix address family */
      if (ospf6_af_validate_prefix (oa->ospf6,
				    &prefix.u.prefix6, prefix.prefixlen, false))
	{
	  char buf[PREFIXSTRLEN];

	  prefix2str (&prefix, buf, sizeof(buf));
	  zlog_warn ("%s: ignoring prefix %s in lsa %s: "
		     "address family incompatibility", __func__,
		     buf, lsa->name);

	  /* ignore this prefix */
	  continue;
	}

      /* check if this prefix is connected */
      if (ospf6_area_prefix_is_connected (oa, &prefix))
	{
	  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
	    {
	      char buf[PREFIXSTRLEN];

	      ospf6_prefix2str (oa->ospf6, &prefix, buf, sizeof (buf));
	      zlog_debug ("%s: ignoring prefix %s in lsa %s: "
			  "prefix is connected", __func__, buf, lsa->name);
	    }

	  /* ignore this prefix */
	  continue;
	}

      route = ospf6_route_lookup (&prefix, oa->route_table);
      if (route == NULL)
        continue;

      for (ospf6_route_lock (route);
           route && ospf6_route_is_prefix (&prefix, route);
           route = ospf6_route_next (route))
        {
          if (route->type != OSPF6_DEST_TYPE_NETWORK)
            continue;
          if (route->path.area_id != oa->area_id)
            continue;
          if (route->path.type != OSPF6_PATH_TYPE_INTRA)
            continue;
          if (route->path.origin.type != lsa->header->type ||
              route->path.origin.id != lsa->header->id ||
              route->path.origin.adv_router != lsa->header->adv_router)
            continue;

          if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
            {
	      char buf[PREFIXSTRLEN];
              ospf6_prefix2str (oa->ospf6, &route->prefix, buf, sizeof (buf));
              zlog_debug ("remove %s", buf);
            }
	  remove_route (route, oa->route_table);
	  numremoved++;
        }
      if (route)
	ospf6_route_unlock (route);
    }

  if (current != end && IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
    zlog_debug ("Trailing garbage ignored");

  return numremoved;
}

void
ospf6_intra_prefix_lsa_remove (struct ospf6_lsa *lsa)
{
  __ospf6_intra_prefix_lsa_remove (lsa, ospf6_route_remove);
}

static void
__ospf6_intra_process_route_table (struct ospf6_route_table *route_table)
{
  int pass, numpass = 2;

  for (pass = 0; pass < numpass; pass++)
    {
      struct ospf6_route *route;
      int skipped = 0;

      for (route = ospf6_route_head (route_table); route;
	   route = ospf6_route_next (route))
	{
	  if (CHECK_FLAG (route->flag, OSPF6_ROUTE_ADD) &&
	      CHECK_FLAG (route->flag, OSPF6_ROUTE_REMOVE))
	    {
	      /* route unchanged */
	      UNSET_FLAG (route->flag, OSPF6_ROUTE_ADD);
	      UNSET_FLAG (route->flag, OSPF6_ROUTE_REMOVE);
	    }
	  else if (CHECK_FLAG (route->flag, OSPF6_ROUTE_REMOVE))
	    {
	      /* remove route */
	      ospf6_route_remove (route, route_table);
	      UNSET_FLAG (route->flag, OSPF6_ROUTE_REMOVE);
	    }
	  else if (CHECK_FLAG (route->flag, OSPF6_ROUTE_ADD) ||
		   CHECK_FLAG (route->flag, OSPF6_ROUTE_CHANGE))
	    {
	      /* add route */
	      int i;
	      bool routablenexthop = true;

	      for (i = 0; i < OSPF6_MULTI_PATH_LIMIT &&
                     ospf6_nexthop_is_set (&route->nexthop[i]); i++)
		{
                  struct prefix nexthop;
                  struct ospf6_route *nhroute;

                  if (!ospf6_af_is_ipv4 (ospf6))
                    {
                      assert (IN6_IS_ADDR_LINKLOCAL (&route->nexthop[i].address) ||
                              IN6_IS_ADDR_UNSPECIFIED (&route->nexthop[i].address));
                      continue;
                    }
                  else if (ospf6_route_directly_connected (&route->prefix,
                                                           &route->nexthop[i]))
                    {
                      continue;
                    }

                  nexthop = (struct prefix) {
                    .family = route->prefix.family,
                    .u = {
                      .prefix6 = route->nexthop[i].address,
                    },
                  };

                  if (ospf6_af_is_ipv4 (ospf6) && ospf6->af_interop)
                    nexthop.prefixlen = 32;
                  else
                    nexthop.prefixlen = 128;

                  nhroute =
                    ospf6_route_lookup_bestmatch (&nexthop, route_table);

                  /* nhroute->flag == OSPF6_ROUTE_BEST implies
                   * that nhroute has already been processed since
                   * other route flags are cleared in each case.
                   * route is skipped if nhroute has not been
                   * processed yet because zebra or the kernel can
                   * reject routes with unreachable nexthops.  If
                   * skipped, route will get added in the second
                   * pass since any prerequisite nexthops should
                   * have been added during the first pass.
                   */
                  if (nhroute == NULL || nhroute->flag != OSPF6_ROUTE_BEST)
                    {
                      routablenexthop = false;
                      break;
                    }
		}

	      if (routablenexthop)
		{
		  if (route_table->hook_add)
		    (*route_table->hook_add) (route);
		  UNSET_FLAG (route->flag, OSPF6_ROUTE_ADD);
		  UNSET_FLAG (route->flag, OSPF6_ROUTE_CHANGE);
		}
	      else
		{
		  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX) ||
		      pass == numpass - 1)
		    {
		      char prefix[PREFIXSTRLEN];
		      char via[1024] = "";
		      int offset = 0;

		      ospf6_prefix2str (ospf6, &route->prefix,
					prefix, sizeof(prefix));
		      for (i = 0; i < OSPF6_MULTI_PATH_LIMIT &&
			     ospf6_nexthop_is_set (&route->nexthop[i]); i++)
			{
			  char nexthop[INET6_ADDRSTRLEN];
			  int r;

			  ospf6_addr2str (ospf6, &route->nexthop[i].address,
					  nexthop, sizeof (nexthop));
			  r = snprintf (via + offset, sizeof (via) - offset,
					"%s%s", i > 0 ? "," : "", nexthop);
			  if (r >= sizeof (via) - offset)
			    break;
			  if (r > 0)
			    offset += r;
			}

		      zlog_debug ("%s: pass %d skipping route to %s via %s "
				  "because nexthop is not routable%s",
				  __func__, pass, prefix, via,
				  pass == numpass - 1 ?
				  "; this shouldn't happen" : "");
		    }
		  if (pass == numpass - 1)
		    {
		      ospf6_route_remove (route, route_table);
		      UNSET_FLAG (route->flag, OSPF6_ROUTE_ADD);
		      UNSET_FLAG (route->flag, OSPF6_ROUTE_CHANGE);
		    }
		  skipped++;
		}
	    }
	  else if (route->flag != OSPF6_ROUTE_BEST && route->flag != 0)
	    {
	      zlog_warn ("%s: unexpected route flag(s): 0x%x",
			 __func__, route->flag);
	    }
	}

      if (skipped == 0)
	break;
    }
}

static void
__ospf6_route_remove_mark (struct ospf6_route *route,
			   struct ospf6_route_table *table)
{
  UNSET_FLAG (route->flag, OSPF6_ROUTE_ADD);
  UNSET_FLAG (route->flag, OSPF6_ROUTE_CHANGE);
  SET_FLAG (route->flag, OSPF6_ROUTE_REMOVE);
}

void
ospf6_intra_prefix_lsa_replace (struct ospf6_lsa *old, struct ospf6_lsa *new)
{
  struct ospf6_area *oa;
  void (*hook_add) (struct ospf6_route *);
  void (*hook_remove) (struct ospf6_route *);
  unsigned int numchange;

  assert (old->lsdb == new->lsdb);

  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
    zlog_debug ("%s: replacing LSA %s", __func__, old->name);

  oa = OSPF6_AREA (new->lsdb->data);

  hook_add = oa->route_table->hook_add;
  hook_remove = oa->route_table->hook_remove;
  oa->route_table->hook_add = NULL;
  oa->route_table->hook_remove = NULL;

  numchange = __ospf6_intra_prefix_lsa_remove (old, __ospf6_route_remove_mark);
  numchange += __ospf6_intra_prefix_lsa_add (new);

  oa->route_table->hook_add = hook_add;
  oa->route_table->hook_remove = hook_remove;

  if (numchange > 0)
    __ospf6_intra_process_route_table (oa->route_table);
}

/**
 * Install connected routes for interfaces associated with an area
 *
 * This directly installs routes to prefixes associated with all OSPF
 * interfaces for the given area.  Connected routes are used instead
 * of self-originated Intra-Area-Prefix-LSAs to simplify using the
 * appropriate interface as the nexthop.
 *
 * @param oa The area to consider.
 */
static void
ospf6_intra_route_calculation_connected (struct ospf6_area *oa)
{
  struct listnode *node;
  struct ospf6_interface *oi;

  for (ALL_LIST_ELEMENTS_RO (oa->if_list, node, oi))
    {
      struct ospf6_route *route;

      if (oi->state < OSPF6_INTERFACE_LOOPBACK)
	{
	  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
	    zlog_debug ("Ignoring connected routes for non-active "
			"interface %s", oi->interface->name);
	  continue;
	}

      if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
	{
	  zlog_debug ("Examining connected routes for interface %s",
		      oi->interface->name);
	}

      for (route = ospf6_route_head (oi->route_connected); route;
	   route = ospf6_route_next (route))
	{
	  struct ospf6_route *copy;

	  if (ospf6_af_validate_prefix (oa->ospf6, &route->prefix.u.prefix6,
					route->prefix.prefixlen, false))
	    {
	      if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
		{
		  char buf[PREFIXSTRLEN];
		  ospf6_prefix2str (oa->ospf6, &route->prefix,
				    buf, sizeof (buf));
		  zlog_debug ("Ignoring connected prefix %s for interface %s",
			      buf, oi->interface->name);
		}
	      continue;
	    }

	  copy = ospf6_route_copy (route);
	  apply_mask (&copy->prefix);

	  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
	    {
	      char buf[PREFIXSTRLEN];
	      ospf6_prefix2str (oa->ospf6, &copy->prefix, buf, sizeof (buf));
	      zlog_debug ("Adding route to connected prefix %s "
			  "for interface %s", buf, oi->interface->name);
	    }

	  ospf6_route_add (copy, oa->route_table);
	}
    }
}

/**
 * Install IPv4 "link local address" routes for each neighbor
 *
 * @param oa The area to consider.
 */
static void
ospf6_intra_route_calculation_link (struct ospf6_area *oa)
{
  struct listnode *n;
  struct ospf6_interface *oi;

  if (!ospf6_af_is_ipv4 (oa->ospf6))
    return;

  for (ALL_LIST_ELEMENTS_RO (oa->if_list, n, oi))
    {
      struct listnode *m;
      struct ospf6_neighbor *on;

      if (oi->type != OSPF6_IFTYPE_POINTOPOINT &&
	  oi->type != OSPF6_IFTYPE_POINTOMULTIPOINT &&
	  oi->type != OSPF6_IFTYPE_MDR)
	continue;

      if (IS_OSPF6_DEBUG_EXAMIN (LINK))
	{
	  zlog_debug ("Examining link-local routes for interface %s",
		      oi->interface->name);
	}

      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, m, on))
	{
	  struct ospf6_lsa *lsa;
	  struct ospf6_link_lsa *link_lsa;
	  struct ospf6_route *route;

	  if (on->state < OSPF6_NEIGHBOR_TWOWAY)
	    continue;

	  lsa = ospf6_lsdb_lookup (htons (OSPF6_LSTYPE_LINK),
				   htonl (on->ifindex), on->router_id,
				   oi->lsdb);
	  if (lsa == NULL || OSPF6_LSA_IS_MAXAGE (lsa))
	    continue;

	  if (IS_OSPF6_DEBUG_EXAMIN (LINK))
	    zlog_debug ("%s found", lsa->name);

	  link_lsa = (struct ospf6_link_lsa *)
	    ((char *) lsa->header + sizeof (struct ospf6_lsa_header));

	  if (ospf6_af_validate_ipv4_unicast (&link_lsa->linklocal_addr))
	    {
	      if (IS_OSPF6_DEBUG_EXAMIN (LINK))
		{
		  char buf[INET6_ADDRSTRLEN];
		  ospf6_addr2str (oa->ospf6, &link_lsa->linklocal_addr,
				  buf, sizeof (buf));
		  zlog_debug ("Ignoring link-local address %s for "
			      "neighbor %s", buf, on->name);
		}
	      continue;
	    }

	  route = ospf6_route_create ();
	  route->type = OSPF6_DEST_TYPE_NETWORK;
	  route->prefix = (struct prefix) {
	    .family = AF_INET6,
	    .prefixlen = oa->ospf6->af_interop ? 32 : 128,
	    .u.prefix6 = link_lsa->linklocal_addr,
	  };
	  route->path.origin.type = lsa->header->type;
	  route->path.origin.id = lsa->header->id;
	  route->path.origin.adv_router = lsa->header->adv_router;
	  route->path.area_id = oa->area_id;
	  route->path.type = OSPF6_PATH_TYPE_LINK;
	  route->path.metric_type = 1;
	  route->path.cost = on->cost;
	  route->nexthop[0] = (struct ospf6_nexthop) {
	    .ifindex = oi->interface->ifindex,
	    .address = link_lsa->linklocal_addr,
	  };

	  if (IS_OSPF6_DEBUG_EXAMIN (LINK))
	    {
	      char buf[INET6_ADDRSTRLEN];
	      ospf6_addr2str (oa->ospf6, &link_lsa->linklocal_addr,
			      buf, sizeof (buf));
	      zlog_debug ("Adding link-local route to %s/32 for neighbor %s",
			  buf, on->name);
	    }

	  ospf6_route_add (route, oa->route_table);
	}
    }
}

void
ospf6_intra_route_calculation (struct ospf6_area *oa)
{
  struct ospf6_route *route;
  u_int16_t type;
  struct ospf6_lsa *lsa;
  void (*hook_add) (struct ospf6_route *) = NULL;
  void (*hook_remove) (struct ospf6_route *) = NULL;

  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
    zlog_debug ("Re-examin intra-routes for area %s", oa->name);

  hook_add = oa->route_table->hook_add;
  hook_remove = oa->route_table->hook_remove;
  oa->route_table->hook_add = NULL;
  oa->route_table->hook_remove = NULL;

  for (route = ospf6_route_head (oa->route_table); route;
       route = ospf6_route_next (route))
    {
      UNSET_FLAG (route->flag, OSPF6_ROUTE_ADD);
      UNSET_FLAG (route->flag, OSPF6_ROUTE_CHANGE);
      SET_FLAG (route->flag, OSPF6_ROUTE_REMOVE);
    }

  /* add routes for prefixes associated with all ospf interfaces */
  ospf6_intra_route_calculation_connected (oa);

  /* add link local address routes */
  if (ospf6_af_is_ipv4 (oa->ospf6))
    ospf6_intra_route_calculation_link (oa);

  type = htons (OSPF6_LSTYPE_INTRA_PREFIX);
  for (lsa = ospf6_lsdb_type_head (type, oa->lsdb); lsa;
       lsa = ospf6_lsdb_type_next (type, lsa))
    {
      /* routes advertised by this router were already added */
      if (lsa->header->adv_router == oa->ospf6->router_id)
	continue;

      ospf6_intra_prefix_lsa_add (lsa);
    }

  oa->route_table->hook_add = hook_add;
  oa->route_table->hook_remove = hook_remove;

  __ospf6_intra_process_route_table (oa->route_table);

  if (IS_OSPF6_DEBUG_EXAMIN (INTRA_PREFIX))
    zlog_debug ("Re-examin intra-routes for area %s: Done", oa->name);
}

static void
ospf6_brouter_debug_print (struct ospf6_route *brouter)
{
  u_int32_t brouter_id;
  char brouter_name[16];
  char area_name[16];
  char destination[64];
  char installed[16], changed[16];
  struct timeval now, res;
  char id[16], adv_router[16];
  char capa[16], options[OSPF6OPTSTRLEN];

  brouter_id = ospf6_adv_router_in_prefix (&brouter->prefix);
  ospf6_id2str (brouter_id, brouter_name, sizeof (brouter_name));
  ospf6_id2str (brouter->path.area_id, area_name, sizeof (area_name));
  ospf6_linkstate_prefix2str (&brouter->prefix, destination,
                              sizeof (destination));

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);
  timersub (&now, &brouter->installed, &res);
  timerstring (&res, installed, sizeof (installed));

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);
  timersub (&now, &brouter->changed, &res);
  timerstring (&res, changed, sizeof (changed));

  ospf6_id2str (brouter->path.origin.id, id, sizeof (id));
  ospf6_id2str (brouter->path.origin.adv_router,
		adv_router, sizeof (adv_router));

  ospf6_options_printbuf (brouter->path.options, options, sizeof (options));
  ospf6_capability_printbuf (brouter->path.router_bits, capa, sizeof (capa));

  zlog_info ("Brouter: %s via area %s", brouter_name, area_name);
  zlog_info ("  memory: prev: %p this: %p next: %p parent rnode: %p",
             brouter->prev, brouter, brouter->next, brouter->rnode);
  zlog_info ("  type: %d prefix: %s installed: %s changed: %s",
             brouter->type, destination, installed, changed);
  zlog_info ("  lock: %d flags: %s%s%s%s", brouter->lock,
           (CHECK_FLAG (brouter->flag, OSPF6_ROUTE_BEST)   ? "B" : "-"),
           (CHECK_FLAG (brouter->flag, OSPF6_ROUTE_ADD)    ? "A" : "-"),
           (CHECK_FLAG (brouter->flag, OSPF6_ROUTE_REMOVE) ? "R" : "-"),
           (CHECK_FLAG (brouter->flag, OSPF6_ROUTE_CHANGE) ? "C" : "-"));
  zlog_info ("  path type: %s ls-origin %s id: %s adv-router %s",
             OSPF6_PATH_TYPE_NAME (brouter->path.type),
             ospf6_lstype_name (brouter->path.origin.type),
             id, adv_router);
  zlog_info ("  options: %s router-bits: %s metric-type: %d metric: %d/%d",
             options, capa, brouter->path.metric_type,
             brouter->path.cost, brouter->path.cost_e2);
}

void
ospf6_intra_brouter_calculation (struct ospf6_area *oa)
{
  struct ospf6_route *brouter, *copy;
  u_int16_t type;
  struct ospf6_lsa *lsa;
  void (*hook_add) (struct ospf6_route *) = NULL;
  void (*hook_remove) (struct ospf6_route *) = NULL;
  u_int32_t brouter_id;
  char brouter_name[16];
  
  if (IS_OSPF6_DEBUG_BROUTER_SPECIFIC_AREA_ID (oa->area_id))
    zlog_info ("border-router calculation for area %s", oa->name);
  
  hook_add = oa->ospf6->brouter_table->hook_add;
  hook_remove = oa->ospf6->brouter_table->hook_remove;
  oa->ospf6->brouter_table->hook_add = NULL;
  oa->ospf6->brouter_table->hook_remove = NULL;

  /* withdraw the previous router entries for the area */
  for (brouter = ospf6_route_head (oa->ospf6->brouter_table); brouter;
       brouter = ospf6_route_next (brouter))
    {
      brouter_id = ospf6_adv_router_in_prefix (&brouter->prefix);
      ospf6_id2str (brouter_id, brouter_name, sizeof (brouter_name));
      if (brouter->path.area_id != oa->area_id)
        continue;

      UNSET_FLAG (brouter->flag, OSPF6_ROUTE_ADD);
      UNSET_FLAG (brouter->flag, OSPF6_ROUTE_CHANGE);
      SET_FLAG (brouter->flag, OSPF6_ROUTE_REMOVE);

      if (IS_OSPF6_DEBUG_BROUTER_SPECIFIC_ROUTER_ID (brouter_id) ||
          IS_OSPF6_DEBUG_ROUTE (MEMORY))
        {
          zlog_info ("%p: mark as removing: area %s brouter %s",
                     brouter, oa->name, brouter_name);
          ospf6_brouter_debug_print (brouter);
        }
    }

  /* add area border routers */
  for (brouter = ospf6_route_head (oa->spf_table); brouter;
       brouter = ospf6_route_next (brouter))
    {
      brouter_id = ospf6_adv_router_in_prefix (&brouter->prefix);
      ospf6_id2str (brouter_id, brouter_name, sizeof (brouter_name));

      if (brouter->type != OSPF6_DEST_TYPE_LINKSTATE)
        continue;
      if (ospf6_linkstate_prefix_id (&brouter->prefix) != htonl (0))
        continue;
      if (! CHECK_FLAG (brouter->path.router_bits, OSPF6_ROUTER_BIT_E) &&
          ! CHECK_FLAG (brouter->path.router_bits, OSPF6_ROUTER_BIT_B))
        continue;

      copy = ospf6_route_copy (brouter);
      copy->type = OSPF6_DEST_TYPE_ROUTER;
      copy->path.area_id = oa->area_id;
      ospf6_route_add (copy, oa->ospf6->brouter_table);

      if (IS_OSPF6_DEBUG_BROUTER_SPECIFIC_ROUTER_ID (brouter_id) ||
          IS_OSPF6_DEBUG_ROUTE (MEMORY))
        {
          zlog_info ("%p: transfer: area %s brouter %s",
                     brouter, oa->name, brouter_name);
          ospf6_brouter_debug_print (brouter);
        }
    }

  /* add AS boundary routers */
  type = htons (OSPF6_LSTYPE_INTER_ROUTER);
  for (lsa = ospf6_lsdb_type_head (type, oa->lsdb); lsa;
       lsa = ospf6_lsdb_type_next (type, lsa))
    {
      ospf6_abr_examin_summary (lsa, oa);
    }

  oa->ospf6->brouter_table->hook_add = hook_add;
  oa->ospf6->brouter_table->hook_remove = hook_remove;

  for (brouter = ospf6_route_head (oa->ospf6->brouter_table); brouter;
       brouter = ospf6_route_next (brouter))
    {
      if (CHECK_FLAG (brouter->flag, OSPF6_ROUTE_WAS_REMOVED))
        continue;

      brouter_id = ospf6_adv_router_in_prefix (&brouter->prefix);
      ospf6_id2str (brouter_id, brouter_name, sizeof (brouter_name));
      
      if (brouter->path.area_id != oa->area_id)
        continue;

      if (CHECK_FLAG (brouter->flag, OSPF6_ROUTE_REMOVE) &&
          CHECK_FLAG (brouter->flag, OSPF6_ROUTE_ADD))
        {
          UNSET_FLAG (brouter->flag, OSPF6_ROUTE_REMOVE);
          UNSET_FLAG (brouter->flag, OSPF6_ROUTE_ADD);
        }

      if (CHECK_FLAG (brouter->flag, OSPF6_ROUTE_REMOVE))
        {
          if (IS_OSPF6_DEBUG_BROUTER ||
              IS_OSPF6_DEBUG_BROUTER_SPECIFIC_ROUTER_ID (brouter_id) ||
              IS_OSPF6_DEBUG_BROUTER_SPECIFIC_AREA_ID (oa->area_id))
            zlog_info ("brouter %s disappears via area %s",
                       brouter_name, oa->name);
          ospf6_route_remove (brouter, oa->ospf6->brouter_table);
          UNSET_FLAG (brouter->flag, OSPF6_ROUTE_REMOVE);
        }
      else if (CHECK_FLAG (brouter->flag, OSPF6_ROUTE_ADD) ||
               CHECK_FLAG (brouter->flag, OSPF6_ROUTE_CHANGE))
        {
          if (IS_OSPF6_DEBUG_BROUTER ||
              IS_OSPF6_DEBUG_BROUTER_SPECIFIC_ROUTER_ID (brouter_id) ||
              IS_OSPF6_DEBUG_BROUTER_SPECIFIC_AREA_ID (oa->area_id))
            zlog_info ("brouter %s appears via area %s",
                       brouter_name, oa->name);

          /* newly added */
          if (hook_add)
            (*hook_add) (brouter);
	  UNSET_FLAG (brouter->flag, OSPF6_ROUTE_ADD);
	  UNSET_FLAG (brouter->flag, OSPF6_ROUTE_CHANGE);
        }
      else
        {
          if (IS_OSPF6_DEBUG_BROUTER_SPECIFIC_ROUTER_ID (brouter_id) ||
              IS_OSPF6_DEBUG_BROUTER_SPECIFIC_AREA_ID (oa->area_id))
            zlog_info ("brouter %s still exists via area %s",
                       brouter_name, oa->name);
        }
    }

  if (IS_OSPF6_DEBUG_BROUTER_SPECIFIC_AREA_ID (oa->area_id))
    zlog_info ("border-router calculation for area %s: done", oa->name);
}

struct ospf6_lsa_handler router_handler =
{
  OSPF6_LSTYPE_ROUTER,
  "Router",
  ospf6_router_lsa_show
};

struct ospf6_lsa_handler network_handler =
{
  OSPF6_LSTYPE_NETWORK,
  "Network",
  ospf6_network_lsa_show
};

struct ospf6_lsa_handler link_handler =
{
  OSPF6_LSTYPE_LINK,
  "Link",
  ospf6_link_lsa_show
};

struct ospf6_lsa_handler intra_prefix_handler =
{
  OSPF6_LSTYPE_INTRA_PREFIX,
  "Intra-Prefix",
  ospf6_intra_prefix_lsa_show
};

void
ospf6_intra_init (void)
{
  ospf6_install_lsa_handler (&router_handler);
  ospf6_install_lsa_handler (&network_handler);
  ospf6_install_lsa_handler (&link_handler);
  ospf6_install_lsa_handler (&intra_prefix_handler);
}

DEFUN (debug_ospf6_brouter,
       debug_ospf6_brouter_cmd,
       "debug ospf6 border-routers",
       DEBUG_STR
       OSPF6_STR
       "Debug border router\n"
      )
{
  OSPF6_DEBUG_BROUTER_ON ();
  return CMD_SUCCESS;
}

DEFUN (no_debug_ospf6_brouter,
       no_debug_ospf6_brouter_cmd,
       "no debug ospf6 border-routers",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Debug border router\n"
      )
{
  OSPF6_DEBUG_BROUTER_OFF ();
  return CMD_SUCCESS;
}

DEFUN (debug_ospf6_brouter_router,
       debug_ospf6_brouter_router_cmd,
       "debug ospf6 border-routers router-id A.B.C.D",
       DEBUG_STR
       OSPF6_STR
       "Debug border router\n"
       "Debug specific border router\n"
       "Specify border-router's router-id\n"
      )
{
  u_int32_t router_id;
  ospf6_str2id (argv[0], &router_id);
  OSPF6_DEBUG_BROUTER_SPECIFIC_ROUTER_ON (router_id);
  return CMD_SUCCESS;
}

DEFUN (no_debug_ospf6_brouter_router,
       no_debug_ospf6_brouter_router_cmd,
       "no debug ospf6 border-routers router-id",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Debug border router\n"
       "Debug specific border router\n"
      )
{
  OSPF6_DEBUG_BROUTER_SPECIFIC_ROUTER_OFF ();
  return CMD_SUCCESS;
}

DEFUN (debug_ospf6_brouter_area,
       debug_ospf6_brouter_area_cmd,
       "debug ospf6 border-routers area-id (A.B.C.D|<0-4294967295>)",
       DEBUG_STR
       OSPF6_STR
       "Debug border router\n"
       "Debug border routers in specific Area\n"
       OSPF6_AREAID_DOT_STR
       OSPF6_AREAID_VAL_STR
      )
{
  u_int32_t area_id;
  ospf6_str2id (argv[0], &area_id);
  OSPF6_DEBUG_BROUTER_SPECIFIC_AREA_ON (area_id);
  return CMD_SUCCESS;
}

DEFUN (no_debug_ospf6_brouter_area,
       no_debug_ospf6_brouter_area_cmd,
       "no debug ospf6 border-routers area-id",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Debug border router\n"
       "Debug border routers in specific Area\n"
      )
{
  OSPF6_DEBUG_BROUTER_SPECIFIC_AREA_OFF ();
  return CMD_SUCCESS;
}

int
config_write_ospf6_debug_brouter (struct vty *vty)
{
  char buf[16];
  if (IS_OSPF6_DEBUG_BROUTER)
    vty_out (vty, "debug ospf6 border-routers%s", VNL);
  if (IS_OSPF6_DEBUG_BROUTER_SPECIFIC_ROUTER)
    {
      ospf6_id2str (conf_debug_ospf6_brouter_specific_router_id,
		    buf, sizeof (buf));
      vty_out (vty, "debug ospf6 border-routers router-id %s%s", buf, VNL);
    }
  if (IS_OSPF6_DEBUG_BROUTER_SPECIFIC_AREA)
    {
      ospf6_id2str (conf_debug_ospf6_brouter_specific_area_id,
		    buf, sizeof (buf));
      vty_out (vty, "debug ospf6 border-routers area-id %s%s", buf, VNL);
    }
  return 0;
}

void
install_element_ospf6_debug_brouter (void)
{
  install_element (ENABLE_NODE, &debug_ospf6_brouter_cmd);
  install_element (ENABLE_NODE, &debug_ospf6_brouter_router_cmd);
  install_element (ENABLE_NODE, &debug_ospf6_brouter_area_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_brouter_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_brouter_router_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_brouter_area_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_brouter_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_brouter_router_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_brouter_area_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_brouter_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_brouter_router_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_brouter_area_cmd);
}


