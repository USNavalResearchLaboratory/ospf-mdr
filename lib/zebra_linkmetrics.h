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

struct zebra_rfc4938_linkmetrics {
  u_int8_t  rlq;
  u_int8_t  resource;
  u_int16_t latency;
  u_int16_t current_datarate;
  u_int16_t max_datarate;
};

typedef struct zebra_linkmetrics {
  u_int32_t ifindex;		     /* local interface index */
  struct    in6_addr linklocal_addr; /* peer ipv6 link-local address */
  struct zebra_rfc4938_linkmetrics metrics; /* link metric values */
} zebra_linkmetrics_t;

typedef struct zebra_linkstatus {
  u_int32_t ifindex;		     /* local interface index */
  struct    in6_addr linklocal_addr; /* peer ipv6 link-local address */
  u_int32_t status;
} zebra_linkstatus_t;

typedef struct {
  u_int32_t ifindex;		     /* local interface index */
  struct    in6_addr linklocal_addr; /* peer ipv6 link-local address */
} zebra_linkmetrics_rqst_t;

struct stream;

void zebra_linkmetrics_logdebug (zebra_linkmetrics_t *linkmetrics);
int zapi_write_linkmetrics (struct stream *s, zebra_linkmetrics_t *linkmetrics);
int zapi_read_linkmetrics (zebra_linkmetrics_t *linkmetrics,
			   struct stream *s, u_short length);

int zebra_linkstatus_string (char *str, size_t size, u_int32_t status);
void zebra_linkstatus_logdebug (zebra_linkstatus_t *linkstatus);

int zapi_write_linkstatus (struct stream *s, zebra_linkstatus_t *linkstatus);
int zapi_read_linkstatus (zebra_linkstatus_t *linkstatus,
			  struct stream *s, u_short length);
int zapi_read_linkmetrics_rqst (zebra_linkmetrics_rqst_t *metrics_rqst,
				struct stream *s, u_short length);

#endif	/* _ZEBRA_LINKMETRICS_H_ */
