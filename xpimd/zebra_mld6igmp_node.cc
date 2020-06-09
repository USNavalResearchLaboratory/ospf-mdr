// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#include "zebra_mld6igmp_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"

#include "libxorp/ipvx.hh"
#include "mld6igmp/mld6igmp_vif.hh"

#include "zebra_mld6igmp_node.hh"
#include "zebra_mld6igmp_client.hh"
#include "zebra_mld6igmp_client_callback.hh"

extern "C" {
#include "if.h"
#include "prefix.h"
#include "command.h"
}


void
ZebraMld6igmpVifConfig::clear_all_applied() const
{
    enabled.clear_applied();
    proto_version.clear_applied();
    ip_router_alert_option_check.clear_applied();
    query_interval.clear_applied();
    query_last_member_interval.clear_applied();
    query_response_interval.clear_applied();
    robust_count.clear_applied();

    for (set<ZebraConfigVal<IPvXNet> >::const_iterator it =
	     alternative_subnets.begin();
	 it != alternative_subnets.end(); ++it)
    {
	const ZebraConfigVal<IPvXNet> &altsubnet = *it;
	altsubnet.clear_applied();
    }
}

ZebraMld6igmpNode::ZebraMld6igmpNode(int family, xorp_module_id module_id,
				     EventLoop &eventloop,
				     ZebraRouterNode &zebra_router_node,
				     ZebraMfeaNode &zebra_mfea_node) :
    Mld6igmpNode(family, module_id, eventloop),
    Mld6igmpNodeCli(*static_cast<Mld6igmpNode *>(this)),
    ZebraRouterClient(zebra_router_node),
    ZebraMfeaClient(*static_cast<Mld6igmpNode *>(this), zebra_mfea_node),
    _terminated(false)
{
}

ZebraMld6igmpNode::~ZebraMld6igmpNode()
{
    terminate();
}

int
ZebraMld6igmpNode::start (string& error_msg)
{
    if (!Mld6igmpNode::is_enabled())
	Mld6igmpNode::enable();

    if (!Mld6igmpNode::is_up() && !Mld6igmpNode::is_pending_up())
    {
	int r = Mld6igmpNode::start();
	if (r != XORP_OK)
	{
	    error_msg = "mld6igmp start failed";
	    return r;
	}

	r = Mld6igmpNode::final_start();
	if (r != XORP_OK)
	{
	    error_msg = "mld6igmp final_start failed";
	    return r;
	}
    }

    return XORP_OK;
}

int
ZebraMld6igmpNode::stop (string& error_msg)
{
    int r = XORP_OK;
    if (Mld6igmpNode::is_up())
    {
	r = Mld6igmpNode::stop();
	if (r != XORP_OK)
	    error_msg = "mld6igmp stop failed";

	// XXX
	// r = Mld6igmpNode::final_stop();
	// if (r != XORP_OK)
	// 	error_msg = "mld6igmp final_stop failed";
    }

    if (Mld6igmpNode::is_enabled())
	Mld6igmpNode::disable();

    return r;
}

int
ZebraMld6igmpNode::add_vif(const Vif& vif, string& error_msg)
{
    int r;

    if ((r = Mld6igmpNode::add_vif(vif, error_msg)) != XORP_OK)
	return r;

    apply_config(vif.name());

    return r;
}

int
ZebraMld6igmpNode::add_vif_addr(const string& vif_name, const IPvX& addr,
				const IPvXNet& subnet_addr,
				const IPvX& broadcast_addr,
				const IPvX& peer_addr, string& error_msg)
{
    int r;

    if ((r = Mld6igmpNode::add_vif_addr(vif_name, addr, subnet_addr,
					broadcast_addr, peer_addr,
					error_msg)) != XORP_OK)
	return r;

    apply_config(vif_name);

    return r;
}

int
ZebraMld6igmpNode::delete_vif(const string& vif_name, string& error_msg)
{
    int r = Mld6igmpNode::delete_vif(vif_name, error_msg);
    if (r != XORP_OK)
	return r;

    clear_config(vif_name);

    const struct interface *ifp = if_lookup_by_name (vif_name.c_str());
    if (ifp != NULL && if_is_transient(ifp))
	del_if_config(ifp->name);

    return r;
}

bool
ZebraMld6igmpNode::try_start_vif(const string& name)
{
    const Mld6igmpVif *vif = vif_find_by_name(name);

    if (vif == NULL)
	return false;
    if (vif->is_up())
	return false;
    if (!vif->is_underlying_vif_up())
	return false;
    if (!vif->is_enabled())
	return false;
    list<VifAddr>::const_iterator iter;
    for (iter = vif->addr_list().begin();
	 iter != vif->addr_list().end(); ++iter)
    {
	const IPvX &addr = iter->addr();
	if (addr.af() == Mld6igmpNode::family() && addr.is_unicast() &&
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

ZebraMld6igmpVifConfig &
ZebraMld6igmpNode::get_if_config(const string &name)
{
    return _if_config[name];
}

void
ZebraMld6igmpNode::del_if_config(const string &name)
{
    _if_config.erase(name);
}

void
ZebraMld6igmpNode::init()
{
    zebra_client_init();
    Mld6igmpNodeCli::enable();
    Mld6igmpNodeCli::start();
}

void
ZebraMld6igmpNode::terminate()
{
    if (!_terminated) {
	string error_msg;
	int r = stop(error_msg);
	if (r != XORP_OK)
	    XLOG_WARNING("stop failed: %s", error_msg.c_str());

	Mld6igmpNodeCli::stop();
	Mld6igmpNodeCli::disable();

	zebra_client_terminate();

	_terminated = true;
    }
}

const char*
ZebraMld6igmpNode::zebra_ipstr() const
{
    if (Mld6igmpNode::family() == AF_INET)
	return "ip";
    else if (Mld6igmpNode::family() == AF_INET6)
	return "ipv6";
    else
	XLOG_UNREACHABLE();
}

const char*
ZebraMld6igmpNode::zebra_protostr() const
{
    if (Mld6igmpNode::family() == AF_INET)
	return "igmp";
    else if (Mld6igmpNode::family() == AF_INET6)
	return "mld6";
    else
	XLOG_UNREACHABLE();
}

const char*
ZebraMld6igmpNode::xorp_protostr() const
{
    if (Mld6igmpNode::family() == AF_INET)
	return "igmp";
    else if (Mld6igmpNode::family() == AF_INET6)
	return "mld";
    else
	XLOG_UNREACHABLE();
}

void
ZebraMld6igmpNode::zebra_client_register()
{
    // we only care about route updates; we don't care about interface
    // information (that comes from the MFEA)

#define ADD_ZEBRA_ROUTER_CB(cbname, cbfunc)				\
    do {								\
	_zebra_router_node.add_ ## cbname ## _cb(callback(this,		\
							  &ZebraMld6igmpNode::cbfunc)); \
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
ZebraMld6igmpNode::zebra_client_unregister()
{
    // we only care about interface information; we don't care about
    // route updates

#define DEL_ZEBRA_ROUTER_CB(cbname, cbfunc)				\
    do {								\
	_zebra_router_node.del_ ## cbname ## _cb(callback(this,		\
							  &ZebraMld6igmpNode::cbfunc)); \
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
ZebraMld6igmpNode::zebra_ipv4_route_add(const struct prefix_ipv4 *p,
					u_char numnexthop,
					const struct in_addr *nexthop,
					const u_int32_t *ifindex,
					u_int32_t metric)
{
    if (p->family != Mld6igmpNode::family())
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

    for (unsigned int i = 0; i < numnexthop; i++)
    {
	struct interface *ifp = if_lookup_by_index(ifindex[i]);
	if (ifp == NULL)
	{
	    XLOG_WARNING("unknown ifindex: %u", ifindex[i]);
	    continue;
	}

	Mld6igmpVif *vif = vif_find_by_name(ifp->name);

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

    MribTable& table = mrib_table();
    table.add_pending_insert(0, mrib);
    table.commit_pending_transactions(0);
}

void
ZebraMld6igmpNode::zebra_ipv4_route_del(const struct prefix_ipv4 *p,
					u_char numnexthop,
					const struct in_addr *nexthop,
					const u_int32_t *ifindex,
					u_int32_t metric)
{
    if (p->family != Mld6igmpNode::family())
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

	Mld6igmpVif *vif = NULL;
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

    MribTable& table = mrib_table();
    table.add_pending_remove(0, mrib);
    table.commit_pending_transactions(0);
}

#ifdef HAVE_IPV6_MULTICAST
void
ZebraMld6igmpNode::zebra_ipv6_route_add(const struct prefix_ipv6 *p,
					u_char numnexthop,
					const struct in6_addr *nexthop,
					const u_int32_t *ifindex,
					u_int32_t metric)
{
    if (p->family != Mld6igmpNode::family())
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

    for (unsigned int i = 0; i < numnexthop; i++)
    {
	struct interface *ifp = if_lookup_by_index(ifindex[i]);
	if (ifp == NULL)
	{
	    XLOG_WARNING("unknown ifindex: %u", ifindex[i]);
	    continue;
	}

	Mld6igmpVif *vif = vif_find_by_name(ifp->name);

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

    MribTable& table = mrib_table();
    table.add_pending_insert(0, mrib);
    table.commit_pending_transactions(0);
}

void
ZebraMld6igmpNode::zebra_ipv6_route_del(const struct prefix_ipv6 *p,
					u_char numnexthop,
					const struct in6_addr *nexthop,
					const u_int32_t *ifindex,
					u_int32_t metric)
{
    if (p->family != Mld6igmpNode::family())
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

	Mld6igmpVif *vif = NULL;
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

    MribTable& table = mrib_table();
    table.add_pending_remove(0, mrib);
    table.commit_pending_transactions(0);
}
#endif	// HAVE_IPV6_MULTICAST

int
ZebraMld6igmpNode::proto_send(const string& dst_module_instance_name,
			      xorp_module_id dst_module_id, uint32_t vif_index,
			      const IPvX& src, const IPvX& dst,
			      int ip_ttl, int ip_tos, bool is_router_alert,
			      const uint8_t *sndbuf, size_t sndlen,
			      string& error_msg)
{
    return ZebraMfeaClient::proto_send(dst_module_instance_name,
				       dst_module_id, vif_index, src, dst,
				       ip_ttl, ip_tos, is_router_alert,
				       sndbuf, sndlen, error_msg);
}

int
ZebraMld6igmpNode::proto_recv(const string& src_module_instance_name,
			      xorp_module_id src_module_id, uint32_t vif_index,
			      const IPvX& src, const IPvX& dst, int ip_ttl,
			      int ip_tos, bool is_router_alert,
			      const uint8_t *rcvbuf, size_t rcvlen,
			      string& error_msg)
{
    return Mld6igmpNode::proto_recv(src_module_instance_name,
				    src_module_id, vif_index, src, dst,
				    ip_ttl, ip_tos, is_router_alert,
				    rcvbuf, rcvlen, error_msg);
}

int
ZebraMld6igmpNode::signal_message_recv(const string& src_module_instance_name,
				       xorp_module_id src_module_id,
				       int message_type, uint32_t vif_index,
				       const IPvX& src, const IPvX& dst,
				       const uint8_t *rcvbuf, size_t rcvlen)
{
    return Mld6igmpNode::signal_message_recv(src_module_instance_name,
					     src_module_id, message_type,
					     vif_index, src, dst,
					     rcvbuf, rcvlen);
}

int
ZebraMld6igmpNode::add_config_vif(const string& vif_name, uint32_t vif_index,
				  string& error_msg)
{
    return Mld6igmpNode::add_config_vif(vif_name, vif_index, error_msg);
}

int
ZebraMld6igmpNode::delete_config_vif(const string& vif_name, string& error_msg)
{
    return Mld6igmpNode::delete_config_vif(vif_name, error_msg);
}

int
ZebraMld6igmpNode::add_config_vif_addr(const string& vif_name, const IPvX& addr,
				       const IPvXNet& subnet,
				       const IPvX& broadcast,
				       const IPvX& peer, string& error_msg)
{
    return Mld6igmpNode::add_config_vif_addr(vif_name, addr, subnet,
					     broadcast, peer, error_msg);
}

int
ZebraMld6igmpNode::delete_config_vif_addr(const string& vif_name,
					  const IPvX& addr, string& error_msg)
{
    return Mld6igmpNode::delete_config_vif_addr(vif_name, addr, error_msg);
}

int
ZebraMld6igmpNode::set_config_vif_flags(const string& vif_name,
					bool is_pim_register, bool is_p2p,
					bool is_loopback, bool is_multicast,
					bool is_broadcast, bool is_up,
					uint32_t mtu, string& error_msg)
{
    return Mld6igmpNode::set_config_vif_flags(vif_name, is_pim_register,
					      is_p2p, is_loopback,
					      is_multicast, is_broadcast,
					      is_up, mtu, error_msg);
}

int
ZebraMld6igmpNode::set_config_all_vifs_done(string& error_msg)
{
    return Mld6igmpNode::set_config_all_vifs_done(error_msg);
}

int
ZebraMld6igmpNode::signal_dataflow_recv(const IPvX& source_addr,
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
    XLOG_UNREACHABLE();
}

int
ZebraMld6igmpNode::start_protocol_kernel_vif(uint32_t vif_index)
{
    return ZebraMfeaClient::start_protocol_kernel_vif(vif_index);
}

int
ZebraMld6igmpNode::stop_protocol_kernel_vif(uint32_t vif_index)
{
    return ZebraMfeaClient::stop_protocol_kernel_vif(vif_index);
}

int
ZebraMld6igmpNode::join_multicast_group(uint32_t vif_index,
					const IPvX& multicast_group)
{
    return ZebraMfeaClient::join_multicast_group(vif_index, multicast_group);
}

int
ZebraMld6igmpNode::leave_multicast_group(uint32_t vif_index,
					 const IPvX& multicast_group)
{
    return ZebraMfeaClient::leave_multicast_group(vif_index, multicast_group);
}

void
ZebraMld6igmpNode::mfea_register_startup()
{
    return ZebraMfeaClient::mfea_register_startup();
}

void
ZebraMld6igmpNode::mfea_register_shutdown()
{
    return ZebraMfeaClient::mfea_register_shutdown();
}

#define MLD6IGMP_CLIENT_CALLBACK(cbclass, funcname, args...)		\
  do {									\
      string error_msg;							\
      ZebraMld6igmpClient *mld6igmp_client =				\
	  find_client(dst_module_instance_name, dst_module_id, error_msg); \
      if (mld6igmp_client == NULL)					\
      {									\
	  return XORP_ERROR;						\
      }									\
      new cbclass(*mld6igmp_client, &ZebraMld6igmpClient::funcname,	\
		  ## args);						\
      return XORP_OK;							\
  } while (0)

int
ZebraMld6igmpNode::send_add_membership(const string& dst_module_instance_name,
				       xorp_module_id dst_module_id,
				       uint32_t vif_index,
				       const IPvX& source,
				       const IPvX& group)
{
    MLD6IGMP_CLIENT_CALLBACK(Mld6igmpClientAddMembershipCallback,
			     add_membership, vif_index, source, group);
}

int
ZebraMld6igmpNode::send_delete_membership(const string& dst_module_instance_name,
					  xorp_module_id dst_module_id,
					  uint32_t vif_index,
					  const IPvX& source,
					  const IPvX& group)
{
    MLD6IGMP_CLIENT_CALLBACK(Mld6igmpClientDeleteMembershipCallback,
			     delete_membership, vif_index, source, group);
}

#undef MLD6IGMP_CLIENT_CALLBACK

void
ZebraMld6igmpNode::apply_config(const string &vif_name)
{
    if (_if_config.count(vif_name) == 0)
	return;

    ZebraMld6igmpVifConfig &config = _if_config[vif_name];

    string error_msg;

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
    APPLY_CONFIG(set_vif_ip_router_alert_option_check,
		 ip_router_alert_option_check);
    APPLY_CONFIG(set_vif_query_interval, query_interval);
    APPLY_CONFIG(set_vif_query_last_member_interval,
		 query_last_member_interval);
    APPLY_CONFIG(set_vif_query_response_interval, query_response_interval);
    APPLY_CONFIG(set_vif_robust_count, robust_count);

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

void
ZebraMld6igmpNode::clear_config(const string &vif_name)
{
    if (_if_config.count(vif_name) == 0)
	return;

    ZebraMld6igmpVifConfig &config = _if_config[vif_name];

    config.clear_all_applied();
}
