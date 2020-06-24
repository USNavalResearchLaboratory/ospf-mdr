// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef _ZEBRA_ROUTER_NODE_HH_
#define _ZEBRA_ROUTER_NODE_HH_

#include "libxorp/xorp.h"
#include "libxorp/callback.hh"

#include "zebra_router.hh"

typedef XorpCallback1<void, const struct prefix *>::RefPtr ZebraRidUpdateCb;
typedef XorpCallback1<void, const struct interface *>::RefPtr ZebraIfCb;
typedef XorpCallback1<void, const struct connected *>::RefPtr ZebraIfAddrCb;
typedef XorpCallback5<void, const struct prefix_ipv4 *,
		      u_char, const struct in_addr *,
		      const u_int32_t *, u_int32_t>::RefPtr ZebraIpv4RtCb;
#ifdef HAVE_IPV6
typedef XorpCallback5<void, const struct prefix_ipv6 *,
		      u_char, const struct in6_addr *,
		      const u_int32_t *, u_int32_t>::RefPtr ZebraIpv6RtCb;
#endif	// HAVE_IPV6
typedef XorpCallback1<int, struct vty *>::RefPtr ZebraConfigWriteCb;

class ZebraRouterNode : public ZebraRouter {

public:

    ZebraRouterNode(EventLoop &eventloop, bool daemonize,
		    const char *config_file, const char *default_config_file,
		    const char *pid_file, const char *zebra_socket,
		    const char *vty_addr, uint16_t vty_port,
		    const char *vtysh_path, bool dryrun,
		    zebra_privs_t &privs, quagga_signal_t *signals,
		    unsigned int signal_count) :
	ZebraRouter(eventloop, daemonize, config_file, default_config_file,
		    pid_file, zebra_socket, vty_addr, vty_port,
		    vtysh_path, dryrun, privs, signals, signal_count)
    {}

    void init()
    {
	zebra_init();
    }
    void terminate()
    {
	zebra_terminate();
    }

    void zebra_init()
    {
	return ZebraRouter::zebra_init(ZLOG_PIM /* XXX change this? */);
    }
    void zebra_start();
    void zebra_terminate()
    {
	return ZebraRouter::zebra_terminate();
    }

    void zebra_rid_update(const struct prefix *rid);

    void zebra_if_add(const struct interface *ifp)
    {
	return dispatch_zebra_if_cblist(_if_add_cblist, ifp);
    }
    void zebra_if_del(const struct interface *ifp)
    {
	return dispatch_zebra_if_cblist(_if_del_cblist, ifp);
    }
    void zebra_if_up(const struct interface *ifp)
    {
	return dispatch_zebra_if_cblist(_if_up_cblist, ifp);
    }
    void zebra_if_down(const struct interface *ifp)
    {
	return dispatch_zebra_if_cblist(_if_down_cblist, ifp);
    }

    void zebra_if_addr_add(const struct connected *c)
    {
	return dispatch_zebra_if_addr_cblist(_if_addr_add_cblist, c);
    }
    void zebra_if_addr_del(const struct connected *c)
    {
	return dispatch_zebra_if_addr_cblist(_if_addr_del_cblist, c);
    }

    void zebra_ipv4_route_add(const struct prefix_ipv4 *p,
			      u_char numnexthop,
			      const struct in_addr *nexthop,
			      const u_int32_t *ifindex,
			      u_int32_t metric)
    {
	return dispatch_zebra_ipv4_route_cblist(_ipv4_rt_add_cblist,
						p, numnexthop, nexthop,
						ifindex, metric);
    }
    void zebra_ipv4_route_del(const struct prefix_ipv4 *p,
			      u_char numnexthop,
			      const struct in_addr *nexthop,
			      const u_int32_t *ifindex,
			      u_int32_t metric)
    {
	return dispatch_zebra_ipv4_route_cblist(_ipv4_rt_del_cblist,
						p, numnexthop, nexthop,
						ifindex, metric);
    }

#ifdef HAVE_IPV6
    void zebra_ipv6_route_add(const struct prefix_ipv6 *p,
			      u_char numnexthop,
			      const struct in6_addr *nexthop,
			      const u_int32_t *ifindex,
			      u_int32_t metric)
    {
	return dispatch_zebra_ipv6_route_cblist(_ipv6_rt_add_cblist,
						p, numnexthop, nexthop,
						ifindex, metric);
    }
    void zebra_ipv6_route_del(const struct prefix_ipv6 *p,
			      u_char numnexthop,
			      const struct in6_addr *nexthop,
			      const u_int32_t *ifindex,
			      u_int32_t metric)
    {
	return dispatch_zebra_ipv6_route_cblist(_ipv6_rt_del_cblist,
						p, numnexthop, nexthop,
						ifindex, metric);
    }
#endif	// HAVE_IPV6

    int zebra_config_write_interface(struct vty *vty);
    int zebra_config_write_debug(struct vty *vty);

#define ZEBRA_CBLIST(name, cbtype)		\
    private:					\
    list<cbtype> _ ## name ## _cblist;		\
    public:					\
    void add_ ## name ## _cb(const cbtype &cb)	\
    {						\
	_ ## name ## _cblist.push_back(cb);	\
    }						\
    void del_ ## name ## _cb(const cbtype &cb)	\
    {						\
	_ ## name ## _cblist.remove(cb);	\
    }

    ZEBRA_CBLIST(rid_update, ZebraRidUpdateCb);
    ZEBRA_CBLIST(if_add, ZebraIfCb);
    ZEBRA_CBLIST(if_del, ZebraIfCb);
    ZEBRA_CBLIST(if_up, ZebraIfCb);
    ZEBRA_CBLIST(if_down, ZebraIfCb);

    ZEBRA_CBLIST(if_addr_add, ZebraIfAddrCb);
    ZEBRA_CBLIST(if_addr_del, ZebraIfAddrCb);

    ZEBRA_CBLIST(ipv4_rt_add, ZebraIpv4RtCb);
    ZEBRA_CBLIST(ipv4_rt_del, ZebraIpv4RtCb);

#if HAVE_IPV6
    ZEBRA_CBLIST(ipv6_rt_add, ZebraIpv6RtCb);
    ZEBRA_CBLIST(ipv6_rt_del, ZebraIpv6RtCb);
#endif	// HAVE_IPV6

    ZEBRA_CBLIST(config_write_interface, ZebraConfigWriteCb);
    ZEBRA_CBLIST(config_write_debug, ZebraConfigWriteCb);

#undef ZEBRA_CBLIST

private:

    void zebra_command_init();

    void dispatch_zebra_if_cblist(const list<ZebraIfCb> &cblist,
				  const struct interface *ifp);
    void dispatch_zebra_if_addr_cblist(const list<ZebraIfAddrCb> &cblist,
				       const struct connected *c);
    void dispatch_zebra_ipv4_route_cblist(const list<ZebraIpv4RtCb> &cblist,
					  const struct prefix_ipv4 *p,
					  u_char numnexthop,
					  const struct in_addr *nexthop,
					  const u_int32_t *ifindex,
					  u_int32_t metric);
#ifdef HAVE_IPV6
    void dispatch_zebra_ipv6_route_cblist(const list<ZebraIpv6RtCb> &cblist,
					  const struct prefix_ipv6 *p,
					  u_char numnexthop,
					  const struct in6_addr *nexthop,
					  const u_int32_t *ifindex,
					  u_int32_t metric);
#endif	// HAVE_IPV6

};

// XXX make sure others are like this
extern "C" {
    int config_write_zrouter_interface(struct vty *vty);
    int config_write_zrouter_debug(struct vty *vty);
}

#endif	// _ZEBRA_ROUTER_NODE_HH_
