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
#include "lmgenl.h"

/* log the link metrics structure as a debug message */
void
zebra_linkmetrics_logdebug (zebra_linkmetrics_t *linkmetrics)
{
  char lladdrstr[INET6_ADDRSTRLEN];

  zlog_debug ("LINKMETRICS:");
  zlog_debug ("  ifindex: %d", linkmetrics->ifindex);
  inet_ntop (AF_INET6, &linkmetrics->linklocal_addr,
	     lladdrstr, sizeof (lladdrstr));
  zlog_debug ("  ipv6 link-local addr: %s", lladdrstr);
  zlog_debug ("  rlq: %u", linkmetrics->metrics.rlq);
  zlog_debug ("  resource: %u", linkmetrics->metrics.resource);
  zlog_debug ("  latency: %u", linkmetrics->metrics.latency);
  zlog_debug ("  current_datarate: %u", linkmetrics->metrics.current_datarate);
  zlog_debug ("  max_datarate: %u", linkmetrics->metrics.max_datarate);

  return;
}

/* serialize a link metrics structure */
int
zapi_write_linkmetrics (struct stream *s, zebra_linkmetrics_t *linkmetrics)
{
  /* initialize the stream */
  stream_reset (s);
  zclient_create_header (s, ZEBRA_LINKMETRICS_METRICS);

  /* write the linkmetrics structure */
  stream_putl (s, linkmetrics->ifindex);
  stream_write (s, (u_char *)&linkmetrics->linklocal_addr,
               sizeof (linkmetrics->linklocal_addr));
  stream_putc (s, linkmetrics->metrics.rlq);
  stream_putc (s, linkmetrics->metrics.resource);
  stream_putw (s, linkmetrics->metrics.latency);
  stream_putw (s, linkmetrics->metrics.current_datarate);
  stream_putw (s, linkmetrics->metrics.max_datarate);

  /* put length at beginning of stream */
  if (stream_putw_at (s, 0, stream_get_endp (s)) != 2)
    zlog_err ("%s: stream_putw_at() failed for setting length", __func__);

  return 0;
}

/* unserialize a link metrics structure */
int
zapi_read_linkmetrics (zebra_linkmetrics_t *linkmetrics,
		      struct stream *s, u_short length)
{
  if (length != sizeof (*linkmetrics))
    {
      zlog_err ("%s: invalid length: %u", __func__, length);
      return -1;
    }

  linkmetrics->ifindex = stream_getl (s);
  stream_get (&linkmetrics->linklocal_addr, s,
	      sizeof (linkmetrics->linklocal_addr));
  linkmetrics->metrics.rlq = stream_getc (s);
  linkmetrics->metrics.resource = stream_getc (s);
  linkmetrics->metrics.latency = stream_getw (s);
  linkmetrics->metrics.current_datarate = stream_getw (s);
  linkmetrics->metrics.max_datarate = stream_getw (s);

  return 0;
}

/* returns nonzero if truncated */
int
zebra_linkstatus_string (char *str, size_t size, u_int32_t status)
{
  int tmp;

  switch (status)
    {
    case LM_STATUS_DOWN:
      tmp = snprintf (str, size, "%s", "DOWN");
      break;

    case LM_STATUS_UP:
      tmp = snprintf (str, size, "%s", "UP");
      break;

    default:
      tmp = snprintf (str, size, "unknown link status: %u", status);
      break;
    }

  return tmp >= (int) size;
}

/* log the link status structure as a debug message */
void
zebra_linkstatus_logdebug (zebra_linkstatus_t *linkstatus)
{
  char str[INET6_ADDRSTRLEN];

  zlog_debug ("LINKSTATUS:");
  zlog_debug ("  ifindex: %d", linkstatus->ifindex);
  inet_ntop (AF_INET6, &linkstatus->linklocal_addr, str, sizeof (str));
  zlog_debug ("  ipv6 link-local addr: %s", str);
  zebra_linkstatus_string (str, sizeof (str), linkstatus->status);
  zlog_debug ("  link status: %s", str);

  return;
}

/* serialize a link status structure */
int
zapi_write_linkstatus (struct stream *s, zebra_linkstatus_t *linkstatus)
{
  /* initialize the stream */
  stream_reset (s);
  zclient_create_header (s, ZEBRA_LINKMETRICS_STATUS);

  /* write the linkstatus structure */
  stream_putl (s, linkstatus->ifindex);
  stream_write (s, (u_char *)&linkstatus->linklocal_addr,
		sizeof (linkstatus->linklocal_addr));
  stream_putl (s, linkstatus->status);

  /* put length at beginning of stream */
  if (stream_putw_at (s, 0, stream_get_endp (s)) != 2)
    zlog_err ("%s: stream_putw_at() failed for setting length", __func__);

  return 0;
}

/* unserialize a link status structure */
int
zapi_read_linkstatus (zebra_linkstatus_t *linkstatus,
		      struct stream *s, u_short length)
{
  if (length != sizeof (*linkstatus))
    {
      zlog_err ("%s: invalid length: %u", __func__, length);
      return -1;
    }

  linkstatus->ifindex = stream_getl (s);
  stream_get (&linkstatus->linklocal_addr, s,
	      sizeof (linkstatus->linklocal_addr));
  linkstatus->status = stream_getl (s);

  return 0;
}
