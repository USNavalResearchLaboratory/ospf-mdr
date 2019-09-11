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
 * This file contains all the structures and constants needed to setup a
 * netlink socket to communicate with the PPP engine or PPP simulator.
 */

#ifndef _LINKMETRICS_NETLINK_H_
#define _LINKMETRICS_NETLINK_H_

#include "lmgenl.h"

/* function prototypes */
int linkmetrics_netlink_init (const char *genlfamily, const char *genlgroup);
void linkmetrics_netlink_close (void);

#ifdef HAVE_LIBNLGENL
/* LM generic netlink structure */
typedef struct {
  struct nl_sock *sk;
  int lmgenl_family;
  int lmgenl_mcgroup;
  struct thread *lmread_thread;
} lmsock_t;

/* Global variables */
extern lmsock_t lmgenl_sock;

struct zebra_linkmetrics_request;
int lmgenl_send_metrics_request (const struct zebra_linkmetrics_request *request);
#endif  /* HAVE_LIBNLGENL */

#endif /* _LINKMETRICS_NETLINK_H_ */
