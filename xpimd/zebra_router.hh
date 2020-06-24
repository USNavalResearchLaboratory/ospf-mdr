// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef _ZEBRA_ROUTER_HH_
#define _ZEBRA_ROUTER_HH_

#include "zebra_router_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"

/*
  include these before zebra.h to work around the C++ include problem
  for nd6.h mentioned below
*/
#ifdef HAVE_NET_IF_VAR_H
#include <net/if_var.h>
#endif
#ifdef HAVE_NETINET6_IN6_VAR_H
#include <netinet6/in6_var.h>
#endif
#ifdef HAVE_NETINET6_ND6_H
#ifdef HAVE_BROKEN_CXX_NETINET6_ND6_H
// XXX: a hack needed if <netinet6/nd6.h> is not C++ friendly
#define prf_ra in6_prflags::prf_ra
#endif
#include <netinet6/nd6.h>
#endif

extern "C" {
#include "zebra.h"
#include "zclient.h"
#include "thread.h"
#include "log.h"
#include "privs.h"
#include "sigevent.h"
}

#include "libxorp/eventloop.hh"

#include "zebra_client.hh"

typedef int (*zthread_func_t)(struct thread *);

class ZebraRouter {

public:

    ZebraRouter(EventLoop &eventloop, bool daemonize,
		const char *config_file, const char *default_config_file,
		const char *pid_file, const char *zebra_socket,
		const char *vty_addr, uint16_t vty_port,
		const char *vtysh_path, bool dryrun, zebra_privs_t &privs,
		quagga_signal_t *signals, unsigned int signal_count) :
	_eventloop(eventloop), _zclient(NULL), _daemonize(daemonize),
	_config_file(config_file), _default_config_file(default_config_file),
	_pid_file(pid_file), _zebra_socket(zebra_socket),
	_vty_addr(vty_addr), _vty_port(vty_port),
	_vtysh_path(vtysh_path), _dryrun(dryrun), _privs(privs),
	_signals(signals), _signal_count(signal_count)
    {}

    virtual ~ZebraRouter()
    {
	if (_zclient != NULL)
	{
	    if (_zclient->sock >= 0)
	    {
		if (_eventloop.remove_ioevent_cb(XorpFd(_zclient->sock),
						 IOT_ANY) == false)
		    XLOG_WARNING("remove_ioevent_cb() failed: "
				 "fd = %d; iotype = %d",
				 _zclient->sock, IOT_ANY);
	    }
	    zclient_stop(_zclient);
	    zclient_free(_zclient);
	    _zclient = NULL;
	}
    }

    virtual void zebra_rid_update(const struct prefix *rid);
    virtual void zebra_if_add(const struct interface *ifp);
    virtual void zebra_if_del(const struct interface *ifp);
    virtual void zebra_if_up(const struct interface *ifp);
    virtual void zebra_if_down(const struct interface *ifp);
    virtual void zebra_if_addr_add(const struct connected *c);
    virtual void zebra_if_addr_del(const struct connected *c);
    virtual void zebra_ipv4_route_add(const struct prefix_ipv4 *p,
				      u_char numnexthop,
				      const struct in_addr *nexthop,
				      const uint32_t *ifindex,
				      u_int32_t metric);
    virtual void zebra_ipv4_route_del(const struct prefix_ipv4 *p,
				      u_char numnexthop,
				      const struct in_addr *nexthop,
				      const uint32_t *ifindex,
				      u_int32_t metric);
#ifdef HAVE_IPV6
    virtual void zebra_ipv6_route_add(const struct prefix_ipv6 *p,
				      u_char numnexthop,
				      const struct in6_addr *nexthop,
				      const uint32_t *ifindex,
				      u_int32_t metric);
    virtual void zebra_ipv6_route_del(const struct prefix_ipv6 *p,
				      u_char numnexthop,
				      const struct in6_addr *nexthop,
				      const uint32_t *ifindex,
				      u_int32_t metric);
#endif	// HAVE_IPV6

    struct thread *zebra_thread_add_read(zthread_func_t func,
					 void *arg, int fd);
    struct thread *zebra_thread_add_write(zthread_func_t func,
					  void *arg, int fd);
    struct thread *zebra_thread_add_timer(zthread_func_t func,
					  void *arg, long waitsec);
    struct thread *zebra_thread_add_event(zthread_func_t func,
					  void *arg, int val);

    void zebra_init(zlog_proto_t zproto);
    void zebra_start(bool redist[ZEBRA_ROUTE_MAX],
		     bool default_information);
    void zebra_terminate();

    int raise_privileges();
    int lower_privileges();

protected:

    virtual void zebra_zclient_init(bool redist[ZEBRA_ROUTE_MAX],
				    bool default_information);
    virtual void zebra_command_init() {}

private:

    EventLoop &_eventloop;
    struct zclient *_zclient;
    const bool _daemonize;
    const char *_config_file;
    const char *_default_config_file;
    const char *_pid_file;
    const char *_zebra_socket;
    const char *_vty_addr;
    uint16_t _vty_port;
    const char *_vtysh_path;
    const bool _dryrun;
    zebra_privs_t &_privs;
    quagga_signal_t *_signals;
    unsigned int _signal_count;
};

#endif	// _ZEBRA_ROUTER_HH_
