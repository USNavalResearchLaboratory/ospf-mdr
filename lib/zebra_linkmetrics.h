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

/*
 * This file contains all the structures and constants needed to pass
 * linkmetric parameters between ZEBRA and OSPF6D engines.
 */

#ifndef _ZEBRA_LINKMETRICS_H_
#define _ZEBRA_LINKMETRICS_H_

#include <netinet/in.h>

struct zebra_rfc4938_linkmetrics
{
  u_int32_t flags;
#define RECEIVE_ONLY (1 << 0)

  u_int8_t  rlq;                /* 0-100 */
  u_int8_t  resource;           /* 0-100 */
  u_int16_t latency;            /* msec */
  u_int64_t current_datarate;   /* kbps */
  u_int64_t max_datarate;       /* kbps */
};
#define ZAPI_RFC4938_LINKMETRICS_LEN 24

struct zebra_linkmetrics
{
  u_int32_t ifindex;         /* local interface index (if known) */
  struct in_addr nbr_addr4;  /* peer ipv4 address (if known) */
  struct in6_addr nbr_addr6; /* peer ipv6 link-local address (if known) */
  struct zebra_rfc4938_linkmetrics metrics; /* link metric values */
};
#define ZAPI_LINKMETRICS_LEN (24 + ZAPI_RFC4938_LINKMETRICS_LEN)

struct zebra_linkstatus
{
  u_int32_t ifindex;         /* local interface index (if known) */
  struct in_addr nbr_addr4;  /* peer ipv4 address (if known) */
  struct in6_addr nbr_addr6; /* peer ipv6 link-local address (if known) */
  u_int8_t status;
};
#define ZAPI_LINKSTATUS_LEN 25

struct zebra_linkmetrics_request
{
  u_int32_t ifindex;         /* local interface index (if known) */
  struct in_addr nbr_addr4;  /* peer ipv4 address (if known) */
  struct in6_addr nbr_addr6; /* peer ipv6 link-local address (if known) */
};
#define ZAPI_LINKMETRICS_REQUEST_LEN 24

struct stream;

void zebra_linkmetrics_logdebug (const struct zebra_linkmetrics *metrics);
int zapi_write_linkmetrics (struct stream *s,
                            const struct zebra_linkmetrics *metrics);
int zapi_read_linkmetrics (struct zebra_linkmetrics *metrics,
			   struct stream *s, u_short length);

void zebra_linkstatus_logdebug (const struct zebra_linkstatus *status);

int zapi_write_linkstatus (struct stream *s,
                           const struct zebra_linkstatus *status);
int zapi_read_linkstatus (struct zebra_linkstatus *status,
			  struct stream *s, u_short length);

void zebra_linkmetrics_request_logdebug (const struct zebra_linkmetrics_request *request);
int zapi_write_linkmetrics_request (struct stream *s,
                                    const struct zebra_linkmetrics_request *request);
int zapi_read_linkmetrics_request (struct zebra_linkmetrics_request *request,
                                   struct stream *s, u_short length);

#endif	/* _ZEBRA_LINKMETRICS_H_ */
