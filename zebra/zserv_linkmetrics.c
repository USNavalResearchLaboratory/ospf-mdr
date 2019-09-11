/* Zebra functions for sending link metrics to clients
 * Copyright (C) 2009 The Boeing Company
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
#include "zserv.h"
#include "debug.h"
#include "log.h"
#include "stream.h"
#include "command.h"
#include "zebra_linkmetrics.h"
#include "zserv_linkmetrics.h"
#include "linkmetrics_netlink.h"

extern struct zebra_t zebrad;

#ifdef HAVE_LIBNL

DEFUN (linkmetrics_netlink_multicast_group,
       linkmetrics_netlink_multicast_group_cmd,
       "netlink linkmetrics-multicast-group <1-64>",
       "Netlink configuration\n"
       "RFC 4938 link metrics multicast group id\n"
       "Group number\n")
{
  int err;

  if (lmgenl_sock.sk != NULL)
    linkmetrics_netlink_close ();

  lmgenl_mcgroup_id = strtol (argv[0], NULL, 10);

  err = linkmetrics_netlink_init ();
  if (err)
    {
      vty_out (vty, "ERROR: linkmetrics netlink initialization failed%s",
	       VTY_NEWLINE);
      return CMD_WARNING;
    }

  return CMD_SUCCESS;
}

DEFUN (no_linkmetrics_netlink_multicast_group,
       no_linkmetrics_netlink_multicast_group_cmd,
       "no netlink linkmetrics-multicast-group",
       NO_STR
       "Netlink configuration\n"
       "RFC 4938 link metrics multicast group id\n")
{
  if (lmgenl_sock.sk != NULL)
    linkmetrics_netlink_close ();

  lmgenl_mcgroup_id = 0;

  return CMD_SUCCESS;
}

void
zserv_linkmetrics_init (void)
{
  install_element (CONFIG_NODE, &linkmetrics_netlink_multicast_group_cmd);
  install_element (CONFIG_NODE, &no_linkmetrics_netlink_multicast_group_cmd);
}

int
zserv_linkmetrics_config_write (struct vty *vty)
{
  if (lmgenl_mcgroup_id != DEFAULT_LMGENL_MCGROUP_ID)
    vty_out (vty, "netlink linkmetrics-multicast-group %d%s",
	     lmgenl_mcgroup_id, VTY_NEWLINE);

  return 0;
}

#else  /* HAVE_LIBNL */

void
zserv_linkmetrics_init (void)
{
}

int
zserv_linkmetrics_config_write (struct vty *vty)
{
  return 0;
}

#endif	/* HAVE_LIBNL */

/* receive a link metrics update */
int
zserv_linkmetrics (zebra_linkmetrics_t *linkmetrics)
{
  struct listnode *node;
  struct zserv *client;

  if (IS_ZEBRA_DEBUG_EVENT)
    {
      zlog_debug ("%s: received link metrics update", __func__);
      zebra_linkmetrics_logdebug (linkmetrics);
    }

  /* send to all subscribed clients */
  for (ALL_LIST_ELEMENTS_RO (zebrad.client_list, node, client))
    if (client->linkmetrics_subscribed)
      {
	if (zapi_write_linkmetrics (client->obuf, linkmetrics))
	  {
	    zlog_warn ("%s: zapi_write_linkmetrics() failed for "
		       "client on fd %d", __func__, client->sock);
	    continue;
	  }

	if (zebra_server_send_message (client))
	  zlog_warn ("%s: zebra_server_send_message() failed for "
		     "client on fd %d", __func__, client->sock);
      }

  return 0;
}

/* receive a link status update */
int
zserv_linkstatus (zebra_linkstatus_t *linkstatus)
{
  struct listnode *node;
  struct zserv *client;

  if (IS_ZEBRA_DEBUG_EVENT)
    {
      zlog_debug ("%s: received link status update", __func__);
      zebra_linkstatus_logdebug (linkstatus);
    }

  /* send to all subscribed clients */
  for (ALL_LIST_ELEMENTS_RO (zebrad.client_list, node, client))
    if (client->linkmetrics_subscribed)
      {
	if (zapi_write_linkstatus (client->obuf, linkstatus))
	  {
	    zlog_warn ("%s: zapi_write_linkstatus() failed for "
		       "client on fd %d", __func__, client->sock);
	    continue;
	  }

	if (zebra_server_send_message (client))
	  zlog_warn ("%s: zebra_server_send_message() failed for "
		     "client on fd %d", __func__, client->sock);
      }

  return 0;
}

/* receive a linkmetrics subscribe/unsubscribe */
int
zserv_linkmetrics_subscribe (uint16_t cmd, struct zserv *client,
			     uint16_t length)
{
  int r = 0;

  if (length)
    {
      zlog_err ("%s: invalid length: %u", __func__, length);
      return -1;
    }

  switch (cmd)
    {
    case ZEBRA_LINKMETRICS_SUBSCRIBE:
      client->linkmetrics_subscribed = 1;
      break;

    case ZEBRA_LINKMETRICS_UNSUBSCRIBE:
      client->linkmetrics_subscribed = 0;
      break;

    default:
      zlog_err ("%s: invalid zebra linkmetrics subscribe command: %u",
		__func__, cmd);
      r = -1;
      break;
    }

  return r;
}

#ifdef HAVE_LIBNL
/* Unserialize the Linkmetric Request structure */
static int
zserv_read_linkmetrics_rqst (zebra_linkmetrics_rqst_t *linkmetrics_rqst,
			     struct stream *s, u_short length)
{
  if (length != sizeof (*linkmetrics_rqst))
    {
      zlog_err ("%s: invalid length: %u", __func__, length);
      return -1;
    }

  linkmetrics_rqst->ifindex = stream_getl (s);

  stream_get (&linkmetrics_rqst->linklocal_addr, s,
              sizeof (linkmetrics_rqst->linklocal_addr));

  if (IS_ZEBRA_DEBUG_EVENT)
    {
      char ipv6_s[INET6_ADDRSTRLEN];

      inet_ntop(AF_INET6, &linkmetrics_rqst->linklocal_addr,
		ipv6_s, sizeof (ipv6_s));
      zlog_debug("%s: Received the followed LINKMETRICS request, "
		 "ifindex=%d, ipv6 = %s",
		 __func__, linkmetrics_rqst->ifindex, ipv6_s);
    }

  return 0;
}

/* Receive a linkmetrics Request */
int
zserv_linkmetrics_rqst (struct stream *s, uint16_t length)
{
  int r;
  zebra_linkmetrics_rqst_t linkmetrics_rqst;

  bzero ((char *)&linkmetrics_rqst, sizeof (zebra_linkmetrics_rqst_t));

  zserv_read_linkmetrics_rqst (&linkmetrics_rqst, s, length);

  /* Now send request to PPP/CVMI */
  r = lmgenl_write ((lmm_rqst_msg_t *)&linkmetrics_rqst);

  return r;
}
#else
int
zserv_linkmetrics_rqst (struct stream *s, uint16_t length)
{
  return 0;
}
#endif	/* HAVE_LIBNL */
