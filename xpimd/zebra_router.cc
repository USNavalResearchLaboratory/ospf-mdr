// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#include "zebra_router_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"

#include "zebra_router.hh"
#include "zebra_client.hh"
#include "zebra_thread.hh"

extern "C" {
#include "prefix.h"
#include "stream.h"
#include "command.h"
#include "vty.h"
#include "memory.h"
#include "filter.h"
#include "plist.h"
#include "log.h"
#include "version.h"
}


struct thread_master *master = NULL;

void
ZebraRouter::zebra_init(zlog_proto_t zproto)
{
    extern const char *__progname;

    master = thread_master_create();
    master->data = this;       // set data to the zrouter object for
			       // use by callbacks

    zlog_default = openzlog(__progname, zproto,
			    LOG_CONS | LOG_NDELAY | LOG_PID, LOG_DAEMON);

    zprivs_init(&_privs);

    signal_init(master, _signal_count, _signals);

    // initialize zebra library stuff
    cmd_init(1);	      // this must be called before vty_init()
    vty_init(master);
    memory_init();
    if_init();
    access_list_init();
    prefix_list_init();

    // add commands
    zebra_command_init();
}

void
ZebraRouter::zebra_start(bool redist[ZEBRA_ROUTE_MAX],
			 bool default_information)
{
    // initialize zclient stuff
    zebra_zclient_init(redist, default_information);

    // initialize vty stuff
    sort_node();

    // read the config file (commands must be defined before this)
    vty_read_config(const_cast<char *>(_config_file),
                    const_cast<char *>(_default_config_file));

    if (_dryrun)
	return;

    if (_daemonize)
    {
	if (daemon(0, 0) < 0)
	    XLOG_FATAL("daemon() failed: %s", strerror(errno));
	xlog_reinit();
    }

    if (_pid_file != NULL)
	pid_output(_pid_file);

    // start the TCP/unix socket listeners
    vty_serv_sock(_vty_addr, _vty_port, _vtysh_path);
}

void
ZebraRouter::zebra_terminate()
{
    if_terminate ();
    vty_terminate ();
    cmd_terminate ();

    if (_zclient)
	zclient_stop (_zclient);

    if (master) {
	thread_master_free (master);
	master = NULL;
    }
}

int
ZebraRouter::raise_privileges ()
{
    return _privs.change(ZPRIVS_RAISE);
}

int
ZebraRouter::lower_privileges ()
{
    return _privs.change(ZPRIVS_LOWER);
}

void
ZebraRouter::zebra_zclient_init(bool redist[ZEBRA_ROUTE_MAX],
				bool default_information)
{
  if (_zebra_socket != NULL)
    zclient_serv_path_set(_zebra_socket);

    _zclient = zclient_new();
    zclient_init(_zclient, ZEBRA_ROUTE_MAX);

    _zclient->router_id_update = zebra_rid_update_cb;

    _zclient->interface_add = zebra_if_add_cb;
    _zclient->interface_delete = zebra_if_del_cb;

    _zclient->interface_up = zebra_if_updown_cb;
    _zclient->interface_down = zebra_if_updown_cb;

    _zclient->interface_address_add = zebra_if_addr_adddel_cb;
    _zclient->interface_address_delete = zebra_if_addr_adddel_cb;

    _zclient->ipv4_route_add = zebra_ipv4_route_adddel_cb;
    _zclient->ipv4_route_delete = zebra_ipv4_route_adddel_cb;

#ifdef HAVE_IPV6
    _zclient->ipv6_route_add = zebra_ipv6_route_adddel_cb;
    _zclient->ipv6_route_delete = zebra_ipv6_route_adddel_cb;
#endif	/* HAVE_IPV6 */

    for(int i = 0; i < ZEBRA_ROUTE_MAX; i++)
	if (redist[i])
	    zclient_redistribute(ZEBRA_REDISTRIBUTE_ADD, _zclient, i);

    if (default_information)
	zclient_redistribute_default(ZEBRA_REDISTRIBUTE_DEFAULT_ADD, _zclient);

    // XXX this might not be needed
    if (zclient_start(_zclient))
        XLOG_ERROR("zclient_start() failed");
}

void
ZebraRouter::zebra_rid_update(const struct prefix *rid)
{
    char tmp[INET6_ADDRSTRLEN];

    if (inet_ntop(rid->family, &rid->u.prefix, tmp, sizeof(tmp)) == NULL)
    {
	XLOG_WARNING("inet_ntop() failed");
	return;
    }

    XLOG_INFO(true, "zebra router id update: %s", tmp);
}

void
ZebraRouter::zebra_if_add(const struct interface *ifp)
{
    XLOG_INFO(true, "zebra interface add: %s index %u",
	      ifp->name, ifp->ifindex);
}

void
ZebraRouter::zebra_if_del(const struct interface *ifp)
{
    XLOG_INFO(true, "zebra interface del: %s index %u",
	      ifp->name, ifp->ifindex);
}

void
ZebraRouter::zebra_if_up(const struct interface *ifp)
{
    XLOG_INFO(true, "zebra interface up: %s index %u",
	      ifp->name, ifp->ifindex);
}

void
ZebraRouter::zebra_if_down(const struct interface *ifp)
{
    XLOG_INFO(true, "zebra interface down: %s index %u",
	      ifp->name, ifp->ifindex);
}

void
ZebraRouter::zebra_if_addr_add(const struct connected *c)
{
    char tmp[INET6_ADDRSTRLEN];

    if (inet_ntop(c->address->family, &c->address->u.prefix,
		  tmp, sizeof(tmp)) == NULL)
    {
	XLOG_WARNING("invalid address: %s", strerror(errno));
	return;
    }

    XLOG_INFO(true, "zebra interface address add: %s %s/%u",
	      c->ifp->name, tmp, c->address->prefixlen);
}

void
ZebraRouter::zebra_if_addr_del(const struct connected *c)
{
    char tmp[INET6_ADDRSTRLEN];

    if (inet_ntop(c->address->family, &c->address->u.prefix,
		  tmp, sizeof(tmp)) == NULL)
    {
	XLOG_WARNING("invalid address: %s", strerror(errno));
	return;
    }

    XLOG_INFO(true, "zebra interface address delete: %s %s/%u",
	      c->ifp->name, tmp, c->address->prefixlen);
}

template<typename P, typename N>
static void
zebra_route_add_del(const char *action, const P *p, u_char numnexthop,
		    const N *nexthop, const uint32_t *ifindex,
		    u_int32_t metric)
{
    char pstr[INET6_ADDRSTRLEN];

    if (inet_ntop(p->family, &p->prefix, pstr, sizeof(pstr)) == NULL)
    {
	XLOG_WARNING("inet_ntop() failed");
	return;
    }

    if (numnexthop == 0)
    {
	XLOG_INFO(true, "zebra %s route for %s/%u: no nexthop(s)",
		  action, pstr, p->prefixlen);
	return;
    }

    XLOG_INFO(true, "zebra %s route for %s/%u", action, pstr, p->prefixlen);
    for (unsigned int i = 0; i < numnexthop; i++)
    {
	char nstr[INET6_ADDRSTRLEN];

	if (inet_ntop(p->family, &nexthop[i], nstr, sizeof(nstr)) == NULL)
	{
	    XLOG_WARNING("inet_ntop() failed");
	    continue;
	}

	XLOG_INFO(true, "    nexthop %s via ifindex %u", nstr, ifindex[i]);
    }
}

void
ZebraRouter::zebra_ipv4_route_add(const struct prefix_ipv4 *p,
				  u_char numnexthop,
				  const struct in_addr *nexthop,
				  const uint32_t *ifindex,
				  u_int32_t metric)
{
    zebra_route_add_del<struct prefix_ipv4, struct in_addr>
	("add", p, numnexthop, nexthop, ifindex, metric);
}

void
ZebraRouter::zebra_ipv4_route_del(const struct prefix_ipv4 *p,
				  u_char numnexthop,
				  const struct in_addr *nexthop,
				  const uint32_t *ifindex,
				  u_int32_t metric)
{
    zebra_route_add_del<struct prefix_ipv4, struct in_addr>
	("del", p, numnexthop, nexthop, ifindex, metric);
}

#ifdef HAVE_IPV6

void
ZebraRouter::zebra_ipv6_route_add(const struct prefix_ipv6 *p,
				  u_char numnexthop,
				  const struct in6_addr *nexthop,
				  const uint32_t *ifindex,
				  u_int32_t metric)
{
    zebra_route_add_del<struct prefix_ipv6, struct in6_addr>
	("add", p, numnexthop, nexthop, ifindex, metric);
}

void
ZebraRouter::zebra_ipv6_route_del(const struct prefix_ipv6 *p,
				  u_char numnexthop,
				  const struct in6_addr *nexthop,
				  const uint32_t *ifindex,
				  u_int32_t metric)
{
    zebra_route_add_del<struct prefix_ipv6, struct in6_addr>
	("del", p, numnexthop, nexthop, ifindex, metric);
}

#endif	// HAVE_IPV6

struct thread *
ZebraRouter::zebra_thread_add_read(zthread_func_t func, void *arg, int fd)
{
    ZthreadIOEventCb *zcb = new ZthreadIOEventCb(_eventloop, fd, IOT_READ,
						 func, arg);
    return zcb->thread();
}

struct thread *
ZebraRouter::zebra_thread_add_write(zthread_func_t func, void *arg, int fd)
{
    ZthreadIOEventCb *zcb = new ZthreadIOEventCb(_eventloop, fd, IOT_WRITE,
						 func, arg);
    return zcb->thread();
}

struct thread *
ZebraRouter::zebra_thread_add_event(zthread_func_t func, void *arg, int val)
{

    ZthreadEventCb *zcb = new ZthreadEventCb(_eventloop, func, arg, val);
    return zcb->thread();
}

struct thread *
ZebraRouter::zebra_thread_add_timer(zthread_func_t func, void *arg,
				    long waitsec)
{
    ZthreadTimerCb *zcb = new ZthreadTimerCb(_eventloop, waitsec, func, arg);
    return zcb->thread();
}
