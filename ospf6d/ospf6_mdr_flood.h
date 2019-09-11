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

#ifndef OSPF6_MDR_FLOOD_H
#define OSPF6_MDR_FLOOD_H

struct ospf6_backupwait_neighbor
{
  /* Neighbor Router ID */
  u_int32_t router_id;

  /* Neighbor Interface ID */
  u_int32_t ifindex;
};

struct ospf6_neighbor;
struct ospf6_lsa;
struct ospf6_interface;

extern int ospf6_flood_interface_mdr (struct ospf6_neighbor *from,
                                      struct ospf6_lsa *lsa,
                                      struct ospf6_interface *oi);
extern void ospf6_mdr_acknowledge_lsa_allother (struct ospf6_lsa *lsa,
						struct ospf6_interface *oi,
						struct in6_addr *dst);
extern void ospf6_backupwait_lsa_add (struct ospf6_lsa *lsa,
				      struct ospf6_neighbor *on);
extern void ospf6_backupwait_lsa_neighbor_delete (struct ospf6_lsa *lsa,
						  struct ospf6_neighbor *on);
extern void ospf6_backupwait_lsa_delete (struct ospf6_lsa *lsa);

#endif	/* OSPF6_MDR_FLOOD_H */
