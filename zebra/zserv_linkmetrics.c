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
#include "memory.h"
#include "zebra_linkmetrics.h"
#include "linkmetrics_netlink.h"

#include "zserv_linkmetrics.h"

extern struct zebra_t zebrad;

#ifdef HAVE_LIBNLGENL

static char *lmgenl_family;
static char *lmgenl_group;

static int
zserv_set_linkmetrics_netlink (struct vty *vty, const char *genlfamily,
                               const char *genlgroup)
{
  int err;

  if (lmgenl_sock.sk != NULL)
    linkmetrics_netlink_close ();

  if (genlfamily == NULL)
    genlfamily = LMGENL_FAMILY_NAME;

  if (genlgroup == NULL)
    genlgroup = LMGENL_MCGROUP_NAME;

  err = linkmetrics_netlink_init (genlfamily, genlgroup);
  if (err)
    {
      vty_out (vty, "ERROR: linkmetrics netlink initialization failed%s",
	       VTY_NEWLINE);
      return CMD_WARNING;
    }

  return CMD_SUCCESS;
}

DEFUN (linkmetrics_netlink_family,
       linkmetrics_netlink_family_cmd,
       "netlink linkmetrics-family NAME",
       "Netlink configuration\n"
       "RFC 4938 link metrics generic netlink family name\n"
       "Name string\n")
{
  if (lmgenl_family)
    {
      XFREE (MTYPE_TMP, lmgenl_family);
      lmgenl_family = NULL;
    }
  lmgenl_family = XSTRDUP (MTYPE_TMP, argv[0]);

  return zserv_set_linkmetrics_netlink (vty, lmgenl_family, lmgenl_group);
}

DEFUN (no_linkmetrics_netlink_family,
       no_linkmetrics_netlink_family_cmd,
       "no netlink linkmetrics-family",
       NO_STR
       "Netlink configuration\n"
       "RFC 4938 link metrics generic netlink family name\n")
{
  if (lmgenl_family)
    {
      XFREE (MTYPE_TMP, lmgenl_family);
      lmgenl_family = NULL;
    }

  return zserv_set_linkmetrics_netlink (vty, lmgenl_family, lmgenl_group);
}

DEFUN (linkmetrics_netlink_group,
       linkmetrics_netlink_group_cmd,
       "netlink linkmetrics-group NAME",
       "Netlink configuration\n"
       "RFC 4938 link metrics generic netlink group name\n"
       "Name string\n")
{
  if (lmgenl_group)
    {
      XFREE (MTYPE_TMP, lmgenl_group);
      lmgenl_group = NULL;
    }
  lmgenl_group = XSTRDUP (MTYPE_TMP, argv[0]);

  return zserv_set_linkmetrics_netlink (vty, lmgenl_family, lmgenl_group);
}

DEFUN (no_linkmetrics_netlink_group,
       no_linkmetrics_netlink_group_cmd,
       "no netlink linkmetrics-group",
       NO_STR
       "Netlink configuration\n"
       "RFC 4938 link metrics generic netlink group name\n")
{
  if (lmgenl_group)
    {
      XFREE (MTYPE_TMP, lmgenl_group);
      lmgenl_group = NULL;
    }

  return zserv_set_linkmetrics_netlink (vty, lmgenl_family, lmgenl_group);
}

void
zserv_linkmetrics_init (void)
{
  install_element (CONFIG_NODE, &linkmetrics_netlink_family_cmd);
  install_element (CONFIG_NODE, &no_linkmetrics_netlink_family_cmd);

  install_element (CONFIG_NODE, &linkmetrics_netlink_group_cmd);
  install_element (CONFIG_NODE, &no_linkmetrics_netlink_group_cmd);
}

int
zserv_linkmetrics_config_write (struct vty *vty)
{
  if (lmgenl_family && strcmp (lmgenl_family, LMGENL_FAMILY_NAME) != 0)
    {
      vty_out (vty, "netlink linkmetrics-family %s%s",
               lmgenl_family, VTY_NEWLINE);
    }

  if (lmgenl_group && strcmp (lmgenl_group, LMGENL_MCGROUP_NAME) != 0)
    {
      vty_out (vty, "netlink linkmetrics-group %s%s",
               lmgenl_group, VTY_NEWLINE);
    }

  return 0;
}

#else  /* HAVE_LIBNLGENL */

void
zserv_linkmetrics_init (void)
{
}

int
zserv_linkmetrics_config_write (struct vty *vty)
{
  return 0;
}

#endif	/* HAVE_LIBNLGENL */

/* Send a link metrics update to subscribed zclients
 *
 * If provided, skip the excluded client
 */
int
zserv_send_linkmetrics (struct zebra_linkmetrics *metrics,
                        struct zserv *exclude_client)
{
  struct listnode *node;
  struct zserv *client;

  if (IS_ZEBRA_DEBUG_EVENT)
    {
      zlog_debug ("%s: sending link metrics update", __func__);
      zebra_linkmetrics_logdebug (metrics);
    }

  /* send to all subscribed clients */
  for (ALL_LIST_ELEMENTS_RO (zebrad.client_list, node, client))
    {
      /* skip an excluded client */
      if (client == exclude_client)
        continue;

      if (client->linkmetrics_subscribed)
        {
          if (zapi_write_linkmetrics (client->obuf, metrics))
            {
              zlog_warn ("%s: zapi_write_linkmetrics() failed for "
                         "client on fd %d", __func__, client->sock);
              continue;
            }

          if (zebra_server_send_message (client))
            zlog_warn ("%s: zebra_server_send_message() failed for "
                       "client on fd %d", __func__, client->sock);
        }
    }

  return 0;
}

/* Send a link metrics request to subscribed zclients
 *
 * If provided, skip the excluded client
 */
int
zserv_send_linkmetrics_request (struct zebra_linkmetrics_request *request,
                                struct zserv *exclude_client)
{
  struct listnode *node;
  struct zserv *client;

  if (IS_ZEBRA_DEBUG_EVENT)
    {
      zlog_debug ("%s: sending link metrics request", __func__);
      zebra_linkmetrics_request_logdebug (request);
    }

  /* send to all subscribed clients */
  for (ALL_LIST_ELEMENTS_RO (zebrad.client_list, node, client))
    {
      /* skip an excluded client */
      if (client == exclude_client)
        continue;

      if (client->linkmetrics_subscribed)
        {
          if (zapi_write_linkmetrics_request (client->obuf, request))
            {
              zlog_warn ("%s: zapi_write_linkmetrics_request() failed for "
                         "client on fd %d", __func__, client->sock);
              continue;
            }

          if (zebra_server_send_message (client))
            zlog_warn ("%s: zebra_server_send_message() failed for "
                       "client on fd %d", __func__, client->sock);
        }
    }

  return 0;
}


/* Send a link status update to subscribed zclients
 *
 * If provided, skip the excluded client
 */
int
zserv_send_linkstatus (struct zebra_linkstatus *status,
                       struct zserv *exclude_client)
{
  struct listnode *node;
  struct zserv *client;

  if (IS_ZEBRA_DEBUG_EVENT)
    {
      zlog_debug ("%s: sending link status update", __func__);
      zebra_linkstatus_logdebug (status);
    }

  /* send to all subscribed clients */
  for (ALL_LIST_ELEMENTS_RO (zebrad.client_list, node, client))
    {
      /* skip an excluded client */
      if (client == exclude_client)
        continue;

      if (client->linkmetrics_subscribed)
        {
          if (zapi_write_linkstatus (client->obuf, status))
            {
              zlog_warn ("%s: zapi_write_linkstatus() failed for "
                         "client on fd %d", __func__, client->sock);
              continue;
            }

          if (zebra_server_send_message (client))
            zlog_warn ("%s: zebra_server_send_message() failed for "
                       "client on fd %d", __func__, client->sock);
        }
    }

  return 0;
}

/* receive a linkmetrics subscribe/unsubscribe */
int
zserv_recv_linkmetrics_subscribe (uint16_t cmd, struct zserv *client,
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

/* receive a link metrics message from a zclient */
int
zserv_recv_linkmetrics (struct zserv *client, uint16_t length)
{
  int r;
  struct zebra_linkmetrics metrics;

  r = zapi_read_linkmetrics (&metrics, client->ibuf, length);
  if (r)
    {
      zlog_err ("%s: reading link metrics failed", __func__);
      return r;
    }

  if (IS_ZEBRA_DEBUG_EVENT)
    {
      zlog_debug ("%s: received link metrics", __func__);
      zebra_linkmetrics_logdebug (&metrics);
    }

  /* forward to subscribed zclients, but exclude sender */
  zserv_send_linkmetrics (&metrics, client);

  return r;
}

/* receive a link metrics request message from a zclient */
int
zserv_recv_linkmetrics_request (struct zserv *client, uint16_t length)
{
  int r;
  struct zebra_linkmetrics_request request;

  r = zapi_read_linkmetrics_request (&request, client->ibuf, length);
  if (r)
    {
      zlog_err ("%s: reading link metrics request failed", __func__);
      return r;
    }

  if (IS_ZEBRA_DEBUG_EVENT)
    {
      zlog_debug ("%s: received link metrics request", __func__);
      zebra_linkmetrics_request_logdebug (&request);
    }

#ifdef HAVE_LIBNLGENL
  r = lmgenl_send_metrics_request (&request);
#endif  /* HAVE_LIBNLGENL */

  /* forward to subscribed zclients, but exclude sender */
  zserv_send_linkmetrics_request (&request, client);

  return r;
}

/* receive a link status update from a zclient */
int
zserv_recv_linkstatus (struct zserv *client, uint16_t length)
{
  int r;
  struct zebra_linkstatus status;

  r = zapi_read_linkstatus (&status, client->ibuf, length);
  if (r)
    {
      zlog_err ("%s: reading link status failed", __func__);
      return r;
    }

  if (IS_ZEBRA_DEBUG_EVENT)
    {
      zlog_debug ("%s: received link status", __func__);
      zebra_linkstatus_logdebug (&status);
    }

  /* forward to subscribed zclients, but exclude sender */
  zserv_send_linkstatus (&status, client);

  return r;
}
