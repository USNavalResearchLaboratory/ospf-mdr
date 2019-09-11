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

#include <math.h>

#include "zebra.h"
#include "memory.h"
#include "command.h"
#include "zebra_linkmetrics.h"
#include "stream.h"
#include "lmgenl.h"

#include "ospf6d.h"
#include "ospf6_af.h"
#include "ospf6_interface.h"
#include "ospf6_interface_neighbor_metric.h"
#include "ospf6_zebra_linkmetrics.h"
#include "ospf6_neighbor.h"
#include "ospf6_area.h"
#include "ospf6_zebra.h"

/* these are the values recommended by cisco */
#define DEFAULT_THROUGHPUT_WEIGHT	0
#define DEFAULT_RESOURCES_WEIGHT	29
#define DEFAULT_LATENCY_WEIGHT		29
#define DEFAULT_L2_FACTOR_WEIGHT	29

struct ospf6_linkmetrics_formula {
  const char *vtyname;
  u_int16_t (*linkmetrics_cost)(struct ospf6_neighbor *,
				struct zebra_linkmetrics *);
};

struct ospf6_linkmetrics_filter {
  const char *vtyname;
  int (*filter)(struct ospf6_neighbor *, struct zebra_linkmetrics *);
};

struct ospf6_interface_linkmetrics {
  const struct ospf6_linkmetrics_formula *linkmetrics_formula;
  const struct ospf6_linkmetrics_filter *linkmetrics_filter;
  u_int8_t throughput_weight;
  u_int8_t resources_weight;
  u_int8_t latency_weight;
  u_int8_t l2_factor_weight;
};

struct ospf6_neighbor_linkmetrics {
  /* statistics */
  unsigned int numupdates;
  struct timeval last_update;

  /* most recent raw values */
  struct zebra_rfc4938_linkmetrics last_metrics;

  /* current effective values */
  struct zebra_rfc4938_linkmetrics metrics;
};

static const char *linkmetrics_name = "linkmetrics";
static unsigned int linkmetrics_nbrmetric_id;
static unsigned int linkmetrics_neighbor_data_id;

static void
ospf6_linkstatus_update (struct ospf6_interface *oi,
			 struct ospf6_neighbor *on,
			 zebra_linkstatus_t *linkstatus);
static void
ospf6_linkmetrics_update (struct ospf6_neighbor *on,
			  zebra_linkmetrics_t *linkmetrics);
static void
ospf6_interface_config_write_linkmetrics (struct ospf6_interface *oi,
					  struct vty *vty);
static void
ospf6_interface_cost_update_linkmetrics(struct ospf6_interface *oi);
static int
ospf6_neighbor_create_linkmetrics (struct ospf6_neighbor *on);
static void
ospf6_neighbor_delete_linkmetrics (struct ospf6_neighbor *on);
static void
ospf6_neighbor_state_change_linkmetrics (struct ospf6_neighbor *on,
					 u_char prev_state);

static void
ospf6_interface_delete_linkmetrics (struct ospf6_interface *oi)
{
  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_interface_neighbor_metric_data (oi, linkmetrics_nbrmetric_id);
  assert (ilm);

  ospf6_remove_linkstatus_hook (ospf6_linkstatus_update);
  ospf6_remove_linkmetrics_hook (ospf6_linkmetrics_update);

  XFREE (MTYPE_OSPF6_IF, ilm);
}

/* Send a Linkmetrics request to zebra stream */
static int
ospf6_send_linkmetrics_rqst (struct zclient *zeb_client,
                             zebra_linkmetrics_rqst_t *metrics_rqst)
{
  struct stream *s;
  s = zeb_client->obuf;

  /* initialize the stream */
  stream_reset (s);
  zclient_create_header (s, ZEBRA_LINKMETRICS_METRICS_RQST);

  /* write the metrics_rqst structure */
  stream_putl (s, metrics_rqst->ifindex);
  stream_write (s, (u_char *)&metrics_rqst->linklocal_addr,
                sizeof (metrics_rqst->linklocal_addr));

  /* put length at beginning of stream */
  if (stream_putw_at (s, 0, stream_get_endp (s)) != 2)
      zlog_err ("%s: stream_putw_at() failed for setting length", __func__);

  return zclient_send_message (zeb_client);
}

/* Build a request for Linkmetrics info and send it to Zebra */
static void
ospf6_zebra_linkmetrics_rqst (struct ospf6_neighbor *on)
{
  zebra_linkmetrics_rqst_t msg;

  msg.ifindex = on->ospf6_if->interface->ifindex;
  memcpy (&msg.linklocal_addr, &on->linklocal_addr, sizeof (struct in6_addr));

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    {
      char ipv6_s[INET6_ADDRSTRLEN];

      ospf6_addr2str6 (&msg.linklocal_addr, ipv6_s, sizeof (ipv6_s));
      zlog_debug ("%s: Build a request for LINKMETRICS Info,"
		  " ifindex=%d, ipv6 = %s", __func__, msg.ifindex, ipv6_s);
    }

  ospf6_send_linkmetrics_rqst (zclient, &msg);
}

static int
ospf6_linkmetrics_filter_adjustvalues (struct ospf6_neighbor *on,
				       struct zebra_linkmetrics *linkmetrics)
{
  if (linkmetrics->metrics.resource > 100)
    {
      zlog_warn ("%s: overriding invalid link metric resource value: "
		 "%u -> 100", __func__, linkmetrics->metrics.resource);
      linkmetrics->metrics.resource = 100;
    }

  if (linkmetrics->metrics.rlq > 100)
    {
      zlog_warn ("%s: overriding invalid link metric rlq value: "
		 "%u -> 100", __func__, linkmetrics->metrics.rlq);
      linkmetrics->metrics.rlq = 100;
    }

  if (linkmetrics->metrics.current_datarate >
      linkmetrics->metrics.max_datarate)
    {
      u_int16_t cdr = linkmetrics->metrics.current_datarate;

      /* assume the current datarate value is more correct */
      zlog_warn ("%s: overriding invalid link metric datarate values: "
		 "(current, max) = (%u, %u) -> (%u, %u)", __func__,
		 cdr, linkmetrics->metrics.max_datarate, cdr, cdr);
      linkmetrics->metrics.max_datarate = cdr;
    }

  return 0;
}

static int
ospf6_linkmetrics_validate (zebra_linkmetrics_t *linkmetrics)
{
  if (linkmetrics->metrics.resource > 100)
    {
      zlog_err ("%s: invalid link metric resource value: %u",
		__func__, linkmetrics->metrics.resource);
      return -1;
    }

  if (linkmetrics->metrics.rlq > 100)
    {
      zlog_err ("%s: invalid link metric rlq value: %u",
		__func__, linkmetrics->metrics.rlq);
      return -1;
    }

  if (linkmetrics->metrics.current_datarate >
      linkmetrics->metrics.max_datarate)
    {
      zlog_err ("%s: invalid link metric datarate values: "
		"current = %u; max = %u", __func__,
		linkmetrics->metrics.current_datarate,
		linkmetrics->metrics.max_datarate);
      return -1;
    }

  return 0;
}

static void
ospf6_linkmetrics_update (struct ospf6_neighbor *on,
			  zebra_linkmetrics_t *linkmetrics)
{
  struct ospf6_interface_linkmetrics *ilm;
  struct ospf6_neighbor_linkmetrics *nlm;
  struct ospf6_interface *oi = on->ospf6_if;
  u_int16_t newcost;
  int err;

  ilm = ospf6_interface_neighbor_metric_data (oi, linkmetrics_nbrmetric_id);
  assert (ilm);

  if (!ilm->linkmetrics_formula)
    {
      if (IS_OSPF6_DEBUG_ZEBRA (RECV))
	zlog_debug ("%s: ignoring link metrics update for interface %s: "
		    "no linkmetrics formula enabled",
		    __func__, oi->interface->name);
      return;
    }

  nlm = ospf6_get_neighbor_data (on, linkmetrics_neighbor_data_id);
  assert (nlm);

  /* update statistics */
  nlm->numupdates++;
  quagga_gettime (QUAGGA_CLK_MONOTONIC, &nlm->last_update);
  /* save raw values */
  nlm->last_metrics = linkmetrics->metrics;

  if (ilm->linkmetrics_filter)
    {
      err = ilm->linkmetrics_filter->filter (on, linkmetrics);
      if (err)
	{
	  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
	    {
	      zlog_debug ("%s: link metrics update for neighbor %s "
			  "suppressed by filter %s:", __func__,
			  on->name, ilm->linkmetrics_filter->vtyname);
	      zebra_linkmetrics_logdebug (linkmetrics);
	    }
	  return;
	}
    }

  err = ospf6_linkmetrics_validate (linkmetrics);
  if (err)
    {
      zlog_warn ("%s: invalid link metrics update for neighbor %s:",
		 __func__, on->name);
      zebra_linkmetrics_logdebug (linkmetrics);
      return;
    }

  newcost = ilm->linkmetrics_formula->linkmetrics_cost (on, linkmetrics);
  if (newcost == 0)
    {
      zlog_warn ("%s: link metrics cost formula %s "
		 "returned invalid cost: %u", __func__,
		 ilm->linkmetrics_formula->vtyname, newcost);
      newcost = oi->cost;
    }

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    zlog_debug ("%s: new cost for neighbor %s: %u",
		__func__, on->name, newcost);

  /* save effective (filtered) values */
  nlm->metrics = linkmetrics->metrics;

  err = ospf6_interface_update_neighbor_metric (on, newcost,
						linkmetrics_nbrmetric_id);
  if (err)
    zlog_err ("%s: ospf6_interface_update_neighbor_metric() failed "
	      "for neighbor %s", __func__, on->name);
}

/*
 * Send a hello now, without waiting for hello interval.  Used to find
 * neighbors that just came up
 */
static void
ospf6_send_hello_now (struct ospf6_interface *oi)
{
    /* Stop hello timer thread */
    THREAD_OFF (oi->thread_send_hello);

    /* Start ospf_hello_send thread to send immediate hello message on
       the interface and in turn activate the hello timer mechanism
       for future polls */
    oi->thread_send_hello = thread_add_event (master, ospf6_hello_send, oi, 0);
}

static void
ospf6_linkstatus_update (struct ospf6_interface *oi,
			 struct ospf6_neighbor *on,
			 zebra_linkstatus_t *linkstatus)
{
  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_interface_neighbor_metric_data (oi, linkmetrics_nbrmetric_id);
  assert (ilm);

  if (!ilm->linkmetrics_formula)
    {
      if (IS_OSPF6_DEBUG_ZEBRA (RECV))
	{
	  char statusstr[40], lladdrstr[INET6_ADDRSTRLEN];

	  zebra_linkstatus_string (statusstr, sizeof (statusstr),
				   linkstatus->status);
	  ospf6_addr2str6 (&linkstatus->linklocal_addr,
			   lladdrstr, sizeof (lladdrstr));
	  zlog_debug ("%s: ignoring link status update on interface %s "
		      "from %s: %s: no linkmetrics formula enabled",
		      __func__, oi->interface->name, lladdrstr, statusstr);
	}

      return;
    }

  switch (linkstatus->status)
    {
    case LM_STATUS_UP:
      if (conf_debug_ospf6_zebra)
	{
	  char statusstr[40], lladdrstr[INET6_ADDRSTRLEN];

	  zebra_linkstatus_string (statusstr, sizeof (statusstr),
				   linkstatus->status);
	  ospf6_addr2str6 (&linkstatus->linklocal_addr,
			   lladdrstr, sizeof (lladdrstr));
	  zlog_debug ("%s: Expediting Hello mechanism due to reception of "
		      "link status UP message on interface %s from %s: %s",
		      __func__, oi->interface->name, lladdrstr, statusstr);
	}

      /* Expedite the Hello mechanism to find the new neighbor that
	 just came up */
      if (on == NULL)
	ospf6_send_hello_now (oi);
      break;

    case LM_STATUS_DOWN:
      if (conf_debug_ospf6_zebra)
	zlog_debug ("%s: removing neighbor %s: link status down",
		    __func__, on->name);
      THREAD_OFF (on->inactivity_timer);
      thread_add_event (master, inactivity_timer, on, 0);
      break;

    default:
      {
	char statusstr[40], lladdrstr[INET6_ADDRSTRLEN];

	zebra_linkstatus_string (statusstr, sizeof (statusstr),
				 linkstatus->status);
	ospf6_addr2str6 (&linkstatus->linklocal_addr,
			 lladdrstr, sizeof (lladdrstr));
	zlog_err ("%s: ignoring link status for neighbor %s "
		  "on interface %s from %s: %s", __func__, on->name,
		  oi->interface->name, lladdrstr, statusstr);
      }
      break;
    }
}

static u_int16_t
ospf6_linkmetrics_formula_cisco (struct ospf6_neighbor *on,
				 zebra_linkmetrics_t *linkmetrics)
{
  u_int16_t newcost;
  struct ospf6_interface *oi = on->ospf6_if;
  double oc, bw, res, lat, l2, cost;
  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_interface_neighbor_metric_data (oi, linkmetrics_nbrmetric_id);
  assert (ilm);

  /*
    based on Cisco MANET configuration guide:

    http://www.cisco.com/en/US/docs/ios/ipmobility/configuration/guide/imo_adhoc_rtr2rd_ps6441_TSD_Products_Configuration_Guide_Chapter.html
  */

  if (linkmetrics->metrics.max_datarate)
    {
      oc = 1e5 / (double)linkmetrics->metrics.max_datarate;
    }
  else
    {
      zlog_warn ("%s: link metrics max_datarate is zero", __func__);
      oc = (double)oi->cost;
    }

  if (linkmetrics->metrics.max_datarate &&
      linkmetrics->metrics.current_datarate)
    {
      bw = ((65536.0 *
	     (100.0 -
	      (100.0 * ((double)linkmetrics->metrics.current_datarate /
			(double)linkmetrics->metrics.max_datarate)))) / 100.0) *
	((double)ilm->throughput_weight / 100.0);
    }
  else
    {
      zlog_warn ("%s: link metrics max_datarate or current_datarate is zero",
		 __func__);
      bw = 0.0;
    }

  if (linkmetrics->metrics.resource)
    {
      long tmp;

      tmp = (100 - linkmetrics->metrics.resource);
      res = ((double)(tmp * tmp * tmp) * 65536.0 / 1e6) *
	(double)ilm->resources_weight / 100.0;
    }
  else
    {
      zlog_warn ("%s: link metrics resource is zero", __func__);
      res = 0.0;
    }

  if (linkmetrics->metrics.latency)
    {
      lat = (double)linkmetrics->metrics.latency *
	(double)ilm->latency_weight / 100.0;
    }
  else
    {
      zlog_warn ("%s: link metrics latency is zero", __func__);
      lat = 0.0;
    }

  if (linkmetrics->metrics.rlq)
    {
      l2 = ((double)(100 - linkmetrics->metrics.rlq) * 65536.0 / 100.0) *
	(double)ilm->l2_factor_weight / 100.0;
    }
  else
    {
      zlog_warn ("%s: link metrics rlq is zero", __func__);
      l2 = 0.0;
    }

  cost = oc + bw + res + lat + l2;

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    zlog_debug ("%s: cost calculation for neighbor %s: cost = %0.2f; "
		"oc = %0.2f; bw = %0.2f; res = %0.2f; lat = %0.2f; l2 = %0.2f",
		__func__, on->name, cost, oc, bw, res, lat, l2);

  if (cost < 0.0)
    {
      zlog_err ("%s: invalid cost calculated for neighbor %s: cost = %0.2f; "
		"oc = %0.2f; bw = %0.2f; res = %0.2f; lat = %0.2f; l2 = %0.2f",
		__func__, on->name, cost, oc, bw, res, lat, l2);
      zlog_err ("%s: input link metrics for invalid cost:", __func__);
      zebra_linkmetrics_logdebug (linkmetrics);
      zlog_err ("%s: weights used for invalid cost: %u %u %u %u",
		__func__, ilm->throughput_weight, ilm->resources_weight,
		ilm->latency_weight, ilm->l2_factor_weight);

      return on->cost;
    }

  if (cost < 1.0)
    newcost = 1;
  else if (cost > (double)UINT16_MAX)
    newcost = UINT16_MAX;
  else
    newcost = (u_int16_t)cost;

  return newcost;
}

static u_int16_t
ospf6_linkmetrics_formula_nrlcable (struct ospf6_neighbor *on,
				    zebra_linkmetrics_t *linkmetrics)
{
  u_int16_t newcost;
  struct ospf6_interface *oi = on->ospf6_if;
  double lat_cost, cdr_cost, cost;

  /* formula variables */
  u_int16_t max_cost = 1000;
  double lat_steepness = 0.0015;
  double cdr_steepness = 0.0015;

  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_interface_neighbor_metric_data (oi, linkmetrics_nbrmetric_id);
  assert (ilm);

  /*
    lat_cost = 1000*(1-e^-0.0015*lat)*(lat_weight/100)
    cdr_cost = 1000*(e^-0.0015*cdr)*(cdr_weight/100)

    cost = lat_cost + cdr_cost
  */

  if (linkmetrics->metrics.current_datarate)
    {
      cdr_cost = max_cost *
	exp (-1.0 * cdr_steepness * (double)linkmetrics->metrics.current_datarate) *
	(double)ilm->throughput_weight / 100.0;
    }
  else
    {
      zlog_warn ("%s: link metrics current_datarate is zero", __func__);
      cdr_cost = 0.0;
    }

  if (linkmetrics->metrics.latency)
    {
      lat_cost = max_cost *
	(1.0 - exp (-1.0 * lat_steepness * (double)linkmetrics->metrics.latency)) *
	(double)ilm->latency_weight / 100.0;
    }
  else
    {
      zlog_warn ("%s: link metrics latency is zero", __func__);
      lat_cost = 0.0;
    }

  cost = lat_cost + cdr_cost;

  if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    zlog_debug ("%s: cost calculation for neighbor %s: cost = %0.2f; "
		"cdr_cost = %0.2f; lat_cost = %0.2f",
		__func__, on->name, cost, cdr_cost, lat_cost);

  if (cost < 0.0)
    {
      zlog_err ("%s: invalid cost calculated for neighbor %s: cost = %0.2f; "
		"cdr_cost = %0.2f; lat_cost = %0.2f",
		__func__, on->name, cost, cdr_cost, lat_cost);
      zlog_err ("%s: input link metrics for invalid cost:", __func__);
      zebra_linkmetrics_logdebug (linkmetrics);
      zlog_err ("%s: weights used for invalid cost: %u %u %u %u",
		__func__, ilm->throughput_weight, ilm->resources_weight,
		ilm->latency_weight, ilm->l2_factor_weight);

      return on->cost;
    }

  if (cost < 1.0)
    newcost = 1;
  else if (cost > (double)UINT16_MAX)
    newcost = UINT16_MAX;
  else
    newcost = (u_int16_t)cost;

  return newcost;
}

static int
ospf6_interface_register_linkmetrics (struct ospf6_interface *oi,
				      struct vty *vty)
{
  int err;
  struct ospf6_interface_linkmetrics *ilm;
  struct ospf6_interface_neighbor_metric_params nbrmetric_params = {
    .name = linkmetrics_name,
    .delete = ospf6_interface_delete_linkmetrics,
    .config_write = ospf6_interface_config_write_linkmetrics,
    .cost_update = ospf6_interface_cost_update_linkmetrics,
    .nbrops = {
      .create = ospf6_neighbor_create_linkmetrics,
      .delete = ospf6_neighbor_delete_linkmetrics,
      .state_change = ospf6_neighbor_state_change_linkmetrics,
    },
  };

  ilm = XCALLOC (MTYPE_OSPF6_IF, sizeof (*ilm));
  /* link metrics default values */
  ilm->linkmetrics_formula = NULL;
  ilm->linkmetrics_filter = NULL;
  ilm->throughput_weight = DEFAULT_THROUGHPUT_WEIGHT;
  ilm->resources_weight = DEFAULT_RESOURCES_WEIGHT;
  ilm->latency_weight = DEFAULT_LATENCY_WEIGHT;
  ilm->l2_factor_weight = DEFAULT_L2_FACTOR_WEIGHT;

  nbrmetric_params.data = ilm;
  err =
    ospf6_interface_register_neighbor_metric (oi, &linkmetrics_nbrmetric_id,
					      &nbrmetric_params, vty);
  if (err)
    {
      vty_out (vty, "could not register neighbor metric %s on interface %s%s",
	       nbrmetric_params.name, oi->interface->name, VNL);
      XFREE (MTYPE_OSPF6_IF, ilm);
      return -1;
    }

  return 0;
}

static struct ospf6_linkmetrics_formula linkmetric_formulas[] =  {
  {
    .vtyname = "cisco",
    .linkmetrics_cost = ospf6_linkmetrics_formula_cisco,
  },
  {
    .vtyname = "nrl-cable",
    .linkmetrics_cost = ospf6_linkmetrics_formula_nrlcable,
  },
  {}				/* terminating entry */
};

/* the order here doesn't matter */
#define OSPF6_LINKMETRICS_FORMULAS "(cisco|nrl-cable)"
/* this order must correspond to the order above */
#define OSPF6_LINKMETRICS_FORMULA_STR			\
  "The default Cisco link metrics formula\n"		\
  "The link metrics formula used by NRL CABLE\n"

DEFUN (ipv6_ospf6_linkmetrics_formula,
       ipv6_ospf6_linkmetrics_formula_cmd,
       "ipv6 ospf6 linkmetric-formula " OSPF6_LINKMETRICS_FORMULAS,
       IP6_STR
       OSPF6_STR
       "Enable using the specified link metrics formula\n"
       OSPF6_LINKMETRICS_FORMULA_STR)
{
  struct ospf6_interface *oi;
  size_t arglen;
  const struct ospf6_linkmetrics_formula *formula;
  struct ospf6_interface_linkmetrics *ilm;
  int registered, err;

  oi = ospf6_interface_vtyget (vty);

  registered =
    ospf6_interface_neighbor_metric_registered (oi,
						linkmetrics_nbrmetric_id);
  if (!registered)
    {
      err = ospf6_interface_register_linkmetrics (oi, vty);
      if (err)
	return CMD_WARNING;
    }

  err = ospf6_interface_enable_neighbor_metric (oi, linkmetrics_nbrmetric_id);
  if (err)
    vty_out (vty, "could not enable neighbor metric %s on interface %s%s",
	     linkmetrics_name, oi->interface->name, VNL);

  arglen = strlen (argv[0]);
  for (formula = linkmetric_formulas; formula->vtyname; formula++)
    if (strncmp (argv[0], formula->vtyname, arglen) == 0)
      break;

  if (!formula->vtyname)
    {
      vty_out (vty, "unknown link metrics formula: %s%s", argv[0], VNL);
      return CMD_WARNING;
    }

  ilm = ospf6_interface_neighbor_metric_data (oi, linkmetrics_nbrmetric_id);
  assert (ilm);

  if (formula != ilm->linkmetrics_formula)
    {
      struct listnode *node;
      struct ospf6_neighbor *on;
      int err;

      ospf6_remove_linkstatus_hook (ospf6_linkstatus_update);
      err = ospf6_add_linkstatus_hook (ospf6_linkstatus_update);
      if (err)
	{
	  vty_out (vty, "error adding link status callback%s", VNL);
	  return CMD_WARNING;
	}

      ospf6_remove_linkmetrics_hook (ospf6_linkmetrics_update);
      err = ospf6_add_linkmetrics_hook (ospf6_linkmetrics_update);
      if (err)
	{
	  vty_out (vty, "error adding link metrics update callback%s", VNL);
	  return CMD_WARNING;
	}

      ilm->linkmetrics_formula = formula;

      for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
	{
	  struct ospf6_neighbor_linkmetrics *nlm;

	  nlm = ospf6_get_neighbor_data (on, linkmetrics_neighbor_data_id);
	  assert (nlm);

	  if (nlm->metrics.rlq || nlm->metrics.resource ||
	      nlm->metrics.latency || nlm->metrics.current_datarate ||
	      nlm->metrics.max_datarate)
	    {
	      zebra_linkmetrics_t linkmetrics = {
		.ifindex = oi->interface->ifindex,
		.linklocal_addr = on->linklocal_addr,
		.metrics = nlm->metrics,
	      };

	      ospf6_zebra_update_linkmetrics (on, &linkmetrics);
	    }
	  else
	    {
	      ospf6_zebra_linkmetrics_rqst (on);
	    }
	}
    }

  return CMD_SUCCESS;
}

#undef OSPF6_LINKMETRICS_FORMULAS
#undef OSPF6_LINKMETRICS_FORMULA_STR

DEFUN (no_ipv6_ospf6_linkmetrics_formula,
       no_ipv6_ospf6_linkmetrics_formula_cmd,
       "no ipv6 ospf6 linkmetric-formula",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Disable using link metrics from zebra\n")
{
  struct ospf6_interface *oi;
  struct ospf6_interface_linkmetrics *ilm;
  int enabled, err;

  oi = ospf6_interface_vtyget (vty);

  enabled = ospf6_interface_neighbor_metric_enabled (oi,
						     linkmetrics_nbrmetric_id);
  if (!enabled)
    {
      vty_out (vty, "link metrics not enabled for interface %s%s",
	       oi->interface->name, VNL);
      return CMD_WARNING;
    }

  ilm = ospf6_interface_neighbor_metric_data (oi, linkmetrics_nbrmetric_id);
  assert (ilm);

  if (ilm->linkmetrics_formula)
    {
      ospf6_remove_linkstatus_hook (ospf6_linkstatus_update);
      ospf6_remove_linkmetrics_hook (ospf6_linkmetrics_update);

      ilm->linkmetrics_formula = NULL;

      ospf6_interface_reset_neighbor_metric (oi, linkmetrics_nbrmetric_id);
    }

  err = ospf6_interface_disable_neighbor_metric (oi,
						 linkmetrics_nbrmetric_id);
  if (err)
    {
      vty_out (vty, "could not disable %s for interface %s%s",
	       linkmetrics_name, oi->interface->name, VNL);
      return CMD_WARNING;
    }

  return CMD_SUCCESS;
}

static struct ospf6_interface_linkmetrics *
ospf6_linkmetrics_interface_data (struct vty *vty)
{
  struct ospf6_interface *oi;
  int enabled;
  struct ospf6_interface_linkmetrics *ilm;

  oi = ospf6_interface_vtyget (vty);

  enabled = ospf6_interface_neighbor_metric_enabled (oi,
						     linkmetrics_nbrmetric_id);
  if (!enabled)
    {
      vty_out (vty, "link metrics not enabled for interface %s%s",
	       oi->interface->name, VNL);
      return NULL;
    }

  ilm = ospf6_interface_neighbor_metric_data (oi,
					      linkmetrics_nbrmetric_id);
  assert (ilm);

  return ilm;
}

DEFUN (ipv6_ospf6_linkmetric_weight_throughput,
       ipv6_ospf6_linkmetric_weight_throughput_cmd,
       "ipv6 ospf6 linkmetric-weight-throughput <0-100>",
       IP6_STR
       OSPF6_STR
       "Throughput weight used in link metrics cost function\n"
       "Throughput weight value\n")
{
  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_linkmetrics_interface_data (vty);
  if (ilm == NULL)
    return CMD_WARNING;

  /* command parsing ensures that argv[0] is a valid integer and in
     range specified above */
  ilm->throughput_weight = strtol (argv[0], NULL, 10);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_linkmetric_weight_resources,
       ipv6_ospf6_linkmetric_weight_resources_cmd,
       "ipv6 ospf6 linkmetric-weight-resources <0-100>",
       IP6_STR
       OSPF6_STR
       "Resources weight used in link metrics cost function\n"
       "Resources weight value\n")
{
  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_linkmetrics_interface_data (vty);
  if (ilm == NULL)
    return CMD_WARNING;

  /* command parsing ensures that argv[0] is a valid integer and in
     range specified above */
  ilm->resources_weight = strtol (argv[0], NULL, 10);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_linkmetric_weight_latency,
       ipv6_ospf6_linkmetric_weight_latency_cmd,
       "ipv6 ospf6 linkmetric-weight-latency <0-100>",
       IP6_STR
       OSPF6_STR
       "Latency weight used in link metrics cost function\n"
       "Latency weight value\n")
{
  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_linkmetrics_interface_data (vty);
  if (ilm == NULL)
    return CMD_WARNING;

  /* command parsing ensures that argv[0] is a valid integer and in
     range specified above */
  ilm->latency_weight = strtol (argv[0], NULL, 10);

  return CMD_SUCCESS;
}

DEFUN (ipv6_ospf6_linkmetric_weight_l2_factor,
       ipv6_ospf6_linkmetric_weight_l2_factor_cmd,
       "ipv6 ospf6 linkmetric-weight-l2_factor <0-100>",
       IP6_STR
       OSPF6_STR
       "L2_Factor weight used in link metrics cost function\n"
       "L2_Factor weight value\n")
{
  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_linkmetrics_interface_data (vty);
  if (ilm == NULL)
    return CMD_WARNING;

  /* command parsing ensures that argv[0] is a valid integer and in
     range specified above */
  ilm->l2_factor_weight = strtol (argv[0], NULL, 10);

  return CMD_SUCCESS;
}

static struct ospf6_linkmetrics_filter linkmetric_filters[] =  {
  {
    .vtyname = "adjust-values",
    .filter = ospf6_linkmetrics_filter_adjustvalues,
  },
  {}				/* terminating entry */
};

/* the order here doesn't matter */
#define OSPF6_LINKMETRICS_FILTERS "(adjust-values|)"
/* this order must correspond to the order above */
#define OSPF6_LINKMETRICS_FILTER_STR			\
  "Override invalid link metrics values\n"

DEFUN (ipv6_ospf6_linkmetrics_filter_updates,
       ipv6_ospf6_linkmetrics_filter_updates_cmd,
       "ipv6 ospf6 linkmetric-update-filter " OSPF6_LINKMETRICS_FILTERS,
       IP6_STR
       OSPF6_STR
       "Enable filtering link metrics updates from zebra\n"
       OSPF6_LINKMETRICS_FILTER_STR)
{
  struct ospf6_interface_linkmetrics *ilm;
  size_t arglen;
  const struct ospf6_linkmetrics_filter *filter;

  ilm = ospf6_linkmetrics_interface_data (vty);
  if (ilm == NULL)
    return CMD_WARNING;

  arglen = strlen (argv[0]);
  for (filter = linkmetric_filters; filter->vtyname; filter++)
    if (strncmp (argv[0], filter->vtyname, arglen) == 0)
      break;

  if (!filter->vtyname)
    {
      vty_out (vty, "unknown link metrics filter: %s%s", argv[0], VNL);
      return CMD_WARNING;
    }

  ilm->linkmetrics_filter = filter;

  return CMD_SUCCESS;
}

DEFUN (no_ipv6_ospf6_linkmetrics_filter_updates,
       no_ipv6_ospf6_linkmetrics_filter_updates_cmd,
       "no ipv6 ospf6 linkmetric-update-filter",
       NO_STR
       IP6_STR
       OSPF6_STR
       "Disable filtering link metrics updates from zebra\n")
{
  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_linkmetrics_interface_data (vty);
  if (ilm == NULL)
    return CMD_WARNING;

  ilm->linkmetrics_filter = NULL;

  return CMD_SUCCESS;
}

static void
ospf6_show_neighbor_linkmetrics (struct vty *vty, struct ospf6_neighbor *on,
				 struct timeval *now)
{
  struct ospf6_neighbor_linkmetrics *nlm;
  struct timeval delta;
  char timestr[32];

  nlm = ospf6_get_neighbor_data (on, linkmetrics_neighbor_data_id);
  assert (nlm);

  vty_out (vty, "neighbor %s link metrics:%s", on->name, VNL);
  if (nlm->numupdates == 0)
    {
      vty_out (vty, "  no updates received%s", VNL);
      return;
    }

  vty_out (vty, "  num updates:        %u%s", nlm->numupdates, VNL);
  timersub (now, &nlm->last_update, &delta);
  timerstring (&delta, timestr, sizeof (timestr));
  vty_out (vty, "  last update time:   -%s.%06ld%s",
	   timestr, delta.tv_usec, VNL);

  vty_out (vty, "  last update values:%s", VNL);
  vty_out (vty, "    rlq:              %u%s",
	   nlm->last_metrics.rlq, VNL);
  vty_out (vty, "    resource:         %u%s",
	   nlm->last_metrics.resource, VNL);
  vty_out (vty, "    latency:          %u%s",
	   nlm->last_metrics.latency, VNL);
  vty_out (vty, "    current datarate: %u%s",
	   nlm->last_metrics.current_datarate, VNL);
  vty_out (vty, "    max datarate:     %u%s",
	   nlm->last_metrics.max_datarate, VNL);

  vty_out (vty, "  current effective values:%s", VNL);
  vty_out (vty, "    rlq:              %u%s",
	   nlm->metrics.rlq, VNL);
  vty_out (vty, "    resource:         %u%s",
	   nlm->metrics.resource, VNL);
  vty_out (vty, "    latency:          %u%s",
	   nlm->metrics.latency, VNL);
  vty_out (vty, "    current datarate: %u%s",
	   nlm->metrics.current_datarate, VNL);
  vty_out (vty, "    max datarate:     %u%s",
	   nlm->metrics.max_datarate, VNL);
}

DEFUN (show_ipv6_ospf6_neighbor_linkmetrics,
       show_ipv6_ospf6_neighbor_linkmetrics_cmd,
       "show ipv6 ospf6 neighbor-linkmetrics [A.B.C.D]",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Neighbor link metrics\n"
       "Optional router-id in dotted quad notation\n")
{
  struct listnode *i;
  struct ospf6_area *oa;
  u_int32_t routerid;
  int numnbr = 0;
  struct timeval now;

  OSPF6_CMD_CHECK_RUNNING ();

  if (argc && ospf6_str2id (argv[0], &routerid))
    {
      vty_out (vty, "invalid router-id: '%s'%s", argv[0], VNL);
      return CMD_WARNING;
    }

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);

  for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, i, oa))
    {
      struct listnode *j;
      struct ospf6_interface *oi;

      for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
	{
	  struct listnode *k;
	  struct ospf6_neighbor *on;
	  int enabled;

	  enabled = ospf6_interface_neighbor_metric_enabled (oi,
							     linkmetrics_nbrmetric_id);
	  if (argc == 0 && !enabled)
	    continue;

	  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, k, on))
	    {
	      if (argc && on->router_id != routerid)
		continue;

	      if (enabled)
		{
		  if (numnbr)
		    vty_out (vty, "%s", VNL);

		  ospf6_show_neighbor_linkmetrics (vty, on, &now);
		}
	      else
		{
		  vty_out (vty, "link metrics not enabled for interface %s%s",
			   oi->interface->name, VNL);
		}

	      numnbr++;
	    }
	}
    }

  if (!numnbr)
    {
      if (argc)
	vty_out (vty, "neighbor %s not found%s", argv[0], VNL);
      else
	vty_out (vty, "no neighbors found with link metrics enabled%s", VNL);
    }

  return CMD_SUCCESS;
}

static int
ospf6_neighbor_create_linkmetrics (struct ospf6_neighbor *on)
{
  struct ospf6_neighbor_linkmetrics *nlm;
  int err;

  nlm = XCALLOC (MTYPE_OSPF6_NEIGHBOR, sizeof (*nlm));

  err = ospf6_add_neighbor_data (on, &linkmetrics_neighbor_data_id, nlm);
  if (err)
    XFREE (MTYPE_OSPF6_NEIGHBOR, nlm);

  return err;
}

static void
ospf6_neighbor_delete_linkmetrics (struct ospf6_neighbor *on)
{
  struct ospf6_neighbor_linkmetrics *nlm;

  nlm = ospf6_del_neighbor_data (on, linkmetrics_neighbor_data_id);
  assert (nlm);

  XFREE (MTYPE_OSPF6_NEIGHBOR, nlm);
}

static void
ospf6_neighbor_state_change_linkmetrics (struct ospf6_neighbor *on,
					 u_char prev_state)
{
  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_interface_neighbor_metric_data (on->ospf6_if,
					      linkmetrics_nbrmetric_id);
  assert (ilm);

  /* If we reached TWO way state for this neighbor, we need to request
     linkmetrics information from zebra and subsequent ppp/CVMI */
  if (ilm->linkmetrics_formula &&
      on->state >= OSPF6_NEIGHBOR_TWOWAY && prev_state < OSPF6_NEIGHBOR_TWOWAY)
    ospf6_zebra_linkmetrics_rqst (on);
}

static void
ospf6_interface_init_linkmetrics (void)
{
  install_element (INTERFACE_NODE,
		   &ipv6_ospf6_linkmetrics_formula_cmd);
  install_element (INTERFACE_NODE,
		   &no_ipv6_ospf6_linkmetrics_formula_cmd);
  install_element (INTERFACE_NODE,
		   &ipv6_ospf6_linkmetric_weight_throughput_cmd);
  install_element (INTERFACE_NODE,
		   &ipv6_ospf6_linkmetric_weight_resources_cmd);
  install_element (INTERFACE_NODE,
		   &ipv6_ospf6_linkmetric_weight_latency_cmd);
  install_element (INTERFACE_NODE,
		   &ipv6_ospf6_linkmetric_weight_l2_factor_cmd);
  install_element (INTERFACE_NODE,
		   &ipv6_ospf6_linkmetrics_filter_updates_cmd);
  install_element (INTERFACE_NODE,
		   &no_ipv6_ospf6_linkmetrics_filter_updates_cmd);

  install_element (ENABLE_NODE, &show_ipv6_ospf6_neighbor_linkmetrics_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_neighbor_linkmetrics_cmd);
}

static void
ospf6_interface_config_write_linkmetrics (struct ospf6_interface *oi,
					  struct vty *vty)
{
  struct ospf6_interface_linkmetrics *ilm;

  ilm = ospf6_interface_neighbor_metric_data (oi, linkmetrics_nbrmetric_id);
  assert (ilm);

  if (ilm->linkmetrics_formula)
    {
      vty_out (vty, " ipv6 ospf6 linkmetric-formula %s%s",
	       ilm->linkmetrics_formula->vtyname, VNL);
      vty_out (vty, " ipv6 ospf6 linkmetric-weight-throughput %u%s",
	       ilm->throughput_weight, VNL);
      vty_out (vty, " ipv6 ospf6 linkmetric-weight-resources %u%s",
	       ilm->resources_weight, VNL);
      vty_out (vty, " ipv6 ospf6 linkmetric-weight-latency %u%s",
	       ilm->latency_weight, VNL);
      vty_out (vty, " ipv6 ospf6 linkmetric-weight-l2_factor %u%s",
	       ilm->l2_factor_weight, VNL);
    }

  if (ilm->linkmetrics_filter)
    vty_out (vty, " ipv6 ospf6 linkmetric-update-filter %s%s",
	     ilm->linkmetrics_filter->vtyname, VNL);
}

static void
ospf6_interface_cost_update_linkmetrics(struct ospf6_interface *oi)
{
  struct ospf6_interface_linkmetrics *ilm;
  struct listnode *node;
  struct ospf6_neighbor *on;

  ilm = ospf6_interface_neighbor_metric_data (oi, linkmetrics_nbrmetric_id);
  assert (ilm);

  if (!ilm->linkmetrics_formula)
    {
      /* no link metrics formula is enabled; just reset cost for all
	 neighbors */
      ospf6_interface_reset_neighbor_metric (oi, linkmetrics_nbrmetric_id);
      return;
    }

  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, node, on))
    {
      struct ospf6_neighbor_linkmetrics *nlm;
      int err;

      nlm = ospf6_get_neighbor_data (on, linkmetrics_neighbor_data_id);
      assert (nlm);

      if (timerisset(&nlm->last_update))
	continue;    /* cost was determined by link metrics formula */

      err = ospf6_interface_update_neighbor_metric (on, oi->cost,
						    linkmetrics_nbrmetric_id);
      if (err)
	zlog_warn ("could not update cost for neighbor %s", on->name);
    }
}

static struct ospf6_interface_operations linkmetrics_ifops = {
  .init = ospf6_interface_init_linkmetrics,
};

OSPF6_INTERFACE_OPERATIONS (linkmetrics_ifops);
