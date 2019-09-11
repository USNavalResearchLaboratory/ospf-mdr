/* Zebra netlink functions for link metrics
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

#ifndef HAVE_LIBNLGENL

int
linkmetrics_netlink_init (int mcgroup)
{
  return 0;
}

void
linkmetrics_netlink_close (void)
{
}

#else  /* HAVE_LIBNLGENL */
/*
 * This file contains all the functions needed to communicate with the PPP
 * engine in order to receive Link metrics information. It will also pass
 * these metrics to the OSPF6D engine for modified metrics recalculations
 * and SPF routing table recomputations.
 */

/* General Include header files */
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <net/ethernet.h>
#include "thread.h"
#include "log.h"
#include "privs.h"
#include "zserv.h"
#include "debug.h"
#include "zserv_linkmetrics.h"
#include "linkmetrics_netlink.h"
#include "lmgenl.h"

/* Global Variables */
extern struct zebra_t zebrad;
extern struct zebra_privs_t zserv_privs;

lmsock_t lmgenl_sock = {
  .sk = NULL,
  .lmgenl_family = -1,
  .lmgenl_mcgroup = -1,
  .lmread_thread = NULL,
};

/* policy used to parse generic netlink link metrics/status message
   attributes */
static struct nla_policy lmgenl_policy[LMGENL_ATTR_MAX + 1] = {
	[LMGENL_ATTR_ACMACADDR] = {.minlen = ETH_ALEN, .maxlen = ETH_ALEN},
	[LMGENL_ATTR_HOSTMACADDR] = {.minlen = ETH_ALEN, .maxlen = ETH_ALEN},
	[LMGENL_ATTR_SESSIONID] = {.type = NLA_U16},

	[LMGENL_ATTR_IFINDEX] = {.type = NLA_U32},
	[LMGENL_ATTR_REMOTEV6LLADDR] = {.minlen = sizeof (struct in6_addr),
					.maxlen = sizeof (struct in6_addr)},
	[LMGENL_ATTR_REMOTEV4ADDR] = {.type = NLA_U32},

	[LMGENL_ATTR_STS_STATUS] = {.type = NLA_U8},

	[LMGENL_ATTR_PADQ_RLQ] = {.type = NLA_U8},
	[LMGENL_ATTR_PADQ_RESOURCE] = {.type = NLA_U8},
	[LMGENL_ATTR_PADQ_LATENCY] = {.type = NLA_U16},
	[LMGENL_ATTR_PADQ_CDR] = {.type = NLA_U16},
	[LMGENL_ATTR_PADQ_MDR] = {.type = NLA_U16},
	[LMGENL_ATTR_PADQ_RESERVED1] = {.type = NLA_U8},
	[LMGENL_ATTR_PADQ_RESERVED2] = {.type = NLA_U8},
};

/* Local Function prototypes */
static void lmgenl_close (lmsock_t *lmsock);
static int __lmgenl_recv (struct nl_msg *msg, void *arg);

/*
 * Function: lmgenl_open()
 *
 * Description:
 * ============
 * Setup and connect a Generic Netlink socket
 *
 * Input parameters:
 * ================
 *   lmsock = Pointer to LM socket structure
 *   genlfamily = generic netlink family name
 *   genlgroup = generic netlink multicast group name
 *
 * Output Parameters:
 * ==================
 *    0, Success
 *    Otherwise failure
 */
static int
lmgenl_open (lmsock_t *lmsock, const char *genlfamily, const char *genlgroup)
{
  int err;

  /* allocate and connect a new generic netlink socket */
  lmsock->sk = nl_socket_alloc ();
  if (lmsock->sk == NULL)
    {
      zlog_err ("%s: nl_socket_alloc() failed", __func__);
      return -1;
    }

  err = genl_connect (lmsock->sk);
  if (err)
    {
      zlog_err ("%s: genl_connect() failed: %s", __func__, nl_geterror (err));
      lmgenl_close (lmsock);
      return -1;
    }

  /* resolve family name to family id (dynamically allocated by the
   * generic netlink module)
   */
  lmsock->lmgenl_family = genl_ctrl_resolve (lmsock->sk, genlfamily);
  if (lmsock->lmgenl_family < 0)
    {
      zlog_err ("%s: genl_ctrl_resolve() failed: %s",
                __func__, nl_geterror (lmsock->lmgenl_family));
      lmgenl_close (lmsock);
      return -1;
    }

  /* resolve group name to group id (dynamically allocated by the
   * generic netlink module)
   */
  lmsock->lmgenl_mcgroup =
    genl_ctrl_resolve_grp (lmsock->sk, genlfamily, genlgroup);
  if (lmsock->lmgenl_mcgroup < 0)
    {
      zlog_err ("%s: genl_ctrl_resolve_grp() failed: %s",
                __func__, nl_geterror (lmsock->lmgenl_mcgroup));
      lmgenl_close (lmsock);
      return -1;
    }

  return 0;
}

/*
 * Function: lmgenl_open_recv()
 *
 * Description:
 * ============
 * Setup and connect a Generic Netlink socket to receive all
 * linkmetrics messages
 *
 * Input parameters:
 * ================
 *   lmsock = Pointer to LM socket structure
 *   genlfamily = generic netlink family name
 *   genlgroup = generic netlink multicast group name
 *
 * Output Parameters:
 * ==================
 *    0, Success
 *    Otherwise failure
 */
static int
lmgenl_open_recv (lmsock_t *lmsock, const char *genlfamily,
                  const char *genlgroup)
{
  int err;

  err = lmgenl_open (lmsock, genlfamily, genlgroup);
  if (err)
    return err;

  err = nl_socket_add_membership (lmsock->sk, lmsock->lmgenl_mcgroup);
  if (err)
    {
      zlog_err ("%s: nl_socket_add_membership() failed: %s",
		__func__, nl_geterror (err));
      lmgenl_close (lmsock);
      return err;
    }

  nl_socket_disable_seq_check (lmsock->sk);

  err = nl_socket_set_nonblocking (lmsock->sk);
  if (err)
    {
      zlog_err ("%s: nl_socket_set_nonblocking() failed: %s",
		__func__, nl_geterror (err));
      lmgenl_close (lmsock);
      return err;
    }

  err = nl_socket_modify_cb (lmsock->sk, NL_CB_VALID, NL_CB_CUSTOM,
                             __lmgenl_recv, NULL);
  if (err)
    {
      zlog_err ("%s: nl_socket_modify_cb() failed: %s",
		__func__, nl_geterror (err));
      lmgenl_close (lmsock);
      return err;
    }

  return 0;
}

/*
 * Function: lmgenl_close()
 *
 * Description:
 * ============
 * Close netlink socket
 *
 * Input parameters:
 * ================
 *   lmsock = Pointer to the LM socket structure to close
 *
 * Output Parameters:
 * ==================
 *   NONE
 */
static void
lmgenl_close (lmsock_t *lmsock)
{
  if (lmsock->sk)
    {
      nl_close (lmsock->sk);
      nl_socket_free (lmsock->sk);
      lmsock->sk = NULL;
    }

  return;
}

#define IFREQATTR(a)					\
  if (!attrs[a])					\
  {							\
    if (IS_ZEBRA_DEBUG_KERNEL)                          \
      zlog_debug ("%s: no " #a " attribute", __func__);	\
    return -1;						\
  }							\
  else

#define IFOPTATTR(a)					\
  if (!attrs[a])					\
  {							\
    if (IS_ZEBRA_DEBUG_KERNEL)                          \
      zlog_debug ("%s: no " #a " attribute", __func__);	\
  }							\
  else

/*
 * Function: lmgenl_send_metrics_request()
 *
 * Description:
 * ============
 * send a link metrics request
 *
 * Input parameters:
 * ================
 *   request = pointer to link metrics request structure
 *
 * Output Parameters:
 * ==================
 *    0, Success
 *    Otherwise failure
 */
int
lmgenl_send_metrics_request (const struct zebra_linkmetrics_request *request)
{
  struct nl_msg *nlmsg;
  int len;

  if (lmgenl_sock.sk == NULL)
    return -1;

  nlmsg = nlmsg_alloc();
  if (nlmsg == NULL)
    {
      zlog_err("%s: nlmsg_alloc() failed", __func__);
      return -1;
    }

  genlmsg_put (nlmsg, NL_AUTO_PID, NL_AUTO_SEQ, lmgenl_sock.lmgenl_family,
	       0, 0, LMGENL_CMD_PADQ_RQST, LMGENL_VERSION);

  NLA_PUT_U32 (nlmsg, LMGENL_ATTR_IFINDEX, request->ifindex);

  if (!IN6_IS_ADDR_UNSPECIFIED (&request->nbr_addr6))
    {
      NLA_PUT (nlmsg, LMGENL_ATTR_REMOTEV6LLADDR,
               sizeof(request->nbr_addr6), &request->nbr_addr6);
    }

  if (request->nbr_addr4.s_addr != INADDR_ANY)
    NLA_PUT_U32 (nlmsg, LMGENL_ATTR_REMOTEV4ADDR, request->nbr_addr4.s_addr);

  len = nl_send_auto_complete (lmgenl_sock.sk, nlmsg);
  if (len < 0)
    {
      zlog_err ("%s: nl_send_auto_complete() failed: %s",
                __func__, nl_geterror (len));
    }

  nlmsg_free (nlmsg);

  return len;

 nla_put_failure:

  nlmsg_free (nlmsg);
  return -1;
}

/*
 * Scale rate (given in kbps) by the units defined by RFC 5578 and
 * return the resulting datarate in kbps.
 */
static uint64_t
lmgenl_datarate (uint16_t rate, uint8_t units)
{
  uint64_t r = rate;

  switch (units)
    {
    case 0:                     /* kbps */
      break;

    case 1:                     /* mbps */
      r *= 1000;
      break;

    case 2:                     /* gbps */
      r *= 1000000;
      break;

    case 3:                     /* tbps */
      r *= 1000000000;
      break;

    default:
      zlog_err ("%s: invalid datarate units: 0x%x", __func__, units);
      break;
    }

  return r;
}

/*
 * Function: lmgenl_recv_metrics()
 *
 * Description:
 * ============
 * Parse a link metrics generic netlink message and call
 * zserv_send_linkmetrics() to process the update.
 *
 * Input parameters:
 * ================
 *   nlh = Pointer to the netlink message header
 *
 * Output Parameters:
 * ==================
 *    0, Success
 *    Otherwise failure
 */
static int
lmgenl_recv_metrics (struct nlmsghdr *nlh)
{
  struct nlattr *attrs[LMGENL_ATTR_MAX + 1];
  struct zebra_linkmetrics metrics;
  int err;

  err = genlmsg_parse (nlh, 0, attrs, LMGENL_ATTR_MAX, lmgenl_policy);
  if (err)
    {
      zlog_err ("%s: genlmsg_parse() failed: %s", __func__, nl_geterror (err));
      return -1;
    }

  memset (&metrics, 0, sizeof (metrics));

  IFREQATTR (LMGENL_ATTR_IFINDEX)
    {
      metrics.ifindex = nla_get_u32 (attrs[LMGENL_ATTR_IFINDEX]);
      if (metrics.ifindex < 1)
	{
	  zlog_err ("%s: invalid ifindex: %u", __func__, metrics.ifindex);
	  return -1;
	}
    }

  IFOPTATTR (LMGENL_ATTR_REMOTEV6LLADDR)
    {
      nla_memcpy (&metrics.nbr_addr6, attrs[LMGENL_ATTR_REMOTEV6LLADDR],
		  sizeof (metrics.nbr_addr6));
      if (!IN6_IS_ADDR_LINKLOCAL (&metrics.nbr_addr6))
	{
	  char buf[INET6_ADDRSTRLEN];

	  inet_ntop (AF_INET6, &metrics.nbr_addr6, buf, sizeof (buf));
	  zlog_err ("%s: invalid link-local address: %s", __func__, buf);
	  return -1;
	}
    }

  IFOPTATTR (LMGENL_ATTR_REMOTEV4ADDR)
    {
      metrics.nbr_addr4.s_addr =
        nla_get_u32 (attrs[LMGENL_ATTR_REMOTEV4ADDR]);
    }

  IFREQATTR (LMGENL_ATTR_PADQ_RLQ)
    {
      metrics.metrics.rlq = nla_get_u8 (attrs[LMGENL_ATTR_PADQ_RLQ]);
      if (metrics.metrics.rlq > 100)
	{
	  zlog_err ("%s: invalid relative link quality: %u",
		    __func__, metrics.metrics.rlq);
	  return -1;
	}
    }

  IFREQATTR (LMGENL_ATTR_PADQ_RESOURCE)
    {
      metrics.metrics.resource = nla_get_u8 (attrs[LMGENL_ATTR_PADQ_RESOURCE]);
      if (metrics.metrics.resource > 100)
	{
	  zlog_err ("%s: invalid resources: %u",
		    __func__, metrics.metrics.resource);
	  return -1;
	}
    }

  IFREQATTR (LMGENL_ATTR_PADQ_LATENCY)
    {
      metrics.metrics.latency = nla_get_u16 (attrs[LMGENL_ATTR_PADQ_LATENCY]);
    }

  IFREQATTR (LMGENL_ATTR_PADQ_CDR)
    {
      metrics.metrics.current_datarate =
        nla_get_u16 (attrs[LMGENL_ATTR_PADQ_CDR]);
    }

  IFREQATTR (LMGENL_ATTR_PADQ_MDR)
    {
      metrics.metrics.max_datarate =
        nla_get_u16 (attrs[LMGENL_ATTR_PADQ_MDR]);
    }

  IFOPTATTR (LMGENL_ATTR_PADQ_RESERVED2)
    {
      uint8_t reserved2;
      uint8_t units;

      reserved2 = nla_get_u8 (attrs[LMGENL_ATTR_PADQ_RESERVED2]);

      units = (reserved2 >> 1) & 3;
      metrics.metrics.current_datarate =
        lmgenl_datarate (metrics.metrics.current_datarate, units);

      units = (reserved2 >> 3) & 3;
      metrics.metrics.max_datarate =
        lmgenl_datarate (metrics.metrics.max_datarate, units);

      if ((reserved2 & 1))
        metrics.metrics.flags |= RECEIVE_ONLY;
    }

  if (metrics.metrics.current_datarate > metrics.metrics.max_datarate)
    {
      zlog_err ("%s: invalid current/maximum datarate: %" PRIu64 "/%" PRIu64,
                __func__, metrics.metrics.current_datarate,
                metrics.metrics.max_datarate);
      return -1;
    }

  if (IS_ZEBRA_DEBUG_KERNEL)
    zebra_linkmetrics_logdebug (&metrics);

  zserv_send_linkmetrics (&metrics, NULL);

  return 0;
}

/*
 * Function: lmgenl_recv_status()
 *
 * Description:
 * ============
 * Parse a link status generic netlink message and call
 * zserv_send_linkstatus() to process the update.
 *
 * Input parameters:
 * ================
 *   nlh = Pointer to the netlink message header
 *
 * Output Parameters:
 * ==================
 *    0, Success
 *    Otherwise failure
 */
static int
lmgenl_recv_status (struct nlmsghdr *nlh)
{
  struct nlattr *attrs[LMGENL_ATTR_MAX + 1];
  struct zebra_linkstatus status;
  int err;

  err = genlmsg_parse (nlh, 0, attrs, LMGENL_ATTR_MAX, lmgenl_policy);
  if (err)
    {
      zlog_err ("%s: genlmsg_parse() failed: %s", __func__, nl_geterror (err));
      return -1;
    }

  memset (&status, 0, sizeof (status));

  IFREQATTR (LMGENL_ATTR_IFINDEX)
    {
      status.ifindex = nla_get_u32 (attrs[LMGENL_ATTR_IFINDEX]);
      if (status.ifindex < 1)
	{
	  zlog_err ("%s: invalid ifindex: %u", __func__, status.ifindex);
	  return -1;
	}
    }

  IFOPTATTR (LMGENL_ATTR_REMOTEV6LLADDR)
    {
      nla_memcpy (&status.nbr_addr6, attrs[LMGENL_ATTR_REMOTEV6LLADDR],
		  sizeof (status.nbr_addr6));
      if (!IN6_IS_ADDR_LINKLOCAL (&status.nbr_addr6))
	{
	  char buf[INET6_ADDRSTRLEN];

	  inet_ntop (AF_INET6, &status.nbr_addr6, buf, sizeof (buf));
	  zlog_err ("%s: invalid link-local address: %s", __func__, buf);
	  return -1;
	}
    }

  IFOPTATTR (LMGENL_ATTR_REMOTEV4ADDR)
    {
      status.nbr_addr4.s_addr =
        nla_get_u32 (attrs[LMGENL_ATTR_REMOTEV4ADDR]);
    }

  IFREQATTR (LMGENL_ATTR_STS_STATUS)
    {
      status.status = nla_get_u8 (attrs[LMGENL_ATTR_STS_STATUS]);
    }

  if (IS_ZEBRA_DEBUG_KERNEL)
    zebra_linkstatus_logdebug (&status);

  zserv_send_linkstatus (&status, NULL);

  return 0;
}

#undef IFREQATTR
#undef IFOPTATTR

/*
 * function: lmgenl_recv()
 *
 * Description:
 * ============
 * Reveive a generic netlink message and call the appropriate function
 * depending on the message type.
 *
 * Input parameters:
 * ================
 *   sk = Pointer to a Generic Netlink handle
 *
 * Output Parameters:
 * ==================
 *    0, Success
 *    Otherwise failure
 */
static int
__lmgenl_recv (struct nl_msg *msg, void *arg)
{
  struct nlmsghdr *nlh;
  struct genlmsghdr *ghdr;

  nlh = nlmsg_hdr (msg);
  if (!genlmsg_valid_hdr (nlh, 0))
    return NL_SKIP;

  ghdr = nlmsg_data (nlh);

  if (IS_ZEBRA_DEBUG_KERNEL)
    zlog_debug ("%s: received generic netlink message: "
		"cmd: %u; version: %u", __func__, ghdr->cmd, ghdr->version);

  switch (ghdr->cmd)
    {
    case LMGENL_CMD_PADQ:
      lmgenl_recv_metrics (nlh);
      break;

    case LMGENL_CMD_STATUS:
      lmgenl_recv_status (nlh);
      break;

    case LMGENL_CMD_PADQ_RQST:
      /* ignore */
      break;

    default:
      zlog_err ("%s: unknown ghdr->cmd: %u", __func__, ghdr->cmd);
      break;
    }

  return NL_OK;
}

static int
lmgenl_recv (struct nl_sock *sk)
{
  int err;

  err = nl_recvmsgs_default (sk);
  if (err)
    {
      zlog_err ("%s: nl_recv() failed: %s", __func__, nl_geterror (err));
      return -1;
    }

  return 0;
}

/*
 * Function: lmgenl_read()
 *
 * Description:
 * ============
 * Setup read function via thread callback mechanism
 *
 * Input parameters:
 * ================
 *   thread = Thread descriptor
 *
 * Output Parameters:
 * ==================
 *    0, Success
 *    Otherwise failure
 */
static int
lmgenl_read (struct thread *thread)
{
  int ret;
  lmsock_t *lmsock;

  lmsock = THREAD_ARG (thread);
  if (lmsock == NULL)
    {
      zlog_err ("%s: invalid generic netlink socket", __func__);
      return -1;
    }

  ret = lmgenl_recv (lmsock->sk);

  lmsock->lmread_thread =
    thread_add_read (zebrad.master, lmgenl_read,
		     &lmgenl_sock, nl_socket_get_fd (lmgenl_sock.sk));
  if (lmsock->lmread_thread == NULL)
    {
      zlog_err ("%s: thread_add_read() failed", __func__);
      return -1;
    }

  return ret;
}

/*
 * Function: linkmetrics_netlink_init()
 *
 * Description:
 * ============
 * Initial netlink socket entry point function used to do:
 *    to setup/open the netlink socket
 * Call lmgenl_read, to receive data on the netlink socket.
 *
 * Input parameters:
 * ================
 *   genlfamily = generic netlink family name
 *   genlgroup = generic netlink multicast group name
 *
 * Output Parameters:
 * ==================
 *   -1, Failure
 *    0, Success
 */
int
linkmetrics_netlink_init (const char *genlfamily, const char *genlgroup)
{
  if (lmgenl_open_recv (&lmgenl_sock, genlfamily, genlgroup))
    {
      zlog_err ("%s: lmgenl_open_recv() failed", __func__);
      return -1;
    }

  lmgenl_sock.lmread_thread =
    thread_add_read (zebrad.master, lmgenl_read,
		     &lmgenl_sock, nl_socket_get_fd (lmgenl_sock.sk));
  if (lmgenl_sock.lmread_thread == NULL)
    {
      zlog_err ("%s: thread_add_read() failed", __func__);
      return -1;
    }

  return 0;
}

/*
 * Function: linkmetrics_netlink_close()
 *
 * Description:
 * ============
 * Close the generic netlink socket used for link status/metrics
 * messages.
 *
 * Input parameters:
 * ================
 *   NONE
 *
 * Output Parameters:
 * ==================
 *   NONE
 */
void
linkmetrics_netlink_close (void)
{
  THREAD_OFF (lmgenl_sock.lmread_thread);
  lmgenl_close (&lmgenl_sock);

  return;
}

#endif  /* HAVE_LIBNLGENL */
