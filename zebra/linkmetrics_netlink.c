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
#include "zebra.h"
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

/* XXX This must match whatever is assigned when LMGENL_MCGROUP_NAME
   is registered with the generic netlink core.  It's hardcoded for
   now because libnl doesn't currently have a simple function to
   resolve multicast groups.

   This can be configured at runtime using the "netlink
   linkmetrics-multicast-group" vty command.
*/
int lmgenl_mcgroup_id = DEFAULT_LMGENL_MCGROUP_ID;

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
};

/* Local Function prototypes */
static int lmgenl_open (lmsock_t *lmsock);
static int lmgenl_open_recv (lmsock_t *lmsock);
static void lmgenl_close (lmsock_t *lmsock);
static int lmgenl_recv_status (struct nlmsghdr *nlh);
static int lmgenl_recv_padq (struct nlmsghdr *nlh);
static int lmgenl_recv (struct nl_handle *sk);
static int lmgenl_read (struct thread *thread);
static int lmsend_padq_rqst (lmsock_t *lmsock, lmm_rqst_msg_t *msg);

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
 *
 * Output Parameters:
 * ==================
 *    0, Success
 *    Otherwise failure
 */
static int
lmgenl_open (lmsock_t *lmsock)
{
  if (lmgenl_mcgroup_id <= 0)
    {
      zlog_err ("%s: invalid link metrics netlink multicast group id: %d",
		__func__, lmgenl_mcgroup_id);
      return -1;
    }

  /* allocate and connect a new generic netlink socket */
  lmsock->sk = nl_handle_alloc ();
  if (lmsock->sk == NULL)
    {
      zlog_err ("%s: nl_handle_alloc() failed: %s", __func__, nl_geterror ());
      return -1;
    }

  if (genl_connect (lmsock->sk))
    {
      zlog_err ("%s: genl_connect() failed: %s", __func__, nl_geterror ());
      lmgenl_close (lmsock);
      return -1;
    }

  /* resolve family name to family id (dynamically allocated by the
   * generic netlink module)
   */
  lmsock->lmgenl_family = genl_ctrl_resolve (lmsock->sk, LMGENL_FAMILY_NAME);
  if (lmsock->lmgenl_family < 0)
    {
      zlog_err ("%s: genl_ctrl_resolve() failed: %s", __func__, nl_geterror ());
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
 *
 * Output Parameters:
 * ==================
 *    0, Success
 *    Otherwise failure
 */
static int
lmgenl_open_recv (lmsock_t *lmsock)
{
  int err;

  err = lmgenl_open (lmsock);
  if (err)
    return err;

#if 0
  /* XXX libnl should provide a resolve function for multicast groups */
  lmsock->lmgenl_mcgroup = nl_get_multicast_id (lmsock->sk, LMGENL_FAMILY_NAME,
						LMGENL_MCGROUP_NAME);
  if (lmsock->lmgenl_mcgroup < 0)
    {
      zlog_err ("%s: nl_get_multicast_id() failed: %s",
		__func__, nl_geterror ());
      lmgenl_close (lmsock);
      return lmsock->lmgenl_mcgroup;
    }
#else
  lmsock->lmgenl_mcgroup = lmgenl_mcgroup_id;
#endif

  err = nl_socket_add_membership (lmsock->sk, lmsock->lmgenl_mcgroup);
  if (err)
    {
      zlog_err ("%s: nl_socket_add_membership() failed: %s",
		__func__, nl_geterror ());
      lmgenl_close (lmsock);
      return err;
    }

  nl_disable_sequence_check (lmsock->sk);

  err = nl_socket_set_nonblocking (lmsock->sk);
  if (err)
    {
      zlog_err ("%s: nl_socket_set_nonblocking() failed: %s",
		__func__, nl_geterror ());
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
      nl_handle_destroy (lmsock->sk);
      lmsock->sk = NULL;
    }

  return;
}

#define IFREQATTR(a)					\
  if (!attrs[a])					\
  {							\
    zlog_err ("%s: no " #a " attribute", __func__);	\
    return -1;						\
  }							\
  else

/*
 * Function: lmsend_padq_rqst()
 *
 * Description:
 * ============
 * Send a PADQ Request message to PPP/CVMI
 *
 * Input parameters:
 * ================
 *   lmsock = Pointer to the LM socket structure
 *   msg    = Pointer to the Linkmetric Request message to send
 *
 * Output Parameters:
 * ==================
 *   The number of bytes sent or a negative error value
 */
static int lmsend_padq_rqst (lmsock_t *lmsock, lmm_rqst_msg_t *msg)
{
  struct nl_msg *nlmsg;
  int len;

  nlmsg = nlmsg_alloc();
  if (nlmsg == NULL)
    {
      zlog_err("%s: nlmsg_alloc() failed", __func__);
      return -1;
    }

  genlmsg_put (nlmsg, NL_AUTO_PID, NL_AUTO_SEQ, lmsock->lmgenl_family,
	       0, 0, LMGENL_CMD_PADQ_RQST, LMGENL_VERSION);

  NLA_PUT_U32 (nlmsg, LMGENL_ATTR_IFINDEX, msg->ifindex);

  NLA_PUT (nlmsg, LMGENL_ATTR_REMOTEV6LLADDR, sizeof(msg->linklocal_addr),
	   &msg->linklocal_addr);

  len = nl_send_auto_complete (lmsock->sk, nlmsg);
  if (len < 0)
    zlog_err ("%s: nl_send_auto_complete() failed: %s",
	      __func__, nl_geterror ());

  nlmsg_free (nlmsg);

  return len;

 nla_put_failure:

  nlmsg_free (nlmsg);
  return -1;
}

/*
 * Function: lmgenl_recv_padq()
 *
 * Description:
 * ============
 * Parse a padq generic netlink message and call zserv_linkmetrics()
 * to process the link metrics update.
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
lmgenl_recv_padq (struct nlmsghdr *nlh)
{
  struct nlattr *attrs[LMGENL_ATTR_MAX + 1];
  zebra_linkmetrics_t linkmetrics;

  if (genlmsg_parse (nlh, 0, attrs, LMGENL_ATTR_MAX, lmgenl_policy))
    {
      zlog_err ("%s: genlmsg_parse() failed: %s", __func__, nl_geterror ());
      return -1;
    }

  memset (&linkmetrics, 0, sizeof (linkmetrics));

  IFREQATTR (LMGENL_ATTR_IFINDEX)
    linkmetrics.ifindex = nla_get_u32 (attrs[LMGENL_ATTR_IFINDEX]);

  IFREQATTR (LMGENL_ATTR_REMOTEV6LLADDR)
    nla_memcpy (&linkmetrics.linklocal_addr, attrs[LMGENL_ATTR_REMOTEV6LLADDR],
		sizeof (linkmetrics.linklocal_addr));

  IFREQATTR (LMGENL_ATTR_PADQ_RLQ)
    linkmetrics.metrics.rlq = nla_get_u8 (attrs[LMGENL_ATTR_PADQ_RLQ]);

  IFREQATTR (LMGENL_ATTR_PADQ_RESOURCE)
    linkmetrics.metrics.resource =
      nla_get_u8 (attrs[LMGENL_ATTR_PADQ_RESOURCE]);

  IFREQATTR (LMGENL_ATTR_PADQ_LATENCY)
    linkmetrics.metrics.latency =
      nla_get_u16 (attrs[LMGENL_ATTR_PADQ_LATENCY]);

  IFREQATTR (LMGENL_ATTR_PADQ_CDR)
    linkmetrics.metrics.current_datarate =
      nla_get_u16 (attrs[LMGENL_ATTR_PADQ_CDR]);

  IFREQATTR (LMGENL_ATTR_PADQ_MDR)
    linkmetrics.metrics.max_datarate =
      nla_get_u16 (attrs[LMGENL_ATTR_PADQ_MDR]);

  if (IS_ZEBRA_DEBUG_KERNEL)
    zebra_linkmetrics_logdebug (&linkmetrics);

  zserv_linkmetrics (&linkmetrics);

  return 0;
}

/*
 * Function: lmgenl_recv_status()
 *
 * Description:
 * ============
 * Parse a link status generic netlink message and call
 * zserv_linkstatus() to process the update.
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
  zebra_linkstatus_t linkstatus;

  if (genlmsg_parse (nlh, 0, attrs, LMGENL_ATTR_MAX, lmgenl_policy))
    {
      zlog_err ("%s: genlmsg_parse() failed: %s", __func__, nl_geterror ());
      return -1;
    }

  memset (&linkstatus, 0, sizeof (linkstatus));

  IFREQATTR (LMGENL_ATTR_IFINDEX)
    linkstatus.ifindex = nla_get_u32 (attrs[LMGENL_ATTR_IFINDEX]);

  IFREQATTR (LMGENL_ATTR_REMOTEV6LLADDR)
    nla_memcpy (&linkstatus.linklocal_addr, attrs[LMGENL_ATTR_REMOTEV6LLADDR],
		sizeof (linkstatus.linklocal_addr));

  IFREQATTR (LMGENL_ATTR_STS_STATUS)
    linkstatus.status = nla_get_u8 (attrs[LMGENL_ATTR_STS_STATUS]);

  if (IS_ZEBRA_DEBUG_KERNEL)
    zebra_linkstatus_logdebug (&linkstatus);

  zserv_linkstatus (&linkstatus);

  return 0;
}

#undef IFREQATTR

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
lmgenl_recv (struct nl_handle *sk)
{
  int len;
  struct sockaddr_nl peer;
  unsigned char *buf;
  struct nlmsghdr *nlh;
  struct genlmsghdr *ghdr;

  len = nl_recv (sk, &peer, &buf, NULL);
  if (len <= 0)
    {
      if (len)
	zlog_err ("%s: nl_recv() failed: %s", __func__, nl_geterror ());
      return -1;
    }

  nlh = (struct nlmsghdr *)buf;
  ghdr = nlmsg_data (nlh);

  if (IS_ZEBRA_DEBUG_KERNEL)
    zlog_debug ("%s: received generic netlink message: "
		"cmd: %u; version: %u", __func__, ghdr->cmd, ghdr->version);

  switch (ghdr->cmd)
    {
    case LMGENL_CMD_PADQ:
      lmgenl_recv_padq (nlh);
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

  free (buf);

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
 * Function: lmgenl_write()
 *
 * Description:
 * ============
 * write to Multicast Group to send a PADQ request msg
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
int lmgenl_write (lmm_rqst_msg_t *msg)
{
  int ret=0;

  if (lmgenl_sock.sk == NULL)
    return -1;

  /* Send a PADQ request message to PPP/CVMI layer */
  ret = lmsend_padq_rqst (&lmgenl_sock, msg);

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
 *   NONE
 *
 * Output Parameters:
 * ==================
 *   -1, Failure
 *    0, Success
 */
int
linkmetrics_netlink_init (void)
{
  if (lmgenl_open_recv (&lmgenl_sock))
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
