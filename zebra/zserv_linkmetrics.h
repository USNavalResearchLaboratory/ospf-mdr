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

#ifndef _ZSERV_LINKMETRICS_H_
#define _ZSERV_LINKMETRICS_H_

#include "zserv.h"
#include "zebra_linkmetrics.h"

struct vty;

void zserv_linkmetrics_init (void);
int zserv_linkmetrics_config_write (struct vty *vty);

int zserv_send_linkmetrics (struct zebra_linkmetrics *metrics,
                            struct zserv *exclude_client);
int zserv_send_linkmetrics_request (struct zebra_linkmetrics_request *request,
                                    struct zserv *exclude_client);
int zserv_send_linkstatus (struct zebra_linkstatus *status,
                           struct zserv *exclude_client);

int zserv_recv_linkmetrics_subscribe (uint16_t cmd, struct zserv *client,
				 uint16_t length);
int zserv_recv_linkmetrics (struct zserv *client, uint16_t length);
int zserv_recv_linkmetrics_request (struct zserv *client, uint16_t length);
int zserv_recv_linkstatus (struct zserv *client, uint16_t length);

#endif	/* _ZSERV_LINKMETRICS_H_ */
