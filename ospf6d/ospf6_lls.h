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

#ifndef OSPF6_LLS_H
#define OSPF6_LLS_H

#include <stdint.h>

/* OSPFv3 LLS header */
struct ospf6_lls_header
{
  uint16_t cksum;
  uint16_t datalen; /* length of entire LLS data block in 32 bit words */
};

/* OSPFv3 TLV header */
struct ospf6_tlv_header
{
  uint16_t type;
  uint16_t vallen;		/* length of value field in bytes */
};

struct ospf6_header;

int ospf6_lls_option_isset (struct ospf6_header *oh);
void ospf6_lls_option_clear (struct ospf6_header *oh);
void ospf6_set_lls_header (struct ospf6_lls_header *lls, size_t len);
int ospf6_lls_validate_datablock (struct ospf6_lls_header *lls,
				  size_t len, int debug);

#endif	/* OSPF6_LLS_H */
