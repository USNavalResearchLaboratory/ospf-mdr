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
#include "thread.h"
#include "command.h"

#include "ospf6d.h"
#include "ospf6_interface.h"
#include "ospf6_interface_neighbor_metric.h"
#include "ospf6_neighbor.h"
#include "ospf6_intra.h"
#include "ospf6_area.h"

struct ospf6_interface_metricfunction {
  u_int16_t (*metric_function)(struct ospf6_neighbor *on, void *data);
  void *metric_function_data;
  struct thread *thread_metric_function;
  u_int16_t metric_function_interval;
};

static const char *metricfunction_name = "metric-function";
static unsigned int metricfunction_nbrmetric_id;

static int
ospf6_neighbor_create_metricfunction (struct ospf6_neighbor *on);
static void
ospf6_interface_config_write_metricfunction (struct ospf6_interface *oi,
					     struct vty *vty);

static void
ospf6_interface_delete_metricfunction (struct ospf6_interface *oi)
{
  struct ospf6_interface_metricfunction *imf;

  imf = ospf6_interface_neighbor_metric_data (oi, metricfunction_nbrmetric_id);
  assert (imf);

  THREAD_OFF (imf->thread_metric_function);
  if (imf->metric_function_data)
    {
      XFREE (MTYPE_OSPF6_OTHER, imf->metric_function_data);
      imf->metric_function_data = NULL;
    }

  XFREE (MTYPE_OSPF6_IF, imf);
}

static int
ospf6_neighbor_run_metricfunction (struct ospf6_interface_metricfunction *imf,
				   struct ospf6_neighbor *on)
{
  u_int16_t newmetric;
  int err;

  newmetric = imf->metric_function (on, imf->metric_function_data);

  err = ospf6_interface_update_neighbor_metric (on, newmetric,
						metricfunction_nbrmetric_id);
  if (err)
    zlog_err ("%s: ospf6_interface_update_neighbor_metric() failed "
	      "for neighbor %s", __func__, on->name);

  return err;
}

static int
ospf6_interface_run_metricfunction (struct thread *thread)
{
  struct ospf6_interface *oi;
  struct ospf6_interface_metricfunction *imf;
  struct listnode *node;
  struct ospf6_neighbor *on;

  oi = (struct ospf6_interface *) THREAD_ARG (thread);
  assert (oi);

  imf = ospf6_interface_neighbor_metric_data (oi, metricfunction_nbrmetric_id);
  assert (imf);

  if (imf->metric_function == NULL)
    {
      zlog_err ("%s: attempt made to use NULL metric function", __func__);
      return 0;
    }

  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
    ospf6_neighbor_run_metricfunction (imf, on);

  if (imf->metric_function_interval)
    imf->thread_metric_function =
      thread_add_timer (master, ospf6_interface_run_metricfunction, oi,
			imf->metric_function_interval);

  return 0;
}

static u_int16_t
neighbor_time_metric_function (struct ospf6_neighbor *on, void *data)
{
  struct ospf6_interface *oi = on->ospf6_if;
  u_int16_t minmetric, maxmetric;
  u_char minstate;
  struct timeval now, dt;
  int err;

  minmetric = oi->cost;
  maxmetric = MIN (USHRT_MAX, minmetric + *(u_int16_t *)data);

  if (oi->type == OSPF6_IFTYPE_MDR)
    minstate = OSPF6_NEIGHBOR_TWOWAY;
  else
    minstate = OSPF6_NEIGHBOR_FULL;

  if (on->state < minstate)
    return maxmetric;

  err = quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);
  if (err)
    {
      zlog_err ("%s: quagga_gettime() failed", __func__);
      return minmetric;
    }

  timersub (&now, &on->last_changed, &dt);
  if (dt.tv_sec < 0)
    {
      zlog_err ("%s: time went backwards", __func__);
      return minmetric;
    }

  return MAX (minmetric, maxmetric - dt.tv_sec / oi->hello_interval);
}

static void
schedule_metric_function (struct ospf6_interface *oi,
			  u_int16_t (*metric_function)(struct ospf6_neighbor *,
						       void *),
			  void *data, u_int16_t interval)
{
  struct ospf6_interface_metricfunction *imf;

  imf = ospf6_interface_neighbor_metric_data (oi, metricfunction_nbrmetric_id);
  assert (imf);

  imf->metric_function = metric_function;
  if (imf->metric_function_data)
    XFREE (MTYPE_OSPF6_OTHER, imf->metric_function_data);
  imf->metric_function_data = data;
  imf->metric_function_interval = interval;

  THREAD_OFF (imf->thread_metric_function);
  if (imf->metric_function)
    imf->thread_metric_function =
      thread_add_timer (master, ospf6_interface_run_metricfunction, oi, 0);

  return;
}

static int
ospf6_interface_register_metricfunction (struct ospf6_interface *oi,
					 struct vty *vty)
{
  int err;
  struct ospf6_interface_metricfunction *imf;
  struct ospf6_interface_neighbor_metric_params nbrmetric_params = {
    .name = metricfunction_name,
    .delete = ospf6_interface_delete_metricfunction,
    .config_write = ospf6_interface_config_write_metricfunction,
    .nbrops = {
      .create = ospf6_neighbor_create_metricfunction,
    },
  };

  imf = XCALLOC (MTYPE_OSPF6_IF, sizeof (*imf));

  nbrmetric_params.data = imf;
  err =
    ospf6_interface_register_neighbor_metric (oi, &metricfunction_nbrmetric_id,
					      &nbrmetric_params, vty);
  if (err)
    {
      vty_out (vty, "could not register neighbor metric %s on interface %s%s",
	       nbrmetric_params.name, oi->interface->name, VNL);
      XFREE (MTYPE_OSPF6_IF, imf);
      return -1;
    }

  return 0;
}

DEFUN (ipv6_ospf6_metric_function_neighbor_time,
       ipv6_ospf6_metric_function_neighbor_time_cmd,
       "ipv6 ospf6 periodic-metric-function neighbor-time [<0-65535>] "
       "recalculate-interval <1-65535>",
       IP6_STR
       OSPF6_STR
       "Use the specified periodic metric function\n"
       "Inversely proportional to the time a neighbor has "
       "been in the full state\n"
       "Maximum metric offset\n"
       "Minimum time between periodic metric function calculations\n"
       SECONDS_STR)
{
  struct ospf6_interface *oi;
  u_int16_t *metric, interval;
  int registered, err;

  oi = ospf6_interface_vtyget (vty);

  registered =
    ospf6_interface_neighbor_metric_registered (oi,
						metricfunction_nbrmetric_id);
  if (!registered)
    {
      err = ospf6_interface_register_metricfunction (oi, vty);
      if (err)
	return CMD_WARNING;
    }

  err = ospf6_interface_enable_neighbor_metric (oi,
						metricfunction_nbrmetric_id);
  if (err)
    vty_out (vty, "could not enable neighbor metric %s on interface %s%s",
	     metricfunction_name, oi->interface->name, VNL);

  metric = XMALLOC (MTYPE_OSPF6_OTHER, sizeof (*metric));
  if (argc > 0)
    *metric = atoi (argv[0]);
  else
    /* XXX is this a good default value? */
    *metric = (4 * oi->dead_interval) / oi->hello_interval;

  if (argc > 1)
    interval = atoi (argv[1]);
  else
    interval = oi->dead_interval; /* XXX is this a good default value? */

  schedule_metric_function (oi, neighbor_time_metric_function,
			    metric, interval);

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_metric_function,
       no_ipv6_ospf6_metric_function_cmd,
       "no ipv6 ospf6 periodic-metric-function",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Disable the periodic metric function\n")
{
  struct ospf6_interface *oi;
  int enabled, err;

  oi = ospf6_interface_vtyget (vty);

  enabled =
    ospf6_interface_neighbor_metric_enabled (oi,
					     metricfunction_nbrmetric_id);
  if (!enabled)
    {
      vty_out (vty, "%s is not enabled for interface %s%s",
	       metricfunction_name, oi->interface->name, VNL);
      return CMD_WARNING;
    }

  schedule_metric_function (oi, NULL, NULL, 0);

  ospf6_interface_reset_neighbor_metric (oi, metricfunction_nbrmetric_id);

  err = ospf6_interface_disable_neighbor_metric (oi,
						 metricfunction_nbrmetric_id);
  if (err)
    {
      vty_out (vty, "could not disable %s for interface %s%s",
	       metricfunction_name, oi->interface->name, VNL);
      return CMD_WARNING;
    }

  return CMD_SUCCESS;
}

static int
ospf6_neighbor_create_metricfunction (struct ospf6_neighbor *on)
{
  struct ospf6_interface_metricfunction *imf;
  int err;

  imf = ospf6_interface_neighbor_metric_data (on->ospf6_if,
					      metricfunction_nbrmetric_id);
  assert (imf);

  if (imf->metric_function)
    err = ospf6_neighbor_run_metricfunction (imf, on);
  else
    err = 0;

  return err;
}

static void
ospf6_interface_config_write_metricfunction (struct ospf6_interface *oi,
					     struct vty *vty)
{
  struct ospf6_interface_metricfunction *imf;

  imf = ospf6_interface_neighbor_metric_data (oi, metricfunction_nbrmetric_id);
  assert (imf);

  if (imf->metric_function == neighbor_time_metric_function)
    vty_out (vty, " ipv6 ospf6 periodic-metric-function neighbor-time %u "
	     "recalculate-interval %u%s",
	     *(u_int16_t *)imf->metric_function_data,
	     imf->metric_function_interval, VNL);
  else if (imf->metric_function)
    zlog_err ("%s: unknown metric function: %p",
	      __func__, imf->metric_function);
}

static void
ospf6_interface_init_metricfunction (void)
{
  install_element (INTERFACE_NODE,
		   &ipv6_ospf6_metric_function_neighbor_time_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_ospf6_metric_function_cmd);
}

static struct ospf6_interface_operations metricfunction_ifops = {
  .init = ospf6_interface_init_metricfunction,
};

OSPF6_INTERFACE_OPERATIONS (metricfunction_ifops);
