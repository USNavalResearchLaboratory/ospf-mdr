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

#include "zebra.h"

#include "linklist.h"
#include "command.h"

#include "ospf6d.h"
#include "ospf6_af.h"
#include "ospf6_area.h"
#include "ospf6_interface.h"
#include "ospf6_neighbor.h"
#include "ospf6_lsdb.h"
#include "ospf6_flood.h"
#include "ospf6_mdr_interface.h"

void
ospf6_mdr_interface_create (struct ospf6_interface *oi)
{
  if (oi->type == OSPF6_IFTYPE_MDR)
    ospf6_mdr_interface_configure_defaults (oi);

  oi->mdr.ackInterval = 1000;
  oi->mdr.ack_cache_timeout = 100;  //Sec
  oi->mdr.BackupWaitInterval = 500; //msec ( > flood_delay + prop delay)
  oi->mdr.TwoHopRefresh = 1;
  oi->mdr.HelloRepeatCount = 3;
  oi->mdr.AdjConnectivity = OSPF6_ADJ_UNICONNECTED;
  oi->mdr.LSAFullness = OSPF6_LSA_FULLNESS_MINCOST;
  oi->mdr.MDRConstraint = 3;        // constraint h for MPN, should be 2 or 3.
  oi->mdr.consec_hello_threshold = 1;

  oi->mdr.lnl = list_new ();
  oi->mdr.hsn = 0;
  oi->mdr.full_hello_count = 0;

  oi->mdr.update_routable_neighbors_immediately = false;
}

/* set default values for MDR interfaces from RFC 5614, Section 3.2 */
void
ospf6_mdr_interface_configure_defaults (struct ospf6_interface *oi)
{
  assert (oi->type == OSPF6_IFTYPE_MDR);

  if (!(oi->config_status & HELLO_INTERVAL_CONFIGURED))
    oi->hello_interval = OSPF6_MDR_HELLO_INTERVAL;

  if (!(oi->config_status & DEAD_INTERVAL_CONFIGURED))
    oi->dead_interval = OSPF6_MDR_DEAD_INTERVAL;

  if (!(oi->config_status & RXMT_INTERVAL_CONFIGURED))
    oi->rxmt_interval = OSPF6_MDR_RXMT_INTERVAL;

  if (!(oi->config_status & LINK_LSA_SUPPRESSION_CONFIGURED))
    oi->LinkLSASuppression = 1;

  if (!(oi->config_status & ALLOW_IMMEDIATE_HELLO_CONFIGURED))
    oi->allow_immediate_hello = true;
}

void
ospf6_mdr_interface_delete (struct ospf6_interface *oi)
{
  struct ospf6_lnl_element *lnl_element;
  struct listnode *node, *nnode;

  if (!oi->mdr.lnl)
    return;

  //lnl
  for (ALL_LIST_ELEMENTS (oi->mdr.lnl, node, nnode, lnl_element))
    free (lnl_element);
  list_delete (oi->mdr.lnl);
}

void
ospf6_mdr_interface_show (struct vty *vty, struct ospf6_interface *oi)
{
  const char *type;
  struct listnode *node, *nnode;
  struct ospf6_neighbor *on;
  char router_id[32];

  switch (oi->mdr.mdr_level)
    {
    case OSPF6_MDR:
      type = "MDR";
      break;
    case OSPF6_BMDR:
      type = "BMDR";
      break;
    case OSPF6_OTHER:
      type = "OTHER";
      break;
    default:
      type = "???";
      break;
    }
  vty_out (vty, "    Router is an %s router%s", type, VTY_NEWLINE);

  if (oi->mdr.parent)
    {
      ospf6_id2str (oi->mdr.parent->router_id, router_id, sizeof (router_id));
      vty_out (vty, "    Parent:  %s %s", router_id, VTY_NEWLINE);
    }
  if (oi->mdr.bparent)
    {
      ospf6_id2str (oi->mdr.bparent->router_id, router_id, sizeof (router_id));
      vty_out (vty, "    Backup Parent:  %s %s", router_id, VTY_NEWLINE);
    }
  vty_out (vty, "    Dependent Neighbors:  ");
  for (ALL_LIST_ELEMENTS (oi->neighbor_list, node, nnode, on))
    {
      if (on->mdr.dependent && on->state > OSPF6_NEIGHBOR_INIT)
	{
	  ospf6_id2str (on->router_id, router_id, sizeof (router_id));
	  vty_out (vty, "%s,", router_id);
	}
    }
  vty_out (vty, "%s", VTY_NEWLINE);

  vty_out (vty, "    Dependent Selectors:  ");
  for (ALL_LIST_ELEMENTS (oi->neighbor_list, node, nnode, on))
    {
      if (on->mdr.dependent_selector && on->state > OSPF6_NEIGHBOR_INIT)
	{
	  ospf6_id2str (on->router_id, router_id, sizeof (router_id));
	  vty_out (vty, "%s,", router_id);
	}
    }
  vty_out (vty, "%s", VTY_NEWLINE);

  vty_out (vty, "    Children:  ");
  for (ALL_LIST_ELEMENTS (oi->neighbor_list, node, nnode, on))
    {
      if (on->mdr.child && on->state > OSPF6_NEIGHBOR_INIT)
	{
	  ospf6_id2str (on->router_id, router_id, sizeof (router_id));
	  vty_out (vty, "%s,", router_id);
	}
    }
  vty_out (vty, "%s", VTY_NEWLINE);
}

DEFUN (ipv6_ospf6_ackinterval,
       ipv6_ospf6_ackinterval_cmd,
       "ipv6 ospf6 ackinterval <1-65535>",
       IP6_STR
       OSPF6_STR
       "Interval of time to coalesce acks\n"
       "Milliseconds\n"
       )
{
  struct ospf6_interface *oi;
  long tmp;

  oi = ospf6_interface_vtyget (vty);

  tmp = strtol (argv[0], NULL, 10);
  if (tmp > 1000 * (long) (oi->rxmt_interval))
    {
      vty_out (vty, "ERROR: ack interval cannot exceed retransmit interval%s",
	       VNL);
      return CMD_WARNING;
    }

  if (tmp > 1000)
    vty_out (vty, "WARNING: ack interval should not exceed one second%s",
	     VNL);

  oi->mdr.ackInterval = tmp;

  return CMD_SUCCESS;
}

DEFUN_DEPRECATED (ipv6_ospf6_diffhellos,
       ipv6_ospf6_diffhellos_cmd,
       "ipv6 ospf6 diffhellos",
       IP6_STR
       OSPF6_STR
       "Enable differential hellos\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  vty_out (vty, "WARNING: use 'ipv6 ospf6 twohoprefresh' instead%s", VNL);
  oi->mdr.TwoHopRefresh = 3;

  return CMD_SUCCESS;
}

DEFUN_DEPRECATED (no_ipv6_ospf6_diffhellos,
       no_ipv6_ospf6_diffhellos_cmd,
       "no ipv6 ospf6 diffhellos",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Disable differential hellos\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  vty_out (vty, "WARNING: use 'ipv6 ospf6 twohoprefresh' instead%s", VNL);
  oi->mdr.TwoHopRefresh = 1;

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_backupwaitinterval,
       ipv6_ospf6_backupwaitinterval_cmd,
       "ipv6 ospf6 backupwaitinterval <1-65535>",
       IP6_STR
       OSPF6_STR
       "Interval of time for MBDRs to wait before flooding\n"
       SECONDS_STR
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->mdr.BackupWaitInterval = strtol (argv[0], NULL, 10);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_twohoprefresh,
       ipv6_ospf6_twohoprefresh_cmd,
       "ipv6 ospf6 twohoprefresh <1-65535>",
       IP6_STR
       OSPF6_STR
       "Full Hellos are sent every TwoHopRefresh Hellos\n"
       "TwoHopRefresh count\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->mdr.TwoHopRefresh = strtol (argv[0], NULL, 10);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_hellorepeatcount,
       ipv6_ospf6_hellorepeatcount_cmd,
       "ipv6 ospf6 hellorepeatcount <1-65535>",
       IP6_STR
       OSPF6_STR
       "Total hellos in succession that cannot be missed using diff hellos\n"
       "Number of successive losses\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->mdr.HelloRepeatCount = strtol (argv[0], NULL, 10);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_adjacencyconnectivity,
       ipv6_ospf6_adjacencyconnectivity_cmd,
       "ipv6 ospf6 adjacencyconnectivity (uniconnected|biconnected|fully)",
       IP6_STR
       OSPF6_STR
       "Level of adjacencies between neighbors\n"
       "Specify uniconnected adjacencies between routers\n"
       "Specify biconnected adjacencies between routers\n"
       "Specify fully connected adjacencies between routers\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  if (strncmp (argv[0], "uni", 3) == 0)
    oi->mdr.AdjConnectivity = OSPF6_ADJ_UNICONNECTED;
  else if (strncmp (argv[0], "bi", 2) == 0)
    oi->mdr.AdjConnectivity = OSPF6_ADJ_BICONNECTED;
  else if (strncmp (argv[0], "ful", 3) == 0)
    oi->mdr.AdjConnectivity = OSPF6_ADJ_FULLYCONNECTED;
  else
    oi->mdr.AdjConnectivity = OSPF6_ADJ_BICONNECTED;

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_lsafullnesss,
       ipv6_ospf6_lsafullness_cmd,
       "ipv6 ospf6 lsafullness (minlsa|mincostlsa|mincost2lsa|mdrfulllsa|fulllsa)",
       IP6_STR
       OSPF6_STR
       "Level of LSA fullness\n"
       "Specify min size LSAs (only adjacent neighbors)\n"
       "Specify partial LSAs for min-hop routing\n"
       "Specify partial LSAs for two min-hop routing paths\n"
       "Specify full LSAs from MDR/MBDRs\n"
       "Specify full LSAs (all routable neighbors)\n"
       )
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  if (strncmp (argv[0], "minlsa", 6) == 0)
    oi->mdr.LSAFullness = OSPF6_LSA_FULLNESS_MIN;
  else if (strncmp (argv[0], "mincost", 6) == 0)
    oi->mdr.LSAFullness = OSPF6_LSA_FULLNESS_MINCOST;
  else if (strncmp (argv[0], "mincost2paths", 6) == 0)
    oi->mdr.LSAFullness = OSPF6_LSA_FULLNESS_MINCOST2PATHS;
  else if (strncmp (argv[0], "mdrfull", 6) == 0)
    oi->mdr.LSAFullness = OSPF6_LSA_FULLNESS_MDRFULL;
  else if (strncmp (argv[0], "full", 4) == 0)
    oi->mdr.LSAFullness = OSPF6_LSA_FULLNESS_FULL;
  else
    oi->mdr.LSAFullness = OSPF6_LSA_FULLNESS_MIN;

  return CMD_SUCCESS;

}

DEFUN (ipv6_ospf6_mdrconstraint,
       ipv6_ospf6_mdrconstraint_cmd,
       "ipv6 ospf6 mdrconstraint <2-3>",
       IP6_STR
       OSPF6_STR
       "MDRConstraint parameter (default =3) for MDR redundancy\n"
       "MDRConstraint value\n")
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->mdr.MDRConstraint = strtol (argv[0], NULL, 10);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_consechellothresh,
       ipv6_ospf6_consechellothresh_cmd,
       "ipv6 ospf6 consec-hello-threshold <1-65535>",
       IP6_STR
       OSPF6_STR
       "Neighbor acceptance criteria:  number of consecutive hellos to move from Down to Init\n")
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->mdr.consec_hello_threshold = strtol (argv[0], NULL, 10);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_update_routable_neighbors_immediately,
       ipv6_ospf6_update_routable_neighbors_immediately_cmd,
       "ipv6 ospf6 update-routable-neighbors-immediately",
       IP6_STR
       OSPF6_STR
       "Update the set of routable neighbors immediately after performing a SPF calculation\n")
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->mdr.update_routable_neighbors_immediately = true;

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_update_routable_neighbors_immediately,
       no_ipv6_ospf6_update_routable_neighbors_immediately_cmd,
       "no ipv6 ospf6 update-routable-neighbors-immediately",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Update the set of routable neighbors immediately after performing a SPF calculation\n")
{
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  oi->mdr.update_routable_neighbors_immediately = false;

  return CMD_SUCCESS;
}

void
ospf6_mdr_interface_config_write (struct vty *vty, struct ospf6_interface *oi)
{
  vty_out (vty, " ipv6 ospf6 network manet-designated-router %s", VNL);
  vty_out (vty, " ipv6 ospf6 ackinterval %ld%s", oi->mdr.ackInterval, VNL);
  vty_out (vty, " ipv6 ospf6 backupwaitinterval %ld%s",
	   oi->mdr.BackupWaitInterval, VNL);
  vty_out (vty, " ipv6 ospf6 twohoprefresh %d%s", oi->mdr.TwoHopRefresh, VNL);
  vty_out (vty, " ipv6 ospf6 mdrconstraint %d%s", oi->mdr.MDRConstraint, VNL);
  vty_out (vty, " ipv6 ospf6 hellorepeatcount %d%s",
	   oi->mdr.HelloRepeatCount, VNL);
  if (oi->mdr.consec_hello_threshold > 1)
    vty_out (vty, " ipv6 ospf6 consec-hello-threshold %d%s",
	     oi->mdr.consec_hello_threshold, VNL);
  switch (oi->mdr.AdjConnectivity)
    {
    case OSPF6_ADJ_UNICONNECTED:
      vty_out (vty, " ipv6 ospf6 adjacencyconnectivity uniconnected%s", VNL);
      break;
    case OSPF6_ADJ_BICONNECTED:
      vty_out (vty, " ipv6 ospf6 adjacencyconnectivity biconnected%s", VNL);
      break;
    case OSPF6_ADJ_FULLYCONNECTED:
      vty_out (vty, " ipv6 ospf6 adjacencyconnectivity fully%s", VNL);
      break;
    }
  switch (oi->mdr.LSAFullness)
    {
    case OSPF6_LSA_FULLNESS_MIN:
      vty_out (vty, " ipv6 ospf6 lsafullness minlsa%s", VNL);
      break;
    case OSPF6_LSA_FULLNESS_FULL:
      vty_out (vty, " ipv6 ospf6 lsafullness fulllsa%s", VNL);
      break;
    case OSPF6_LSA_FULLNESS_MDRFULL:
      vty_out (vty, " ipv6 ospf6 lsafullness mdrfulllsa%s", VNL);
      break;
    case OSPF6_LSA_FULLNESS_MINCOST:
      vty_out (vty, " ipv6 ospf6 lsafullness mincostlsa%s", VNL);
      break;
    case OSPF6_LSA_FULLNESS_MINCOST2PATHS:
      vty_out (vty, " ipv6 ospf6 lsafullness mincost2lsa%s", VNL);
      break;
    }
  if (oi->mdr.update_routable_neighbors_immediately)
    {
      vty_out (vty, " ipv6 ospf6 update-routable-neighbors-immediately%s",
               VNL);
    }
}

DEFUN (show_ipv6_ospf6_mdrlevel,
       show_ipv6_ospf6_mdrlevel_cmd,
       "show ipv6 ospf6 mdrlevel",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "The MANET Designated Router level\n"
       )
{
  struct listnode *j, *k;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  const char *type;

  for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, j, oa))
    for (ALL_LIST_ELEMENTS_RO (oa->if_list, k, oi))
      {
        switch (oi->mdr.mdr_level)
          {
          case OSPF6_MDR:
            type = "MDR";
            break;
          case OSPF6_BMDR:
            type = "BMDR";
            break;
          case OSPF6_OTHER:
            type = "OTHER";
            break;
          default:
            type = "???";
            break;
          }
        if (oi->type == OSPF6_IFTYPE_MDR)
          vty_out (vty, " area %s interface %s %s%s",
                   oa->name, oi->interface->name, type, VNL);
      }
  return CMD_SUCCESS;
}

DEFUN (show_ipv6_ospf6_mdrconstraint,
       show_ipv6_ospf6_mdrconstraint_cmd,
       "show ipv6 ospf6 mdrconstraint",
       SHOW_STR IP6_STR OSPF6_STR "The MDRConstraint value\n")
{
  struct listnode *j, *k;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;

  for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, j, oa))
    for (ALL_LIST_ELEMENTS_RO (oa->if_list, k, oi))
      {
        if (oi->type == OSPF6_IFTYPE_MDR)
          vty_out (vty, " area %s interface %s MDRConstraint %d%s",
                   oa->name, oi->interface->name, oi->mdr.MDRConstraint, VNL);
      }
  return CMD_SUCCESS;
}

DEFUN (show_ipv6_ospf6_consechellothresh,
       show_ipv6_ospf6_consechellothresh_cmd,
       "show ipv6 ospf6 consec-hello-threshold",
       SHOW_STR IP6_STR OSPF6_STR "The neighbor acceptance criteria\n")
{
  struct listnode *j, *k;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;

  for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, j, oa))
    for (ALL_LIST_ELEMENTS_RO (oa->if_list, k, oi))
      {
        if (oi->type == OSPF6_IFTYPE_MDR)
          vty_out (vty, " area %s interface %s consec-hello-threshold %d%s",
                   oa->name, oi->interface->name,
		   oi->mdr.consec_hello_threshold, VNL);
      }
  return CMD_SUCCESS;
}

void
ospf6_mdr_interface_init (void)
{
  install_element (INTERFACE_NODE, &ipv6_ospf6_ackinterval_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_diffhellos_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_diffhellos_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_backupwaitinterval_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_twohoprefresh_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_mdrconstraint_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_hellorepeatcount_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_adjacencyconnectivity_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_lsafullness_cmd);
  install_element (INTERFACE_NODE, &ipv6_ospf6_consechellothresh_cmd);
  install_element (INTERFACE_NODE,
                   &ipv6_ospf6_update_routable_neighbors_immediately_cmd);
  install_element (INTERFACE_NODE,
                   &no_ipv6_ospf6_update_routable_neighbors_immediately_cmd);

  install_element (VIEW_NODE, &show_ipv6_ospf6_mdrlevel_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_mdrlevel_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_mdrconstraint_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_mdrconstraint_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_consechellothresh_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_consechellothresh_cmd);
}

void
ospf6_update_adjacencies (struct ospf6_interface *oi)
{
  struct listnode *j;
  struct ospf6_lsa *lsa;
  struct ospf6_neighbor *on;
  // Check need adjacency for each 2-way neighbor.
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, on))
    {
      if (on && on->state == OSPF6_NEIGHBOR_TWOWAY && need_adjacency (on))
	ospf6_neighbor_exstart (on);
    }
  // Check keep adjacency for each adjacent neighbor.
  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, on))
    {
      if (on && on->state > OSPF6_NEIGHBOR_TWOWAY && !keep_adjacency (on))
        {
          ospf6_neighbor_state_change (OSPF6_NEIGHBOR_TWOWAY, on);
          // Clear retrans_list
          ospf6_lsdb_remove_all (on->summary_list);
          ospf6_lsdb_remove_all (on->request_list);
          for (lsa = ospf6_lsdb_head (on->retrans_list); lsa;
               lsa = ospf6_lsdb_next (lsa))
            {
              ospf6_decrement_retrans_count (lsa);
              ospf6_lsdb_remove (lsa, on->retrans_list);
            }
        }
      if (on && on->state == OSPF6_NEIGHBOR_TWOWAY
          && on->retrans_list->count > 0)
        zlog_err ("Error:2-way nbr has nonempty retrans list, "
		  "count %d dep %d\n", on->retrans_list->count, on->mdr.dependent);
    }
}
