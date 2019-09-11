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

#include <stdbool.h>

#include "zebra.h"
#include "thread.h"
#include "log.h"

#include "ospf6d.h"
#include "ospf6_af.h"
#include "ospf6_lls.h"
#include "ospf6_message.h"
#include "ospf6_interface.h"
#include "ospf6_neighbor.h"
#include "ospf6_proto.h"
#include "ospf6_area.h"
#include "ospf6_network.h"
#include "ospf6_mdr.h"
#include "ospf6_mdr_message.h"

static uint16_t ospf6_mdr_tlv_type_hello = OSPF6_MDR_TLV_TYPE_HELLO;
static uint16_t ospf6_mdr_tlv_type_dd = OSPF6_MDR_TLV_TYPE_DD;

void
ospf6_mdr_tlv_set_interoperability (struct ospf6 *o, bool val)
{
  if (val)
    {
      ospf6_mdr_tlv_type_hello = OSPF6_MDR_TLV_TYPE_HELLO;
      ospf6_mdr_tlv_type_dd = OSPF6_MDR_TLV_TYPE_DD;
    }
  else
    {
      ospf6_mdr_tlv_type_hello = OSPF6_MDR_TLV_TYPE_HELLO_DRAFT;
      ospf6_mdr_tlv_type_dd = OSPF6_MDR_TLV_TYPE_DD_DRAFT;
    }

  o->mdr_tlv_interop = val;
}

static bool
ospf6_is_rtrid_in_list (struct ospf6_interface *oi,
			uint32_t *router_id, int num)
{
  int i;
  bool found = false;
  for (i = 0; i < num; i++)
    {
      if (*router_id == oi->area->ospf6->router_id)
        {
          found = true;
          break;
        }
      router_id++;
    }
  return found;
}

static bool
ospf6_mdr_process_neighbor_lists (struct ospf6_neighbor *on,
				  uint32_t *rid, int num_lnl,
				  int num_hnl, int num_dnl, int num_sanl,
				  int num_rnl, bool diff, int hsn)
{
  struct ospf6_interface *oi = on->ospf6_if;
  bool twoway = false;
  bool insufficienthellosreceived = false;
  uint16_t prev_seq = 0;
  uint32_t *lnl = rid;     // List 1
  uint32_t *hnl = lnl + num_lnl;       // List 2
  uint32_t *dnl = hnl + num_hnl;       // List 3
  uint32_t *sanl = dnl + num_dnl;      // List 4
  uint32_t *rnl = sanl + num_sanl;     // List 5
  int i;

  prev_seq = on->mdr.hsn;
  on->mdr.hsn = hsn;
  if (hsn == ((prev_seq + 1) & 0xffff))    // modulo 2^16
    on->mdr.consec_hellos++;
  else
    on->mdr.consec_hellos = 1;

  //differential hello
  if (diff)
    {
      bool found = false;

      if (on->state > OSPF6_NEIGHBOR_DOWN &&
          on->mdr.hsn > prev_seq + oi->mdr.HelloRepeatCount)
        insufficienthellosreceived = true;

      //check hello LNL (list type 1)
      for (i = 0; i < num_lnl; i++)
        {
          if (lnl[i] == oi->area->ospf6->router_id)
            {
              twoway = false;
              found = true;
              // Neighbor does not consider me to be 2-way.
              on->mdr.reverse_2way = false;
            }
          ospf6_mdr_delete_neighbor (on->mdr.rnl, lnl[i]);
          ospf6_mdr_delete_neighbor (on->mdr.dnl, lnl[i]);
          ospf6_mdr_delete_neighbor (on->mdr.sanl, lnl[i]);
        }
      //check hello HNL (list type 2)
      for (i = 0; i < num_hnl; i++)
        {
          if (hnl[i] == oi->area->ospf6->router_id)
            {
              twoway = true;
              found = true;
              // Neighbor does not consider me to be 2-way.
              on->mdr.reverse_2way = false;
            }
          // Remove from any neighbor list to which the neighbor belongs
          ospf6_mdr_delete_neighbor (on->mdr.rnl, hnl[i]);
          ospf6_mdr_delete_neighbor (on->mdr.dnl, hnl[i]);
          ospf6_mdr_delete_neighbor (on->mdr.sanl, hnl[i]);
        }
      //check hello DNL (list type 3)
      for (i = 0; i < num_dnl; i++)
        {
          if (dnl[i] == oi->area->ospf6->router_id)
            {
              twoway = true;
              found = true;
              on->mdr.reverse_2way = true;
            }
          // Add to both DNL and RNL
          if (!ospf6_mdr_lookup_neighbor (on->mdr.dnl, dnl[i]))
            ospf6_mdr_add_neighbor (on->mdr.dnl, dnl[i]);
          if (!ospf6_mdr_lookup_neighbor (on->mdr.rnl, dnl[i]))
            ospf6_mdr_add_neighbor (on->mdr.rnl, dnl[i]);
          // Remove from SANL if it belongs
          ospf6_mdr_delete_neighbor (on->mdr.sanl, dnl[i]);
        }
      //check hello SANL (list type 4)
      for (i = 0; i < num_sanl; i++)
        {
          if (sanl[i] == oi->area->ospf6->router_id)
            {
              twoway = true;
              found = true;
              on->mdr.reverse_2way = true;
            }
          // Add to both SANL and RNL
          if (!ospf6_mdr_lookup_neighbor (on->mdr.sanl, sanl[i]))
            ospf6_mdr_add_neighbor (on->mdr.sanl, sanl[i]);
          if (!ospf6_mdr_lookup_neighbor (on->mdr.rnl, sanl[i]))
            ospf6_mdr_add_neighbor (on->mdr.rnl, sanl[i]);
          // Remove from DNL if it belongs
          ospf6_mdr_delete_neighbor (on->mdr.dnl, sanl[i]);
        }
      //check hello RNL (list type 5)
      for (i = 0; i < num_rnl; i++)
        {
          if (rnl[i] == oi->area->ospf6->router_id)
            {
              twoway = true;
              found = true;
              on->mdr.reverse_2way = true;
            }
          // Add to RNL
          if (!ospf6_mdr_lookup_neighbor (on->mdr.rnl, rnl[i]))
            ospf6_mdr_add_neighbor (on->mdr.rnl, rnl[i]);
          // Remove from DNL and SANL if it belongs
          ospf6_mdr_delete_neighbor (on->mdr.dnl, rnl[i]);
          ospf6_mdr_delete_neighbor (on->mdr.sanl, rnl[i]);
        }

      //keep same state - not found in any list
      // Insufficient hellos implies oneway if router does not find itself.
      if (!found && on->state >= OSPF6_NEIGHBOR_TWOWAY &&
          !insufficienthellosreceived)
        {
          twoway = true;
        }

      return twoway;
    }

  // not a differential hello
  on->mdr.Report2Hop = true;        // full hello received

  if (ospf6_is_rtrid_in_list (oi, hnl, num_hnl))        // List 2
    {
      twoway = true;
      on->mdr.reverse_2way = false;
    }
  else if (ospf6_is_rtrid_in_list (oi, dnl, num_dnl) || // List 3
           ospf6_is_rtrid_in_list (oi, sanl, num_sanl) ||       // List 4
           ospf6_is_rtrid_in_list (oi, rnl, num_rnl))   // List 5
    {
      twoway = true;
      on->mdr.reverse_2way = true;
    }
  else                          // not in any list of full hello
    {
      twoway = false;
      on->mdr.reverse_2way = false;
    }

  // Since this is a full hello, clear all 3 neighbor lists.
  ospf6_mdr_delete_all_neighbors (on->mdr.rnl);
  ospf6_mdr_delete_all_neighbors (on->mdr.dnl);
  ospf6_mdr_delete_all_neighbors (on->mdr.sanl);
  // on->rnl is the list of bidirectional neighbors, which
  // is the union of lists 3, 4, and 5.

  //check hello DNL (list type 3)
  for (i = 0; i < num_dnl; i++)
    {
      // Add to both DNL and RNL
      if (!ospf6_mdr_lookup_neighbor (on->mdr.dnl, dnl[i]))
        ospf6_mdr_add_neighbor (on->mdr.dnl, dnl[i]);
      if (!ospf6_mdr_lookup_neighbor (on->mdr.rnl, dnl[i]))
        ospf6_mdr_add_neighbor (on->mdr.rnl, dnl[i]);
    }
  //check hello SANL (list type 4)
  for (i = 0; i < num_sanl; i++)
    {
      // Add to both SANL and RNL
      if (!ospf6_mdr_lookup_neighbor (on->mdr.sanl, sanl[i]))
        ospf6_mdr_add_neighbor (on->mdr.sanl, sanl[i]);
      if (!ospf6_mdr_lookup_neighbor (on->mdr.rnl, sanl[i]))
        ospf6_mdr_add_neighbor (on->mdr.rnl, sanl[i]);
    }
  //check hello RNL (list type 5)
  for (i = 0; i < num_rnl; i++)
    {
      // Add to RNL
      if (!ospf6_mdr_lookup_neighbor (on->mdr.rnl, rnl[i]))
        ospf6_mdr_add_neighbor (on->mdr.rnl, rnl[i]);
    }
  return twoway;
}

static int
ospf6_mdr_process_hello_tlv (struct ospf6_neighbor *on,
			     struct ospf6_lls_header *lls,
			     int *n1, int *n2, int *n3, int *n4,
			     bool *diff, int *hsn)
{
  int length_lls;
  struct ospf6_tlv_header *tlv_header = NULL;
  struct ospf6_mdr_hello_tlv *hello_tlv = NULL;
  int err = 0;

  *n1 = *n2 = *n3 = *n4 = 0;

  length_lls =
    ((int) ntohs (lls->datalen) << 2) - sizeof (struct ospf6_lls_header);
  tlv_header = (struct ospf6_tlv_header *) (lls + 1);
  while (length_lls > (ssize_t) sizeof (*tlv_header))
    {
      int tlvlen;

      tlvlen = sizeof (*tlv_header) + ntohs (tlv_header->vallen);
      if (tlvlen > length_lls)
	{
	  zlog_warn ("%s: inconsistent tlv: tlv length %d exceeds remaining "
		     "lls length %d", __func__, tlvlen, length_lls);
	  break;
	}

      if (ntohs (tlv_header->type) == ospf6_mdr_tlv_type_hello)
        {
	  assert (!hello_tlv);        //Malformed packet: 2 option TLVs
	  assert (ntohs (tlv_header->vallen) ==
		  sizeof (struct ospf6_mdr_hello_tlv));
	  hello_tlv = (struct ospf6_mdr_hello_tlv *) (tlv_header + 1);
	}

      tlv_header =
	(struct ospf6_tlv_header *) ((char *) tlv_header + tlvlen);
      length_lls -= tlvlen;
    }

  if (hello_tlv)
    {
      *hsn = ntohs (hello_tlv->hsn);
      if (OSPF6_MDR_OPT_ISSET (hello_tlv->bits, OSPF6_MDR_OPT_D, 0))
        *diff = true;
      else
        *diff = false;
      if (OSPF6_MDR_OPT_ISSET (hello_tlv->bits, OSPF6_MDR_OPT_A, 0))
        on->mdr.Abit = true;
      else
        on->mdr.Abit = false;
      *n1 = hello_tlv->n1;
      *n2 = hello_tlv->n2;
      *n3 = hello_tlv->n3;
      *n4 = hello_tlv->n4;
    }
  else
    {
      zlog_err ("%s: Error: MDR Hello packet must contain hello TLV",
		__func__);
      err = -1;
    }

  return err;
}

void
ospf6_mdr_hello_recv (struct ospf6_neighbor *on, struct ospf6_header *oh,
		      struct ospf6_lls_header *lls)
{
  struct ospf6_hello *hello;
  bool twoway = false;
  int n1, n2, n3, n4, n5;
  bool diff;
  int hsn;
  uint32_t *rid;

  hello = (struct ospf6_hello *)
    ((caddr_t) oh + sizeof (struct ospf6_header));

  if (!OSPF6_OPT_ISSET (hello->options, OSPF6_OPT_L, 1))
    {
      if (IS_OSPF6_DEBUG_MESSAGE (oh->type, RECV))
	zlog_debug ("%s: L-Bit not set in MDR Hello packet", __func__);
      return;
    }

  assert (lls);

  /* process Hello TLV */

  // Determine number of neighbor IDs in hello
  if (ospf6_mdr_process_hello_tlv (on, lls, &n1, &n2, &n3, &n4, &diff, &hsn))
    return;
  n5 = ((ntohs (oh->length) - sizeof (struct ospf6_header) -
	 sizeof (struct ospf6_hello)) / 4) - n1 - n2 - n3 - n4;
  if (n5 < 0)
    {
      zlog_warn ("%s: invalid MDR neighbor list numbers", __func__);
      return;
    }

  // Set pointer to beginning of neighbor lists
  rid = (uint32_t *) (hello + 1);

  twoway = ospf6_mdr_process_neighbor_lists (on, rid, n1, n2, n3, n4, n5,
					     diff, hsn);

  if (ospf6_mdr_lookup_neighbor (on->mdr.dnl, ospf6->router_id))
    on->mdr.dependent_selector = 1;
  else
    on->mdr.dependent_selector = 0;

  on->priority = hello->priority;
  on->drouter = hello->drouter;
  on->bdrouter = hello->bdrouter;

  ospf6_mdr_set_mdr_level (on, on->drouter, on->bdrouter);

  /* execute neighbor events */
  thread_execute (master, hello_received, on, 0);
  if (twoway)
    thread_execute (master, twoway_received, on, 0);
  else
    thread_execute (master, oneway_received, on, 0);

  // Check if adjacency should be formed with this neighbor.
  if (on->state == OSPF6_NEIGHBOR_TWOWAY && need_adjacency (on))
    ospf6_neighbor_exstart (on);
}

static int
ospf6_mdr_hello_list_type (struct ospf6_neighbor *on)
{
  // Return hello list type (1 to 5) for this neighbor.
  // Check that neighbor is not both dependent and sel_adv.
  if (on->mdr.dependent && on->mdr.sel_adv)
    zlog_err ("Error: sel_adv should be 0 for dependent neighbor");

  if (on->state == OSPF6_NEIGHBOR_DOWN)
    return 1;
  if (on->state == OSPF6_NEIGHBOR_INIT)
    return 2;
  if (on->mdr.dependent)
    return 3;
  if (on->mdr.sel_adv)
    return 4;
  return 5;                     // Other bidirectional
}

static uint
ospf6_mdr_create_neighbor_lists (struct ospf6_interface *oi,
                                 u_int *num_hnl, u_char *hnl,
                                 u_int *num_rnl, u_char *rnl,
                                 u_int *num_lnl, u_char *lnl,
                                 u_int *num_dnl, u_char *dnl,
                                 u_int *num_sanl, u_char *sanl,
                                 bool diff)
{
  struct listnode *node, *nnode;
  struct ospf6_neighbor *on;
  int size_rid = sizeof (uint32_t);
  int new_list_type;
  int num_rids;

  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
    {
      new_list_type = ospf6_mdr_hello_list_type (on);
      if (new_list_type != on->mdr.list_type)
        on->mdr.changed_hsn = oi->mdr.hsn;
      on->mdr.list_type = new_list_type;

      if (diff && oi->mdr.hsn >= on->mdr.changed_hsn + oi->mdr.HelloRepeatCount &&
          !(on->state >= OSPF6_NEIGHBOR_TWOWAY && !on->mdr.reverse_2way))
        continue;               // neighbor not included in hello

      if (on->mdr.list_type == 2)   // state is INIT
        {
          memcpy (hnl, &on->router_id, size_rid);
          hnl += size_rid;
          (*num_hnl)++;
        }
      if (on->mdr.list_type == 3)   // neighbor is dependent (in DNL)
        {
          memcpy (dnl, &on->router_id, size_rid);
          dnl += size_rid;
          (*num_dnl)++;
        }
      if (on->mdr.list_type == 4)   // neighbor is in SANL
        {
          memcpy (sanl, &on->router_id, size_rid);
          sanl += size_rid;
          (*num_sanl)++;
        }
      if (on->mdr.list_type == 5)   // bidirectional, not in DNL or SANL
        {
          memcpy (rnl, &on->router_id, size_rid);
          rnl += size_rid;
          (*num_rnl)++;
        }
    }

  if (oi->mdr.lnl && diff)
    {
      struct ospf6_lnl_element *lnl_element;
      for (ALL_LIST_ELEMENTS (oi->mdr.lnl, node, nnode, lnl_element))
        {
          if (lnl_element->hsn + oi->mdr.HelloRepeatCount <= oi->mdr.hsn)
            {
              ospf6_mdr_delete_lnl_element (oi, lnl_element);
              continue;
            }
          memcpy (lnl, &lnl_element->id, size_rid);
          lnl += size_rid;
          (*num_lnl)++;
        }
    }

  num_rids = *num_lnl + *num_hnl + *num_dnl + *num_sanl + *num_rnl;
  return size_rid * num_rids;
}

static size_t
ospf6_mdr_append_hello_tlv (struct ospf6_interface *oi, void *buf,
                            int n1, int n2, int n3, int n4, bool diff)
{
  struct ospf6_tlv_header *tlv_header = buf;
  struct ospf6_mdr_hello_tlv *hello_tlv =
    (struct ospf6_mdr_hello_tlv *) (tlv_header + 1);

  /* XXX should check that buf has enough room */

  if (n1 > 255 || n2 > 255 || n3 > 255 || n4 > 255)
    zlog_err ("Error: neighbor list has more than 255 IDs");

  tlv_header->type = htons (ospf6_mdr_tlv_type_hello);
  tlv_header->vallen = htons (sizeof (struct ospf6_mdr_hello_tlv));

  hello_tlv->hsn = htons (oi->mdr.hsn++);
  OSPF6_MDR_OPT_CLEAR_ALL (hello_tlv->bits);
  if (oi->mdr.AdjConnectivity == 0)
    OSPF6_MDR_OPT_SET (hello_tlv->bits, OSPF6_MDR_OPT_A, 0);
  if (diff)
    OSPF6_MDR_OPT_SET (hello_tlv->bits, OSPF6_MDR_OPT_D, 0);
  hello_tlv->n1 = n1;
  hello_tlv->n2 = n2;
  hello_tlv->n3 = n3;
  hello_tlv->n4 = n4;

  return (char *) (hello_tlv + 1) - (char *) buf;
}

// Ogierv3 10.1
int
ospf6_mdr_hello_send (struct ospf6_interface *oi,
		      void *sendbuf, size_t iobuflen)
{
  int length = 0;
  u_int lls_length = 0;
  struct ospf6_header *oh;
  struct ospf6_hello *hello;
  char *pos;
  struct ospf6_lls_header *lls;
  u_char hnl[iobuflen], rnl[iobuflen], lnl[iobuflen];
  u_int num_hnl = 0, num_rnl = 0, num_lnl = 0;
  u_int num_dnl = 0, num_sanl = 0;
  u_char dnl[iobuflen], sanl[iobuflen];
  bool diff = false;
  int size_rid = sizeof (uint32_t);

  // Calculate cds, and update adjacencies and LSA before sending Hello.
  ospf6_calculate_mdr (oi);
  ospf6_update_adjacencies (oi);
  ospf6_mdr_update_lsa (oi);

  memset (sendbuf, 0, iobuflen);
  oh = (struct ospf6_header *) sendbuf;
  oh->type = OSPF6_MESSAGE_TYPE_HELLO;

  hello =
    (struct ospf6_hello *) ((caddr_t) oh + sizeof (struct ospf6_header));
  hello->interface_id = htonl (oi->interface->ifindex);
  hello->priority = oi->priority;
  hello->options[0] = oi->area->options[0];
  hello->options[1] = oi->area->options[1];
  hello->options[2] = oi->area->options[2];
  hello->hello_interval = htons (oi->hello_interval);
  hello->dead_interval = htons (oi->dead_interval);

  // Set DR field
  if (oi->mdr.mdr_level == OSPF6_MDR)
    hello->drouter = oi->area->ospf6->router_id;
  else if (oi->mdr.parent)
    hello->drouter = oi->mdr.parent->router_id;
  else
    hello->drouter = 0;

  // Set BDR field
  if (oi->mdr.mdr_level == OSPF6_BMDR)
    hello->bdrouter = oi->area->ospf6->router_id;
  else if (oi->mdr.bparent)
    hello->bdrouter = oi->mdr.bparent->router_id;
  else
    hello->bdrouter = 0;

  pos = (char *) hello + sizeof (struct ospf6_hello);

  //Is this a Diff Hello
  if (oi->mdr.full_hello_count > 1)
    {
      oi->mdr.full_hello_count--;
      diff = true;
      // Set option bit in Hello TLV.
      //OSPF6_OPT_SET (hello->options, OSPF6_OPT_D, 1);
    }
  else
    oi->mdr.full_hello_count = oi->mdr.TwoHopRefresh;

  ospf6_mdr_create_neighbor_lists (oi, &num_hnl, hnl, &num_rnl, rnl,
                                   &num_lnl, lnl, &num_dnl, dnl,
                                   &num_sanl, sanl, diff);

  // Add neighbor lists to hello, in correct order.
  memcpy (pos, lnl, num_lnl * size_rid);        // List 1
  pos += num_lnl * size_rid;
  memcpy (pos, hnl, num_hnl * size_rid);        // List 2
  pos += num_hnl * size_rid;
  memcpy (pos, dnl, num_dnl * size_rid);        // List 3
  pos += num_dnl * size_rid;
  memcpy (pos, sanl, num_sanl * size_rid);      // List 4
  pos += num_sanl * size_rid;
  memcpy (pos, rnl, num_rnl * size_rid);        // List 5
  pos += num_rnl * size_rid;

  oh->length = htons (pos - (char *) sendbuf);

  //LLS will be sent
  OSPF6_OPT_SET (hello->options, OSPF6_OPT_L, 1);

  // Hello TLV is appended via LLS.
  // Metric TLV is omitted, implying all link metrics are 1.
  /* leave room for LLS header */
  lls = (struct ospf6_lls_header *) pos;
  pos += sizeof (struct ospf6_lls_header);

  //Hello TLV
  pos += ospf6_mdr_append_hello_tlv (oi, pos, num_lnl, num_hnl,
                                     num_dnl, num_sanl, diff);
  lls_length = sizeof (struct ospf6_lls_header) +
    sizeof (struct ospf6_tlv_header) + sizeof (struct ospf6_mdr_hello_tlv);
  //LLS header must be added here, so the checksum is computed correctly
  ospf6_set_lls_header (lls, lls_length);

  //Send hello
  length = pos - (char *) sendbuf;
  ospf6_send (oi->linklocal_addr, &allspfrouters6, oi, oh, length);
  ospf6_schedule_hello (oi);
  return 0;
}

size_t
ospf6_mdr_append_dd_tlv (struct ospf6_interface *oi, void *buf)
{
  struct ospf6_tlv_header *tlv_header = buf;
  struct ospf6_mdr_dd_tlv *dd_tlv = (void *)(tlv_header + 1);

  /* XXX should check that buf has enough room */

  tlv_header->type = htons (ospf6_mdr_tlv_type_dd);
  tlv_header->vallen = htons (sizeof (struct ospf6_mdr_dd_tlv));

  // Set DR field
  if (oi->mdr.mdr_level == OSPF6_MDR)
    dd_tlv->drouter = oi->area->ospf6->router_id;
  else if (oi->mdr.parent)
    dd_tlv->drouter = oi->mdr.parent->router_id;
  else
    dd_tlv->drouter = 0;

  // Set BDR field
  if (oi->mdr.mdr_level == OSPF6_BMDR)
    dd_tlv->bdrouter = oi->area->ospf6->router_id;
  else if (oi->mdr.bparent)
    dd_tlv->bdrouter = oi->mdr.bparent->router_id;
  else
    dd_tlv->bdrouter = 0;

  return (char *) (dd_tlv + 1) - (char *) buf;
}

// Returns true if on->mdr_level changed.
bool
ospf6_mdr_process_dd_tlv (struct ospf6_neighbor *on,
			  struct ospf6_lls_header *lls)
{
  int length_lls;
  struct ospf6_tlv_header *tlv_header = NULL;
  struct ospf6_mdr_dd_tlv *dd = NULL;
  bool mdr_level_changed = false;

  length_lls =
    ((int) ntohs (lls->datalen) << 2) - sizeof (struct ospf6_lls_header);
  tlv_header = (struct ospf6_tlv_header *) (lls + 1);
  while (length_lls > (ssize_t) sizeof (*tlv_header))
    {
      int tlvlen;

      tlvlen = sizeof (*tlv_header) + ntohs (tlv_header->vallen);
      if (tlvlen > length_lls)
	{
	  zlog_warn ("%s: inconsistent tlv: tlv length %d exceeds remaining "
		     "lls length %d", __func__, tlvlen, length_lls);
	  break;
	}

      if (ntohs (tlv_header->type) == ospf6_mdr_tlv_type_dd)
	{
	  assert (!dd);       //Malformed packet: 2 option TLVs
	  assert (ntohs (tlv_header->vallen) ==
		  sizeof (struct ospf6_mdr_dd_tlv));
	  dd = (struct ospf6_mdr_dd_tlv *) (tlv_header + 1);
	}

      tlv_header =
	(struct ospf6_tlv_header *) ((char *) tlv_header + tlvlen);
      length_lls -= tlvlen;
    }

  if (dd)
    {
      mdr_level_changed = ospf6_mdr_set_mdr_level (on, dd->drouter,
						   dd->bdrouter);
      if (on->mdr.mdr_level == OSPF6_MDR || on->mdr.mdr_level == OSPF6_BMDR)
        on->mdr.dependent_selector = 1;     // For need_adjacency().
    }

  return mdr_level_changed;
}

static void
print_tlv (struct ospf6_tlv_header *tlv_header)
{
  uint16_t type;

  zlog_info ("    TLV len:%d type:", ntohs (tlv_header->vallen));

  type = ntohs (tlv_header->type);
  if (type == ospf6_mdr_tlv_type_dd)
    {
      struct ospf6_mdr_dd_tlv *dd_tlv;
      char idstr[INET_ADDRSTRLEN];
      dd_tlv = (struct ospf6_mdr_dd_tlv *) (tlv_header + 1);
      ospf6_id2str (dd_tlv->drouter, idstr, sizeof (idstr));
      zlog_info ("     DD-DR %s", idstr);
      ospf6_id2str (dd_tlv->bdrouter, idstr, sizeof (idstr));
      zlog_info ("     DD-BDR %s", idstr);
    }
  else if (type == ospf6_mdr_tlv_type_hello)
    {
      struct ospf6_mdr_hello_tlv *hello_tlv;
      hello_tlv = (struct ospf6_mdr_hello_tlv *) (tlv_header + 1);
      zlog_info ("     HELLO-Seq #=%x", ntohs (hello_tlv->hsn));
      zlog_info ("     HELLO-A=%d D=%d",
		 OSPF6_MDR_OPT_ISSET (hello_tlv->bits, OSPF6_MDR_OPT_A, 0),
		 OSPF6_MDR_OPT_ISSET (hello_tlv->bits, OSPF6_MDR_OPT_D, 0));
      zlog_info ("     HELLO-n1=%d n2=%d n3=%d n4=%d", hello_tlv->n1,
		 hello_tlv->n2, hello_tlv->n3, hello_tlv->n4);
    }
  else
    {
      zlog_info ("     %d", ntohs (tlv_header->type));
    }
}

void
ospf6_mdr_hello_print (struct ospf6_header *oh, struct ospf6_lls_header *lls)
{
  struct ospf6_hello *hello;
  struct ospf6_tlv_header *tlv_header;
  int length_lls;

  ospf6_hello_print (oh);

  hello =
    (struct ospf6_hello *) ((char *) oh + sizeof (struct ospf6_header));

  /* process TLVs */
  /* set LLS pointer */
  if (!(OSPF6_OPT_ISSET (hello->options, OSPF6_OPT_L, 1)))
    return;			/* no LLS data */

  assert (lls);

  length_lls =
    ((int) ntohs (lls->datalen) << 2) - sizeof (struct ospf6_lls_header);
  tlv_header = (struct ospf6_tlv_header *) (lls + 1);
  while (length_lls > (ssize_t) sizeof (*tlv_header))
    {
      int tlvlen;

      tlvlen = sizeof (*tlv_header) + ntohs (tlv_header->vallen);
      if (tlvlen > length_lls)
	{
	  zlog_warn ("%s: inconsistent tlv: tlv length %d exceeds remaining "
		     "lls length %d", __func__, tlvlen, length_lls);
	  break;
	}

      print_tlv (tlv_header);

      length_lls -= tlvlen;
      tlv_header =
	(struct ospf6_tlv_header *) ((char *) tlv_header + tlvlen);
    }

  if (length_lls != 0)
    {
      zlog_warn ("%s: LLS/TLV length error", __func__);
    }
}
