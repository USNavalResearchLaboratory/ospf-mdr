/* -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*- */

// Copyright 2014 The Boeing Company

#include "zebra_pim_module.h"

#include "libxorp/xorp.h"
#include "pim/pim_vif.hh"

#include "zebra_pim_node.hh"
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

#define ZPIM_STR "Protocol Independent Multicast (PIM)\n"
#ifdef HAVE_IPV6_MULTICAST
#define ZPIM6_STR ZPIM_STR
#endif	// HAVE_IPV6_MULTICAST
#define TRACE_STR "Detailed tracing\n"
#define PIM_NEIGHBOR_STR "Neighbor events\n"

static ZebraPimNode *_zpim = NULL;

// zpim node
static struct cmd_node zpim_node = {
    PIM_NODE,
    "%s(config-pim)# ",
    1				// vtysh
};


// zpim configuration write
static int
zpim_config_write_zpim(ZebraPimNode *zpim, struct vty *vty)
{
    if (zpim && zpim->PimNode::is_enabled())
    {
        vty_out(vty, "router %s%s", zpim->zebra_protostr(), VNL);
	zpim->zebra_config_write(vty);
	vty_out(vty, "!%s", VNL);
    }

    return CMD_SUCCESS;
}

static int
config_write_zpim(struct vty *vty)
{
    return zpim_config_write_zpim(_zpim, vty);
}

void
ZebraPimNode::zebra_config_write(struct vty *vty)
{
    if (default_ip_tos().get() != default_ip_tos().get_initial_value())
    {
	const char *cmd;

	switch (PimNode::family())
	{
	case AF_INET:
	    cmd = "protocol-type-of-service";
	    break;

	case AF_INET6:
	    cmd = "protocol-traffic-class";
	    break;

	default:
	    XLOG_UNREACHABLE();
	    break;
	}

	vty_out (vty, " %s %s %s %u%s",
		 zebra_ipstr(), zebra_protostr(),
		 cmd, default_ip_tos().get(), VNL);
    }

    for (list<PimRp *>::const_iterator it = rp_table().rp_list().begin();
	 it != rp_table().rp_list().end(); ++it)
    {
	const PimRp *pim_rp = *it;

	if (pim_rp->rp_learned_method() != PimRp::RP_LEARNED_METHOD_STATIC)
	    continue;

	vty_out(vty, " %s %s rp-address %s %s "
		"priority %d hash-mask-length %d%s",
		zebra_ipstr(), zebra_protostr(),
		pim_rp->rp_addr().str().c_str(),
		pim_rp->group_prefix().str().c_str(),
		pim_rp->rp_priority(),
		pim_rp->hash_mask_len(), VNL);
    }

    for (list<BsrZone *>::const_iterator it =
	     pim_bsr().config_bsr_zone_list().begin();
	 it != pim_bsr().config_bsr_zone_list().end(); ++it)
    {
	const BsrZone *bsr_zone = *it;
	const PimVif *vif = vif_find_by_vif_index(bsr_zone->my_vif_index());

	if (vif == NULL)
	    continue;

	vty_out(vty, " %s %s bsr-candidate %s %s %s "
		"priority %d hash-mask-length %d%s",
		zebra_ipstr(), zebra_protostr(),
		vif->name().c_str(),
		bsr_zone->zone_id().scope_zone_prefix().str().c_str(),
		bsr_zone->zone_id().is_scope_zone() ? "scoped" : "non-scoped",
		bsr_zone->my_bsr_priority(),
		bsr_zone->hash_mask_len(), VNL);
    }

    for (list<BsrZone *>::const_iterator it =
	     pim_bsr().config_bsr_zone_list().begin();
	 it != pim_bsr().config_bsr_zone_list().end(); ++it)
    {
	const BsrZone *bsr_zone = *it;
	for (list<BsrGroupPrefix *>::const_iterator it =
		 bsr_zone->bsr_group_prefix_list().begin();
	     it != bsr_zone->bsr_group_prefix_list().end(); ++it)
	{
	    const BsrGroupPrefix *bsr_group_prefix = *it;
	    for (list<BsrRp *>::const_iterator it =
		     bsr_group_prefix->rp_list().begin();
		 it != bsr_group_prefix->rp_list().end(); ++it)
	    {
		BsrRp *bsr_rp = *it;
		XLOG_ASSERT(is_my_addr(bsr_rp->rp_addr())); // XXX

		vty_out(vty, " %s %s rp-candidate %s %s %s "
			"priority %d holdtime %d%s",
			zebra_ipstr(), zebra_protostr(),
			vif_find_by_vif_index(bsr_rp->my_vif_index())->name().c_str(),
			bsr_rp->bsr_group_prefix().group_prefix().str().c_str(),
			bsr_rp->bsr_group_prefix().is_scope_zone() ? "scoped" : "non-scoped",
			bsr_rp->rp_priority(),
			bsr_rp->rp_holdtime(), VNL);
	    }
	}
    }

    if (is_switch_to_spt_enabled().get())
	vty_out(vty, " %s %s spt-threshold interval %d bytes %d%s",
		zebra_ipstr(), zebra_protostr(),
		switch_to_spt_threshold_interval_sec().get(),
		switch_to_spt_threshold_bytes().get(),  VNL);

    if (_register_source_vif_name.is_set())
    {
	vty_out (vty, " %s %s register-source %s%s",
		 zebra_ipstr(), zebra_protostr(),
		 _register_source_vif_name.get().c_str(), VNL);
    }
}

static int
zpim_router_pim(ZebraPimNode *zpim, struct vty *vty,
		int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    string error_msg;
    if (zpim->start(error_msg) != XORP_OK)
    {
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    if (zpim->PimNode::proto_is_pimsm())
	vty->node = PIM_NODE;
    else
	XLOG_UNREACHABLE();

    vty->index = zpim;

    return CMD_SUCCESS;
}

static int
zpim_no_router_pim(ZebraPimNode *zpim, struct vty *vty,
		   int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    int r = CMD_SUCCESS;
    string error_msg;
    if (zpim->stop(error_msg) != XORP_OK)
    {
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);
	r = CMD_WARNING;
    }

    // return to config node
    vty->node = CONFIG_NODE;
    vty->index = NULL;

    return r;
}

DEFUN(router_pim,
      router_pim_cmd,
      "router pim",
      ROUTER_STR
      ZPIM_STR)
{
    return zpim_router_pim(_zpim, vty, argc, argv);
}

DEFUN(no_router_pim,
      no_router_pim_cmd,
      "no router pim",
      NO_STR
      ROUTER_STR
      ZPIM_STR)
{
    return zpim_no_router_pim(_zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(router_pim,
      router_pim6_cmd,
      "router pim6",
      ROUTER_STR
      ZPIM6_STR);

ALIAS(no_router_pim,
      no_router_pim6_cmd,
      "no router pim6",
      NO_STR
      ROUTER_STR
      ZPIM6_STR);

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_rp_address(ZebraPimNode *zpim, struct vty *vty,
		       int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    IPvX rp_addr(argv[0]);
    IPvXNet group_prefix =
	IPvXNet::ip_multicast_base_prefix(zpim->PimNode::family());
    uint8_t rp_priority = PIM_CAND_RP_ADV_RP_PRIORITY_DEFAULT; // XXX is this the right default value?
    uint8_t hash_mask_len =
	PIM_BOOTSTRAP_HASH_MASK_LEN_DEFAULT(zpim->PimNode::family());

    switch (argc)
    {
    case 6:
	hash_mask_len = strtol(argv[5], NULL, 10);
	// fall through

    case 4:
	rp_priority = strtol(argv[3], NULL, 10);
	// fall through

    case 2:
	try {
	    group_prefix = IPvXNet(argv[1]);
	} catch (...) {
	    vty_out(vty, "invalid group address/prefix length: %s%s",
		    argv[2], VNL);
	    return CMD_WARNING;
	}
	break;

    case 1:
	break;

    case 5:
    case 3:
	return CMD_ERR_INCOMPLETE;

    default:
	return CMD_ERR_NO_MATCH;
    }

    string error_msg;
    if (zpim->add_config_static_rp(group_prefix, rp_addr, rp_priority,
				   hash_mask_len, error_msg) != XORP_OK)
    {
 	vty_out(vty, "couldn't add rendezvous point: %s%s",
 		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    // config_static_rp_done() will fail if there aren't any vifs yet
    if (zpim->vif_find_pim_register() == NULL)
    {
	zpim->set_pending_rp_update();
	return CMD_WARNING;
    }

    if (zpim->config_static_rp_done(error_msg) != XORP_OK)
    {
 	vty_out(vty, "couldn't add rendezvous point: %s%s",
 		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_rp_address(ZebraPimNode *zpim, struct vty *vty,
			  int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    IPvX rp_addr;
    IPvXNet group_prefix(zpim->PimNode::family());

    int ret;
    string error_msg;
    switch (argc)
    {
    case 0:
	ret = zpim->delete_config_all_static_rps(error_msg);
	break;

    case 1:
	try {
	    rp_addr = IPvX(argv[0]);
	} catch (...) {
	    vty_out(vty, "invalid RP address: %s%s", argv[0], VNL);
	    return CMD_WARNING;
	}
	ret = zpim->delete_config_all_static_group_prefixes_rp(rp_addr,
							       error_msg);
	break;

    case 2:
	try {
	    rp_addr = IPvX(argv[0]);
	} catch (...) {
	    vty_out(vty, "invalid RP address: %s%s", argv[0], VNL);
	    return CMD_WARNING;
	}
	try {
	    group_prefix = IPvXNet(argv[1]);
	} catch (...) {
	    vty_out(vty, "invalid group address/prefix length: %s%s",
		    argv[1], VNL);
	    return CMD_WARNING;
	}
	ret = zpim->delete_config_static_rp(group_prefix, rp_addr,
					    error_msg);
	break;

    default:
	return CMD_ERR_NO_MATCH;
    }

    if (ret != XORP_OK)
    {
 	vty_out(vty, "couldn't delete rendezvous point: %s%s",
 		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    if (zpim->config_static_rp_done(error_msg) != XORP_OK)
    {
 	vty_out(vty, "couldn't delete rendezvous point: %s%s",
 		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}


DEFUN(ip_pim_rp_address,
      ip_pim_rp_address_cmd,
      "ip pim rp-address A.B.C.D [A.B.C.D/M] (priority|) <0-255> (hash-mask-length|) <4-32>",
      IP_STR
      ZPIM_STR
      "Static rendezvous point (RP) address\n"
      "RP Address\n"
      "Optional multicast group address range for this RP (all groups if omitted)\n"
      "Optional RP priority (smaller is higher priority)\n"
      "UNUSED\n"
      "RP priority\n"
      "Optional hash mask length for load balancing\n"
      "UNUSED\n"
      "Hash mask length\n")
{
    ZebraPimNode *zpim = (ZebraPimNode *)vty->index;
    XLOG_ASSERT(zpim == _zpim);

    return zpim_ip_pim_rp_address(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_rp_address,
      no_ip_pim_rp_address_cmd,
      "no ip pim rp-address [A.B.C.D] [A.B.C.D/M]",
      NO_STR
      IP_STR
      ZPIM_STR
      "Static rendezvous point (RP) address\n"
      "Optional RP Address (all RPs if omitted)\n"
      "Optional multicast group address range for this RP (all groups if omitted)\n")
{
    ZebraPimNode *zpim = (ZebraPimNode *)vty->index;
    XLOG_ASSERT(zpim == _zpim);

    return zpim_no_ip_pim_rp_address(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_rp_address,
      ipv6_pim6_rp_address_cmd,
      "ipv6 pim6 rp-address X:X::X:X [X:X::X:X/M] (priority|) <0-255> (hash-mask-length|) <8-128>",
      IP6_STR
      ZPIM6_STR
      "Static rendezvous point (RP) address\n"
      "RP Address\n"
      "Optional multicast group address range for this RP (all groups if omitted)\n"
      "Optional RP priority (smaller is higher priority)\n"
      "UNUSED\n"
      "RP priority\n"
      "Optional hash mask length for load balancing\n"
      "UNUSED\n"
      "Hash mask length\n");

ALIAS(no_ip_pim_rp_address,
      no_ipv6_pim6_rp_address_cmd,
      "no ipv6 pim6 rp-address [X:X::X:X] [X:X::X:X/M]",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Static rendezvous point (RP) address\n"
      "Optional RP Address (all RPs if omitted)\n"
      "Optional multicast group address range for this RP (all groups if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_bsr_candidate(ZebraPimNode *zpim, struct vty *vty,
			  int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    IPvXNet scope_zone_id =
	IPvXNet::ip_multicast_base_prefix(zpim->PimNode::family());
    bool is_scope_zone = false;
    IPvX vif_addr; // XXX specifying an interface address is not implemented
    uint8_t bsr_priority = PIM_BOOTSTRAP_PRIORITY_DEFAULT;
    uint8_t hash_mask_len =
	PIM_BOOTSTRAP_HASH_MASK_LEN_DEFAULT(zpim->PimNode::family());

    switch (argc)
    {
    case 7:
	hash_mask_len = strtol(argv[6], NULL, 10);
	// fall through

    case 5:
	bsr_priority = strtol(argv[4], NULL, 10);
	// fall through

    case 3:
	if (strncmp(argv[2], "scoped", strlen(argv[2])) == 0)
	    is_scope_zone = true;
	else
	    is_scope_zone = false;
	// fall through

    case 2:
	try {
	    scope_zone_id = IPvXNet(argv[1]);
	} catch (...) {
	    vty_out(vty, "invalid scope zone group prefix/group range: %s%s",
		    argv[1], VNL);
	    return CMD_WARNING;
	}
	// fall through

    case 1:
	break;

    case 6:
    case 4:
	return CMD_ERR_INCOMPLETE;

    default:
	return CMD_ERR_NO_MATCH;
    }

    string error_msg;
    if (zpim->add_cand_bsr_config(scope_zone_id, is_scope_zone,
				   argv[0], vif_addr,
				   bsr_priority,  hash_mask_len,
				   error_msg) != XORP_OK)
    {
	vty_out(vty, "couldn't add candidate BSR: %s%s",
		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_bsr_candidate(ZebraPimNode *zpim, struct vty *vty,
			     int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    IPvXNet scope_zone_id =
	IPvXNet::ip_multicast_base_prefix(zpim->PimNode::family());
    bool is_scope_zone = false;

    switch (argc)
    {
    case 2:
	if (strncmp(argv[1], "scoped", strlen(argv[1])) == 0)
	    is_scope_zone = true;
	else
	    is_scope_zone = false;
	// fall through

    case 1:
	try {
	    scope_zone_id = IPvXNet(argv[0]);
	} catch (...) {
	    vty_out(vty, "invalid scope zone group prefix/group range: %s%s",
		    argv[0], VNL);
	    return CMD_WARNING;
	}
	// fall through

    case 0:
	break;

    default:
	return CMD_ERR_NO_MATCH;
    }

    string error_msg;
    if (zpim->delete_cand_bsr_config(scope_zone_id, is_scope_zone,
				      error_msg) != XORP_OK)
    {
	vty_out(vty, "couldn't delete candidate BSR: %s%s",
		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_bsr_candidate,
      ip_pim_bsr_candidate_cmd,
      "ip pim bsr-candidate IFNAME [A.B.C.D/M] (scoped|non-scoped) (priority|) <0-255> (hash-mask-length|) <4-32>",
      IP_STR
      ZPIM_STR
      "Bootstrap Router (BSR) candidate\n"
      "Interface whose address is used in bootstrap messages\n"
      "Optional Multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n"
      "Optional BSR priority (larger is higher priority)\n"
      "UNUSED\n"
      "BSR priority\n"
      "Optional hash mask length for load balancing\n"
      "UNUSED\n"
      "Hash mask length\n")
{
    ZebraPimNode *zpim = (ZebraPimNode *)vty->index;
    XLOG_ASSERT(zpim == _zpim);

    return zpim_ip_pim_bsr_candidate(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_bsr_candidate,
      no_ip_pim_bsr_candidate_cmd,
      "no ip pim bsr-candidate [A.B.C.D/M] (scoped|non-scoped)",
      NO_STR
      IP_STR
      ZPIM_STR
      "Bootstrap Router (BSR) candidate\n"
      "Optional Multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n")
{
    ZebraPimNode *zpim = (ZebraPimNode *)vty->index;
    XLOG_ASSERT(zpim == _zpim);

    return zpim_no_ip_pim_bsr_candidate(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_bsr_candidate,
      ipv6_pim6_bsr_candidate_cmd,
      "ipv6 pim6 bsr-candidate IFNAME [X:X::X:X/M] (scoped|non-scoped) (priority|) <0-255> (hash-mask-length|) <8-128>",
      IP6_STR
      ZPIM6_STR
      "Bootstrap Router (BSR) candidate\n"
      "Interface whose address is used in bootstrap messages\n"
      "Optional Multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n"
      "Optional BSR priority (larger is higher priority)\n"
      "UNUSED\n"
      "BSR priority\n"
      "Optional hash mask length for load balancing\n"
      "UNUSED\n"
      "Hash mask length\n");

ALIAS(no_ip_pim_bsr_candidate,
      no_ipv6_pim6_bsr_candidate_cmd,
      "no ipv6 pim6 bsr-candidate [X:X::X:X/M] (scoped|non-scoped)",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Bootstrap Router (BSR) candidate\n"
      "Optional Multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_rp_candidate(ZebraPimNode *zpim, struct vty *vty,
			 int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    IPvXNet group_prefix =
	IPvXNet::ip_multicast_base_prefix(zpim->PimNode::family());
    bool is_scope_zone = false;
    IPvX vif_addr; // XXX specifying an interface address is not implemented
    uint8_t rp_priority = PIM_CAND_RP_ADV_RP_PRIORITY_DEFAULT;
    uint16_t rp_holdtime = PIM_CAND_RP_ADV_RP_HOLDTIME_DEFAULT;

    switch (argc)
    {
    case 7:
	rp_holdtime = strtol(argv[6], NULL, 10);
	// fall through

    case 5:
	rp_priority = strtol(argv[4], NULL, 10);
	// fall through

    case 3:
	if (strncmp(argv[2], "scoped", strlen(argv[2])) == 0)
	    is_scope_zone = true;
	else
	    is_scope_zone = false;
	// fall through

    case 2:
	try {
	    group_prefix = IPvXNet(argv[1]);
	} catch (...) {
	    vty_out(vty, "invalid scope zone group prefix/group range: %s%s",
		    argv[1], VNL);
	    return CMD_WARNING;
	}
	// fall through

    case 1:
	break;

    case 6:
    case 4:
	return CMD_ERR_INCOMPLETE;

    default:
	return CMD_ERR_NO_MATCH;
    }

    string error_msg;
    if (zpim->add_cand_rp_config(group_prefix, is_scope_zone,
				  argv[0], vif_addr,
				  rp_priority,  rp_holdtime,
				  error_msg) != XORP_OK)
    {
	vty_out(vty, "couldn't add candidate RP: %s%s",
		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_rp_candidate(ZebraPimNode *zpim, struct vty *vty,
			    int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    IPvXNet group_prefix =
	IPvXNet::ip_multicast_base_prefix(zpim->PimNode::family());
    bool is_scope_zone = false;
    IPvX vif_addr; // XXX specifying an interface address is not implemented

    switch (argc)
    {
    case 3:
	if (strncmp(argv[2], "scoped", strlen(argv[2])) == 0)
	    is_scope_zone = true;
	else
	    is_scope_zone = false;
	// fall through

    case 2:
	try {
	    group_prefix = IPvXNet(argv[1]);
	} catch (...) {
	    vty_out(vty, "invalid scope zone group prefix/group range: %s%s",
		    argv[1], VNL);
	    return CMD_WARNING;
	}
	// fall through

    case 1:
	break;

    default:
	return CMD_ERR_NO_MATCH;
    }

    string error_msg;
    if (zpim->delete_cand_rp_config(group_prefix, is_scope_zone,
				     argv[0], vif_addr,
				     error_msg) != XORP_OK)
    {
	vty_out(vty, "couldn't delete candidate RP: %s%s",
		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_rp_candidate,
      ip_pim_rp_candidate_cmd,
      "ip pim rp-candidate IFNAME [A.B.C.D/M] (scoped|non-scoped) (priority|) <0-255> (holdtime|) <0-65535>",
      IP_STR
      ZPIM_STR
      "Rendezvous Point (RP) candidate\n"
      "Interface whose address is used as a candidate RP address\n"
      "Optional Multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n"
      "Optional RP priority (smaller is higher priority)\n"
      "UNUSED\n"
      "RP priority\n"
      "Optional RP holdtime (seconds) advertised to the BSR\n"
      "UNUSED\n"
      "Seconds\n")
{
    ZebraPimNode *zpim = (ZebraPimNode *)vty->index;
    XLOG_ASSERT(zpim == _zpim);

    return zpim_ip_pim_rp_candidate(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_rp_candidate,
      no_ip_pim_rp_candidate_cmd,
      "no ip pim rp-candidate IFNAME [A.B.C.D/M] (scoped|non-scoped)",
      NO_STR
      IP_STR
      ZPIM_STR
      "Rendezvous Point (RP) candidate\n"
      "Interface whose address is used as a candidate RP address\n"
      "Optional Multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n")
{
    ZebraPimNode *zpim = (ZebraPimNode *)vty->index;
    XLOG_ASSERT(zpim == _zpim);

    return zpim_no_ip_pim_rp_candidate(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_rp_candidate,
      ipv6_pim6_rp_candidate_cmd,
      "ipv6 pim6 rp-candidate IFNAME [X:X::X:X/M] (scoped|non-scoped) (priority|) <0-255> (holdtime|) <0-65535>",
      IP6_STR
      ZPIM6_STR
      "Rendezvous Point (RP) candidate\n"
      "Interface whose address is used as a candidate RP address\n"
      "Optional Multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n"
      "Optional RP priority (smaller is higher priority)\n"
      "UNUSED\n"
      "RP priority\n"
      "Optional RP holdtime (seconds) advertised to the BSR\n"
      "UNUSED\n"
      "Seconds\n");

DEFUN(no_ip_pim_rp_candidate,
      no_ipv6_pim6_rp_candidate_cmd,
      "no ipv6 pim6 rp-candidate IFNAME [A.B.C.D/M] (scoped|non-scoped)",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Rendezvous Point (RP) candidate\n"
      "Interface whose address is used as a candidate RP address\n"
      "Optional Multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_spt_threshold(ZebraPimNode *zpim, struct vty *vty,
			  int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    uint32_t interval_sec = strtol(argv[0], NULL, 10);
    uint32_t bytes = strtol(argv[1], NULL, 10);

    string error_msg;
    if (zpim->set_switch_to_spt_threshold(true, interval_sec, bytes,
					  error_msg) != XORP_OK)
	vty_out(vty, "couldn't set spt threshold: %s%s",
		error_msg.c_str(), VNL);

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_spt_threshold(ZebraPimNode *zpim, struct vty *vty,
			     int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    string error_msg;
    if (zpim->set_switch_to_spt_threshold(false, 0, 0, error_msg) != XORP_OK)
	vty_out(vty, "couldn't set spt threshold: %s%s",
		error_msg.c_str(), VNL);

    return CMD_SUCCESS;
}

DEFUN(ip_pim_spt_threshold,
      ip_pim_spt_threshold_cmd,
      "ip pim spt-threshold interval <3-2147483647> bytes <0-4294967295>",
      IP_STR
      ZPIM_STR
      "Switch to shortest-path tree threshold\n"
      "Time interval (seconds) used to measure traffic bitrate\n"
      "Seconds\n"
      "Received Number of bytes during measurement interval needed to trigger spt switch\n"
      "Bytes\n")
{
    ZebraPimNode *zpim = (ZebraPimNode *)vty->index;
    XLOG_ASSERT(zpim == _zpim);

    return zpim_ip_pim_spt_threshold(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_spt_threshold,
      no_ip_pim_spt_threshold_cmd,
      "no ip pim spt-threshold",
      NO_STR
      IP_STR
      ZPIM_STR
      "Switch to shortest-path tree threshold\n")
{
    ZebraPimNode *zpim = (ZebraPimNode *)vty->index;
    XLOG_ASSERT(zpim == _zpim);

    return zpim_no_ip_pim_spt_threshold(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_spt_threshold,
      ipv6_pim6_spt_threshold_cmd,
      "ipv6 pim6 spt-threshold interval <3-2147483647> bytes <0-4294967295>",
      IP6_STR
      ZPIM6_STR
      "Switch to shortest-path tree threshold\n"
      "Time interval (seconds) used to measure traffic bitrate\n"
      "Seconds\n"
      "Received Number of bytes during measurement interval needed to trigger spt switch\n"
      "Bytes\n");

ALIAS(no_ip_pim_spt_threshold,
      no_ipv6_pim6_spt_threshold_cmd,
      "no ipv6 pim6 spt-threshold",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Switch to shortest-path tree threshold\n");

#endif	// HAVE_IPV6_MULTICAST

// zpim interface configuration write
int
ZebraPimNode::zebra_config_write_interface(struct vty *vty)
{
    for (vector<PimVif *>::const_iterator it = const_proto_vifs().begin();
	 it != const_proto_vifs().end(); ++it)
    {
	const PimVif *vif = *it;
	if (vif == NULL)
	    continue;

	if (!vif->is_enabled())
	    continue;

	if (vif->is_pim_register())
	    continue;

	vty_out(vty, "interface %s%s", vif->name().c_str(), VNL);
	zebra_config_write_interface(vty, vif);
	vty_out(vty, "!%s", VNL);
    }

    return CMD_SUCCESS;
}

void
ZebraPimNode::zebra_config_write_interface(struct vty *vty,
					   const PimVif *vif)
{
    if (vif->is_enabled())
	vty_out(vty, " %s %s%s", zebra_ipstr(), zebra_protostr(), VNL);

    string error_msg;
    int tmpint;
    bool tmpbool;
    uint16_t tmpuint16;
    uint32_t tmpuint32;

#define WRITE_CONFIG(getfunc, getvar, writestmnt)		\
    do {							\
	if (getfunc(vif->name(), getvar,			\
		    error_msg) == XORP_OK)			\
	{							\
	    writestmnt;						\
	}							\
	else							\
	    XLOG_WARNING("%s: " #getfunc "() failed: %s",	\
			 __func__, error_msg.c_str());		\
    } while(0)

    WRITE_CONFIG(get_vif_proto_version, tmpint,
		 vty_out(vty, " %s %s version %d%s",
			 zebra_ipstr(), zebra_protostr(), tmpint, VNL));

    WRITE_CONFIG(get_vif_passive, tmpbool,
		 if (tmpbool)
		     vty_out(vty, " %s %s passive%s",
			     zebra_ipstr(), zebra_protostr(), VNL));

    WRITE_CONFIG(get_vif_ip_router_alert_option_check, tmpbool,
		 if (tmpbool)
		     vty_out(vty, " %s %s ip-router-alert-option-check%s",
			     zebra_ipstr(), zebra_protostr(), VNL));

    WRITE_CONFIG(get_vif_hello_triggered_delay, tmpuint16,
		 vty_out(vty, " %s %s hello-triggered-delay %d%s",
			 zebra_ipstr(), zebra_protostr(), tmpuint16, VNL));

    WRITE_CONFIG(get_vif_hello_period, tmpuint16,
		 vty_out(vty, " %s %s hello-interval %d%s",
			 zebra_ipstr(), zebra_protostr(), tmpuint16, VNL));

    WRITE_CONFIG(get_vif_hello_holdtime, tmpuint16,
		 vty_out(vty, " %s %s hello-holdtime %d%s",
			 zebra_ipstr(), zebra_protostr(), tmpuint16, VNL));

    WRITE_CONFIG(get_vif_dr_priority, tmpuint32,
		 vty_out(vty, " %s %s dr-priority %d%s",
			 zebra_ipstr(), zebra_protostr(), tmpuint32, VNL));

    WRITE_CONFIG(get_vif_propagation_delay, tmpuint16,
		 vty_out(vty, " %s %s propagation-delay %d%s",
			 zebra_ipstr(), zebra_protostr(), tmpuint16, VNL));

    WRITE_CONFIG(get_vif_override_interval, tmpuint16,
		 vty_out(vty, " %s %s override-interval %d%s",
			 zebra_ipstr(), zebra_protostr(), tmpuint16, VNL));

    WRITE_CONFIG(get_vif_is_tracking_support_disabled, tmpbool,
		 if (tmpbool)
		     vty_out(vty, " %s %s is-tracking-support-disabled%s",
			     zebra_ipstr(), zebra_protostr(), VNL));

    WRITE_CONFIG(get_vif_accept_nohello_neighbors, tmpbool,
		 if (tmpbool)
		     vty_out(vty, " %s %s accept-nohello-neighbors%s",
			     zebra_ipstr(), zebra_protostr(), VNL));

    WRITE_CONFIG(get_vif_join_prune_period, tmpuint16,
		 vty_out(vty, " %s %s join-prune-interval %d%s",
			 zebra_ipstr(), zebra_protostr(), tmpuint16, VNL));

    for (list<IPvXNet>::const_iterator it =
	     vif->alternative_subnet_list().begin();
	 it != vif->alternative_subnet_list().end(); ++it)
    {
	const IPvXNet& ipvxnet = *it;
	vty_out(vty, " %s %s alternative-subnet %s%s",
		zebra_ipstr(), zebra_protostr(),
		ipvxnet.str().c_str(), VNL);
    }

    ZebraPimNode *zpim = dynamic_cast<ZebraPimNode *>(&vif->pim_node());
    if (zpim != NULL)
    {
	// XXX static_memberships should be read directly from vif
	// attributes instead of config settings
	set<ZebraConfigVal<ZebraStaticMembership> > &static_memberships =
	    zpim->get_if_config(vif->name()).static_memberships;

	for (set<ZebraConfigVal<ZebraStaticMembership> >::const_iterator it =
		 static_memberships.begin();
	     it != static_memberships.end(); ++it)
	{
	    const ZebraStaticMembership &staticmbr = it->get();
	    if (staticmbr.source().is_zero())
		vty_out(vty, " %s %s static-membership %s%s",
			zebra_ipstr(), zebra_protostr(),
			staticmbr.group().str().c_str(), VNL);
	    else
		vty_out(vty, " %s %s static-membership %s %s%s",
			zebra_ipstr(), zebra_protostr(),
			staticmbr.source().str().c_str(),
			staticmbr.group().str().c_str(), VNL);
	}
    }

#undef WRITE_CONFIG
}

static int
zpim_ip_pim(ZebraPimNode *zpim, struct vty *vty,
	    int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    zpim->get_if_config(ifp->name).enabled.set(true);

    // try to enable the interface now if it exists
    PimVif *vif = zpim->vif_find_by_name(ifp->name);
    if (vif != NULL)
    {
	string error_msg;
	if (zpim->enable_vif(ifp->name, error_msg) != XORP_OK)
	    vty_out(vty, "couldn't enable interface %s: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
	else
	    // try to start the interface
	    zpim->try_start_vif(ifp->name);
    }

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim(ZebraPimNode *zpim, struct vty *vty,
	       int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    zpim->get_if_config(ifp->name).enabled.set(false);

    // check if the interface exists
    PimVif *vif = zpim->vif_find_by_name(ifp->name);
    if (vif == NULL)
    {
	vty_out(vty, "couldn't find interface %s%s", ifp->name, VNL);
	return CMD_WARNING;
    }

    // check if the interface is already not enabled
    if (!vif->is_enabled())
	return CMD_SUCCESS;

    string error_msg;
    if (zpim->stop_vif(ifp->name, error_msg) != XORP_OK)
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);

    if (zpim->disable_vif(ifp->name, error_msg) != XORP_OK)
    {
	vty_out(vty, "%s%s", error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim,
      ip_pim_cmd,
      "ip pim",
      IP_STR
      ZPIM_STR)
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim,
      no_ip_pim_cmd,
      "no ip pim",
      NO_STR
      IP_STR
      ZPIM_STR)
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_ip_pim(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim,
      ipv6_pim6_cmd,
      "ipv6 pim6",
      IP6_STR
      ZPIM6_STR);

ALIAS(no_ip_pim,
      no_ipv6_pim6_cmd,
      "no ipv6 pim6",
      NO_STR
      IP6_STR
      ZPIM6_STR);

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_version(ZebraPimNode *zpim, struct vty *vty,
		    int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    int version = strtol(argv[0], NULL, 10);
    zpim->get_if_config(ifp->name).proto_version.set(version);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_proto_version(ifp->name, version,
					 error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set protocol version for interface %s to %d: %s%s",
		    ifp->name, version, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_version,
      ip_pim_version_cmd,
      "ip pim version <" XSTRINGIFY(PIMSM_VERSION_MIN) "-" XSTRINGIFY(PIMSM_VERSION_MAX) ">",
      IP_STR
      ZPIM_STR
      "PIM version\n"
      "Protocol version\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_version(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_version,
      ipv6_pim6_version_cmd,
      "ipv6 pim6 version <" XSTRINGIFY(PIMSM_VERSION_MIN) "-" XSTRINGIFY(PIMSM_VERSION_MAX) ">",
      IP6_STR
      ZPIM6_STR
      "PIM version\n"
      "Protocol version\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_passive(ZebraPimNode *zpim, struct vty *vty,
		    int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    zpim->get_if_config(ifp->name).passive.set(true);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_passive(ifp->name, true, error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set passive for interface %s to true: %s%s",
		    ifp->name, error_msg.c_str(), VNL);

	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_passive(ZebraPimNode *zpim, struct vty *vty,
		       int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    zpim->get_if_config(ifp->name).passive.set(false);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_passive(ifp->name, false, error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set passive for interface %s to false: %s%s",
		    ifp->name, error_msg.c_str(), VNL);

	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_passive,
      ip_pim_passive_cmd,
      "ip pim passive",
      IP_STR
      ZPIM_STR
      "Passive interface\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_passive(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_passive,
      no_ip_pim_passive_cmd,
      "no ip pim passive",
      NO_STR
      IP_STR
      ZPIM_STR
      "Passive interface\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_ip_pim_passive(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_passive,
      ipv6_pim6_passive_cmd,
      "ipv6 pim6 passive",
      IP6_STR
      ZPIM6_STR
      "Passive interface\n");

ALIAS(no_ip_pim_passive,
      no_ipv6_pim6_passive_cmd,
      "no ipv6 pim6 passive",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Passive interface\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_ip_router_alert_option_check(ZebraPimNode *zpim, struct vty *vty,
					 int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    zpim->get_if_config(ifp->name).ip_router_alert_option_check.set(true);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_ip_router_alert_option_check(ifp->name, true,
							error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set ip router alert option check for interface %s to true: %s%s",
		    ifp->name, error_msg.c_str(), VNL);

	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_ip_router_alert_option_check(ZebraPimNode *zpim, struct vty *vty,
					    int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    zpim->get_if_config(ifp->name).ip_router_alert_option_check.set(false);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_ip_router_alert_option_check(ifp->name, false,
							error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set ip router alert option check for interface %s to false: %s%s",
		    ifp->name, error_msg.c_str(), VNL);

	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_ip_router_alert_option_check,
      ip_pim_ip_router_alert_option_check_cmd,
      "ip pim ip-router-alert-option-check",
      IP_STR
      ZPIM_STR
      "IP Router Alert option (see RFC 2113)\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_ip_router_alert_option_check(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_ip_router_alert_option_check,
      no_ip_pim_ip_router_alert_option_check_cmd,
      "no ip pim ip-router-alert-option-check",
      NO_STR
      IP_STR
      ZPIM_STR
      "IP Router Alert option (see RFC 2113)\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_ip_pim_ip_router_alert_option_check(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_ip_router_alert_option_check,
      ipv6_pim6_ip_router_alert_option_check_cmd,
      "ipv6 pim6 ip-router-alert-option-check",
      IP6_STR
      ZPIM6_STR
      "IP Router Alert option (see RFC 2113)\n");

ALIAS(no_ip_pim_ip_router_alert_option_check,
      no_ipv6_pim6_ip_router_alert_option_check_cmd,
      "no ipv6 pim6 ip-router-alert-option-check",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "IP Router Alert option (see RFC 2113)\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_hello_triggered_delay(ZebraPimNode *zpim, struct vty *vty,
				  int argc, const char *argv[])
{
    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    uint16_t delay = strtol(argv[0], NULL, 10);
    zpim->get_if_config(ifp->name).hello_triggered_delay.set(delay);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_hello_triggered_delay(ifp->name, delay,
						 error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set hello triggered delay for interface %s to %d: %s%s",
		    ifp->name, delay, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_hello_triggered_delay,
      ip_pim_hello_triggered_delay_cmd,
      "ip pim hello-triggered-delay <1-255>",
      IP_STR
      ZPIM_STR
      "PIM Hello message randomized triggered delay\n"
      "Seconds\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_hello_triggered_delay(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_hello_triggered_delay,
      ipv6_pim6_hello_triggered_delay_cmd,
      "ipv6 pim6 hello-triggered-delay <1-255>",
      IP6_STR
      ZPIM6_STR
      "PIM Hello message randomized triggered delay\n"
      "Seconds\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_hello_period(ZebraPimNode *zpim, struct vty *vty,
			 int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    uint16_t hello_period = strtol(argv[0], NULL, 10);
    zpim->get_if_config(ifp->name).hello_period.set(hello_period);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_hello_period(ifp->name, hello_period,
					error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set hello period for interface %s to %d: %s%s",
		    ifp->name, hello_period, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_hello_period,
      ip_pim_hello_period_cmd,
      "ip pim hello-interval <1-18724>",
      IP_STR
      ZPIM_STR
      "PIM Hello message interval\n"
      "Seconds\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_hello_period(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_hello_period,
      ipv6_pim6_hello_period_cmd,
      "ipv6 pim6 hello-interval <1-18724>",
      IP6_STR
      ZPIM6_STR
      "PIM Hello message interval\n"
      "Seconds\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_hello_holdtime(ZebraPimNode *zpim, struct vty *vty,
			   int argc, const char *argv[])
{
    struct interface *ifp;

    ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    XLOG_ASSERT(zpim != NULL);

    uint16_t hello_holdtime = strtol(argv[0], NULL, 10);
    zpim->get_if_config(ifp->name).hello_holdtime.set(hello_holdtime);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_hello_holdtime(ifp->name, hello_holdtime,
					  error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set hello holdtime for interface %s to %d: %s%s",
		    ifp->name, hello_holdtime, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_hello_holdtime,
      ip_pim_hello_holdtime_cmd,
      "ip pim hello-holdtime <0-65535>",
      IP_STR
      ZPIM_STR
      "PIM Hello holdtime\n"
      "Seconds\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_hello_holdtime(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_hello_holdtime,
      ipv6_pim6_hello_holdtime_cmd,
      "ipv6 pim6 hello-holdtime <0-65535>",
      IP6_STR
      ZPIM6_STR
      "PIM Hello message holdtime\n"
      "Seconds\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_dr_priority(ZebraPimNode *zpim, struct vty *vty,
			int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    uint32_t dr_priority = strtol(argv[0], NULL, 10);
    zpim->get_if_config(ifp->name).dr_priority.set(dr_priority);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_dr_priority(ifp->name, dr_priority,
				       error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set dr priority for interface %s to %d: %s%s",
		    ifp->name, dr_priority, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_dr_priority,
      ip_pim_dr_priority_cmd,
      "ip pim dr-priority <0-4294967295>",
      IP_STR
      ZPIM_STR
      "Designated Router (DR) priority\n"
      "Priority\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_dr_priority(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_dr_priority,
      ipv6_pim6_dr_priority_cmd,
      "ipv6 pim6 dr-priority <0-4294967295>",
      IP6_STR
      ZPIM6_STR
      "Designated Router (DR) priority\n"
      "Priority\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_propagation_delay(ZebraPimNode *zpim, struct vty *vty,
			      int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    uint16_t propagation_delay = strtol(argv[0], NULL, 10);
    zpim->get_if_config(ifp->name).propagation_delay.set(propagation_delay);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_propagation_delay(ifp->name, propagation_delay,
					     error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set propagation delay for interface %s to %d: %s%s",
		    ifp->name, propagation_delay, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_propagation_delay,
      ip_pim_propagation_delay_cmd,
      "ip pim propagation-delay <0-65535>",
      IP_STR
      ZPIM_STR
      "Propagation delay\n"
      "Milliseconds\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_propagation_delay(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_propagation_delay,
      ipv6_pim6_propagation_delay_cmd,
      "ipv6 pim6 propagation-delay <0-65535>",
      IP6_STR
      ZPIM6_STR
      "Propagation delay\n"
      "Milliseconds\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_override_interval(ZebraPimNode *zpim, struct vty *vty,
			      int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    uint16_t override_interval = strtol(argv[0], NULL, 10);
    zpim->get_if_config(ifp->name).override_interval.set(override_interval);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_override_interval(ifp->name, override_interval,
					     error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set override interval for interface %s to %d: %s%s",
		    ifp->name, override_interval, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_override_interval,
      ip_pim_override_interval_cmd,
      "ip pim override-interval <0-65535>",
      IP_STR
      ZPIM_STR
      "Override interval\n"
      "Milliseconds\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_override_interval(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_override_interval,
      ipv6_pim6_override_interval_cmd,
      "ipv6 pim6 override-interval <0-65535>",
      IP6_STR
      ZPIM6_STR
      "Override interval\n"
      "Milliseconds\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_is_tracking_support_disabled(ZebraPimNode *zpim, struct vty *vty,
					 int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    zpim->get_if_config(ifp->name).is_tracking_support_disabled.set(true);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_is_tracking_support_disabled(ifp->name, true,
							error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set is tracking support disabled for interface %s to true: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_is_tracking_support_disabled(ZebraPimNode *zpim, struct vty *vty,
					    int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    zpim->get_if_config(ifp->name).is_tracking_support_disabled.set(false);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_is_tracking_support_disabled(ifp->name, false,
							error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set is tracking support disabled for interface %s to false: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_is_tracking_support_disabled,
      ip_pim_is_tracking_support_disabled_cmd,
      "ip pim is-tracking-support-disabled",
      IP_STR
      ZPIM_STR
      "Is tracking support disabled\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_is_tracking_support_disabled(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_is_tracking_support_disabled,
      no_ip_pim_is_tracking_support_disabled_cmd,
      "no ip pim is-tracking-support-disabled",
      NO_STR
      IP_STR
      ZPIM_STR
      "Is tracking support disabled\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_ip_pim_is_tracking_support_disabled(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_is_tracking_support_disabled,
      ipv6_pim6_is_tracking_support_disabled_cmd,
      "ipv6 pim6 is-tracking-support-disabled",
      IP6_STR
      ZPIM6_STR
      "Is tracking support disabled\n");

ALIAS(no_ip_pim_is_tracking_support_disabled,
      no_ipv6_pim6_is_tracking_support_disabled_cmd,
      "no ipv6 pim6 is-tracking-support-disabled",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Is tracking support disabled\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_accept_nohello_neighbors(ZebraPimNode *zpim, struct vty *vty,
				     int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    zpim->get_if_config(ifp->name).accept_nohello_neighbors.set(true);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_accept_nohello_neighbors(ifp->name, true,
						    error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set accept nohello neighbors for interface %s to true: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_accept_nohello_neighbors(ZebraPimNode *zpim, struct vty *vty,
					int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    zpim->get_if_config(ifp->name).accept_nohello_neighbors.set(false);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_accept_nohello_neighbors(ifp->name, false,
						    error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set accept nohello neighbors for interface %s to false: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_accept_nohello_neighbors,
      ip_pim_accept_nohello_neighbors_cmd,
      "ip pim accept-nohello-neighbors",
      IP_STR
      ZPIM_STR
      "Accept nohello neighbors\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_accept_nohello_neighbors(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_accept_nohello_neighbors,
      no_ip_pim_accept_nohello_neighbors_cmd,
      "no ip pim accept-nohello-neighbors",
      NO_STR
      IP_STR
      ZPIM_STR
      "Accept nohello neighbors\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_ip_pim_accept_nohello_neighbors(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_accept_nohello_neighbors,
      ipv6_pim6_accept_nohello_neighbors_cmd,
      "ipv6 pim6 accept-nohello-neighbors",
      IP6_STR
      ZPIM6_STR
      "Accept nohello neighbors\n");

ALIAS(no_ip_pim_accept_nohello_neighbors,
      no_ipv6_pim6_accept_nohello_neighbors_cmd,
      "no ipv6 pim6 accept-nohello-neighbors",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Accept nohello neighbors\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_join_prune_period(ZebraPimNode *zpim, struct vty *vty,
			      int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    uint16_t join_prune_period = strtol(argv[0], NULL, 10);
    zpim->get_if_config(ifp->name).join_prune_period.set(join_prune_period);

    // try to set now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->set_vif_join_prune_period(ifp->name, join_prune_period,
					     error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't set join prune period for interface %s to %d: %s%s",
		    ifp->name, join_prune_period, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_join_prune_period,
      ip_pim_join_prune_period_cmd,
      "ip pim join-prune-interval <1-65535>",
      IP_STR
      ZPIM_STR
      "Join/Prune interval\n"
      "Seconds\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_join_prune_period(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_join_prune_period,
      ipv6_pim6_join_prune_period_cmd,
      "ipv6 pim6 join-prune-interval <1-65535>",
      IP6_STR
      ZPIM6_STR
      "Join/Prune Interval\n"
      "Seconds\n");

#endif	// HAVE_IPV6_MULTICAST

// XXX add a show command for alternative subnets?

static int
zpim_ip_pim_alternative_subnet(ZebraPimNode *zpim, struct vty *vty,
			       int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    IPvXNet ipvxnet(argv[0]);

    pair<set<ZebraConfigVal<IPvXNet> >::const_iterator, bool> ret =
	zpim->get_if_config(ifp->name).alternative_subnets.insert(ZebraConfigVal<IPvXNet>(ipvxnet));
    if (ret.second == false)
	vty_out(vty, "alternative subnet %s already exists for interface %s%s",
		ipvxnet.str().c_str(), ifp->name,  VNL);

    // try now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->add_alternative_subnet(ifp->name, ipvxnet,
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
zpim_no_ip_pim_alternative_subnet(ZebraPimNode *zpim, struct vty *vty,
				  int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    switch (argc)
    {
    case 0:
	zpim->get_if_config(ifp->name).alternative_subnets.clear();

	// try now if the interface exists
	if (zpim->vif_find_by_name(ifp->name) != NULL)
	{
	    string error_msg;
	    if (zpim->remove_all_alternative_subnets(ifp->name,
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
	    if (zpim->get_if_config(ifp->name).alternative_subnets.erase(ZebraConfigVal<IPvXNet>(ipvxnet)) == 0)
		vty_out(vty, "alternative subnet %s does not exist for interface %s%s",
			ipvxnet.str().c_str(), ifp->name,  VNL);

	    // try now if the interface exists
	    if (zpim->vif_find_by_name(ifp->name) != NULL)
	    {
		string error_msg;
		if (zpim->delete_alternative_subnet(ifp->name, ipvxnet,
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

DEFUN(ip_pim_alternative_subnet,
      ip_pim_alternative_subnet_cmd,
      "ip pim alternative-subnet A.B.C.D/M",
      IP_STR
      ZPIM_STR
      "Associate an additional subnet with this network interface\n"
      "Subnet address/prefix length\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_alternative_subnet(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_alternative_subnet,
      no_ip_pim_alternative_subnet_cmd,
      "no ip pim alternative-subnet [A.B.C.D/M]",
      NO_STR
      IP_STR
      ZPIM_STR
      "Remove additional subnet association from this network interface\n"
      "Optional Subnet address/prefix length (all additional subnets if omitted)\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_ip_pim_alternative_subnet(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_alternative_subnet,
      ipv6_pim6_alternative_subnet_cmd,
      "ipv6 pim6 alternative-subnet X:X::X:X/M",
      IP6_STR
      ZPIM6_STR
      "Associate an additional subnet with this network interface\n"
      "Subnet address/prefix length\n");

ALIAS(no_ip_pim_alternative_subnet,
      no_ipv6_pim6_alternative_subnet_cmd,
      "no ipv6 pim6 alternative-subnet [X:X::X:X/M]",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Remove additional subnet association from this network interface\n"
      "Optional Subnet address/prefix length (all additional subnets if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_protocol_tos(ZebraPimNode *zpim, struct vty *vty,
			 int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    uint8_t ip_tos = strtol(argv[0], NULL, 10);

    string error_msg;
    if (zpim->set_default_ip_tos(ip_tos, error_msg) != XORP_OK)
    {
	vty_out(vty, "couldn't set type of service / traffic class: %s%s",
		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_protocol_tos(ZebraPimNode *zpim, struct vty *vty,
			    int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    string error_msg;
    if (zpim->reset_default_ip_tos(error_msg) != XORP_OK)
    {
	vty_out(vty, "couldn't reset type of service / traffic class: %s%s",
		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_protocol_type_of_service,
      ip_pim_protocol_type_of_service_cmd,
      "ip pim protocol-type-of-service <0-255>",
      IP_STR
      ZPIM_STR
      "The default type of service used for outgoing protocol packets\n"
      "Type of service value\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_protocol_tos(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_protocol_type_of_service,
      no_ip_pim_protocol_type_of_service_cmd,
      "no ip pim protocol-type-of-service",
      NO_STR
      IP_STR
      ZPIM_STR
      "Use the default type of service for outgoing protocol packets\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_ip_pim_protocol_tos(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_protocol_type_of_service,
      ipv6_pim6_protocol_traffic_class_cmd,
      "ipv6 pim6 protocol-traffic-class <0-255>",
      IP6_STR
      ZPIM6_STR
      "The default traffic class used for outgoing protocol packets\n"
      "Traffic class value\n")

ALIAS(no_ip_pim_protocol_type_of_service,
      no_ipv6_pim6_protocol_traffic_class_cmd,
      "no ipv6 pim6 protocol-traffic-class",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Use the default traffic class for outgoing protocol packets\n")

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_register_source(ZebraPimNode *zpim, struct vty *vty,
			    int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    string error_msg;
    if (zpim->set_register_source_config(argv[0], error_msg) != XORP_OK)
    {
	vty_out(vty, "couldn't set PIM register source: %s%s",
		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_register_source(ZebraPimNode *zpim, struct vty *vty,
			    int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    string error_msg;
    if (zpim->clear_register_source_config(error_msg) != XORP_OK)
    {
	vty_out(vty, "couldn't clear PIM register source: %s%s",
		error_msg.c_str(), VNL);
	return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_register_source,
      ip_pim_register_source_cmd,
      "ip pim register-source IFNAME",
      IP_STR
      ZPIM_STR
      "PIM register message source address\n"
      "Interface whose address is used as the source of PIM register messages sent to RPs\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_register_source(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_register_source,
      no_ip_pim_register_source_cmd,
      "no ip pim register-source",
      NO_STR
      IP_STR
      ZPIM_STR
      "PIM register message source address\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_ip_pim_register_source(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_register_source,
      ipv6_pim6_register_source_cmd,
      "ipv6 pim6 register-source IFNAME",
      IP6_STR
      ZPIM6_STR
      "PIM register message source address\n"
      "Interface whose address is used as the source of PIM register messages sent to RPs\n")

ALIAS(no_ip_pim_register_source,
      no_ipv6_pim6_register_source_cmd,
      "no ipv6 pim6 register-source",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "PIM register message source address\n")

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_ip_pim_static_membership(ZebraPimNode *zpim, struct vty *vty,
			      int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    IPvX source(zpim->PimNode::family()), group;
    int i = 0;
    switch (argc)
    {
    case 2:
	try {
	    source = IPvX(argv[i++]);
	} catch (...) {
	    vty_out(vty, "invalid source address: %s%s", argv[0], VNL);
	    return CMD_WARNING;
	}
	// fall through

    case 1:
	try {
	    group = IPvX(argv[i++]);
	} catch (...) {
	    vty_out(vty, "invalid multicast group: %s%s", argv[0], VNL);
	    return CMD_WARNING;
	}
	break;

    default:
	return CMD_ERR_NO_MATCH;
    }

    ZebraStaticMembership staticmbr(source, group);
    if (zpim->get_if_config(ifp->name).static_memberships.count(staticmbr) > 0)
    {
	vty_out(vty, "static membership (%s,%s) already exists for interface %s%s",
		staticmbr.source().str().c_str(),
		staticmbr.group().str().c_str(), ifp->name,  VNL);
	return CMD_WARNING;
    }

    // try now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->add_static_membership(ifp->name, source, group,
					error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't add static membership for interface %s: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    zpim->get_if_config(ifp->name).static_memberships.insert(staticmbr);

    return CMD_SUCCESS;
}

static int
zpim_no_ip_pim_static_membership(ZebraPimNode *zpim, struct vty *vty,
				 int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    struct interface *ifp = (struct interface *)vty->index;
    XLOG_ASSERT(ifp != NULL);

    IPvX source, group;
    int i = 0;
    switch (argc)
    {
    case 2:
	try {
	    source = IPvX(argv[i++]);
	} catch (...) {
	    vty_out(vty, "invalid source address: %s%s", argv[0], VNL);
	    return CMD_WARNING;
	}
	// fall through

    case 1:
	try {
	    group = IPvX(argv[i++]);
	} catch (...) {
	    vty_out(vty, "invalid multicast group: %s%s", argv[0], VNL);
	    return CMD_WARNING;
	}
	break;

    default:
	return CMD_ERR_NO_MATCH;
    }

    ZebraStaticMembership staticmbr(source, group);
    if (zpim->get_if_config(ifp->name).static_memberships.erase(ZebraConfigVal<ZebraStaticMembership>(staticmbr)) == 0)
    {
	vty_out(vty, "static membership (%s,%s) does not exist for interface %s%s",
		staticmbr.source().str().c_str(),
		staticmbr.group().str().c_str(), ifp->name,  VNL);
	return CMD_WARNING;
    }

    // try now if the interface exists
    if (zpim->vif_find_by_name(ifp->name) != NULL)
    {
	string error_msg;
	if (zpim->delete_static_membership(ifp->name, source, group,
					   error_msg) != XORP_OK)
	{
	    vty_out(vty, "couldn't delete static membership for interface %s: %s%s",
		    ifp->name, error_msg.c_str(), VNL);
	    return CMD_WARNING;
	}
    }

    return CMD_SUCCESS;
}

DEFUN(ip_pim_static_membership,
      ip_pim_static_membership_cmd,
      "ip pim static-membership A.B.C.D",
      IP_STR
      ZPIM_STR
      "Add a static membership for this network interface\n"
      "Multicast group address\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_static_membership(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_static_membership,
      no_ip_pim_static_membership_cmd,
      "no ip pim static-membership A.B.C.D",
      NO_STR
      IP_STR
      ZPIM_STR
      "Remove a static membership for this network interface\n"
      "Multicast group address\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_ip_pim_static_membership(zpim, vty, argc, argv);
}

DEFUN(ip_pim_static_ssm_membership,
      ip_pim_static_ssm_membership_cmd,
      "ip pim static-ssm-membership A.B.C.D A.B.C.D",
      IP_STR
      ZPIM_STR
      "Add a static source-specific membership for this network interface\n"
      "Source address\n"
      "Multicast group address\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_ip_pim_static_membership(zpim, vty, argc, argv);
}

DEFUN(no_ip_pim_static_ssm_membership,
      no_ip_pim_static_ssm_membership_cmd,
      "no ip pim static-ssm-membership A.B.C.D A.B.C.D",
      NO_STR
      IP_STR
      ZPIM_STR
      "Remove a static source-specific membership for this network interface\n"
      "Source address\n"
      "Multicast group address\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_ip_pim_static_membership(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(ip_pim_static_membership,
      ipv6_pim6_static_membership_cmd,
      "ipv6 pim6 static-membership X:X::X:X",
      IP6_STR
      ZPIM6_STR
      "Add a static membership for this network interface\n"
      "Multicast group address\n");

ALIAS(no_ip_pim_static_membership,
      no_ipv6_pim6_static_membership_cmd,
      "no ipv6 pim6 static-membership X:X::X:X",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Remove a static membership for this network interface\n"
      "Multicast group address\n");

ALIAS(ip_pim_static_ssm_membership,
      ipv6_pim6_static_ssm_membership_cmd,
      "ipv6 pim6 static-ssm-membership X:X::X:X X:X::X:X",
      IP6_STR
      ZPIM6_STR
      "Add a static source-specific membership for this network interface\n"
      "Source address\n"
      "Multicast group address\n");

ALIAS(no_ip_pim_static_ssm_membership,
      no_ipv6_pim6_static_ssm_membership_cmd,
      "no ipv6 pim6 static-ssm-membership X:X::X:X X:X::X:X",
      NO_STR
      IP6_STR
      ZPIM6_STR
      "Remove a static source-specific membership for this network interface\n"
      "Source address\n"
      "Multicast group address\n");

#endif	// HAVE_IPV6_MULTICAST

// zpim debug configuration write
int
ZebraPimNode::zebra_config_write_debug(struct vty *vty)
{
    if (is_log_info() || is_log_nbr() || is_log_trace())
    {
	if (is_log_info())
	    vty_out(vty, "debug %s%s", zebra_protostr(), VNL);
	if (is_log_nbr())
	    vty_out(vty, "debug %s neighbor%s", zebra_protostr(), VNL);
	if (is_log_trace())
	    vty_out(vty, "debug %s trace%s", zebra_protostr(), VNL);
	vty_out(vty, "!%s", VNL);
    }

    return CMD_SUCCESS;
}

static int
zpim_debug_pim(ZebraPimNode *zpim, struct vty *vty,
	       int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    zpim->set_log_info(true);

    return CMD_SUCCESS;
}

static int
zpim_no_debug_pim(ZebraPimNode *zpim, struct vty *vty,
		  int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    zpim->set_log_info(false);

    return CMD_SUCCESS;
}

DEFUN(debug_pim,
      debug_pim_cmd,
      "debug pim",
      DEBUG_STR
      ZPIM_STR)
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_debug_pim(zpim, vty, argc, argv);
}

DEFUN(no_debug_pim,
      no_debug_pim_cmd,
      "no debug pim",
      NO_STR
      DEBUG_STR
      ZPIM_STR)
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_debug_pim(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(debug_pim,
      debug_pim6_cmd,
      "debug pim6",
      DEBUG_STR
      ZPIM6_STR);

ALIAS(no_debug_pim,
      no_debug_pim6_cmd,
      "no debug pim6",
      NO_STR
      DEBUG_STR
      ZPIM6_STR);

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_debug_pim_neighbor(ZebraPimNode *zpim, struct vty *vty,
		     int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    zpim->set_log_nbr(true);

    return CMD_SUCCESS;
}

static int
zpim_no_debug_pim_neighbor(ZebraPimNode *zpim, struct vty *vty,
			int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    zpim->set_log_nbr(false);

    return CMD_SUCCESS;
}

DEFUN(debug_pim_neighbor,
      debug_pim_neighbor_cmd,
      "debug pim neighbor",
      DEBUG_STR
      ZPIM_STR
      PIM_NEIGHBOR_STR)
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_debug_pim_neighbor(zpim, vty, argc, argv);
}

DEFUN(no_debug_pim_neighbor,
      no_debug_pim_neighbor_cmd,
      "no debug pim neighbor",
      NO_STR
      DEBUG_STR
      ZPIM_STR
      PIM_NEIGHBOR_STR)
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_debug_pim_neighbor(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(debug_pim_neighbor,
      debug_pim6_neighbor_cmd,
      "debug pim6 neighbor",
      DEBUG_STR
      ZPIM6_STR
      PIM_NEIGHBOR_STR);

ALIAS(no_debug_pim_neighbor,
      no_debug_pim6_neighbor_cmd,
      "no debug pim6 neighbor",
      NO_STR
      DEBUG_STR
      ZPIM6_STR
      PIM_NEIGHBOR_STR);

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_debug_pim_trace(ZebraPimNode *zpim, struct vty *vty,
		     int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    zpim->set_log_trace(true);

    return CMD_SUCCESS;
}

static int
zpim_no_debug_pim_trace(ZebraPimNode *zpim, struct vty *vty,
			int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    zpim->set_log_trace(false);

    return CMD_SUCCESS;
}

DEFUN(debug_pim_trace,
      debug_pim_trace_cmd,
      "debug pim trace",
      DEBUG_STR
      ZPIM_STR
      TRACE_STR)
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_debug_pim_trace(zpim, vty, argc, argv);
}

DEFUN(no_debug_pim_trace,
      no_debug_pim_trace_cmd,
      "no debug pim trace",
      NO_STR
      DEBUG_STR
      ZPIM_STR
      TRACE_STR)
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_no_debug_pim_trace(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(debug_pim_trace,
      debug_pim6_trace_cmd,
      "debug pim6 trace",
      DEBUG_STR
      ZPIM6_STR
      TRACE_STR);

ALIAS(no_debug_pim_trace,
      no_debug_pim6_trace_cmd,
      "no debug pim6 trace",
      NO_STR
      DEBUG_STR
      ZPIM6_STR
      TRACE_STR);

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_bootstrap(ZebraPimNode *zpim, struct vty *vty,
			   int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    string command_args = "";
    for (int i = 0; i < argc; i++)
	command_args += string(" ") + argv[i];

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() + string(" bootstrap"),
			       command_args, vty);
}

DEFUN(show_ip_pim_bootstrap,
      show_ip_pim_bootstrap_cmd,
      "show ip pim bsr [A.B.C.D/M] (scoped|non-scoped)",
      SHOW_STR
      IP_STR
      ZPIM_STR
      "PIM bootstrap router information\n"
      "Optional multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_bootstrap(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_bootstrap,
      show_ipv6_pim6_bootstrap_cmd,
      "show ipv6 pim6 bsr [X:X::X:X/M] (scoped|non-scoped)",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      "PIM bootstrap router information\n"
      "Optional multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_bootstrap_rps(ZebraPimNode *zpim, struct vty *vty,
			       int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    string command_args = "";
    for (int i = 0; i < argc; i++)
	command_args += string(" ") + argv[i];

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() +
			       string(" bootstrap rps"),
			       command_args, vty);
}

DEFUN(show_ip_pim_bootstrap_rps,
      show_ip_pim_bootstrap_rps_cmd,
      "show ip pim bsr-rp [A.B.C.D/M] (scoped|non-scoped)",
      SHOW_STR
      IP_STR
      ZPIM_STR
      "PIM bootstrap router RP information\n"
      "Optional multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_bootstrap_rps(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_bootstrap_rps,
      show_ipv6_pim6_bootstrap_rps_cmd,
      "show ipv6 pim6 bsr-rp [X:X::X:X/M] (scoped|non-scoped)",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      "PIM bootstrap router RP information\n"
      "Optional multicast scope zone group prefix/group range\n"
      "Multicast group prefix defines a multicast scope zone\n"
      "Multicast group prefix represents a range of multicast groups\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_interface(ZebraPimNode *zpim, struct vty *vty,
			   int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() + string(" interface"),
			       (argc ? argv[0] : ""), vty);
}

DEFUN(show_ip_pim_interface,
      show_ip_pim_interface_cmd,
      "show ip pim interface [IFNAME]",
      SHOW_STR
      IP_STR
      ZPIM_STR
      INTERFACE_STR
      "Optional interface name (all interfaces if omitted)\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_interface(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_interface,
      show_ipv6_pim6_interface_cmd,
      "show ipv6 pim6 interface [IFNAME]",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      INTERFACE_STR
      "Optional interface name (all interfaces if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_interface_address(ZebraPimNode *zpim, struct vty *vty,
				   int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() +
			       string(" interface address"),
			       (argc ? argv[0] : ""), vty);
}

DEFUN(show_ip_pim_interface_address,
      show_ip_pim_interface_address_cmd,
      "show ip pim interface-address [IFNAME]",
      SHOW_STR
      IP_STR
      ZPIM_STR
      "Interface address information\n"
      "Optional interface name (all interfaces if omitted)\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_interface_address(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_interface_address,
      show_ipv6_pim6_interface_address_cmd,
      "show ipv6 pim6 interface-address [IFNAME]",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      "Interface address information\n"
      "Optional interface name (all interfaces if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_join(ZebraPimNode *zpim, struct vty *vty,
		      int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() + string(" join"),
			       (argc ? argv[0] : ""), vty);
}

DEFUN(show_ip_pim_join,
      show_ip_pim_join_cmd,
      "show ip pim join [A.B.C.D[/M]]",
      SHOW_STR
      IP_STR
      ZPIM_STR
      "PIM group information\n"
      "Optional multicast group/group range (all groups if omitted)\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_join(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_join,
      show_ipv6_pim6_join_cmd,
      "show ipv6 pim6 join [X:X::X:X[/M]]",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      "PIM group information\n"
      "Optional multicast group/group range (all groups if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_join_all(ZebraPimNode *zpim, struct vty *vty,
			  int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() + string(" join all"),
			       (argc ? argv[0] : ""), vty);
}

DEFUN(show_ip_pim_join_all,
      show_ip_pim_join_all_cmd,
      "show ip pim join-all [A.B.C.D[/M]]",
      SHOW_STR
      IP_STR
      ZPIM_STR
      "All PIM group information\n"
      "Optional multicast group/group range (all groups if omitted)\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_join_all(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_join_all,
      show_ipv6_pim6_join_all_cmd,
      "show ipv6 pim6 join all [X:X::X:X[/M]]",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      "All PIM group information\n"
      "Optional multicast group/group range (all groups if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_mfc(ZebraPimNode *zpim, struct vty *vty,
		     int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() + string(" mfc"),
			       (argc ? argv[0] : ""), vty);
}

DEFUN(show_ip_pim_mfc,
      show_ip_pim_mfc_cmd,
      "show ip pim mfc [A.B.C.D[/M]]",
      SHOW_STR
      IP_STR
      ZPIM_STR
      "PIM Multicast Forwarding Cache information\n"
      "Optional multicast group/group range (all groups if omitted)\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_mfc(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_mfc,
      show_ipv6_pim6_mfc_cmd,
      "show ipv6 pim6 mfc [X:X::X:X[/M]]",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      "PIM Multicast Forwarding Cache information\n"
      "Optional multicast group/group range (all groups if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_neighbor(ZebraPimNode *zpim, struct vty *vty,
			  int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() + string(" neighbors"),
			       (argc ? argv[0] : ""), vty);
}

DEFUN(show_ip_pim_neighbor,
      show_ip_pim_neighbor_cmd,
      "show ip pim neighbor [IFNAME]",
      SHOW_STR
      IP_STR
      ZPIM_STR
      "PIM neighbor information\n"
      "Optional interface name (all interfaces if omitted)\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_neighbor(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_neighbor,
      show_ipv6_pim6_neighbor_cmd,
      "show ipv6 pim6 neighbor [IFNAME]",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      "PIM neighbor information\n"
      "Optional interface name (all interfaces if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_mrib(ZebraPimNode *zpim, struct vty *vty,
		      int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() + string(" mrib"),
			       (argc ? argv[0] : ""), vty);
}

DEFUN(show_ip_pim_mrib,
      show_ip_pim_mrib_cmd,
      "show ip pim mrib [A.B.C.D]",
      SHOW_STR
      IP_STR
      ZPIM_STR
      "Multicast Routing Information Base (MRIB) information\n"
      "Optional destination address\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_mrib(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_mrib,
      show_ipv6_pim6_mrib_cmd,
      "show ipv6 pim6 mrib [X:X::X:X]",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      "Multicast Routing Information Base (MRIB) information\n"
      "Optional destination address\n")

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_rp(ZebraPimNode *zpim, struct vty *vty,
		    int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);
    XLOG_ASSERT(zpim->PimNode::proto_is_pimsm());

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() + string(" rps"),
			       (argc ? argv[0] : ""), vty);
}

DEFUN(show_ip_pim_rp,
      show_ip_pim_rp_cmd,
      "show ip pim rp [A.B.C.D]",
      SHOW_STR
      IP_STR
      ZPIM_STR
      "PIM rendezvous point (RP) information\n"
      "Optional multicast group (all groups if omitted)\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_rp(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_rp,
      show_ipv6_pim6_rp_cmd,
      "show ipv6 pim6 rp [X:X::X:X]",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      "PIM rendezvous point (RP) information\n"
      "Optional multicast group (all groups if omitted)\n");

#endif	// HAVE_IPV6_MULTICAST

static int
zpim_show_ip_pim_scope(ZebraPimNode *zpim, struct vty *vty,
		       int argc, const char *argv[])
{
    XLOG_ASSERT(zpim != NULL);

    return cli_process_command(zpim, string("show ") +
			       zpim->xorp_protostr() + string(" scope"),
			       "", vty);
}

DEFUN(show_ip_pim_scope,
      show_ip_pim_scope_cmd,
      "show ip pim scope",
      SHOW_STR
      IP_STR
      ZPIM_STR
      "PIM scope zones information\n")
{
    ZebraPimNode *zpim = _zpim;
    XLOG_ASSERT(zpim != NULL);

    return zpim_show_ip_pim_scope(zpim, vty, argc, argv);
}

#ifdef HAVE_IPV6_MULTICAST

ALIAS(show_ip_pim_scope,
      show_ipv6_pim6_scope_cmd,
      "show ipv6 pim6 scope",
      SHOW_STR
      IP6_STR
      ZPIM6_STR
      "PIM scope zones information\n");

#endif	// HAVE_IPV6_MULTICAST

static void
zebra_command_init_pimsm(ZebraPimNode *zpim, int family)
{
    XLOG_ASSERT(_zpim == NULL);
    _zpim = zpim;

    // install the zpim node
    install_node(&zpim_node, config_write_zpim);
    install_default(PIM_NODE);	// add the default commands (exit, etc.)

    if (family == AF_INET)
    {
	install_element(CONFIG_NODE, &router_pim_cmd);
	install_element(CONFIG_NODE, &no_router_pim_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (family == AF_INET6)
    {
	install_element(CONFIG_NODE, &router_pim6_cmd);
	install_element(CONFIG_NODE, &no_router_pim6_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
	XLOG_UNREACHABLE();

    // protocol configuration commands
    if (family == AF_INET)
    {
	install_element(PIM_NODE, &ip_pim_rp_address_cmd);
	install_element(PIM_NODE, &no_ip_pim_rp_address_cmd);
	install_element(PIM_NODE, &ip_pim_bsr_candidate_cmd);
	install_element(PIM_NODE, &no_ip_pim_bsr_candidate_cmd);
	install_element(PIM_NODE, &ip_pim_rp_candidate_cmd);
	install_element(PIM_NODE, &no_ip_pim_rp_candidate_cmd);
	install_element(PIM_NODE, &ip_pim_spt_threshold_cmd);
	install_element(PIM_NODE, &no_ip_pim_spt_threshold_cmd);
	install_element(PIM_NODE, &ip_pim_protocol_type_of_service_cmd);
	install_element(PIM_NODE, &no_ip_pim_protocol_type_of_service_cmd);
	install_element(PIM_NODE, &ip_pim_register_source_cmd);
	install_element(PIM_NODE, &no_ip_pim_register_source_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (family == AF_INET6)
    {
	install_element(PIM_NODE, &ipv6_pim6_rp_address_cmd);
	install_element(PIM_NODE, &no_ipv6_pim6_rp_address_cmd);
	install_element(PIM_NODE, &ipv6_pim6_bsr_candidate_cmd);
	install_element(PIM_NODE, &no_ipv6_pim6_bsr_candidate_cmd);
	install_element(PIM_NODE, &ipv6_pim6_rp_candidate_cmd);
	install_element(PIM_NODE, &no_ipv6_pim6_rp_candidate_cmd);
	install_element(PIM_NODE, &ipv6_pim6_spt_threshold_cmd);
	install_element(PIM_NODE, &no_ipv6_pim6_spt_threshold_cmd);
	install_element(PIM_NODE, &ipv6_pim6_protocol_traffic_class_cmd);
	install_element(PIM_NODE, &no_ipv6_pim6_protocol_traffic_class_cmd);
	install_element(PIM_NODE, &ipv6_pim6_register_source_cmd);
	install_element(PIM_NODE, &no_ipv6_pim6_register_source_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
	XLOG_UNREACHABLE();

    // interface commands
    if (family == AF_INET)
    {
	install_element(INTERFACE_NODE, &ip_pim_cmd);
	install_element(INTERFACE_NODE, &no_ip_pim_cmd);
	install_element(INTERFACE_NODE, &ip_pim_version_cmd);
	install_element(INTERFACE_NODE, &ip_pim_passive_cmd);
	install_element(INTERFACE_NODE, &no_ip_pim_passive_cmd);
	install_element(INTERFACE_NODE,
			&ip_pim_ip_router_alert_option_check_cmd);
	install_element(INTERFACE_NODE,
			&no_ip_pim_ip_router_alert_option_check_cmd);
	install_element(INTERFACE_NODE, &ip_pim_hello_triggered_delay_cmd);
	install_element(INTERFACE_NODE, &ip_pim_hello_period_cmd);
	install_element(INTERFACE_NODE, &ip_pim_hello_holdtime_cmd);
	install_element(INTERFACE_NODE, &ip_pim_dr_priority_cmd);
	install_element(INTERFACE_NODE, &ip_pim_propagation_delay_cmd);
	install_element(INTERFACE_NODE, &ip_pim_override_interval_cmd);
	install_element(INTERFACE_NODE,
			&ip_pim_is_tracking_support_disabled_cmd);
	install_element(INTERFACE_NODE,
			&no_ip_pim_is_tracking_support_disabled_cmd);
	install_element(INTERFACE_NODE,
			&ip_pim_accept_nohello_neighbors_cmd);
	install_element(INTERFACE_NODE,
			&no_ip_pim_accept_nohello_neighbors_cmd);
	install_element(INTERFACE_NODE, &ip_pim_join_prune_period_cmd);
 	install_element(INTERFACE_NODE, &ip_pim_alternative_subnet_cmd);
 	install_element(INTERFACE_NODE, &no_ip_pim_alternative_subnet_cmd);
 	install_element(INTERFACE_NODE, &ip_pim_static_membership_cmd);
 	install_element(INTERFACE_NODE, &no_ip_pim_static_membership_cmd);
 	install_element(INTERFACE_NODE, &ip_pim_static_ssm_membership_cmd);
 	install_element(INTERFACE_NODE, &no_ip_pim_static_ssm_membership_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (family == AF_INET6)
    {
	install_element(INTERFACE_NODE, &ipv6_pim6_cmd);
	install_element(INTERFACE_NODE, &no_ipv6_pim6_cmd);
	install_element(INTERFACE_NODE, &ipv6_pim6_version_cmd);
	install_element(INTERFACE_NODE, &ipv6_pim6_passive_cmd);
	install_element(INTERFACE_NODE, &no_ipv6_pim6_passive_cmd);
	install_element(INTERFACE_NODE,
			&ipv6_pim6_ip_router_alert_option_check_cmd);
	install_element(INTERFACE_NODE,
			&no_ipv6_pim6_ip_router_alert_option_check_cmd);
	install_element(INTERFACE_NODE, &ipv6_pim6_hello_triggered_delay_cmd);
	install_element(INTERFACE_NODE, &ipv6_pim6_hello_period_cmd);
	install_element(INTERFACE_NODE, &ipv6_pim6_hello_holdtime_cmd);
	install_element(INTERFACE_NODE, &ipv6_pim6_dr_priority_cmd);
	install_element(INTERFACE_NODE, &ipv6_pim6_propagation_delay_cmd);
	install_element(INTERFACE_NODE, &ipv6_pim6_override_interval_cmd);
	install_element(INTERFACE_NODE,
			&ipv6_pim6_is_tracking_support_disabled_cmd);
	install_element(INTERFACE_NODE,
			&no_ipv6_pim6_is_tracking_support_disabled_cmd);
	install_element(INTERFACE_NODE,
			&ipv6_pim6_accept_nohello_neighbors_cmd);
	install_element(INTERFACE_NODE,
			&no_ipv6_pim6_accept_nohello_neighbors_cmd);
	install_element(INTERFACE_NODE, &ipv6_pim6_join_prune_period_cmd);
 	install_element(INTERFACE_NODE, &ipv6_pim6_alternative_subnet_cmd);
 	install_element(INTERFACE_NODE, &no_ipv6_pim6_alternative_subnet_cmd);
 	install_element(INTERFACE_NODE, &ipv6_pim6_static_membership_cmd);
 	install_element(INTERFACE_NODE, &no_ipv6_pim6_static_membership_cmd);
 	install_element(INTERFACE_NODE, &ipv6_pim6_static_ssm_membership_cmd);
 	install_element(INTERFACE_NODE,
			&no_ipv6_pim6_static_ssm_membership_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
	XLOG_UNREACHABLE();

    // debug commands
    if (family == AF_INET)
    {
	install_element(CONFIG_NODE, &debug_pim_cmd);
	install_element(CONFIG_NODE, &no_debug_pim_cmd);
	install_element(CONFIG_NODE, &debug_pim_trace_cmd);
	install_element(CONFIG_NODE, &no_debug_pim_trace_cmd);
	install_element(CONFIG_NODE, &debug_pim_neighbor_cmd);
	install_element(CONFIG_NODE, &no_debug_pim_neighbor_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (family == AF_INET6)
    {
	install_element(CONFIG_NODE, &debug_pim6_cmd);
	install_element(CONFIG_NODE, &no_debug_pim6_cmd);
	install_element(CONFIG_NODE, &debug_pim6_trace_cmd);
	install_element(CONFIG_NODE, &no_debug_pim6_trace_cmd);
	install_element(CONFIG_NODE, &debug_pim6_neighbor_cmd);
	install_element(CONFIG_NODE, &no_debug_pim6_neighbor_cmd);
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
    if (family == AF_INET)
    {
	ADD_SHOW_CMD(show_ip_pim_bootstrap_cmd);
	ADD_SHOW_CMD(show_ip_pim_bootstrap_rps_cmd);
	ADD_SHOW_CMD(show_ip_pim_rp_cmd);
	ADD_SHOW_CMD(show_ip_pim_interface_cmd);
	ADD_SHOW_CMD(show_ip_pim_interface_address_cmd);
	ADD_SHOW_CMD(show_ip_pim_join_cmd);
	ADD_SHOW_CMD(show_ip_pim_join_all_cmd);
	ADD_SHOW_CMD(show_ip_pim_mfc_cmd);
	ADD_SHOW_CMD(show_ip_pim_neighbor_cmd);
	ADD_SHOW_CMD(show_ip_pim_mrib_cmd);
	ADD_SHOW_CMD(show_ip_pim_scope_cmd);
    }
#ifdef HAVE_IPV6_MULTICAST
    else if (family == AF_INET6)
    {
	ADD_SHOW_CMD(show_ipv6_pim6_bootstrap_cmd);
	ADD_SHOW_CMD(show_ipv6_pim6_bootstrap_rps_cmd);
	ADD_SHOW_CMD(show_ipv6_pim6_rp_cmd);
	ADD_SHOW_CMD(show_ipv6_pim6_interface_cmd);
	ADD_SHOW_CMD(show_ipv6_pim6_interface_address_cmd);
	ADD_SHOW_CMD(show_ipv6_pim6_join_cmd);
	ADD_SHOW_CMD(show_ipv6_pim6_join_all_cmd);
	ADD_SHOW_CMD(show_ipv6_pim6_mfc_cmd);
	ADD_SHOW_CMD(show_ipv6_pim6_neighbor_cmd);
	ADD_SHOW_CMD(show_ipv6_pim6_mrib_cmd);
	ADD_SHOW_CMD(show_ipv6_pim6_scope_cmd);
    }
#endif	// HAVE_IPV6_MULTICAST
    else
    {
	XLOG_UNREACHABLE();
    }

#undef ADD_SHOW_CMD
}

void
ZebraPimNode::zebra_command_init()
{
    if (PimNode::proto_is_pimsm())
	zebra_command_init_pimsm(this, PimNode::family());
    else
	XLOG_UNREACHABLE();
}
