/* -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*- */

// Copyright 2014 The Boeing Company

#include "zebra_mfea_module.h"

#include "libxorp/xorp.h"

#include "fea/mfea_vif.hh"

#include "zebra_mfea_node.hh"
#include "zebra_misc.hh"

extern "C" {
#include "if.h"
#include "command.h"
#include "vty.h"
}


#ifndef VNL
#define VNL VTY_NEWLINE
#endif

#define VTY_TERM vty::VTY_TERM

#define ZMFEA_STR  "Multicast Forwarding Engine Abstraction (MFEA)\n"
#ifdef HAVE_IPV6_MULTICAST
#define ZMFEA6_STR "IPv6 Multicast Forwarding Engine Abstraction (MFEA)\n"
#endif	// HAVE_IPV6_MULTICAST
#define TRACE_STR "Detailed tracing\n"


static ZebraMfeaNode *_zmfea = NULL;

// zmfea node
static struct cmd_node zmfea_node = {
    MFEA_NODE,
    "%s(config-mfea)# ",
    1				// vtysh
};


// zmfea configuration write
int
config_write_zmfea(struct vty *vty)
{
    XLOG_ASSERT(_zmfea != NULL);

    if (_zmfea->MfeaNode::is_enabled())
    {
	vty_out(vty, "router %s%s", _zmfea->zebra_protostr(), VNL);
	vty_out(vty, "!%s", VNL);
    }

    return CMD_SUCCESS;
}

DEFUN(router_mfea,
      router_mfea_cmd,
      "router mfea",
      ROUTER_STR
      ZMFEA_STR)
{
    XLOG_ASSERT(_zmfea != NULL);

    string error_msg;
    if (_zmfea->start(error_msg) != XORP_OK)
    {
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    vty->node = MFEA_NODE;
    vty->index = _zmfea;

    return CMD_SUCCESS;
}

DEFUN(no_router_mfea,
      no_router_mfea_cmd,
      "no router mfea",
      NO_STR
      ROUTER_STR
      ZMFEA_STR)
{
    XLOG_ASSERT(_zmfea != NULL);

    int r = CMD_SUCCESS;
    string error_msg;
    if (_zmfea->stop(error_msg) != XORP_OK)
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

ALIAS(router_mfea,
      router_mfea6_cmd,
      "router mfea6",
      ROUTER_STR
      ZMFEA6_STR);

ALIAS(no_router_mfea,
      no_router_mfea6_cmd,
      "no router mfea6",
      NO_STR
      ROUTER_STR
      ZMFEA6_STR);

#endif	// HAVE_IPV6_MULTICAST

// zmfea interface configuration write
int
ZebraMfeaNode::zebra_config_write_interface(struct vty *vty)
{
    for (vector<MfeaVif *>::const_iterator it = const_proto_vifs().begin();
	 it != const_proto_vifs().end(); ++it)
    {
	const MfeaVif *vif = *it;
	if (vif == NULL)
	    continue;

	if (!vif->is_enabled())
	    continue;

	if (vif->is_pim_register())
	    continue;

	vty_out(vty, "interface %s%s", vif->name().c_str(), VNL);
	if (vif->is_enabled())
	    vty_out(vty, " %s %s%s", zebra_ipstr(), zebra_protostr(), VNL);

	vty_out(vty, "!%s", VNL);
    }

    return CMD_SUCCESS;
}

DEFUN(ip_mfea,
      ip_mfea_cmd,
      "ip mfea",
      IP_STR
      ZMFEA_STR)
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmfea != NULL);

    _zmfea->get_if_config(ifp->name).enabled.set(true);

    // try to enable the interface now if it exists
    MfeaVif *vif = _zmfea->vif_find_by_name(ifp->name);
    if (vif != NULL)
    {
	string error_msg;
	if (_zmfea->enable_vif(ifp->name, error_msg) != XORP_OK)
	    vty_out(vty, "couldn't enable interface %s: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
	else
	    // try to start the interface
	    _zmfea->try_start_vif(ifp->name);
    }

    return CMD_SUCCESS;
}

DEFUN(no_ip_mfea,
      no_ip_mfea_cmd,
      "no ip mfea",
      NO_STR
      IP_STR
      ZMFEA_STR)
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(_zmfea != NULL);

    _zmfea->get_if_config(ifp->name).enabled.set(false);

    // check if the interface exists
    MfeaVif *vif = _zmfea->vif_find_by_name(ifp->name);
    if (vif == NULL)
    {
	vty_out(vty, "couldn't find interface %s%s", ifp->name, VNL);
	return CMD_WARNING;
    }

    // check if the interface is already not enabled
    if (!vif->is_enabled())
	return CMD_SUCCESS;

    string error_msg;
    if (_zmfea->stop_vif(ifp->name, error_msg) != XORP_OK)
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);

    if (_zmfea->disable_vif(ifp->name, error_msg) != XORP_OK)
    {
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_mfea,
      ipv6_mfea_cmd,
      "ipv6 mfea6",
      IP6_STR
      ZMFEA6_STR);

ALIAS(no_ip_mfea,
      no_ipv6_mfea_cmd,
      "no ipv6 mfea6",
      NO_STR
      IP6_STR
      ZMFEA6_STR);

#endif	// HAVE_IPV6_MULTICAST

// zmfea debug configuration write
int
ZebraMfeaNode::zebra_config_write_debug(struct vty *vty)
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

DEFUN(debug_mfea,
      debug_mfea_cmd,
      "debug mfea",
      DEBUG_STR
      ZMFEA_STR)
{
    XLOG_ASSERT(_zmfea != NULL);

    _zmfea->set_log_info(true);

    return CMD_SUCCESS;
}

DEFUN(no_debug_mfea,
      no_debug_mfea_cmd,
      "no debug mfea",
      NO_STR
      DEBUG_STR
      ZMFEA_STR)
{
    XLOG_ASSERT(_zmfea != NULL);

    _zmfea->set_log_info(false);

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(debug_mfea,
      debug_mfea6_cmd,
      "debug mfea6",
      DEBUG_STR
      ZMFEA6_STR);

ALIAS(no_debug_mfea,
      no_debug_mfea6_cmd,
      "no debug mfea6",
      NO_STR
      DEBUG_STR
      ZMFEA6_STR);

#endif	// HAVE_IPV6_MULTICAST

DEFUN(debug_mfea_trace,
      debug_mfea_trace_cmd,
      "debug mfea trace",
      DEBUG_STR
      ZMFEA_STR
      TRACE_STR)
{
    XLOG_ASSERT(_zmfea != NULL);

    _zmfea->set_log_trace(true);

    return CMD_SUCCESS;
}

DEFUN(no_debug_mfea_trace,
      no_debug_mfea_trace_cmd,
      "no debug mfea trace",
      NO_STR
      DEBUG_STR
      ZMFEA_STR
      TRACE_STR)
{
    XLOG_ASSERT(_zmfea != NULL);

    _zmfea->set_log_trace(false);

    return CMD_SUCCESS;
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(debug_mfea_trace,
      debug_mfea6_trace_cmd,
      "debug mfea6 trace",
      DEBUG_STR
      ZMFEA6_STR
      TRACE_STR);

ALIAS(no_debug_mfea_trace,
      no_debug_mfea6_trace_cmd,
      "no debug mfea6 trace",
      NO_STR
      DEBUG_STR
      ZMFEA6_STR
      TRACE_STR);

#endif	// HAVE_IPV6_MULTICAST

DEFUN(show_ip_mfea_dataflow,
      show_ip_mfea_dataflow_cmd,
      "show ip mfea dataflow [A.B.C.D[/M]]",
      SHOW_STR
      IP_STR
      ZMFEA_STR
      "Dataflow filter information\n"
      "Optional multicast group/group range (all groups if omitted)\n")
{
    XLOG_ASSERT(_zmfea != NULL);

    return cli_process_command(_zmfea, string("show ") +
			       _zmfea->xorp_protostr() + string(" dataflow"),
			       (argc ? argv[0] : ""), vty);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_mfea_dataflow,
      show_ipv6_mfea6_dataflow_cmd,
      "show ipv6 mfea6 dataflow [X:X::X:X[/M]]",
      SHOW_STR
      IP6_STR
      ZMFEA6_STR
      "Dataflow filter information\n"
      "Optional multicast group/group range (all groups if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

DEFUN(show_ip_mfea_interface,
      show_ip_mfea_interface_cmd,
      "show ip mfea interface [IFNAME]",
      SHOW_STR
      IP_STR
      ZMFEA_STR
      INTERFACE_STR
      "Optional interface name (all interfaces if omitted)\n")
{
    XLOG_ASSERT(_zmfea != NULL);

    return cli_process_command(_zmfea, string("show ") +
			       _zmfea->xorp_protostr() + string(" interface"),
			       (argc ? argv[0] : ""), vty);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_mfea_interface,
      show_ipv6_mfea6_interface_cmd,
      "show ipv6 mfea6 interface [IFNAME]",
      SHOW_STR
      IP6_STR
      ZMFEA6_STR
      INTERFACE_STR
      "Optional interface name (all interfaces if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

DEFUN(show_ip_mfea_interface_address,
      show_ip_mfea_interface_address_cmd,
      "show ip mfea interface-address [IFNAME]",
      SHOW_STR
      IP_STR
      ZMFEA_STR
      "Interface address information\n"
      "Optional interface name (all interfaces if omitted)\n")
{
    XLOG_ASSERT(_zmfea != NULL);

    return cli_process_command(_zmfea, string("show ") +
			       _zmfea->xorp_protostr() +
			       string(" interface address"),
			       (argc ? argv[0] : ""), vty);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_mfea_interface_address,
      show_ipv6_mfea6_interface_address_cmd,
      "show ipv6 mfea6 interface-address [IFNAME]",
      SHOW_STR
      IP6_STR
      ZMFEA6_STR
      "Interface address information\n"
      "Optional interface name (all interfaces if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

void
ZebraMfeaNode::zebra_command_init()
{
    XLOG_ASSERT(_zmfea == NULL);

    _zmfea = this;

    // install the zmfea node
    install_node(&zmfea_node, config_write_zmfea);
    install_default(MFEA_NODE); // add the default commands (exit, etc.)

    // zmfea commands
    if (MfeaNode::family() == AF_INET)
    {
	install_element(CONFIG_NODE, &router_mfea_cmd);
	install_element(CONFIG_NODE, &no_router_mfea_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (MfeaNode::family() == AF_INET6)
    {
	install_element(CONFIG_NODE, &router_mfea6_cmd);
	install_element(CONFIG_NODE, &no_router_mfea6_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
	XLOG_UNREACHABLE();

    // interface commands
    if (MfeaNode::family() == AF_INET)
    {
	install_element(INTERFACE_NODE, &ip_mfea_cmd);
	install_element(INTERFACE_NODE, &no_ip_mfea_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (MfeaNode::family() == AF_INET6)
    {
	install_element(INTERFACE_NODE, &ipv6_mfea_cmd);
	install_element(INTERFACE_NODE, &no_ipv6_mfea_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
	XLOG_UNREACHABLE();

    // debug commands
    if (MfeaNode::family() == AF_INET)
    {
	install_element(CONFIG_NODE, &debug_mfea_cmd);
	install_element(CONFIG_NODE, &no_debug_mfea_cmd);
	install_element(CONFIG_NODE, &debug_mfea_trace_cmd);
	install_element(CONFIG_NODE, &no_debug_mfea_trace_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (MfeaNode::family() == AF_INET6)
    {
	install_element(CONFIG_NODE, &debug_mfea6_cmd);
	install_element(CONFIG_NODE, &no_debug_mfea6_cmd);
	install_element(CONFIG_NODE, &debug_mfea6_trace_cmd);
	install_element(CONFIG_NODE, &no_debug_mfea6_trace_cmd);
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
    if (MfeaNode::family() == AF_INET)
    {
	ADD_SHOW_CMD(show_ip_mfea_dataflow_cmd);
	ADD_SHOW_CMD(show_ip_mfea_interface_cmd);
	ADD_SHOW_CMD(show_ip_mfea_interface_address_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (MfeaNode::family() == AF_INET6)
    {
	ADD_SHOW_CMD(show_ipv6_mfea6_dataflow_cmd);
	ADD_SHOW_CMD(show_ipv6_mfea6_interface_cmd);
	ADD_SHOW_CMD(show_ipv6_mfea6_interface_address_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
    {
	XLOG_UNREACHABLE();
    }

#undef ADD_SHOW_CMD
}
