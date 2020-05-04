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

#ifndef OSPF6_MDR_INTERFACE_H
#define OSPF6_MDR_INTERFACE_H

#include <stdbool.h>

/* default values from RFC 5614, Section 3.2 */
#define OSPF6_MDR_HELLO_INTERVAL	2
#define OSPF6_MDR_DEAD_INTERVAL		6
#define OSPF6_MDR_RXMT_INTERVAL		7

#define OSPF6_OTHER       0
#define OSPF6_BMDR        1
#define OSPF6_MDR         2

typedef enum
{
  OSPF6_ADJ_FULLYCONNECTED = 0,
  OSPF6_ADJ_UNICONNECTED,
  OSPF6_ADJ_BICONNECTED
} ospf6_AdjConnectivity;

//How much information to include in LSAs
//These are defined in Ogier's draft, Appendix C
typedef enum
{
  OSPF6_LSA_FULLNESS_MIN = 0,   //minimal LSAs (only adjacent neighbors)
  OSPF6_LSA_FULLNESS_MINCOST,   //partial LSAs for min-cost routing
  OSPF6_LSA_FULLNESS_MINCOST2PATHS,     //same as above, with some path redundancy
  OSPF6_LSA_FULLNESS_MDRFULL,   //full LSAs from MDR/MBDRs
  OSPF6_LSA_FULLNESS_FULL       //full LSAs (all routable neighbors)
} ospf6_LSAFullness;

struct ospf6_mdr_interface
{
  long ackInterval;
  int ack_cache_timeout;
  bool nonflooding_mdr;
  long BackupWaitInterval;
  int **cost_matrix;
  int **adj_matrix;             // RGO2. Indicates which nbr pairs are adjacent
  int **san_matrix;             // RGO2. Selected advertised nbr matrix
  int AdjConnectivity;          //1=uniconnected, 2=biconnected, 0=fully connected
  int LSAFullness;
  int MDRConstraint;            // MPN parameter h, should be 2 or 3.
  int mdr_level;
  int consec_hello_threshold;   // Neighbor acceptance criteria
  struct ospf6_neighbor *parent;
  struct ospf6_neighbor *bparent;
  u_int16_t TwoHopRefresh;
  u_int16_t HelloRepeatCount;

  struct list *lnl;
  u_int16_t hsn;
  u_int full_hello_count;

  bool update_routable_neighbors_immediately;
};

struct ospf6_interface;
struct vty;

extern void ospf6_mdr_interface_create (struct ospf6_interface *oi);
extern void ospf6_mdr_interface_configure_defaults (struct ospf6_interface *oi);
extern void ospf6_mdr_interface_delete (struct ospf6_interface *oi);
extern void ospf6_mdr_interface_show (struct vty *vty,
				      struct ospf6_interface *oi);
extern void ospf6_mdr_interface_config_write (struct vty *vty,
					      struct ospf6_interface *oi);
extern void ospf6_mdr_interface_init (void);
extern void ospf6_update_adjacencies (struct ospf6_interface *oi);

#endif	/* OSPF6_MDR_INTERFACE_H */
