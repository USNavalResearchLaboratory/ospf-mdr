/* Zebra link metrics functions
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

#include <zebra.h>

#include "zclient.h"
#include "log.h"
#include "stream.h"
#include "zebra_linkmetrics.h"

/* log the link metrics structure as a debug message */
void
zebra_linkmetrics_logdebug (const struct zebra_linkmetrics *metrics)
{
  char addr[INET6_ADDRSTRLEN];

  zlog_debug ("LINK METRICS:");
  zlog_debug ("  ifindex: %u", metrics->ifindex);
  inet_ntop (AF_INET, &metrics->nbr_addr4, addr, sizeof (addr));
  zlog_debug ("  ipv4 address: %s", addr);
  inet_ntop (AF_INET6, &metrics->nbr_addr6, addr, sizeof (addr));
  zlog_debug ("  ipv6 link-local address: %s", addr);
  zlog_debug ("  flags: 0x%x", metrics->metrics.flags);
  zlog_debug ("  rlq: %u", metrics->metrics.rlq);
  zlog_debug ("  resource: %u", metrics->metrics.resource);
  zlog_debug ("  latency: %u", metrics->metrics.latency);
  zlog_debug ("  current_datarate: %" PRIu64,
              metrics->metrics.current_datarate);
  zlog_debug ("  max_datarate: %" PRIu64, metrics->metrics.max_datarate);

  return;
}

/* serialize a link metrics structure */
int
zapi_write_linkmetrics (struct stream *s,
                        const struct zebra_linkmetrics *metrics)
{
  /* initialize the stream */
  stream_reset (s);
  zclient_create_header (s, ZEBRA_LINKMETRICS_METRICS);

  /* write the linkmetrics structure */
  stream_putl (s, metrics->ifindex);
  stream_put_in_addr (s, &metrics->nbr_addr4);
  stream_write (s, &metrics->nbr_addr6, sizeof (metrics->nbr_addr6));

  stream_putl (s, metrics->metrics.flags);
  stream_putc (s, metrics->metrics.rlq);
  stream_putc (s, metrics->metrics.resource);
  stream_putw (s, metrics->metrics.latency);
  stream_putq (s, metrics->metrics.current_datarate);
  stream_putq (s, metrics->metrics.max_datarate);

  /* put length at beginning of stream */
  if (stream_putw_at (s, 0, stream_get_endp (s)) != 2)
    zlog_err ("%s: stream_putw_at() failed for setting length", __func__);

  return 0;
}

/* unserialize a link metrics structure */
int
zapi_read_linkmetrics (struct zebra_linkmetrics *metrics,
                       struct stream *s, u_short length)
{
  if (length != ZAPI_LINKMETRICS_LEN)
    {
      zlog_err ("%s: invalid length: %u", __func__, length);
      return -1;
    }

  metrics->ifindex = stream_getl (s);
  metrics->nbr_addr4.s_addr = stream_get_ipv4 (s);
  stream_get (&metrics->nbr_addr6, s, sizeof (metrics->nbr_addr6));

  metrics->metrics.flags = stream_getl (s);
  metrics->metrics.rlq = stream_getc (s);
  metrics->metrics.resource = stream_getc (s);
  metrics->metrics.latency = stream_getw (s);
  metrics->metrics.current_datarate = stream_getq (s);
  metrics->metrics.max_datarate = stream_getq (s);

  return 0;
}

/* log the link status structure as a debug message */
void
zebra_linkstatus_logdebug (const struct zebra_linkstatus *status)
{
  char str[INET6_ADDRSTRLEN];

  zlog_debug ("LINK STATUS:");
  zlog_debug ("  ifindex: %u", status->ifindex);
  inet_ntop (AF_INET, &status->nbr_addr4, str, sizeof (str));
  zlog_debug ("  ipv4 address: %s", str);
  inet_ntop (AF_INET6, &status->nbr_addr6, str, sizeof (str));
  zlog_debug ("  ipv6 link-local address: %s", str);
  zlog_debug ("  link status: %s", status->status ? "up" : "down");

  return;
}

/* serialize a link status structure */
int
zapi_write_linkstatus (struct stream *s,
                       const struct zebra_linkstatus *status)
{
  /* initialize the stream */
  stream_reset (s);
  zclient_create_header (s, ZEBRA_LINKMETRICS_STATUS);

  /* write the linkstatus structure */
  stream_putl (s, status->ifindex);
  stream_put_in_addr (s, &status->nbr_addr4);
  stream_write (s, &status->nbr_addr6, sizeof (status->nbr_addr6));
  stream_putc (s, status->status);

  /* put length at beginning of stream */
  if (stream_putw_at (s, 0, stream_get_endp (s)) != 2)
    zlog_err ("%s: stream_putw_at() failed for setting length", __func__);

  return 0;
}

/* unserialize a link status structure */
int
zapi_read_linkstatus (struct zebra_linkstatus *status,
                      struct stream *s, u_short length)
{
  if (length != ZAPI_LINKSTATUS_LEN)
    {
      zlog_err ("%s: invalid length: %u", __func__, length);
      return -1;
    }

  status->ifindex = stream_getl (s);
  status->nbr_addr4.s_addr = stream_get_ipv4 (s);
  stream_get (&status->nbr_addr6, s, sizeof (status->nbr_addr6));
  status->status = stream_getc (s);

  return 0;
}

/* log the link metrics request as a debug message */
void
zebra_linkmetrics_request_logdebug (const struct zebra_linkmetrics_request *request)
{
  char str[INET6_ADDRSTRLEN];

  zlog_debug ("LINK METRICS REQUEST:");
  zlog_debug ("  ifindex: %u", request->ifindex);
  inet_ntop (AF_INET, &request->nbr_addr4, str, sizeof (str));
  zlog_debug ("  ipv4 address: %s", str);
  inet_ntop (AF_INET6, &request->nbr_addr6, str, sizeof (str));
  zlog_debug ("  ipv6 link-local address: %s", str);

  return;
}

/* serialize a link metrics request structure */
int
zapi_write_linkmetrics_request (struct stream *s,
                                const struct zebra_linkmetrics_request *request)
{
  /* initialize the stream */
  stream_reset (s);
  zclient_create_header (s, ZEBRA_LINKMETRICS_METRICS_REQUEST);

  /* write the link metrics request structure */
  stream_putl (s, request->ifindex);
  stream_put_in_addr (s, &request->nbr_addr4);
  stream_write (s, &request->nbr_addr6, sizeof (request->nbr_addr6));

  /* put length at beginning of stream */
  if (stream_putw_at (s, 0, stream_get_endp (s)) != 2)
    zlog_err ("%s: stream_putw_at() failed for setting length", __func__);

  return 0;
}

/* unserialize a link metrics request structure */
int
zapi_read_linkmetrics_request (struct zebra_linkmetrics_request *request,
                               struct stream *s, u_short length)
{
  if (length != ZAPI_LINKMETRICS_REQUEST_LEN)
    {
      zlog_err ("%s: invalid length: %u", __func__, length);
      return -1;
    }

  request->ifindex = stream_getl (s);
  request->nbr_addr4.s_addr = stream_get_ipv4 (s);
  stream_get (&request->nbr_addr6, s, sizeof (request->nbr_addr6));

  return 0;
}
