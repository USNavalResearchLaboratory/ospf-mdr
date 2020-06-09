// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#include "zebra_pim_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"

#include "libxorp/ipvx.hh"
#include "pim/pim_vif.hh"
#include "mrt/mrib_table.hh"

#include "zebra_pim_node.hh"

extern "C" {
#include "if.h"
#include "prefix.h"
#include "command.h"
}


ZebraBsrCandidateConfig::ZebraBsrCandidateConfig(const IPvXNet &scope_zone_id,
						 bool is_scope_zone,
						 const string& vif_name,
						 const IPvX& vif_addr,
						 uint8_t bsr_priority,
						 uint8_t hash_mask_len) :
    _scope_zone_id(scope_zone_id), _is_scope_zone(is_scope_zone),
    _vif_name(vif_name), _vif_addr(vif_addr),
    _bsr_priority(bsr_priority), _hash_mask_len(hash_mask_len)
{
}

ZebraBsrCandidateConfig::ZebraBsrCandidateConfig(const IPvXNet &scope_zone_id,
						 bool is_scope_zone) :
    _scope_zone_id(scope_zone_id), _is_scope_zone(is_scope_zone),
    _vif_name(), _vif_addr(),
    _bsr_priority(), _hash_mask_len()
{
}

bool
ZebraBsrCandidateConfig::operator<(const ZebraBsrCandidateConfig& other) const
{
    return _scope_zone_id < other._scope_zone_id ||
	_is_scope_zone < other._is_scope_zone;
}

ZebraRpCandidateConfig::ZebraRpCandidateConfig(const IPvXNet& group_prefix,
					       bool is_scope_zone,
					       const string& vif_name,
					       const IPvX& vif_addr,
					       uint8_t rp_priority,
					       uint16_t rp_holdtime) :
    _group_prefix(group_prefix), _is_scope_zone(is_scope_zone),
    _vif_name(vif_name), _vif_addr(vif_addr),
    _rp_priority(rp_priority), _rp_holdtime(rp_holdtime)
{
}

ZebraRpCandidateConfig::ZebraRpCandidateConfig(const IPvXNet& group_prefix,
					       bool is_scope_zone) :
    _group_prefix(group_prefix), _is_scope_zone(is_scope_zone),
    _vif_name(), _vif_addr(), _rp_priority(), _rp_holdtime()
{
}

bool
ZebraRpCandidateConfig::operator<(const ZebraRpCandidateConfig& other) const
{
    return _group_prefix < other._group_prefix ||
	_is_scope_zone < other._is_scope_zone;
}

ZebraStaticMembership::ZebraStaticMembership(const IPvX &source,
					     const IPvX &group) :
    _source(source), _group(group)
{
}

const IPvX &
ZebraStaticMembership::source() const
{
    return _source;
}

const IPvX &
ZebraStaticMembership::group() const
{
    return _group;
}

bool
ZebraStaticMembership::operator<(const ZebraStaticMembership &other) const
{
    if (_source == other._source)
	return (_group < other._group);
    else
	return (_source < other._source);
}

void
ZebraPimVifConfig::clear_all_applied() const
{
    enabled.clear_applied();
    proto_version.clear_applied();
    passive.clear_applied();
    ip_router_alert_option_check.clear_applied();
    hello_triggered_delay.clear_applied();
    hello_period.clear_applied();
    hello_holdtime.clear_applied();
    dr_priority.clear_applied();
    propagation_delay.clear_applied();
    override_interval.clear_applied();
    is_tracking_support_disabled.clear_applied();
    accept_nohello_neighbors.clear_applied();
    join_prune_period.clear_applied();

    for (set<ZebraConfigVal<IPvXNet> >::const_iterator it =
	     alternative_subnets.begin();
	 it != alternative_subnets.end(); ++it)
    {
	const ZebraConfigVal<IPvXNet> &altsubnet = *it;
	altsubnet.clear_applied();
    }

    for (set<ZebraConfigVal<ZebraStaticMembership> >::const_iterator it =
	     static_memberships.begin();
	 it != static_memberships.end(); ++it)
    {
	const ZebraConfigVal<ZebraStaticMembership> &staticmbr = *it;
	staticmbr.clear_applied();
    }
}

ZebraPimNode::ZebraPimNode(int family, xorp_module_id module_id,
			   EventLoop &eventloop,
			   ZebraRouterNode &zebra_router_node,
			   ZebraMfeaNode &zebra_mfea_node,
			   ZebraMld6igmpNode &zebra_mld6igmp_node) :
    PimNode(family, module_id, eventloop),
    PimNodeCli(*static_cast<PimNode *>(this)),
    ZebraRouterClient(zebra_router_node),
    ZebraMfeaClient(*static_cast<PimNode *>(this), zebra_mfea_node),
    ZebraMld6igmpClient(*static_cast<PimNode *>(this), zebra_mld6igmp_node),
    _pending_rp_update(false),
    _terminated(false)
{
    if (zebra_mfea_node.add_allow_kernel_signal_messages(PimNode::module_name(),
							 PimNode::module_id()) != XORP_OK)
	XLOG_ERROR("MfeaNode::add_allow_kernel_signal_messages() failed");
}

ZebraPimNode::~ZebraPimNode()
{
    terminate();
}

int
ZebraPimNode::start(string& error_msg)
{
    if (!PimNode::is_enabled())
	PimNode::enable();

    if (!PimNode::is_up() && !PimNode::is_pending_up())
    {
	int r = PimNode::start();
	if (r != XORP_OK)
	{
	    error_msg = "pim start failed";
	    return r;
	}

	r = PimNode::final_start();
	if (r != XORP_OK)
	{
	    error_msg = "pim final_start failed";
	    return r;
	}
    }

    return XORP_OK;
}

int
ZebraPimNode::stop(string& error_msg)
{
    int r = XORP_OK;
    if (PimNode::is_up())
    {
	r = PimNode::stop();
	if (r != XORP_OK)
	    error_msg = "pim stop failed";

	// XXX
	// r = PimNode::final_stop();
	// if (r != XORP_OK)
	// 	error_msg = "pim final_stop failed";
    }

    if (PimNode::is_enabled())
	PimNode::disable();

    return r;
}

int
ZebraPimNode::add_vif(const Vif& vif, string& error_msg)
{
    int r;

    r = PimNode::add_vif(vif, error_msg);

    if (vif.is_pim_register())
    {
	string error_msg;
	if (enable_vif(vif.name(), error_msg) != XORP_OK)
	    XLOG_ERROR("enable_vif() failed: %s", error_msg.c_str());
	if (start_vif(vif.name(), error_msg) != XORP_OK)
	    XLOG_ERROR("start_vif() failed: %s", error_msg.c_str());
    }
    else
    {
	apply_config(vif.name());
	check_static_rp(vif.name());
    }

    return r;
}

int
ZebraPimNode::add_vif_addr(const string& vif_name, const IPvX& addr,
			   const IPvXNet& subnet_addr,
			   const IPvX& broadcast_addr,
			   const IPvX& peer_addr, bool& should_send_pim_hello,
			   string& error_msg)
{
    int r;

    if ((r = PimNode::add_vif_addr(vif_name, addr, subnet_addr,
				   broadcast_addr, peer_addr,
				   should_send_pim_hello,
				   error_msg)) != XORP_OK)
	return r;

    PimVif *vif = vif_find_by_name(vif_name);
    if (vif == NULL)
    {
	XLOG_ERROR("%s: vif not found: %s", __func__, vif_name.c_str());
	return XORP_ERROR;
    }

    if (!vif->is_pim_register())
    {
	apply_config(vif_name);
	check_static_rp(vif_name);
    }

    return r;
}

int
ZebraPimNode::delete_vif(const string& vif_name, string& error_msg)
{
    int r;

    if ((r = PimNode::delete_vif(vif_name, error_msg)) != XORP_OK)
	return r;

    clear_config(vif_name);

    const struct interface *ifp = if_lookup_by_name (vif_name.c_str());
    if (ifp != NULL && if_is_transient(ifp))
	del_if_config(ifp->name);

    return r;
}

bool
ZebraPimNode::try_start_vif(const string& name)
{
    PimVif *vif = vif_find_by_name(name);

    if (vif == NULL)
	return false;
    if (vif->is_up())
	return false;
    if (!vif->is_underlying_vif_up())
	return false;
    if (vif->addr_ptr() == NULL)
	return false;
    list<VifAddr>::const_iterator iter;
    for (iter = vif->addr_list().begin();
	 iter != vif->addr_list().end(); ++iter)
    {
	const IPvX &addr = iter->addr();
	if (addr.af() == PimNode::family() && addr.is_unicast() &&
	    (addr.is_ipv4() || addr.is_linklocal_unicast()))
	    break;
    }
    if (iter == vif->addr_list().end())
	return false;

    string error_msg;
    if (start_vif(name, error_msg) != XORP_OK)
    {
	XLOG_ERROR("start_vif() failed: %s", error_msg.c_str());
	return false;
    }

    return true;
}

ZebraPimVifConfig &
ZebraPimNode::get_if_config(const string &vif_name)
{
    return _if_config[vif_name];
}

void
ZebraPimNode::del_if_config(const string &vif_name)
{
    _if_config.erase(vif_name);
}

void
ZebraPimNode::set_pending_rp_update()
{
    _pending_rp_update = true;
}

int
ZebraPimNode::add_cand_bsr_config(const IPvXNet& scope_zone_id,
				  bool is_scope_zone,
				  const string& vif_name, const IPvX& vif_addr,
				  uint8_t bsr_priority, uint8_t hash_mask_len,
				  string& error_msg)
{
    int ret = XORP_OK;

    ZebraBsrCandidateConfig bsrcancfg = ZebraBsrCandidateConfig(scope_zone_id,
								is_scope_zone,
								vif_name, vif_addr,
								bsr_priority,
								hash_mask_len);
    pair<set<ZebraBsrCandidateConfig>::const_iterator, bool> tmp =
	_cand_bsrs.insert(bsrcancfg);
    if (tmp.second == false)
    {
	error_msg = "config for BSR candidate already exists";
	ret = XORP_ERROR;
    }

    // try now if the interface exists
    if (vif_find_by_name(vif_name) != NULL)
    {
	ret = add_config_cand_bsr(scope_zone_id, is_scope_zone,
				  vif_name, vif_addr,
				  bsr_priority, hash_mask_len, error_msg);
	enable_bsr();
	if (stop_bsr() != XORP_OK)
	    XLOG_WARNING("stop_bsr() failed");
	if (start_bsr() != XORP_OK)
	{
	    error_msg = "start_bsr() failed";
	    ret = XORP_ERROR;
	}
    }

    return ret;
}

int
ZebraPimNode::delete_cand_bsr_config(const IPvXNet& scope_zone_id,
				     bool is_scope_zone, string& error_msg)
{
    ZebraBsrCandidateConfig bsrcancfg = ZebraBsrCandidateConfig(scope_zone_id,
								is_scope_zone);
    _cand_bsrs.erase(bsrcancfg);
    return delete_config_cand_bsr(scope_zone_id, is_scope_zone, error_msg);
}

int
ZebraPimNode::add_cand_rp_config(const IPvXNet& group_prefix,
				 bool is_scope_zone,
				 const string& vif_name, const IPvX& vif_addr,
				 uint8_t rp_priority, uint16_t rp_holdtime,
				 string& error_msg)
{
    int ret = XORP_OK;

    ZebraRpCandidateConfig rpcancfg = ZebraRpCandidateConfig(group_prefix,
							     is_scope_zone,
							     vif_name, vif_addr,
							     rp_priority,
							     rp_holdtime);
    pair<set<ZebraRpCandidateConfig>::const_iterator, bool> tmp =
	_cand_rps.insert(rpcancfg);
    if (tmp.second == false)
    {
	error_msg = "config for RP candidate already exists";
	ret = XORP_ERROR;
    }

    // try now if the interface exists
    if (vif_find_by_name(vif_name) != NULL)
    {
	ret = add_config_cand_rp(group_prefix, is_scope_zone,
				 vif_name, vif_addr,
				 rp_priority, rp_holdtime, error_msg);
    }

    return ret;
}

int
ZebraPimNode::delete_cand_rp_config(const IPvXNet& group_prefix,
				    bool is_scope_zone, const string& vif_name,
				    const IPvX& vif_addr, string& error_msg)
{
    ZebraRpCandidateConfig rpcancfg = ZebraRpCandidateConfig(group_prefix,
							     is_scope_zone);
    _cand_rps.erase(rpcancfg);
    return delete_config_cand_rp(group_prefix, is_scope_zone,
				 vif_name, vif_addr, error_msg);
}

int
ZebraPimNode::set_register_source_config(const string& vif_name,
					 string& error_msg)
{
    if (!_register_source_vif_name.is_set() ||
	_register_source_vif_name.get() != vif_name)
    {
	bool applied = false;
	PimVif *vif = vif_find_by_name(vif_name);
	if (vif != NULL)
	{
	    // try now if the interface exists
	    int ret = set_vif_register_source(vif_name, error_msg);
	    if (ret != XORP_OK)
		return ret;
	    applied = true;
	}

	_register_source_vif_name.set(vif_name);

	if (applied)
	 {
	     _register_source_vif_name.set_applied();
	 }
	else
	{
	    // clear the current setting
	    string local_error_msg;
	    reset_vif_register_source(local_error_msg);
	}
    }

    return XORP_OK;
}

int
ZebraPimNode::clear_register_source_config(string& error_msg)
{
    _register_source_vif_name.set(string());
    _register_source_vif_name.clear_applied();
    _register_source_vif_name.invalidate();

    return reset_vif_register_source(error_msg);
}

int
ZebraPimNode::add_static_membership(const string &vif_name, const IPvX &source,
				    const IPvX &group, string &error_msg)
{
    PimVif *vif = vif_find_by_name(vif_name);
    if (vif == NULL)
    {
	error_msg = "vif not found: " + vif_name;
	return XORP_ERROR;
    }

    if (add_membership(vif->vif_index(), source, group) != XORP_OK)
    {
	error_msg = "add_membership() failed for (" +
	    source.str() + "," + group.str() + ")";
	return XORP_ERROR;
    }

    return XORP_OK;
}

int
ZebraPimNode::delete_static_membership(const string &vif_name,
				       const IPvX &source, const IPvX &group,
				       string &error_msg)
{
    PimVif *vif = vif_find_by_name(vif_name);
    if (vif == NULL)
    {
	error_msg = "vif not found: " + vif_name;
	return XORP_ERROR;
    }

    if (delete_membership(vif->vif_index(), source, group) != XORP_OK)
    {
	error_msg = "delete_membership() failed for (" +
	    source.str() + "," + group.str() + ")";
	return XORP_ERROR;
    }

    return XORP_OK;
}

void
ZebraPimNode::init()
{
    zebra_client_init();
    PimNodeCli::enable();
    PimNodeCli::start();
}

void
ZebraPimNode::terminate()
{
    if (!_terminated) {
	if (_zebra_mfea_node.delete_allow_kernel_signal_messages(PimNode::module_name(),
								 PimNode::module_id()) != XORP_OK)
	    XLOG_ERROR("MfeaNode::delete_allow_kernel_signal_messages() failed");

	string error_msg;
	int r = stop(error_msg);
	if (r != XORP_OK)
	    XLOG_WARNING("stop failed: %s", error_msg.c_str());

	pim_bsr().clear();
	rp_table().clear();
	pim_mrt().clear();
	delete_all_vifs();

	PimNodeCli::stop();
	PimNodeCli::disable();

	zebra_client_terminate();

	_terminated = true;
    }
}

const char*
ZebraPimNode::zebra_ipstr() const
{
    if (PimNode::family() == AF_INET)
	return "ip";
    else if (PimNode::family() == AF_INET6)
	return "ipv6";
    else
	XLOG_UNREACHABLE();
}

const char*
ZebraPimNode::zebra_protostr() const
{
    if (PimNode::family() == AF_INET)
    {
	if (PimNode::proto_is_pimsm())
	    return "pim";
	else
	    XLOG_UNREACHABLE();
    }
    else if (PimNode::family() == AF_INET6)
    {
	if (PimNode::proto_is_pimsm())
	    return "pim6";
	else
	    XLOG_UNREACHABLE();
    }
    else
	XLOG_UNREACHABLE();
}

const char*
ZebraPimNode::xorp_protostr() const
{
    if (PimNode::family() == AF_INET)
	return "pim";
    else if (PimNode::family() == AF_INET6)
	return "pim6";
    else
	XLOG_UNREACHABLE();
}

//
// Initialize zebra stuff
//
void
ZebraPimNode::zebra_client_register()
{
    // we only care about route updates; we don't care about interface
    // information (that comes from the MFEA)

#define ADD_ZEBRA_ROUTER_CB(cbname, cbfunc)				\
    do {								\
	_zebra_router_node.add_ ## cbname ## _cb(callback(this,		\
							  &ZebraPimNode::cbfunc)); \
    } while (0)

    ADD_ZEBRA_ROUTER_CB(ipv4_rt_add, zebra_ipv4_route_add);
    ADD_ZEBRA_ROUTER_CB(ipv4_rt_del, zebra_ipv4_route_del);
#ifdef HAVE_IPV6_MULTICAST
    ADD_ZEBRA_ROUTER_CB(ipv6_rt_add, zebra_ipv6_route_add);
    ADD_ZEBRA_ROUTER_CB(ipv6_rt_del, zebra_ipv6_route_del);
#endif	// HAVE_IPV6_MULTICAST

#undef ADD_ZEBRA_ROUTER_CB

}

void
ZebraPimNode::zebra_client_unregister()
{
    // we only care about interface information; we don't care about
    // route updates

#define DEL_ZEBRA_ROUTER_CB(cbname, cbfunc)				\
    do {								\
	_zebra_router_node.del_ ## cbname ## _cb(callback(this,		\
							  &ZebraPimNode::cbfunc)); \
    } while (0)

    DEL_ZEBRA_ROUTER_CB(ipv4_rt_add, zebra_ipv4_route_add);
    DEL_ZEBRA_ROUTER_CB(ipv4_rt_del, zebra_ipv4_route_del);
#ifdef HAVE_IPV6_MULTICAST
    DEL_ZEBRA_ROUTER_CB(ipv6_rt_add, zebra_ipv6_route_add);
    DEL_ZEBRA_ROUTER_CB(ipv6_rt_del, zebra_ipv6_route_del);
#endif	// HAVE_IPV6_MULTICAST

#undef DEL_ZEBRA_ROUTER_CB

}

void
ZebraPimNode::zebra_ipv4_route_add(const struct prefix_ipv4 *p,
				   u_char numnexthop,
				   const struct in_addr *nexthop,
				   const u_int32_t *ifindex,
				   u_int32_t metric)
{
    if (p->family != PimNode::family())
	return;

    XLOG_ASSERT(p->family == AF_INET);

    IPvXNet dst_prefix(IPvX(p->family, (uint8_t *)&p->prefix.s_addr),
		       p->prefixlen);
    // ignore the default route
    if (!dst_prefix.is_valid())
	return;

    Mrib mrib(dst_prefix);
    mrib.set_metric(metric);
    mrib.set_metric_preference(0); // XXX

    const char *next_hop_vif_name = "";
    for (unsigned int i = 0; i < numnexthop; i++)
    {
	struct interface *ifp = if_lookup_by_index(ifindex[i]);
	if (ifp == NULL)
	{
	    XLOG_WARNING("unknown ifindex: %u", ifindex[i]);
	    continue;
	}

	next_hop_vif_name = ifp->name;
	PimVif *vif = vif_find_by_name(next_hop_vif_name);

	IPvX next_hop(AF_INET, (uint8_t *)&nexthop[i].s_addr);
	if (next_hop.is_zero())
	{
	    const IPvX &dst_addr = dst_prefix.masked_addr();
	    if (dst_prefix.prefix_len() == dst_addr.addr_bitlen() &&
		(vif == NULL || !vif->is_my_addr(dst_addr)))
	    {
		next_hop = dst_addr;
	    }
#if 0				// XXX
	    // this probably isn't needed, but it makes the mrib more like the
	    // native XORP case
	    else
	    {
		struct listnode *n;
		struct connected *c;

		for (ALL_LIST_ELEMENTS_RO(ifp->connected, n, c))
		    {
			if (c->address->family != AF_INET)
			    continue;
			// XXX are any other checks needed?

			next_hop = IPvX(AF_INET,
					(uint8_t *)&c->address->u.prefix4.s_addr);
			if (!next_hop.is_zero())
			    break;
		    }
	    }
#endif	// 0
	}

	mrib.set_next_hop_router_addr(next_hop);
	if (vif != NULL)
	    mrib.set_next_hop_vif_index(vif->vif_index());

	break;	       // XXX Only one next-hop is currently supported
    }

    PimMribTable& mrib_table = pim_mrib_table();
    mrib_table.add_pending_insert(0, mrib, next_hop_vif_name);
    mrib_table.commit_pending_transactions(0);
}

void
ZebraPimNode::zebra_ipv4_route_del(const struct prefix_ipv4 *p,
				   u_char numnexthop,
				   const struct in_addr *nexthop,
				   const u_int32_t *ifindex,
				   u_int32_t metric)
{
    if (p->family != PimNode::family())
	return;

    XLOG_ASSERT(p->family == AF_INET);

    IPvXNet dst_prefix(IPvX(p->family, (uint8_t *)&p->prefix.s_addr),
		       p->prefixlen);

    Mrib mrib(dst_prefix);
    mrib.set_metric(metric);
    mrib.set_metric_preference(0); // XXX

    for (unsigned int i = 0; i < numnexthop; i++)
    {
	struct interface *ifp = if_lookup_by_index(ifindex[i]);

	PimVif *vif = NULL;
	if (ifp != NULL)
	    vif = vif_find_by_name(ifp->name);
	if (vif == NULL)
	    vif = vif_find_by_pif_index(ifindex[i]);

	IPvX next_hop(AF_INET, (uint8_t *)&nexthop[i].s_addr);
	if (next_hop.is_zero())
	{
	    const IPvX &dst_addr = dst_prefix.masked_addr();
	    if (dst_prefix.prefix_len() == dst_addr.addr_bitlen() &&
		(vif == NULL || !vif->is_my_addr(dst_addr)))
	    {
		next_hop = dst_addr;
	    }
#if 0				// XXX
	    // this probably isn't needed, but it makes the mrib more like the
	    // native XORP case
	    else if (ifp != NULL)
	    {
		struct listnode *n;
		struct connected *c;

		for (ALL_LIST_ELEMENTS_RO(ifp->connected, n, c))
		{
		    if (c->address->family != AF_INET)
			continue;
		    // XXX are any other checks needed?

		    next_hop = IPvX(AF_INET,
				    (uint8_t *)&c->address->u.prefix4.s_addr);
		    if (!next_hop.is_zero())
			break;
		}
	    }
#endif	// 0
	}

	mrib.set_next_hop_router_addr(next_hop);
	if (vif != NULL)
	    mrib.set_next_hop_vif_index(vif->vif_index());

	break;	       // XXX Only one next-hop is currently supported
    }

    PimMribTable& mrib_table = pim_mrib_table();
    mrib_table.add_pending_remove(0, mrib);
    mrib_table.commit_pending_transactions(0);
}

#ifdef HAVE_IPV6_MULTICAST
void
ZebraPimNode::zebra_ipv6_route_add(const struct prefix_ipv6 *p,
				   u_char numnexthop,
				   const struct in6_addr *nexthop,
				   const u_int32_t *ifindex,
				   u_int32_t metric)
{
    if (p->family != PimNode::family())
	return;

    XLOG_ASSERT(p->family == AF_INET6);

    IPvXNet dst_prefix(IPvX(p->family, (uint8_t *)&p->prefix.s6_addr),
		       p->prefixlen);
    // ignore the default route
    if (!dst_prefix.is_valid())
	return;

    Mrib mrib(dst_prefix);
    mrib.set_metric(metric);
    mrib.set_metric_preference(0); // XXX

    const char *next_hop_vif_name = "";
    for (unsigned int i = 0; i < numnexthop; i++)
    {
	struct interface *ifp = if_lookup_by_index(ifindex[i]);
	if (ifp == NULL)
	{
	    XLOG_WARNING("unknown ifindex: %u", ifindex[i]);
	    continue;
	}

	next_hop_vif_name = ifp->name;
	PimVif *vif = vif_find_by_name(next_hop_vif_name);

	IPvX next_hop(AF_INET6, (uint8_t *)&nexthop[i].s6_addr);
	if (next_hop.is_zero())
	{
	    const IPvX &dst_addr = dst_prefix.masked_addr();
	    if (dst_prefix.prefix_len() == dst_addr.addr_bitlen() &&
		(vif == NULL || !vif->is_my_addr(dst_addr)))
	    {
		next_hop = dst_addr;
	    }
#if 0				// XXX
	    // this probably isn't needed, but it makes the mrib more like the
	    // native XORP case
	    else
	    {
		struct listnode *n;
		struct connected *c;

		for (ALL_LIST_ELEMENTS_RO(ifp->connected, n, c))
		    {
			if (c->address->family != AF_INET6)
			    continue;

			if (IN6_IS_ADDR_LINKLOCAL(&c->address->u.prefix6.s6_addr))
			    continue;

			if (IN6_IS_ADDR_SITELOCAL(&c->address->u.prefix6.s6_addr))
			    continue;

			// XXX are any other checks needed?

			next_hop = IPvX(AF_INET6,
					(uint8_t *)&c->address->u.prefix6.s6_addr);
			if (!next_hop.is_zero())
			    break;
		    }
	    }
#endif	// 0
	}

	mrib.set_next_hop_router_addr(next_hop);
	if (vif != NULL)
	    mrib.set_next_hop_vif_index(vif->vif_index());

	break;	       // XXX Only one next-hop is currently supported
    }

    PimMribTable& mrib_table = pim_mrib_table();
    mrib_table.add_pending_insert(0, mrib, next_hop_vif_name);
    mrib_table.commit_pending_transactions(0);
}

void
ZebraPimNode::zebra_ipv6_route_del(const struct prefix_ipv6 *p,
				   u_char numnexthop,
				   const struct in6_addr *nexthop,
				   const u_int32_t *ifindex,
				   u_int32_t metric)
{
    if (p->family != PimNode::family())
	return;

    XLOG_ASSERT(p->family == AF_INET6);

    IPvXNet dst_prefix(IPvX(p->family, (uint8_t *)&p->prefix.s6_addr),
		       p->prefixlen);

    Mrib mrib(dst_prefix);
    mrib.set_metric(metric);
    mrib.set_metric_preference(0); // XXX

    for (unsigned int i = 0; i < numnexthop; i++)
    {
	struct interface *ifp = if_lookup_by_index(ifindex[i]);

	PimVif *vif = NULL;
	if (ifp != NULL)
	    vif = vif_find_by_name(ifp->name);
	if (vif == NULL)
	    vif = vif_find_by_pif_index(ifindex[i]);

	IPvX next_hop(AF_INET6, (uint8_t *)&nexthop[i].s6_addr);
	if (next_hop.is_zero())
	{
	    const IPvX &dst_addr = dst_prefix.masked_addr();
	    if (dst_prefix.prefix_len() == dst_addr.addr_bitlen() &&
		(vif == NULL || !vif->is_my_addr(dst_addr)))
	    {
		next_hop = dst_addr;
	    }
#if 0				// XXX
	    // this probably isn't needed, but it makes the mrib more like the
	    // native XORP case
	    else if (ifp != NULL)
	    {
		struct listnode *n;
		struct connected *c;

		for (ALL_LIST_ELEMENTS_RO(ifp->connected, n, c))
		{
		    if (c->address->family != AF_INET6)
			continue;

		    if (IN6_IS_ADDR_LINKLOCAL(&c->address->u.prefix6.s6_addr))
			continue;

		    if (IN6_IS_ADDR_SITELOCAL(&c->address->u.prefix6.s6_addr))
			continue;

		    // XXX are any other checks needed?

		    next_hop = IPvX(AF_INET6,
				    (uint8_t *)&c->address->u.prefix6.s6_addr);
		    if (!next_hop.is_zero())
			break;
		}
	    }
#endif	// 0
	}

	mrib.set_next_hop_router_addr(next_hop);
	if (vif != NULL)
	    mrib.set_next_hop_vif_index(vif->vif_index());

	break;	       // XXX Only one next-hop is currently supported
    }

    PimMribTable& mrib_table = pim_mrib_table();
    mrib_table.add_pending_remove(0, mrib);
    mrib_table.commit_pending_transactions(0);
}
#endif	// HAVE_IPV6_MULTICAST

int
ZebraPimNode::proto_send(const string& dst_module_instance_name,
			 xorp_module_id dst_module_id, uint32_t vif_index,
			 const IPvX& src, const IPvX& dst,
			 int ip_ttl, int ip_tos, bool is_router_alert,
			 const uint8_t *sndbuf, size_t sndlen,
			 string& error_msg)
{
    return ZebraMfeaClient::proto_send(dst_module_instance_name, dst_module_id,
				       vif_index, src, dst, ip_ttl,
				       ip_tos, is_router_alert,
				       sndbuf, sndlen, error_msg);
}

int
ZebraPimNode::proto_recv(const string& src_module_instance_name,
			 xorp_module_id src_module_id,
			 uint32_t vif_index, const IPvX& src, const IPvX& dst,
			 int ip_ttl, int ip_tos, bool is_router_alert,
			 const uint8_t *rcvbuf, size_t rcvlen,
			 string& error_msg)
{
    return PimNode::proto_recv(src_module_instance_name, src_module_id,
			       vif_index, src, dst, ip_ttl, ip_tos,
			       is_router_alert, rcvbuf, rcvlen, error_msg);
}

int
ZebraPimNode::signal_message_recv(const string& src_module_instance_name,
				  xorp_module_id src_module_id,
				  int message_type, uint32_t vif_index,
				  const IPvX& src, const IPvX& dst,
				  const uint8_t *rcvbuf, size_t rcvlen)
{
    return PimNode::signal_message_recv(src_module_instance_name,
					src_module_id, message_type,
					vif_index, src, dst,
					rcvbuf, rcvlen);
}

int
ZebraPimNode::add_config_vif(const string& vif_name, uint32_t vif_index,
			     string& error_msg)
{
    return PimNode::add_config_vif(vif_name, vif_index, error_msg);
}

int
ZebraPimNode::delete_config_vif(const string& vif_name, string& error_msg)
{
    return PimNode::delete_config_vif(vif_name, error_msg);
}

int
ZebraPimNode::add_config_vif_addr(const string& vif_name, const IPvX& addr,
				  const IPvXNet& subnet, const IPvX& broadcast,
				  const IPvX& peer, string& error_msg)
{
    return PimNode::add_config_vif_addr(vif_name, addr, subnet,
					broadcast, peer, error_msg);
}

int
ZebraPimNode::delete_config_vif_addr(const string& vif_name, const IPvX& addr,
				     string& error_msg)
{
    return PimNode::delete_config_vif_addr(vif_name, addr, error_msg);
}

int
ZebraPimNode::set_config_vif_flags(const string& vif_name, bool is_pim_register,
				   bool is_p2p, bool is_loopback,
				   bool is_multicast, bool is_broadcast,
				   bool is_up, uint32_t mtu, string& error_msg)
{
    return PimNode::set_config_vif_flags(vif_name, is_pim_register,
					 is_p2p, is_loopback, is_multicast,
					 is_broadcast, is_up, mtu, error_msg);
}

int
ZebraPimNode::set_config_all_vifs_done(string& error_msg)
{
    return PimNode::set_config_all_vifs_done(error_msg);
}

int
ZebraPimNode::signal_dataflow_recv(const IPvX& source_addr,
				   const IPvX& group_addr,
				   uint32_t threshold_interval_sec,
				   uint32_t threshold_interval_usec,
				   uint32_t measured_interval_sec,
				   uint32_t measured_interval_usec,
				   uint32_t threshold_packets,
				   uint32_t threshold_bytes,
				   uint32_t measured_packets,
				   uint32_t measured_bytes,
				   bool is_threshold_in_packets,
				   bool is_threshold_in_bytes,
				   bool is_geq_upcall, bool is_leq_upcall)
{
    return pim_mrt().signal_dataflow_recv(source_addr, group_addr,
					  threshold_interval_sec,
					  threshold_interval_usec,
					  measured_interval_sec,
					  measured_interval_usec,
					  threshold_packets,
					  threshold_bytes,
					  measured_packets,
					  measured_bytes,
					  is_threshold_in_packets,
					  is_threshold_in_bytes,
					  is_geq_upcall, is_leq_upcall);
}

int
ZebraPimNode::add_membership(uint32_t vif_index, const IPvX& source,
			     const IPvX& group)
{
    return PimNode::add_membership(vif_index, source, group);
}

int
ZebraPimNode::delete_membership(uint32_t vif_index, const IPvX& source,
				const IPvX& group)
{
    return PimNode::delete_membership(vif_index, source, group);
}

int
ZebraPimNode::start_protocol_kernel_vif(uint32_t vif_index)
{
    return ZebraMfeaClient::start_protocol_kernel_vif(vif_index);
}

int
ZebraPimNode::stop_protocol_kernel_vif(uint32_t vif_index)
{
    return ZebraMfeaClient::stop_protocol_kernel_vif(vif_index);
}

int
ZebraPimNode::join_multicast_group(uint32_t vif_index, const IPvX& multicast_group)
{
    return ZebraMfeaClient::join_multicast_group(vif_index, multicast_group);
}

int
ZebraPimNode::leave_multicast_group(uint32_t vif_index,
				    const IPvX& multicast_group)
{
    return ZebraMfeaClient::leave_multicast_group(vif_index, multicast_group);
}

void
ZebraPimNode::mfea_register_startup()
{
    return ZebraMfeaClient::mfea_register_startup();
}

void
ZebraPimNode::mfea_register_shutdown()
{
    return ZebraMfeaClient::mfea_register_shutdown();
}

int
ZebraPimNode::add_mfc_to_kernel(const PimMfc& pim_mfc)
{
    return ZebraMfeaClient::add_mfc(pim_mfc.source_addr(),
				    pim_mfc.group_addr(),
				    pim_mfc.iif_vif_index(),
				    pim_mfc.olist(),
				    pim_mfc.olist_disable_wrongvif(),
				    pim_mfc.olist().size(),
				    pim_mfc.rp_addr());
}

int
ZebraPimNode::delete_mfc_from_kernel(const PimMfc& pim_mfc)
{
    return ZebraMfeaClient::delete_mfc(pim_mfc.source_addr(),
				       pim_mfc.group_addr());
}

int
ZebraPimNode::add_dataflow_monitor(const IPvX& source_addr,
				   const IPvX& group_addr,
				   uint32_t threshold_interval_sec,
				   uint32_t threshold_interval_usec,
				   uint32_t threshold_packets,
				   uint32_t threshold_bytes,
				   bool is_threshold_in_packets,
				   bool is_threshold_in_bytes,
				   bool is_geq_upcall,
				   bool is_leq_upcall,
				   bool rolling)
{
    return ZebraMfeaClient::add_dataflow_monitor(source_addr, group_addr,
						 threshold_interval_sec,
						 threshold_interval_usec,
						 threshold_packets,
						 threshold_bytes,
						 is_threshold_in_packets,
						 is_threshold_in_bytes,
						 is_geq_upcall,
						 is_leq_upcall,
						 rolling);
}

int
ZebraPimNode::delete_dataflow_monitor(const IPvX& source_addr,
				      const IPvX& group_addr,
				      uint32_t threshold_interval_sec,
				      uint32_t threshold_interval_usec,
				      uint32_t threshold_packets,
				      uint32_t threshold_bytes,
				      bool is_threshold_in_packets,
				      bool is_threshold_in_bytes,
				      bool is_geq_upcall,
				      bool is_leq_upcall,
				      bool rolling)
{
    return ZebraMfeaClient::delete_dataflow_monitor(source_addr, group_addr,
						    threshold_interval_sec,
						    threshold_interval_usec,
						    threshold_packets,
						    threshold_bytes,
						    is_threshold_in_packets,
						    is_threshold_in_bytes,
						    is_geq_upcall,
						    is_leq_upcall,
						    rolling);
}

int
ZebraPimNode::delete_all_dataflow_monitor(const IPvX& source_addr,
					  const IPvX& group_addr)
{
    return ZebraMfeaClient::delete_all_dataflow_monitor(source_addr,
							group_addr);
}

int
ZebraPimNode::add_protocol_mld6igmp(uint32_t vif_index)
{
    return ZebraMld6igmpClient::add_protocol_mld6igmp(vif_index);
}

int
ZebraPimNode::delete_protocol_mld6igmp(uint32_t vif_index)
{
    return ZebraMld6igmpClient::delete_protocol_mld6igmp(vif_index);
}

void
ZebraPimNode::apply_config(const string &vif_name)
{
    string error_msg;

    if (_if_config.count(vif_name))
    {
	ZebraPimVifConfig &config = _if_config[vif_name];

#define APPLY_CONFIG(func, configparam)					\
    do {								\
	if (!config.configparam.is_applied() && config.configparam.is_set()) \
	{								\
	    if (func(vif_name, config.configparam.get(), error_msg) != XORP_OK)	\
		XLOG_WARNING(#func "() failed: %s", error_msg.c_str());	\
	    else							\
		config.configparam.set_applied();			\
	}								\
    } while (0)

	APPLY_CONFIG(set_vif_proto_version, proto_version);
	APPLY_CONFIG(set_vif_passive, passive);
	APPLY_CONFIG(set_vif_ip_router_alert_option_check,
		     ip_router_alert_option_check);
	APPLY_CONFIG(set_vif_hello_triggered_delay, hello_triggered_delay);
	APPLY_CONFIG(set_vif_hello_period, hello_period);
	APPLY_CONFIG(set_vif_hello_holdtime, hello_holdtime);
	APPLY_CONFIG(set_vif_dr_priority, dr_priority);
	APPLY_CONFIG(set_vif_propagation_delay, propagation_delay);
	APPLY_CONFIG(set_vif_override_interval, override_interval);
	APPLY_CONFIG(set_vif_is_tracking_support_disabled,
		     is_tracking_support_disabled);
	APPLY_CONFIG(set_vif_accept_nohello_neighbors, accept_nohello_neighbors);
	APPLY_CONFIG(set_vif_join_prune_period, join_prune_period);

	for (set<ZebraConfigVal<IPvXNet> >::const_iterator it =
		 config.alternative_subnets.begin();
	     it != config.alternative_subnets.end(); ++it)
	{
	    const ZebraConfigVal<IPvXNet> &altsubnet = *it;
	    if (!altsubnet.is_applied())
	    {
		if (add_alternative_subnet(vif_name, altsubnet.get(),
					   error_msg) != XORP_OK)
		    XLOG_WARNING("add_alternative_subnet() failed: %s",
				 error_msg.c_str());
		else
		    altsubnet.set_applied();
	    }
	}

	for (set<ZebraConfigVal<ZebraStaticMembership> >::const_iterator it =
		 config.static_memberships.begin();
	     it != config.static_memberships.end(); ++it)
	{
	    const ZebraConfigVal<ZebraStaticMembership> &staticmbr = *it;
	    if (!staticmbr.is_applied())
	    {
		if (add_static_membership(vif_name, staticmbr.get().source(),
					  staticmbr.get().group(),
					  error_msg) != XORP_OK)
		    XLOG_WARNING("add_static_membership() failed: %s",
				 error_msg.c_str());
		else
		    staticmbr.set_applied();
	    }
	}

#undef APPLY_CONFIG

	if (!config.enabled.is_applied())
	{
	    if (config.enabled.is_set() && config.enabled.get())
	    {
		if (enable_vif(vif_name, error_msg) != XORP_OK)
		    XLOG_WARNING("couldn't enable interface %s: %s",
				 vif_name.c_str(), error_msg.c_str());
		else
		    config.enabled.set_applied();
	    }
	}

	// try to start the interface
	try_start_vif(vif_name);
    }

    if (_pending_rp_update)
    {
	if (config_static_rp_done(error_msg) != XORP_OK)
	    XLOG_WARNING("config_static_rp_done() failed: %s",
			 error_msg.c_str());
	else
	    _pending_rp_update = false;
    }

    bool canbsradded = false;
    for (set<ZebraBsrCandidateConfig>::const_iterator it = _cand_bsrs.begin();
	 it != _cand_bsrs.end(); ++it)
    {
	const ZebraBsrCandidateConfig &canbsrcfg = *it;

	if (canbsrcfg._vif_name != vif_name)
	    continue;

	if (vif_find_by_name(vif_name)->addr_ptr() == NULL)
	    continue;

	if (!canbsrcfg.is_applied())
	{
	    if (add_config_cand_bsr(canbsrcfg._scope_zone_id,
				    canbsrcfg._is_scope_zone,
				    canbsrcfg._vif_name, canbsrcfg._vif_addr,
				    canbsrcfg._bsr_priority,
				    canbsrcfg._hash_mask_len,
				    error_msg) != XORP_OK)
		XLOG_WARNING("add_config_cand_bsr() failed: %s",
			     error_msg.c_str());
	    else
	    {
		canbsrcfg.set_applied();
		canbsradded = true;
	    }
	}
    }

    if (canbsradded)
    {
	enable_bsr();
	if (stop_bsr() != XORP_OK)
	    XLOG_WARNING("stop_bsr() failed");
	if (start_bsr() != XORP_OK)
	    XLOG_ERROR("start_bsr() failed");
    }

    for (set<ZebraRpCandidateConfig>::const_iterator it = _cand_rps.begin();
	 it != _cand_rps.end(); ++it)
    {
	const ZebraRpCandidateConfig &canrpcfg = *it;

	if (canrpcfg._vif_name != vif_name)
	    continue;

	if (vif_find_by_name(vif_name)->addr_ptr() == NULL)
	    continue;

	if (!canrpcfg.is_applied())
	{
	    if (add_config_cand_rp(canrpcfg._group_prefix,
				   canrpcfg._is_scope_zone,
				   canrpcfg._vif_name, canrpcfg._vif_addr,
				   canrpcfg._rp_priority,
				   canrpcfg._rp_holdtime,
				   error_msg) != XORP_OK)
		XLOG_WARNING("add_config_cand_rp() failed: %s",
			     error_msg.c_str());
	    else
		canrpcfg.set_applied();
	}
    }

    if (!_register_source_vif_name.is_applied() &&
	_register_source_vif_name.is_set() &&
	_register_source_vif_name.get() == vif_name)
    {
	if (set_vif_register_source(_register_source_vif_name.get(),
				    error_msg) != XORP_OK)
	    XLOG_WARNING("set_vif_register_source() failed: %s",
			 error_msg.c_str());
	else
	    _register_source_vif_name.set_applied();
    }
}

void
ZebraPimNode::clear_config(const string &vif_name)
{
    if (_if_config.count(vif_name) == 0)
	return;

    ZebraPimVifConfig &config = _if_config[vif_name];

    config.clear_all_applied();
}

void
ZebraPimNode::check_static_rp(const string& vif_name)
{
    PimVif *vif = vif_find_by_name(vif_name);
    if (vif == NULL)
	return;

    bool rp_update = false;
    list<PimRp *>::const_iterator iter = rp_table().rp_list().begin();
    while (iter != rp_table().rp_list().end())
    {
	const PimRp *pim_rp = *iter;
	if (pim_rp->rp_learned_method() == PimRp::RP_LEARNED_METHOD_STATIC &&
	    vif->is_my_addr(pim_rp->rp_addr()) &&
	    !pim_rp->i_am_rp())
	{
	    IPvXNet group_prefix = pim_rp->group_prefix();
	    IPvX rp_addr = pim_rp->rp_addr();
	    uint8_t rp_priority = pim_rp->rp_priority();
	    uint8_t hash_mask_len = pim_rp->hash_mask_len();

	    // delete and re-add the RP to update everything
	    if (rp_table().delete_rp(rp_addr, group_prefix,
				     PimRp::RP_LEARNED_METHOD_STATIC) != XORP_OK)
		XLOG_WARNING("delete_rp() failed");
	    // re-add the RP to update everything
	    if (rp_table().add_rp(rp_addr, rp_priority,
				  group_prefix, hash_mask_len,
				  PimRp::RP_LEARNED_METHOD_STATIC) == NULL)
		XLOG_WARNING("add_rp() failed");

	    // remove the (*,*,RP) entry (do this after updating the RP)
	    PimMre *pim_mre;
	    pim_mre = pim_mrt().pim_mre_find(rp_addr,
					     IPvX::ZERO(PimNode::family()),
					     PIM_MRE_RP,
					     PIM_MRE_RP);
	    XLOG_ASSERT(pim_mre != NULL);
	    XLOG_ASSERT(pim_mre->i_am_rp() == false);
	    pim_mrt().remove_pim_mre(pim_mre);

	    rp_update = true;
	    iter = rp_table().rp_list().begin();
	}
	else
	{
	    ++iter;
	}
    }

    if (rp_update)
	rp_table().apply_rp_changes();
}
