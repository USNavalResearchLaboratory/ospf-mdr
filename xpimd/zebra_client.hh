// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef _ZEBRA_CLIENT_H_
#define _ZEBRA_CLIENT_H_

#include "libxorp/xorp.h"

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
}

extern "C" {

int zebra_rid_update_cb(int command, struct zclient *zclient,
			zebra_size_t length);
int zebra_if_add_cb(int command, struct zclient *zclient,
		    zebra_size_t length);
int zebra_if_del_cb(int command, struct zclient *zclient,
		    zebra_size_t length);
int zebra_if_updown_cb(int command, struct zclient *zclient,
		       zebra_size_t length);
int zebra_if_addr_adddel_cb(int command, struct zclient *zclient,
			    zebra_size_t length);
int zebra_ipv4_route_adddel_cb(int command, struct zclient *zclient,
			       zebra_size_t length);
#ifdef HAVE_IPV6
int zebra_ipv6_route_adddel_cb(int command, struct zclient *zclient,
			       zebra_size_t length);
#endif	// HAVE_IPV6
}

#endif	/* _ZEBRA_CLIENT_H_ */
