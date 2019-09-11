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

#ifndef OSPF6_MDR_MESSAGE_H
#define OSPF6_MDR_MESSAGE_H

#include <stdint.h>
#include <stdbool.h>

/* TLV type */
#define OSPF6_TLV_TYPE_OPTIONS      0x1

#define OSPF6_MDR_TLV_TYPE_HELLO	14
#define OSPF6_MDR_TLV_TYPE_DD		15
/* old values (to be removed) */
#define OSPF6_MDR_TLV_TYPE_HELLO_DRAFT	0x11
#define OSPF6_MDR_TLV_TYPE_DD_DRAFT	0x12

#define OSPF6_MDR_OPT_A  (1 << 1)       // Indicates no adj reduction
#define OSPF6_MDR_OPT_D  (1 << 0)       // Indicates diff hello

#define OSPF6_MDR_OPT_SET(x,opt,i)   ((x)[(i)] |=  (opt))
#define OSPF6_MDR_OPT_ISSET(x,opt,i) ((x)[(i)] &   (opt))
#define OSPF6_MDR_OPT_CLEAR(x,opt,i) ((x)[(i)] &= ~(opt))
#define OSPF6_MDR_OPT_CLEAR_ALL(x) ((x)[0] = (x)[1] = 0)

/* OSPFv3 MDR Hello TLV */
struct ospf6_mdr_hello_tlv
{
  uint16_t hsn;                /* Hello sequence number */
  uint8_t bits[2];
  uint8_t n1;
  uint8_t n2;
  uint8_t n3;
  uint8_t n4;
};

/* OSPFv3 MDR DD TLV */
struct ospf6_mdr_dd_tlv
{
  uint32_t drouter;
  uint32_t bdrouter;
};

void ospf6_mdr_tlv_set_interoperability (struct ospf6 *o, bool val);

void ospf6_mdr_hello_recv (struct ospf6_neighbor *on, struct ospf6_header *oh,
			   struct ospf6_lls_header *lls);
int ospf6_mdr_hello_send (struct ospf6_interface *oi,
			  void *sendbuf, size_t iobuflen);
size_t ospf6_mdr_append_dd_tlv (struct ospf6_interface *oi, void *buf);
bool ospf6_mdr_process_dd_tlv (struct ospf6_neighbor *on,
			       struct ospf6_lls_header *lls);
void ospf6_mdr_hello_print (struct ospf6_header *oh,
			    struct ospf6_lls_header *lls);

#endif	/* OSPF6_MDR_MESSAGE_H */
