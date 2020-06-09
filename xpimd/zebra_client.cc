// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#include "zebra_client_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"

#include "zebra_client.hh"
#include "zebra_router.hh"

extern "C" {
#include "stream.h"
#include "memory.h"
}

extern struct thread_master *master;

/* router id update message from zebra */
int
zebra_rid_update_cb(int command, struct zclient *zclient,
		    zebra_size_t length)
{
    struct prefix rid;
    ZebraRouter *zr = (ZebraRouter *)master->data;

    zebra_router_id_update_read(zclient->ibuf, &rid);
    zr->zebra_rid_update(&rid);

    return 0;
}

/* network inteface add message from zebra */
int
zebra_if_add_cb(int command, struct zclient *zclient, zebra_size_t length)
{
    ZebraRouter *zr = (ZebraRouter *)master->data;
    struct interface *ifp;

    if (command != ZEBRA_INTERFACE_ADD)
    {
	XLOG_WARNING("unknown command: %d", command);
	return -1;
    }

    if ((ifp = zebra_interface_add_read(zclient->ibuf)) == NULL)
    {
	XLOG_ERROR("zebra_interface_add_read() returned NULL");
	return -1;
    }

    zr->zebra_if_add(ifp);

    return 0;
}

/* network inteface delete message from zebra */
int
zebra_if_del_cb(int command, struct zclient *zclient, zebra_size_t length)
{
    ZebraRouter *zr = (ZebraRouter *)master->data;
    struct interface *ifp;

    if (command != ZEBRA_INTERFACE_DELETE)
    {
	XLOG_WARNING("unknown command: %d", command);
	return -1;
    }

    if ((ifp = zebra_interface_state_read(zclient->ibuf)) == NULL)
    {
	XLOG_ERROR("zebra_interface_state_read() returned NULL");
	return -1;
    }

    zr->zebra_if_del(ifp);

    ifp->ifindex = IFINDEX_INTERNAL;

    if (if_is_transient (ifp))
        if_delete (ifp);

    return 0;
}

/* network inteface up/down message from zebra */
int
zebra_if_updown_cb(int command, struct zclient *zclient,
		   zebra_size_t length)
{
    ZebraRouter *zr = (ZebraRouter *)master->data;
    struct interface *ifp;

    if ((ifp = zebra_interface_state_read(zclient->ibuf)) == NULL)
    {
	XLOG_ERROR("zebra_interface_state_read() returned NULL");
	return -1;
    }

    if (command == ZEBRA_INTERFACE_UP)
	zr->zebra_if_up(ifp);
    else if  (command == ZEBRA_INTERFACE_DOWN)
	zr->zebra_if_down(ifp);
    else
    {
	XLOG_WARNING("unknown command: %d", command);
	return -1;
    }

    return 0;
}

/* network inteface up/down message from zebra */
int
zebra_if_addr_adddel_cb(int command, struct zclient *zclient,
			zebra_size_t length)
{
    ZebraRouter *zr = (ZebraRouter *)master->data;
    struct connected *c;

    /* sanity check command before using it */
    switch (command)
    {
    case ZEBRA_INTERFACE_ADDRESS_ADD:
    case ZEBRA_INTERFACE_ADDRESS_DELETE:
	break;

    default:
	XLOG_WARNING("unknown command: %d", command);
	return -1;
    }

    if ((c = zebra_interface_address_read(command, zclient->ibuf)) == NULL)
    {
	XLOG_ERROR("zebra_interface_address_read() returned NULL");
	return -1;
    }

    if (command == ZEBRA_INTERFACE_ADDRESS_ADD)
	zr->zebra_if_addr_add(c);
    else if (command == ZEBRA_INTERFACE_ADDRESS_DELETE)
	zr->zebra_if_addr_del(c);
    else
    {
	XLOG_WARNING("unknown command: %d", command);
	return -1;
    }

    return 0;
}

/* read an ipv4 address from the zebra message stream; *nexthop and
   *ifindex must be freed by the caller */
static int
zebra_route_read_ipv4(struct zclient *zclient, struct zapi_ipv4 *zapi,
		      struct prefix_ipv4 *prefix, struct in_addr **nexthop,
		      unsigned int **ifindex)
{
    struct stream *s;

    memset(zapi, 0, sizeof(*zapi));
    memset(prefix, 0, sizeof(*prefix));
    *nexthop = NULL;
    *ifindex = NULL;

    s = zclient->ibuf;

    /* Type, flags, message. */
    zapi->type = stream_getc(s);
    zapi->flags = stream_getc(s);
    zapi->message = stream_getc(s);

    /* IPv4 prefix. */
    prefix->family = AF_INET;
    prefix->prefixlen = stream_getc(s);
    stream_get(&prefix->prefix, s, PSIZE(prefix->prefixlen));

    /* Nexthop, ifindex, distance, metric. */
    if (CHECK_FLAG(zapi->message, ZAPI_MESSAGE_NEXTHOP))
    {
	zapi->nexthop_num = stream_getc(s);

	*nexthop = (struct in_addr *)
	    XCALLOC(MTYPE_NEXTHOP, zapi->nexthop_num * sizeof(**nexthop));
	if (CHECK_FLAG(zapi->message, ZAPI_MESSAGE_IFINDEX))
	{
	    *ifindex = (unsigned int *)
		XCALLOC(MTYPE_TMP, zapi->nexthop_num * sizeof(**ifindex));
	}

	for (unsigned int i = 0; i < zapi->nexthop_num; i++)
	{
	    (*nexthop + i)->s_addr = stream_get_ipv4 (s);
	    if (CHECK_FLAG(zapi->message, ZAPI_MESSAGE_IFINDEX))
	    {
		u_char ifindex_num;

		ifindex_num = stream_getc(s);
		XLOG_ASSERT(ifindex_num == 1);
		zapi->ifindex_num += ifindex_num;
		*(*ifindex + i) = stream_getl(s);
	    }
	}
    }
    if (CHECK_FLAG(zapi->message, ZAPI_MESSAGE_DISTANCE))
	zapi->distance = stream_getc(s);
    if (CHECK_FLAG(zapi->message, ZAPI_MESSAGE_METRIC))
	zapi->metric = stream_getl(s);

    return 0;
}

/* ipv4 route add/delete message from zebra */
int
zebra_ipv4_route_adddel_cb(int command, struct zclient *zclient,
			   zebra_size_t length)
{
    struct prefix_ipv4 p;
    struct zapi_ipv4 zapi;
    struct in_addr *nexthop;
    unsigned int *ifindex;
    int err = 0;
    ZebraRouter *zr = (ZebraRouter *)master->data;

    if (zebra_route_read_ipv4(zclient, &zapi, &p, &nexthop, &ifindex))
    {
	XLOG_ERROR("zebra_route_read_ipv4() failed");
	return -1;
    }

    if (command == ZEBRA_IPV4_ROUTE_ADD)
	zr->zebra_ipv4_route_add(&p, zapi.nexthop_num, nexthop,
				 ifindex, zapi.metric);
    else if (command == ZEBRA_IPV4_ROUTE_DELETE)
	zr->zebra_ipv4_route_del(&p, zapi.nexthop_num, nexthop,
				 ifindex, zapi.metric);
    else
    {
	XLOG_WARNING("unknown command: %d", command);
	err = -1;
    }

    if (nexthop != NULL)
	XFREE(MTYPE_NEXTHOP, nexthop);
    if (ifindex != NULL)
	XFREE(MTYPE_TMP, ifindex);

    return err;
}

#ifdef HAVE_IPV6

/* read an ipv6 address from the zebra message stream; *nexthop and
   *ifindex must be freed by the caller */
static int
zebra_route_read_ipv6(struct zclient *zclient, struct zapi_ipv6 *zapi,
		      struct prefix_ipv6 *prefix, struct in6_addr **nexthop,
		      unsigned int **ifindex)
{
    struct stream *s;

    memset(zapi, 0, sizeof(*zapi));
    memset(prefix, 0, sizeof(*prefix));
    *nexthop = NULL;
    *ifindex = NULL;

    s = zclient->ibuf;

    /* Type, flags, message. */
    zapi->type = stream_getc(s);
    zapi->flags = stream_getc(s);
    zapi->message = stream_getc(s);

    /* IPv6 prefix. */
    prefix->family = AF_INET6;
    prefix->prefixlen = stream_getc(s);
    stream_get(&prefix->prefix, s, PSIZE(prefix->prefixlen));

    /* Nexthop, ifindex, distance, metric. */
    if (CHECK_FLAG(zapi->message, ZAPI_MESSAGE_NEXTHOP))
    {
	zapi->nexthop_num = stream_getc(s);

	*nexthop = (struct in6_addr *)
	    XCALLOC(MTYPE_NEXTHOP, zapi->nexthop_num * sizeof(**nexthop));
	if (CHECK_FLAG(zapi->message, ZAPI_MESSAGE_IFINDEX))
	{
	    *ifindex = (unsigned int *)
		XCALLOC(MTYPE_TMP, zapi->nexthop_num * sizeof(**ifindex));
	}

	for (unsigned int i = 0; i < zapi->nexthop_num; i++)
	{
	    stream_get((*nexthop + i), s, sizeof(**nexthop));
	    if (CHECK_FLAG(zapi->message, ZAPI_MESSAGE_IFINDEX))
	    {
		u_char ifindex_num;

		ifindex_num = stream_getc(s);
		XLOG_ASSERT(ifindex_num == 1);
		zapi->ifindex_num += ifindex_num;
		*(*ifindex + i) = stream_getl(s);
	    }
	}
    }
    if (CHECK_FLAG(zapi->message, ZAPI_MESSAGE_DISTANCE))
	zapi->distance = stream_getc(s);
    if (CHECK_FLAG(zapi->message, ZAPI_MESSAGE_METRIC))
	zapi->metric = stream_getl(s);

    return 0;
}

/* ipv6 route add/delete message from zebra */
int
zebra_ipv6_route_adddel_cb(int command, struct zclient *zclient,
			   zebra_size_t length)
{
    struct prefix_ipv6 p;
    struct zapi_ipv6 zapi;
    struct in6_addr *nexthop;
    unsigned int *ifindex;
    int err = 0;
    ZebraRouter *zr = (ZebraRouter *)master->data;

    if (zebra_route_read_ipv6(zclient, &zapi, &p, &nexthop, &ifindex))
    {
	XLOG_ERROR("zebra_route_read_ipv6() failed");
	return -1;
    }

    if (command == ZEBRA_IPV6_ROUTE_ADD)
	zr->zebra_ipv6_route_add(&p, zapi.nexthop_num, nexthop,
				 ifindex, zapi.metric);
    else if (command == ZEBRA_IPV6_ROUTE_DELETE)
	zr->zebra_ipv6_route_del(&p, zapi.nexthop_num, nexthop,
				 ifindex, zapi.metric);
    else
    {
	XLOG_WARNING("unknown command: %d", command);
	err = -1;
    }

    if (nexthop != NULL)
	XFREE(MTYPE_NEXTHOP, nexthop);
    if (ifindex != NULL)
	XFREE(MTYPE_TMP, ifindex);

    return err;
}

#endif	/* HAVE_IPV6 */
