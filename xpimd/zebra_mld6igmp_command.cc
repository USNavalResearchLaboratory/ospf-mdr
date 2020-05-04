/* -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*- */

// Copyright 2014 The Boeing Company

#include "zebra_mld6igmp_module.h"

#include "libxorp/xorp.h"
#include "mld6igmp/mld6igmp_vif.hh"

#include "zebra_mld6igmp_node.hh"
#include "zebra_misc.hh"

extern "C" {
#include "if.h"
#include "command.h"
#include "vty.h"
}

#ifndef STRINGIFY
#define STRINGIFY(x) #x
#endif	// STRINGIFY
#ifndef XSTRINGIFY
#define XSTRINGIFY(x) STRINGIFY(x)
#endif	// XSTRINGIFY

#ifndef VNL
#define VNL VTY_NEWLINE
#endif	// VNL

#define VTY_TERM vty::VTY_TERM

#define ZMLD6IGMP_STR "Internet Group Management Protocol (IGMP)\n"
#ifdef HAVE_IPV6_MULTICAST
#define ZMLD6IGMP6_STR "Multicast Listener Discovery (MLD)\n"
#endif	// HAVE_IPV6_MULTICAST
#define TRACE_STR "Detailed tracing\n"


static ZebraMld6igmpNode *_zmld6igmp = NULL;

// zmld6igmp node
static struct cmd_node zmld6igmp_node = {
    MLD6IGMP_NODE,
    "%s(config-mld6igmp)# ",
    1				// vtysh
};


// zmld6igmp configuration write
int
config_write_zmld6igmp(struct vty *vty)
{
    XLOG_ASSERT(_zmld6igmp != NULL);

    if (_zmld6igmp->Mld6igmpNode::is_enabled())
    {
	vty_out(vty, "router %s%s", _zmld6igmp->zebra_protostr(), VNL);
	vty_out(vty, "!%s", VNL);
    }

    return CMD_SUCCESS;
}

DEFUN(router_igmp,
      router_igmp_cmd,
      "router igmp",
      ROUTER_STR
      ZMLD6IGMP_STR)
{
    XLOG_ASSERT(_zmld6igmp != NULL);

    string error_msg;
    if (_zmld6igmp->start(error_msg) != XORP_OK)
    {
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    vty->node = MLD6IGMP_NODE;
    vty->index = _zmld6igmp;

    return CMD_SUCCESS;
}

DEFUN(no_router_igmp,
      no_router_igmp_cmd,
      "no router igmp",
      NO_STR
      ROUTER_STR
      ZMLD6IGMP_STR)
{
    XLOG_ASSERT(_zmld6igmp != NULL);

    int r = CMD_SUCCESS;
    string error_msg;
    if (_zmld6igmp->stop(error_msg) != XORP_OK)
    {
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);
	r = CMD_WARNING;
    }

    // return to config node
    vty->node = CONFIG_NODE;
    vty->index = NULL;

    return r;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(router_igmp,
      router_mld6_cmd,
      "router mld6",
      ROUTER_STR
      ZMLD6IGMP6_STR);

ALIAS(no_router_igmp,
      no_router_mld6_cmd,
      "no router mld6",
      NO_STR
      ROUTER_STR
      ZMLD6IGMP6_STR);

#endif	// HAVE_IPV6_MULTICAST

// zmld6igmp interface configuration write
int
ZebraMld6igmpNode::zebra_config_write_interface(struct vty *vty)
{
    for (vector<Mld6igmpVif *>::const_iterator it =
	     const_proto_vifs().begin();
	 it != const_proto_vifs().end(); ++it)
    {
	Mld6igmpVif *vif = *it;
	if (vif == NULL)
	    continue;

	if (!vif->is_enabled())
	    continue;

	vty_out(vty, "interface %s%s", vif->name().c_str(), VNL);
	if (vif->is_enabled())
	    vty_out(vty, " %s %s%s", zebra_ipstr(), zebra_protostr(), VNL);

	vty_out(vty, " %s %s version %d%s", zebra_ipstr(), zebra_protostr(),
		vif->proto_version(), VNL);

	if (vif->ip_router_alert_option_check().get())
	    vty_out(vty, " %s %s enable-ip-router-alert-option-check%s",
		    zebra_ipstr(), zebra_protostr(), VNL);

	TimeVal tmp;

	tmp = vif->configured_query_interval().get();
	XLOG_ASSERT(tmp.usec() == 0);
	vty_out(vty, " %s %s query-interval %d%s",
		zebra_ipstr(), zebra_protostr(), tmp.sec(), VNL);

	tmp = vif->query_last_member_interval().get();
	XLOG_ASSERT(tmp.usec() == 0);
	vty_out(vty, " %s %s last-member-query-interval %d%s",
		zebra_ipstr(), zebra_protostr(), tmp.sec(), VNL);

	tmp = vif->query_response_interval().get();
	XLOG_ASSERT(tmp.usec() == 0);
	vty_out(vty, " %s %s query-max-response-time %d%s",
		zebra_ipstr(), zebra_protostr(), tmp.sec(), VNL);

	vty_out(vty, " %s %s robust-count %d%s",
		zebra_ipstr(), zebra_protostr(),
		vif->configured_robust_count().get(), VNL);

	for (list<IPvXNet>::const_iterator it =
		 vif->alternative_subnet_list().begin();
	     it != vif->alternative_subnet_list().end(); ++it)
	{
	    const IPvXNet& ipvxnet = *it;
	    vty_out(vty, " %s %s alternative-subnet %s%s",
		    zebra_ipstr(), zebra_protostr(),
		    ipvxnet.str().c_str(), VNL);
	}

	vty_out(vty, "!%s", VNL);
    }

    return CMD_SUCCESS;
}

DEFUN(ip_igmp,
      ip_igmp_cmd,
      "ip igmp",
      IP_STR
      ZMLD6IGMP_STR)
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmld6igmp != NULL);

    _zmld6igmp->get_if_config(ifp->name).enabled.set(true);

    // try to enable the interface now if it exists
    Mld6igmpVif *vif = _zmld6igmp->vif_find_by_name(ifp->name);
    if (vif != NULL)
    {
	string error_msg;
	if (_zmld6igmp->enable_vif(ifp->name, error_msg) != XORP_OK)
	    vty_out(vty, "couldn't enable interface %s: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
	else
	    // try to start the interface
	    _zmld6igmp->try_start_vif(ifp->name);
    }

    return CMD_SUCCESS;
}

DEFUN(no_ip_igmp,
      no_ip_igmp_cmd,
      "no ip igmp",
      NO_STR
      IP_STR
      ZMLD6IGMP_STR)
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmld6igmp != NULL);

    _zmld6igmp->get_if_config(ifp->name).enabled.set(false);

    // check if the interface exists
    Mld6igmpVif *vif = _zmld6igmp->vif_find_by_name(ifp->name);
    if (vif == NULL)
    {
	vty_out(vty, "couldn't find interface %s%s", ifp->name, VNL);
	return CMD_WARNING;
    }

    // check if the interface is already not enabled
    if (!vif->is_enabled())
	return CMD_SUCCESS;

    string error_msg;
    if (_zmld6igmp->stop_vif(ifp->name, error_msg) != XORP_OK)
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);

    if (_zmld6igmp->disable_vif(ifp->name, error_msg) != XORP_OK)
    {
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_igmp,
      ipv6_mld6_cmd,
      "ipv6 mld6",
      IP6_STR
      ZMLD6IGMP6_STR);

ALIAS(no_ip_igmp,
      no_ipv6_mld6_cmd,
      "no ipv6 mld6",
      NO_STR
      IP6_STR
      ZMLD6IGMP6_STR);

#endif	// HAVE_IPV6_MULTICAST

DEFUN(ip_igmp_version,
      ip_igmp_version_cmd,
      "ip igmp version <" XSTRINGIFY(IGMP_VERSION_MIN) "-" XSTRINGIFY(IGMP_VERSION_MAX) ">",
      IP_STR
      ZMLD6IGMP_STR
      "IGMP version\n"
      "Protocol version\n")
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmld6igmp != NULL);

    int version = strtol(argv[0], NULL, 10);
    _zmld6igmp->get_if_config(ifp->name).proto_version.set(version);

    // try to set now if the interface exists
    if (_zmld6igmp->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (_zmld6igmp->set_vif_proto_version(ifp->name, version,
					      error_msg) != XORP_OK)
	    vty_out(vty, "couldn't set protocol version for interface %s to %d: %s%s",
		    ifp->name, version, error_msg.c_str(), VNL);
    }

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_igmp_version,
      ipv6_mld6_version_cmd,
      "ipv6 mld6 version <" XSTRINGIFY(MLD_VERSION_MIN) "-" XSTRINGIFY(MLD_VERSION_MAX) ">",
      IP6_STR
      ZMLD6IGMP6_STR
      "MLD version"
      "Version\n");

#endif	// HAVE_IPV6_MULTICAST

DEFUN(ip_igmp_ip_router_alert_option_check,
      ip_igmp_ip_router_alert_option_check_cmd,
      "ip igmp ip-router-alert-option-check",
      IP_STR
      ZMLD6IGMP_STR
      "IP Router Alert option (see RFC 2113)\n")
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmld6igmp != NULL);

    _zmld6igmp->get_if_config(ifp->name).ip_router_alert_option_check.set(true);

    // try to set it now if the interface exists
    Mld6igmpVif *vif = _zmld6igmp->vif_find_by_name(ifp->name);
    if (vif != NULL)
    {
	string error_msg;
	if ( _zmld6igmp->set_vif_ip_router_alert_option_check(ifp->name, true,
							      error_msg) != XORP_OK)
	    vty_out(vty, "couldn't set ip router alert option check for interface %s to true: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
    }

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_igmp_ip_router_alert_option_check,
      ipv6_mld6_ip_router_alert_option_check_cmd,
      "ipv6 mld6 ip-router-alert-option-check",
      IP6_STR
      ZMLD6IGMP6_STR
      "IP Router Alert option (see RFC 2113)\n");

#endif	// HAVE_IPV6_MULTICAST

DEFUN(no_ip_igmp_ip_router_alert_option_check,
      no_ip_igmp_ip_router_alert_option_check_cmd,
      "no ip igmp ip-router-alert-option-check",
      NO_STR
      IP_STR
      ZMLD6IGMP_STR
      "IP Router Alert option (see RFC 2113)\n")
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmld6igmp != NULL);

    _zmld6igmp->get_if_config(ifp->name).ip_router_alert_option_check.set(false);

    // try to set it now if the interface exists
    Mld6igmpVif *vif = _zmld6igmp->vif_find_by_name(ifp->name);
    if (vif != NULL)
    {
	string error_msg;
	if ( _zmld6igmp->set_vif_ip_router_alert_option_check(ifp->name, false,
							      error_msg) != XORP_OK)
	    vty_out(vty, "couldn't set ip router alert option check for interface %s to false: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
    }

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(no_ip_igmp_ip_router_alert_option_check,
      no_ipv6_mld6_ip_router_alert_option_check_cmd,
      "no ipv6 mld6 ip-router-alert-option-check",
      NO_STR
      IP6_STR
      ZMLD6IGMP6_STR
      "IP Router Alert option (see RFC 2113)\n");

#endif	// HAVE_IPV6_MULTICAST

DEFUN(ip_igmp_query_interval,
      ip_igmp_query_interval_cmd,
      "ip igmp query-interval <1-1024>",
      IP_STR
      ZMLD6IGMP_STR
      "IGMP query interval\n"
      "Seconds\n")
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmld6igmp != NULL);

    int seconds = strtol(argv[0], NULL, 10);
    TimeVal timeval(seconds, 0);
    _zmld6igmp->get_if_config(ifp->name).query_interval.set(timeval);

    // try to set now if the interface exists
    Mld6igmpVif *vif = _zmld6igmp->vif_find_by_name(ifp->name);
    if (vif != NULL)
    {
	string error_msg;
	if ( _zmld6igmp->set_vif_query_interval(ifp->name, timeval,
						error_msg) != XORP_OK)
	    vty_out(vty, "couldn't set query interval for interface %s to %d: %s%s",
		    ifp->name, seconds, error_msg.c_str(), VNL);
    }

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_igmp_query_interval,
      ipv6_mld6_query_interval_cmd,
      "ipv6 mld6 query-interval <1-1024>",
      IP6_STR
      ZMLD6IGMP6_STR
      "MLD query interval\n"
      "Seconds\n");

#endif	// HAVE_IPV6_MULTICAST

DEFUN(ip_igmp_last_member_query_interval,
      ip_igmp_last_member_query_interval_cmd,
      "ip igmp last-member-query-interval <1-1024>",
      IP_STR
      ZMLD6IGMP_STR
      "IGMP last member query interval\n"
      "Seconds\n")
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmld6igmp != NULL);

    int seconds = strtol(argv[0], NULL, 10);
    TimeVal timeval(seconds, 0);
    _zmld6igmp->get_if_config(ifp->name).query_last_member_interval.set(timeval);

    // try to set now if the interface exists
    Mld6igmpVif *vif = _zmld6igmp->vif_find_by_name(ifp->name);
    if (vif != NULL)
    {
	string error_msg;
	if ( _zmld6igmp->set_vif_query_last_member_interval(ifp->name,
							    timeval,
							    error_msg) != XORP_OK)
	    vty_out(vty, "couldn't set last member query interval for interface %s to %d: %s%s",
		    ifp->name, seconds, error_msg.c_str(), VNL);
    }

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_igmp_last_member_query_interval,
      ipv6_mld6_last_member_query_interval_cmd,
      "ipv6 mld6 last-member-query-interval <1-1024>",
      IP6_STR
      ZMLD6IGMP6_STR
      "MLD last member query interval\n"
      "Seconds\n");

#endif	// HAVE_IPV6_MULTICAST

DEFUN(ip_igmp_query_max_response_time,
      ip_igmp_query_max_response_time_cmd,
      "ip igmp query-max-response-time <1-1024>",
      IP_STR
      ZMLD6IGMP_STR
      "IGMP query max response time\n"
      "Seconds\n")
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmld6igmp != NULL);

    int seconds = strtol(argv[0], NULL, 10);
    TimeVal timeval(seconds, 0);
    _zmld6igmp->get_if_config(ifp->name).query_response_interval.set(timeval);

    // try to set now if the interface exists
    Mld6igmpVif *vif = _zmld6igmp->vif_find_by_name(ifp->name);
    if (vif != NULL)
    {
	string error_msg;
	if ( _zmld6igmp->set_vif_query_response_interval(ifp->name,
							 timeval,
							 error_msg) != XORP_OK)
	    vty_out(vty, "couldn't set query response interval for interface %s to %d: %s%s",
		    ifp->name, seconds, error_msg.c_str(), VNL);
    }

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_igmp_query_max_response_time,
      ipv6_mld6_query_max_response_time_cmd,
      "ipv6 mld6 query-max-response-time <1-1024>",
      IP6_STR
      ZMLD6IGMP6_STR
      "MLD query max response time\n"
      "Seconds\n");

#endif	// HAVE_IPV6_MULTICAST

DEFUN(ip_igmp_robust_count,
      ip_igmp_robust_count_cmd,
      "ip igmp robust-count <2-10>",
      IP_STR
      ZMLD6IGMP_STR
      "IGMP robust count\n"
      "Robust count\n")
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmld6igmp != NULL);

    uint32_t robust_count = strtol(argv[0], NULL, 10);
    _zmld6igmp->get_if_config(ifp->name).robust_count.set(robust_count);

    // try to set now if the interface exists
    Mld6igmpVif *vif = _zmld6igmp->vif_find_by_name(ifp->name);
    if (vif != NULL)
    {
	string error_msg;
	if ( _zmld6igmp->set_vif_robust_count(ifp->name, robust_count,
					      error_msg) != XORP_OK)
	    vty_out(vty, "couldn't set robust count for interface %s to %d: %s%s",
		    ifp->name, robust_count, error_msg.c_str(), VNL);
    }

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_igmp_robust_count,
      ipv6_mld6_robust_count_cmd,
      "ipv6 mld6 robust-count <2-10>",
      IP6_STR
      ZMLD6IGMP6_STR
      "MLD robust count\n"
      "Robust count\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zmld6igmp_ip_igmp_alternative_subnet(ZebraMld6igmpNode *zmld6igmp,
				     struct vty *vty,
				     int argc, const char *argv[])
{
    XLOG_ASSERT(zmld6igmp != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    IPvXNet ipvxnet(argv[0]);

    pair<set<ZebraConfigVal<IPvXNet> >::const_iterator, bool> ret =
	zmld6igmp->get_if_config(ifp->name).alternative_subnets.insert(ZebraConfigVal<IPvXNet>(ipvxnet));
    if (ret.second == false)
	vty_out(vty, "alternative subnet %s already exists for interface %s%s",
		ipvxnet.str().c_str(), ifp->name,  VNL);

    // try now if the interface exists
    if (zmld6igmp->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zmld6igmp->add_alternative_subnet(ifp->name, ipvxnet,
					  error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't add alternative subnet %s for interface %s: %s%s",
		    ipvxnet.str().c_str(), ifp->name, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

static int
zmld6igmp_no_ip_igmp_alternative_subnet(ZebraMld6igmpNode *zmld6igmp,
					struct vty *vty,
					int argc, const char *argv[])
{
    XLOG_ASSERT(zmld6igmp != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    switch (argc)
    {
    case 0:
	zmld6igmp->get_if_config(ifp->name).alternative_subnets.clear();

	// try now if the interface exists
	if (zmld6igmp->vif_find_by_name(ifp->name) != NULL)
	{
	    string error_msg;
	    if (zmld6igmp->remove_all_alternative_subnets(ifp->name,
						      error_msg) != XORP_OK)
	    {
		vty_out(vty, "couldn't remove all alternative subnets for interface %s: %s%s",
			ifp->name, error_msg.c_str(), VNL);
		return CMD_WARNING;
	    }
	}
	break;

    case 1:
	{
	    IPvXNet ipvxnet(argv[0]);
	    if (zmld6igmp->get_if_config(ifp->name).alternative_subnets.erase(ZebraConfigVal<IPvXNet>(ipvxnet)) == 0)
		vty_out(vty, "alternative subnet %s does not exist for interface %s%s",
			ipvxnet.str().c_str(), ifp->name,  VNL);

	    // try now if the interface exists
	    if (zmld6igmp->vif_find_by_name(ifp->name) != NULL)
	    {
		string error_msg;
		if (zmld6igmp->delete_alternative_subnet(ifp->name, ipvxnet,
						     error_msg) != XORP_OK)
		{
		    vty_out(vty, "couldn't remove alternative subnet %s for interface %s: %s%s",
			    ipvxnet.str().c_str(), ifp->name, error_msg.c_str(), VNL);
		    return CMD_WARNING;
		}
	    }
	}
	break;

    default:
	return CMD_ERR_NO_MATCH;
    }

    return CMD_SUCCESS;
}

DEFUN(ip_igmp_alternative_subnet,
      ip_igmp_alternative_subnet_cmd,
      "ip igmp alternative-subnet A.B.C.D/M",
      IP_STR
      ZMLD6IGMP_STR
      "Associate an additional subnet with this network interface\n"
      "Subnet address/prefix length\n")
{
    ZebraMld6igmpNode *zmld6igmp = _zmld6igmp;
    XLOG_ASSERT(zmld6igmp != NULL);

    return zmld6igmp_ip_igmp_alternative_subnet(zmld6igmp, vty, argc, argv);
}

DEFUN(no_ip_igmp_alternative_subnet,
      no_ip_igmp_alternative_subnet_cmd,
      "no ip igmp alternative-subnet [A.B.C.D/M]",
      NO_STR
      IP_STR
      ZMLD6IGMP_STR
      "Remove additional subnet association from this network interface\n"
      "Optional Subnet address/prefix length (all additional subnets if omitted)\n")
{
    ZebraMld6igmpNode *zmld6igmp = _zmld6igmp;
    XLOG_ASSERT(zmld6igmp != NULL);

    return zmld6igmp_no_ip_igmp_alternative_subnet(zmld6igmp, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_igmp_alternative_subnet,
      ipv6_mld6_alternative_subnet_cmd,
      "ipv6 mld6 alternative-subnet X:X::X:X/M",
      IP6_STR
      ZMLD6IGMP6_STR
      "Associate an additional subnet with this network interface\n"
      "Subnet address/prefix length\n");

ALIAS(no_ip_igmp_alternative_subnet,
      no_ipv6_mld6_alternative_subnet_cmd,
      "no ipv6 mld6 alternative-subnet [X:X::X:X/M]",
      NO_STR
      IP6_STR
      ZMLD6IGMP6_STR
      "Remove additional subnet association from this network interface\n"
      "Optional Subnet address/prefix length (all additional subnets if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

// zmld6igmp debug configuration write
int
ZebraMld6igmpNode::zebra_config_write_debug(struct vty *vty)
{
    if (is_log_info() || is_log_trace())
    {
	if (is_log_info())
	    vty_out(vty, "debug %s%s", zebra_protostr(), VNL);
	if (is_log_trace())
	    vty_out(vty, "debug %s trace%s", zebra_protostr(), VNL);
	vty_out(vty, "!%s", VNL);
    }

    return CMD_SUCCESS;
}

DEFUN(debug_igmp,
      debug_igmp_cmd,
      "debug igmp",
      DEBUG_STR
      ZMLD6IGMP_STR)
{
    XLOG_ASSERT(_zmld6igmp != NULL);

    _zmld6igmp->set_log_info(true);

    return CMD_SUCCESS;
}

DEFUN(no_debug_igmp,
      no_debug_igmp_cmd,
      "no debug igmp",
      NO_STR
      DEBUG_STR
      ZMLD6IGMP_STR)
{
    XLOG_ASSERT(_zmld6igmp != NULL);

    _zmld6igmp->set_log_info(false);

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(debug_igmp,
      debug_mld6_cmd,
      "debug mld6",
      DEBUG_STR
      ZMLD6IGMP6_STR);

ALIAS(no_debug_igmp,
      no_debug_mld6_cmd,
      "no debug mld6",
      NO_STR
      DEBUG_STR
      ZMLD6IGMP6_STR);

#endif	// HAVE_IPV6_MULTICAST

DEFUN(debug_igmp_trace,
      debug_igmp_trace_cmd,
      "debug igmp trace",
      DEBUG_STR
      ZMLD6IGMP_STR
      TRACE_STR)
{
    XLOG_ASSERT(_zmld6igmp != NULL);

    _zmld6igmp->set_log_trace(true);

    return CMD_SUCCESS;
}

DEFUN(no_debug_igmp_trace,
      no_debug_igmp_trace_cmd,
      "no debug igmp trace",
      NO_STR
      DEBUG_STR
      ZMLD6IGMP_STR
      TRACE_STR)
{
    XLOG_ASSERT(_zmld6igmp != NULL);

    _zmld6igmp->set_log_trace(false);

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(debug_igmp_trace,
      debug_mld6_trace_cmd,
      "debug mld6 trace",
      DEBUG_STR
      ZMLD6IGMP6_STR
      TRACE_STR);

ALIAS(no_debug_igmp_trace,
      no_debug_mld6_trace_cmd,
      "no debug mld6 trace",
      NO_STR
      DEBUG_STR
      ZMLD6IGMP6_STR
      TRACE_STR);

#endif	// HAVE_IPV6_MULTICAST

DEFUN(show_ip_igmp_group,
      show_ip_igmp_group_cmd,
      "show ip igmp group [A.B.C.D] ....",
      SHOW_STR
      IP_STR
      ZMLD6IGMP_STR
      "IGMP group information\n"
      "Optional multicast group(s) (all groups if omitted)\n"
      "Additional multicast group(s)\n")
{
    XLOG_ASSERT(_zmld6igmp != NULL);

    string command_args = "";
    for (int i = 0; i < argc; i++)
	command_args += string(" ") + argv[i];

    return cli_process_command(_zmld6igmp, string("show ") +
			       _zmld6igmp->xorp_protostr() + string(" group"),
			       command_args, vty);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_igmp_group,
      show_ipv6_mld6_group_cmd,
      "show ipv6 mld6 group [X:X::X:X] ....",
      SHOW_STR
      IP6_STR
      ZMLD6IGMP6_STR
      "MLD group information\n"
      "Optional multicast group(s) (all groups if omitted)\n"
      "Additional multicast group(s)\n");

#endif	// HAVE_IPV6_MULTICAST

DEFUN(show_ip_igmp_interface,
      show_ip_igmp_interface_cmd,
      "show ip igmp interface [IFNAME]",
      SHOW_STR
      IP_STR
      ZMLD6IGMP_STR
      INTERFACE_STR
      "Optional interface name (all interfaces if omitted)\n")
{
    XLOG_ASSERT(_zmld6igmp != NULL);

    return cli_process_command(_zmld6igmp, string("show ") +
			       _zmld6igmp->xorp_protostr() +
			       string(" interface"),
			       (argc ? argv[0] : ""), vty);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_igmp_interface,
      show_ipv6_mld6_interface_cmd,
      "show ipv6 mld6 interface [IFNAME]",
      SHOW_STR
      IP6_STR
      ZMLD6IGMP6_STR
      INTERFACE_STR
      "Optional interface name (all interfaces if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

DEFUN(show_ip_igmp_interface_address,
      show_ip_igmp_interface_address_cmd,
      "show ip igmp interface-address [IFNAME]",
      SHOW_STR
      IP_STR
      ZMLD6IGMP_STR
      "Interface address information\n"
      "Optional interface name (all interfaces if omitted)\n")
{
    XLOG_ASSERT(_zmld6igmp != NULL);

    return cli_process_command(_zmld6igmp,  string("show ") +
			       _zmld6igmp->xorp_protostr() +
			       string(" interface address"),
			       (argc ? argv[0] : ""), vty);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_igmp_interface_address,
      show_ipv6_mld6_interface_address_cmd,
      "show ipv6 mld6 interface-address [IFNAME]",
      SHOW_STR
      IP6_STR
      ZMLD6IGMP6_STR
      "Interface address information\n"
      "Optional interface name (all interfaces if omitted)\n")

#endif	// HAVE_IPV6_MULTICAST

void
ZebraMld6igmpNode::zebra_command_init()
{
    XLOG_ASSERT(_zmld6igmp == NULL);

    _zmld6igmp = this;

    // install the zmld6igmp node
    install_node(&zmld6igmp_node, config_write_zmld6igmp);
    install_default(MLD6IGMP_NODE); // add the default commands (exit, etc.)

    if (Mld6igmpNode::family() == AF_INET)
    {
	install_element(CONFIG_NODE, &router_igmp_cmd);
	install_element(CONFIG_NODE, &no_router_igmp_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (Mld6igmpNode::family() == AF_INET6)
    {
	install_element(CONFIG_NODE, &router_mld6_cmd);
	install_element(CONFIG_NODE, &no_router_mld6_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
	XLOG_UNREACHABLE();

    // interface commands
    if (Mld6igmpNode::family() == AF_INET)
    {
	install_element(INTERFACE_NODE, &ip_igmp_cmd);
	install_element(INTERFACE_NODE, &no_ip_igmp_cmd);

	install_element(INTERFACE_NODE, &ip_igmp_version_cmd);
	install_element(INTERFACE_NODE,
			&ip_igmp_ip_router_alert_option_check_cmd);
	install_element(INTERFACE_NODE,
			&no_ip_igmp_ip_router_alert_option_check_cmd);
	install_element(INTERFACE_NODE,
			&ip_igmp_query_interval_cmd);
	install_element(INTERFACE_NODE,
			&ip_igmp_last_member_query_interval_cmd);
	install_element(INTERFACE_NODE,
			&ip_igmp_query_max_response_time_cmd);
	install_element(INTERFACE_NODE, &ip_igmp_robust_count_cmd);
	install_element(INTERFACE_NODE, &ip_igmp_alternative_subnet_cmd);
	install_element(INTERFACE_NODE, &no_ip_igmp_alternative_subnet_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (Mld6igmpNode::family() == AF_INET6)
    {
	install_element(INTERFACE_NODE, &ipv6_mld6_cmd);
	install_element(INTERFACE_NODE, &no_ipv6_mld6_cmd);

	install_element(INTERFACE_NODE, &ipv6_mld6_version_cmd);
	install_element(INTERFACE_NODE,
			&ipv6_mld6_ip_router_alert_option_check_cmd);
	install_element(INTERFACE_NODE,
			&no_ipv6_mld6_ip_router_alert_option_check_cmd);
	install_element(INTERFACE_NODE,
			&ipv6_mld6_query_interval_cmd);
	install_element(INTERFACE_NODE,
			&ipv6_mld6_last_member_query_interval_cmd);
	install_element(INTERFACE_NODE,
			&ipv6_mld6_query_max_response_time_cmd);
	install_element(INTERFACE_NODE, &ipv6_mld6_robust_count_cmd);
	install_element(INTERFACE_NODE, &ipv6_mld6_alternative_subnet_cmd);
	install_element(INTERFACE_NODE, &no_ipv6_mld6_alternative_subnet_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
	XLOG_UNREACHABLE();

    // debug commands
    if (Mld6igmpNode::family() == AF_INET)
    {
	install_element(CONFIG_NODE, &debug_igmp_cmd);
	install_element(CONFIG_NODE, &no_debug_igmp_cmd);
	install_element(CONFIG_NODE, &debug_igmp_trace_cmd);
	install_element(CONFIG_NODE, &no_debug_igmp_trace_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (Mld6igmpNode::family() == AF_INET6)
    {
	install_element(CONFIG_NODE, &debug_mld6_cmd);
	install_element(CONFIG_NODE, &no_debug_mld6_cmd);
	install_element(CONFIG_NODE, &debug_mld6_trace_cmd);
	install_element(CONFIG_NODE, &no_debug_mld6_trace_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
	XLOG_UNREACHABLE();

#define ADD_SHOW_CMD(cmd)			\
    do {					\
 	install_element(VIEW_NODE, &cmd);	\
 	install_element(ENABLE_NODE, &cmd);	\
    } while (0)

    // show commands
    if (Mld6igmpNode::family() == AF_INET)
    {
	ADD_SHOW_CMD(show_ip_igmp_group_cmd);
	ADD_SHOW_CMD(show_ip_igmp_interface_cmd);
	ADD_SHOW_CMD(show_ip_igmp_interface_address_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (Mld6igmpNode::family() == AF_INET6)
    {
	ADD_SHOW_CMD(show_ipv6_mld6_group_cmd);
	ADD_SHOW_CMD(show_ipv6_mld6_interface_cmd);
	ADD_SHOW_CMD(show_ipv6_mld6_interface_address_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
    {
	XLOG_UNREACHABLE();
    }

#undef ADD_SHOW_CMD
}
