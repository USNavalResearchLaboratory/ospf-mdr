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

/* LM metric structure passed between PPP/PPP_SIM to ZEBRA engine */
typedef struct {
    u_int32_t ifindex;                 /* local interface index */
    struct    in6_addr linklocal_addr; /* peer ipv6 link-local address */
    u_int8_t  Access_Concentrator_mac_addr[6];
    u_int8_t  Host_mac_addr[6];
    u_int16_t session_id;
    u_int8_t  rlq;
    u_int8_t  resource;
    u_int16_t latency;
    u_int16_t current_datarate;
    u_int16_t max_datarate;
} lmm_padq_msg_t;

/* LM metric Request structure passed ZEBRA to PPP/CVMI */
typedef struct {
    u_int32_t ifindex;                 /* local interface index */
    struct    in6_addr linklocal_addr; /* peer ipv6 link-local address */
} lmm_rqst_msg_t;

/* LM status structure passed between PPP/CVMI to ZEBRA engine */
typedef struct {
    u_int32_t ifindex;                 /* local interface index */
    struct    in6_addr linklocal_addr; /* peer ipv6 link-local address */
    u_int8_t  Access_Concentrator_mac_addr[6];
    u_int8_t  Host_mac_addr[6];
    u_int16_t session_id;
    u_int8_t  status;  /* define codepoints for up, down */
} lmm_status_msg_t;

/* Linkmetric message passed between PPP/CVMI to ZEBRA engine */
typedef struct {
   u_int8_t  type;     /* message type */
   union {
      lmm_status_msg_t sts;
      lmm_padq_msg_t   met;
      lmm_rqst_msg_t   rqst;
   } m;
} linkmetrics_msg_t;

enum {
   LINKMETRICS_STATUS_TYPE = 0,
   LINKMETRICS_METRICS_TYPE,
   LINKMETRICS_METRICS_RQST,
};

enum {
   LINKMETRICS_STATUS_DOWN = 0,
   LINKMETRICS_STATUS_UP,
};

/* Global variables */
extern lmsock_t lmgenl_sock;

int lmgenl_write (lmm_rqst_msg_t *msg);
#endif	/* HAVE_LIBNLGENL */

#endif /* _LINKMETRICS_NETLINK_H_ */
