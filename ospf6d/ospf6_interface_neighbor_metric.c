/* -*-  c-file-style: "gnu" -*- */

/*
 * Copyright (c) 2010 The Boeing Company
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
#include "memory.h"
#include "command.h"

#include "ospf6_zebra.h"
#include "ospf6_intra.h"
#include "ospf6_area.h"
#include "ospf6_spf.h"
#include "ospf6d.h"
#include "ospf6_interface.h"
#include "ospf6_interface_neighbor_metric.h"

static unsigned int neighbor_metric_data_id;
static unsigned int neighbor_metric_id;

struct ospf6_interface_neighbor_metric {
  unsigned int current_neighbor_metric_id;
  int registered;
  int enabled;
  char *name;
  u_int16_t metric_update_hysteresis;
  void (*delete) (struct ospf6_interface *oi);
  void (*config_write) (struct ospf6_interface *oi, struct vty *vty);
  void (*cost_update) (struct ospf6_interface *oi);
  struct ospf6_neighbor_operations nbrops;
  void (*nbrops_remove) (struct ospf6_interface *oi,
			 struct ospf6_neighbor_operations *ops);
  void *data;
};

static int
ospf6_interface_remove_neighbor_metric (struct ospf6_interface *oi);
static int
__ospf6_interface_enable_neighbor_metric (struct ospf6_interface *oi,
					  unsigned int id, int enable);

static void
ospf6_interface_neighbor_metric_struct_reset (struct ospf6_interface_neighbor_metric *nbrmetric)
{
  u_int16_t metric_update_hysteresis = nbrmetric->metric_update_hysteresis;

  if (nbrmetric->name)
    free (nbrmetric->name);

  memset (nbrmetric, 0, sizeof (*nbrmetric));

  nbrmetric->metric_update_hysteresis = metric_update_hysteresis;
}

static void
ospf6_interface_neighbor_metric_init (struct ospf6_interface_neighbor_metric *nbrmetric)
{
  nbrmetric->metric_update_hysteresis = 1;
  nbrmetric->name = NULL;
  ospf6_interface_neighbor_metric_struct_reset (nbrmetric);
}

static int
ospf6_interface_create_neighbor_metric (struct ospf6_interface *oi)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;
  int err;

  nbrmetric = XMALLOC (MTYPE_OSPF6_IF, sizeof (*nbrmetric));
  ospf6_interface_neighbor_metric_init (nbrmetric);

  err = ospf6_add_interface_data (oi, &neighbor_metric_data_id, nbrmetric);
  if (err)
    {
      XFREE (MTYPE_OSPF6_IF, nbrmetric);
      return err;
    }

  return 0;
}

static void
ospf6_interface_neighbor_metric_remove (struct ospf6_interface *oi,
					struct ospf6_neighbor_operations *ops)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;

  nbrmetric = ospf6_get_interface_data (oi, neighbor_metric_data_id);
  assert (nbrmetric);

  if (ops == &nbrmetric->nbrops)
    {
      assert (nbrmetric->enabled);
      nbrmetric->enabled = 0;
      if (nbrmetric->nbrops_remove)
	nbrmetric->nbrops_remove (oi, ops);
    }
}

static void
ospf6_interface_delete_neighbor_metric (struct ospf6_interface *oi)
{
  int err;
  struct ospf6_interface_neighbor_metric *nbrmetric;

  err = ospf6_interface_remove_neighbor_metric (oi);
  if (err)
    zlog_warn ("%s: ospf6_interface_remove_neighbor_metric() failed",
	       __func__);

  nbrmetric = ospf6_del_interface_data (oi, neighbor_metric_data_id);
  if (nbrmetric == NULL)
    return;

  XFREE (MTYPE_OSPF6_IF, nbrmetric);
}

static struct ospf6_interface_neighbor_metric *
__get_registered_neighbor_metric (struct ospf6_interface *oi, unsigned int id)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;

  nbrmetric = ospf6_get_interface_data (oi, neighbor_metric_data_id);
  assert (nbrmetric);

  if (id != 0)
    {
      if (!nbrmetric->registered ||
	  nbrmetric->current_neighbor_metric_id != id)
	{
	  zlog_err ("%s: neighbor metric id %u is not currently registered",
		    __func__, id);
	  return NULL;
	}
    }

  return nbrmetric;
}

static int
__ospf6_interface_update_neighbor_metric (struct ospf6_neighbor *on,
					  u_int16_t newmetric, unsigned int id)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;
  struct ospf6_interface *oi = on->ospf6_if;
  int delta;
  int update;

  nbrmetric = __get_registered_neighbor_metric (oi, id);
  if (nbrmetric == NULL)
    return -1;

  /*
   * ensure that neighbor cost isn't less than the configured
   * interface cost
   */
  if (newmetric < oi->cost)
    {
      zlog_warn ("%s: new metric %u less than interface cost %u; "
		 "using interface cost instead",
		 __func__, newmetric, oi->cost);
      newmetric = oi->cost;
    }

  delta = on->cost - newmetric;
  if (delta < 0)
    delta = -delta;
  else if (delta == 0)
    return 0;

  update = 0;

  if (delta >= nbrmetric->metric_update_hysteresis ||
      (newmetric == oi->cost && on->cost > oi->cost))
    update++;

  if (update)
    {
      on->cost = newmetric;

      if (on->state == OSPF6_NEIGHBOR_FULL ||
	  (oi->type == OSPF6_IFTYPE_MDR && on->mdr.adv))
	{
	  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
	    zlog_debug ("%s: cost delta for neighbor %s exceeds hysteresis "
			"(%u > %u): updating neighbor cost and scheduling "
			"router lsa", __func__, on->name, delta,
			nbrmetric->metric_update_hysteresis);

	  ospf6_router_lsa_schedule (oi->area);
	}

      if (on->state == OSPF6_NEIGHBOR_FULL ||
	  (oi->type == OSPF6_IFTYPE_MDR && on->state >= OSPF6_NEIGHBOR_TWOWAY))
	ospf6_spf_schedule (oi->area);
    }

  return 0;
}

int
ospf6_interface_update_neighbor_metric (struct ospf6_neighbor *on,
					u_int16_t newmetric, unsigned int id)
{
  if (id == 0)
    return -1;

  return __ospf6_interface_update_neighbor_metric (on, newmetric, id);
}

DEFUN (ipv6_ospf6_neighbor_metric_hysteresis,
       ipv6_ospf6_neighbor_metric_hysteresis_cmd,
       "ipv6 ospf6 neighbor-metric-hysteresis <1-65535>",
       IP6_STR
       OSPF6_STR
       "Hysteresis used for neighbor metric updates\n"
       "Hysteresis value\n")
{
  struct ospf6_interface_neighbor_metric *nbrmetric;
  struct ospf6_interface *oi;

  oi = ospf6_interface_vtyget (vty);

  nbrmetric = ospf6_get_interface_data (oi, neighbor_metric_data_id);
  assert (nbrmetric);

  /*
   * command parsing ensures that argv[0] is a valid integer and in
   * range specified above
   */
  nbrmetric->metric_update_hysteresis = strtol (argv[0], NULL, 10);

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_neighbor_metric,
       no_ipv6_ospf6_neighbor_metric_cmd,
       "no ipv6 ospf6 neighbor-metric",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Disable using neighbor metrics\n")
{
  struct ospf6_interface_neighbor_metric *nbrmetric;
  struct ospf6_interface *oi;
  int err;

  oi = ospf6_interface_vtyget (vty);

  nbrmetric = ospf6_get_interface_data (oi, neighbor_metric_data_id);
  assert (nbrmetric);

  err = ospf6_interface_remove_neighbor_metric (oi);
  if (err)
    {
      vty_out (vty, "failed to remove neighbor metrics%s", VNL);
      return CMD_WARNING;
    }

  return CMD_SUCCESS;
}

int
ospf6_interface_register_neighbor_metric (struct ospf6_interface *oi,
					  unsigned int *id,
					  const struct ospf6_interface_neighbor_metric_params *params,
					  struct vty *vty)
{
  int err;
  struct ospf6_interface_neighbor_metric *nbrmetric;

  nbrmetric = ospf6_get_interface_data (oi, neighbor_metric_data_id);
  assert (nbrmetric);

  if (nbrmetric->enabled)
    {
      zlog_err ("%s: existing neighbor metric manager %s is enabled",
		__func__, nbrmetric->name);
      if (vty)
	vty_out (vty, "existing neighbor metric manager %s is enabled%s",
		 nbrmetric->name, VNL);
      return -1;
    }

  err = ospf6_interface_remove_neighbor_metric (oi);
  if (err)
    {
      zlog_err ("%s: ospf6_interface_remove_neighbor_metric() failed",
		__func__);
      if (vty)
	vty_out (vty, "ospf6_interface_remove_neighbor_metric() failed%s", VNL);
      return -1;
    }

  nbrmetric->registered = 1;
  nbrmetric->name = strdup (params->name);
  nbrmetric->delete = params->delete;
  nbrmetric->config_write = params->config_write;
  nbrmetric->cost_update = params->cost_update;
  nbrmetric->nbrops = params->nbrops;
  nbrmetric->nbrops_remove = nbrmetric->nbrops.remove;
  nbrmetric->nbrops.remove = ospf6_interface_neighbor_metric_remove;
  nbrmetric->data = params->data;
  nbrmetric->enabled = 0;

  if (*id == 0)
    *id = ++neighbor_metric_id;

  nbrmetric->current_neighbor_metric_id = *id;

  return 0;
}

int
ospf6_interface_neighbor_metric_registered (struct ospf6_interface *oi,
					    unsigned int id)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;

  if (id == 0)
    return 0;

  nbrmetric = ospf6_get_interface_data (oi, neighbor_metric_data_id);
  assert (nbrmetric);

  if (nbrmetric->registered && nbrmetric->current_neighbor_metric_id == id)
    return 1;

  return 0;
}

static int
__ospf6_interface_reset_neighbor_metric (struct ospf6_interface *oi)
{
  struct listnode *node;
  struct ospf6_neighbor *on;
  int err = 0;

  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
    {
      int tmperr;

      tmperr = __ospf6_interface_update_neighbor_metric (on, oi->cost, 0);
      if (tmperr)
	zlog_err ("%s: ospf6_interface_update_neighbor_metric() failed "
		  "for neighbor %s", __func__, on->name);
      err += tmperr;
    }

  return err;
}

int
ospf6_interface_reset_neighbor_metric (struct ospf6_interface *oi,
				       unsigned int id)
{
  int registered;

  registered = ospf6_interface_neighbor_metric_registered (oi, id);
  if (!registered)
    {
      zlog_err ("%s: neighbor metric id %u is not currently registered",
		__func__, id);
      return -1;
    }

  return __ospf6_interface_reset_neighbor_metric (oi);
}

static int
ospf6_interface_remove_neighbor_metric (struct ospf6_interface *oi)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;

  nbrmetric = __get_registered_neighbor_metric (oi, 0);
  if (nbrmetric == NULL)
    return -1;

  __ospf6_interface_enable_neighbor_metric (oi, 0, 0);

  if (nbrmetric->delete)
    nbrmetric->delete (oi);

  ospf6_interface_neighbor_metric_struct_reset (nbrmetric);

  __ospf6_interface_reset_neighbor_metric (oi);

  return 0;
}

static int
__ospf6_interface_enable_neighbor_metric (struct ospf6_interface *oi,
					  unsigned int id, int enable)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;

  nbrmetric = __get_registered_neighbor_metric (oi, id);
  if (nbrmetric == NULL)
    return -1;

  if ((enable && nbrmetric->enabled) || (!enable && !nbrmetric->enabled))
    return 0;

  if (enable)
    {
      int err;

      err = ospf6_register_neighbor_operations (oi, &nbrmetric->nbrops);
      if (err)
	{
	  zlog_err ("%s: ospf6_register_neighbor_operations() failed "
		    "for neighbor metric: %s", __func__, nbrmetric->name);
	  return -1;
	}
    }
  else
    {
      ospf6_remove_neighbor_operations (oi, &nbrmetric->nbrops);
    }

  nbrmetric->enabled = enable ? 1 : 0;

  return 0;
}

int
ospf6_interface_enable_neighbor_metric (struct ospf6_interface *oi,
					unsigned int id)
{
  if (id == 0)
    return -1;

  return __ospf6_interface_enable_neighbor_metric (oi, id, 1);
}

int
ospf6_interface_disable_neighbor_metric (struct ospf6_interface *oi,
					 unsigned int id)
{
  if (id == 0)
    return -1;

  return __ospf6_interface_enable_neighbor_metric (oi, id, 0);
}

int
ospf6_interface_neighbor_metric_enabled (struct ospf6_interface *oi,
					 unsigned int id)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;

  if (id == 0)
    return 0;

  nbrmetric = __get_registered_neighbor_metric (oi, id);
  if (nbrmetric == NULL)
    return 0;

  return nbrmetric->enabled;
}

void *
ospf6_interface_neighbor_metric_data (struct ospf6_interface *oi,
				      unsigned int id)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;

  if (id == 0)
    return NULL;

  nbrmetric = __get_registered_neighbor_metric (oi, id);
  if (nbrmetric == NULL)
    return NULL;

  return nbrmetric->data;
}

static void
ospf6_interface_config_write_neighbor_metric (struct ospf6_interface *oi,
					      struct vty *vty)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;

  nbrmetric = ospf6_get_interface_data (oi, neighbor_metric_data_id);
  assert (nbrmetric);

  if (nbrmetric->metric_update_hysteresis != 1)
    vty_out (vty, " ipv6 ospf6 neighbor-metric-hysteresis %u%s",
	     nbrmetric->metric_update_hysteresis, VNL);

  if (nbrmetric->enabled && nbrmetric->config_write)
    nbrmetric->config_write (oi, vty);
}

static void
ospf6_interface_cost_update_neighbor_metric (struct ospf6_interface *oi)
{
  struct ospf6_interface_neighbor_metric *nbrmetric;

  nbrmetric = ospf6_get_interface_data (oi, neighbor_metric_data_id);
  assert (nbrmetric);

  if (nbrmetric->enabled)
    {
      if (nbrmetric->cost_update)
	nbrmetric->cost_update (oi);
    }
  else
    {
      __ospf6_interface_reset_neighbor_metric (oi);
    }
}

static void
ospf6_interface_init_neighbor_metric (void)
{
  install_element (INTERFACE_NODE,
		   &ipv6_ospf6_neighbor_metric_hysteresis_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_neighbor_metric_cmd);
}

static struct ospf6_interface_operations neighbor_metric_ifops = {
  .init = ospf6_interface_init_neighbor_metric,
  .create = ospf6_interface_create_neighbor_metric,
  .delete = ospf6_interface_delete_neighbor_metric,
  .config_write = ospf6_interface_config_write_neighbor_metric,
  .cost_update = ospf6_interface_cost_update_neighbor_metric,
};

OSPF6_INTERFACE_OPERATIONS (neighbor_metric_ifops);
