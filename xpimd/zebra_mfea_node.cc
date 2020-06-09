// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#include "zebra_mfea_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"

#include "libxorp/ipvx.hh"
#include "fea/mfea_vif.hh"

#include "zebra_mfea_node.hh"
#include "zebra_mfea_client.hh"
#include "zebra_mfea_client_callback.hh"

extern "C" {
#include "if.h"
#include "prefix.h"
#include "command.h"
}


void
ZebraMfeaVifConfig::clear_all_applied() const
{
    enabled.clear_applied();
}

ZebraMfeaNode::ZebraMfeaNode(int family, xorp_module_id module_id,
			     EventLoop &eventloop,
			     ZebraRouterNode &zebra_router_node) :
    MfeaNode(family, module_id, eventloop),
    MfeaNodeCli(*static_cast<MfeaNode *>(this)),
    ZebraRouterClient(zebra_router_node),
    _terminated(false)
{
}

ZebraMfeaNode::~ZebraMfeaNode()
{
    terminate();
}

int
ZebraMfeaNode::start(string& error_msg)
{
    if (!MfeaNode::is_enabled())
	MfeaNode::enable();

    if (!MfeaNode::is_up() && !MfeaNode::is_pending_up())
    {
	int r = MfeaNode::start();
	if (r != XORP_OK)
	{
	    error_msg = "mfea start failed";
	    return r;
	}

	r = MfeaNode::final_start();
	if (r != XORP_OK)
	{
	    error_msg = "mfea final_start failed";
	    return r;
	}
    }

    return XORP_OK;
}

int
ZebraMfeaNode::stop(string& error_msg)
{
    int r = XORP_OK;
    if (MfeaNode::is_up())
    {
	r = MfeaNode::stop();
	if (r != XORP_OK)
	    error_msg = "mfea stop failed";

	// XXX
	// r = MfeaNode::final_stop();
	// if (r != XORP_OK)
	// 	error_msg = "mfea final_stop failed";
    }

    if (MfeaNode::is_enabled())
	MfeaNode::disable();

    return r;
}

int
ZebraMfeaNode::add_vif(const Vif& vif, string& error_msg)
{
    int r;

    if ((r = MfeaNode::add_vif(vif, error_msg)) != XORP_OK)
	return r;

    if (vif.is_pim_register())
    {
	string error_msg;
	if (enable_vif(vif.name(), error_msg) != XORP_OK)
	    XLOG_ERROR("enable_vif() failed: %s", error_msg.c_str());
	if (start_vif(vif.name(), error_msg, false) != XORP_OK)
	    XLOG_ERROR("start_vif() failed: %s", error_msg.c_str());
    }

    return r;
}

bool
ZebraMfeaNode::try_start_vif(const string& name)
{
    MfeaVif *vif = vif_find_by_name(name);

    if (vif == NULL)
	return false;
    if (vif->is_up())
	return false;
    if (!vif->is_underlying_vif_up())
	return false;
    if (!vif->is_enabled())
	return false;
    if (vif->addr_ptr() == NULL)
	return false;

    string error_msg;
    if (start_vif(name, error_msg, true) != XORP_OK)
    {
	XLOG_ERROR("start_vif() failed: %s", error_msg.c_str());
	return false;
    }

    return true;
}

ZebraMfeaVifConfig &
ZebraMfeaNode::get_if_config(const string &name)
{
    return _if_config[name];
}

void
ZebraMfeaNode::del_if_config(const string &name)
{
    _if_config.erase(name);
}

void
ZebraMfeaNode::init()
{
    zebra_client_init();
    MfeaNodeCli::enable();
    MfeaNodeCli::start();
}

void
ZebraMfeaNode::terminate()
{
    if (!_terminated) {
	string error_msg;
	int r = stop(error_msg);
	if (r != XORP_OK)
	    XLOG_WARNING("stop failed: %s", error_msg.c_str());

	MfeaNodeCli::stop();
	MfeaNodeCli::disable();

	zebra_client_terminate();

	_terminated = true;
    }
}

const char*
ZebraMfeaNode::zebra_ipstr() const
{
    if (MfeaNode::family() == AF_INET)
	return "ip";
    else if (MfeaNode::family() == AF_INET6)
	return "ipv6";
    else
	XLOG_UNREACHABLE();
}

const char*
ZebraMfeaNode::zebra_protostr() const
{
    if (MfeaNode::family() == AF_INET)
	return "mfea";
    else if (MfeaNode::family() == AF_INET6)
	return "mfea6";
    else
	XLOG_UNREACHABLE();
}

//
// Initialize zebra stuff
//
void
ZebraMfeaNode::zebra_client_register()
{
    // we only care about interface information; we don't care about
    // route updates

#define ADD_ZEBRA_ROUTER_CB(cbname, cbfunc)				\
    do {								\
	_zebra_router_node.add_ ## cbname ## _cb(callback(this,		\
							  &ZebraMfeaNode::cbfunc)); \
    } while (0)

    ADD_ZEBRA_ROUTER_CB(if_add, zebra_if_add);
    ADD_ZEBRA_ROUTER_CB(if_del, zebra_if_del);
    ADD_ZEBRA_ROUTER_CB(if_up, zebra_if_up);
    ADD_ZEBRA_ROUTER_CB(if_down, zebra_if_down);
    ADD_ZEBRA_ROUTER_CB(if_addr_add, zebra_if_addr_add);
    ADD_ZEBRA_ROUTER_CB(if_addr_del, zebra_if_addr_del);

#undef ADD_ZEBRA_ROUTER_CB
}

void
ZebraMfeaNode::zebra_client_unregister()
{
    // we only care about interface information; we don't care about
    // route updates

#define DEL_ZEBRA_ROUTER_CB(cbname, cbfunc)				\
    do {								\
	_zebra_router_node.del_ ## cbname ## _cb(callback(this,		\
							  &ZebraMfeaNode::cbfunc)); \
    } while (0)

    DEL_ZEBRA_ROUTER_CB(if_add, zebra_if_add);
    DEL_ZEBRA_ROUTER_CB(if_del, zebra_if_del);
    DEL_ZEBRA_ROUTER_CB(if_up, zebra_if_up);
    DEL_ZEBRA_ROUTER_CB(if_down, zebra_if_down);
    DEL_ZEBRA_ROUTER_CB(if_addr_add, zebra_if_addr_add);
    DEL_ZEBRA_ROUTER_CB(if_addr_del, zebra_if_addr_del);

#undef DEL_ZEBRA_ROUTER_CB

}

void
ZebraMfeaNode::zebra_if_add(const struct interface *ifp)
{
    string error_msg;

    // create a new vif if needed
    if (vif_find_by_name(ifp->name) == NULL)
    {
	Vif vif(ifp->name, ifp->name);
	uint32_t vif_index = find_unused_config_vif_index();
	XLOG_ASSERT(vif_index != Vif::VIF_INDEX_INVALID);
	vif.set_vif_index(vif_index);
	if (add_config_vif(vif, error_msg) != XORP_OK)
	    XLOG_ERROR("add_config_vif() failed: %s", error_msg.c_str());
    }

    if (set_config_pif_index(ifp->name, ifp->ifindex, error_msg) != XORP_OK)
	XLOG_ERROR("set_config_pif_index() failed: %s", error_msg.c_str());

    if (set_config_vif_flags(ifp->name, false, if_is_pointopoint(ifp),
			     if_is_loopback(ifp), if_is_multicast(ifp),
			     if_is_broadcast(ifp), if_is_operative(ifp),
			     ifp->mtu, error_msg) != XORP_OK)
	XLOG_ERROR("set_config_vif_flags() failed: %s", error_msg.c_str());

    if (set_config_all_vifs_done(error_msg) != XORP_OK)
	XLOG_ERROR("set_config_all_vifs_done() failed: %s",
		   error_msg.c_str());

    apply_config(ifp->name);
}

void
ZebraMfeaNode::zebra_if_del(const struct interface *ifp)
{
    // XXX should the vif be stopped first?

    string error_msg;
    if (delete_config_vif(ifp->name, error_msg) != XORP_OK)
	XLOG_ERROR("delete_config_vif() failed: %s", error_msg.c_str());

    if (set_config_all_vifs_done(error_msg) != XORP_OK)
	XLOG_ERROR("set_config_all_vifs_done() failed: %s", error_msg.c_str());

    clear_config(ifp->name);

    if (if_is_transient(ifp))
	del_if_config(ifp->name);
}

void
ZebraMfeaNode::zebra_if_up(const struct interface *ifp)
{
    if (!if_is_operative(ifp))
    {
	XLOG_ERROR("%s: interface %s is not really up", __func__, ifp->name);
	return;
    }
    zebra_if_add(ifp);

#if 0				// XXX

    MfeaVif *vif = vif_find_by_name(ifp->name);

    if (vif == NULL)
    {
	XLOG_ERROR("unknown interface: %s", ifp->name);
	return;
    }

    /* XXX can this be done directly?
       vif->set_underlying_vif_up(true);
    */

    string error_msg;
    if (set_config_vif_flags(ifp->name, false, vif->is_p2p(),
			     vif->is_loopback(), vif->is_multicast_capable(),
			     vif->is_broadcast_capable(), true, vif->mtu(),
			     error_msg) != XORP_OK)
	XLOG_ERROR("set_config_vif_flags() failed: %s", error_msg.c_str());

    if (set_config_all_vifs_done(error_msg) != XORP_OK)
	XLOG_ERROR("set_config_all_vifs_done() failed: %s", error_msg.c_str());

#endif	// 0
}

void
ZebraMfeaNode::zebra_if_down(const struct interface *ifp)
{
    XLOG_ASSERT(!if_is_operative(ifp));
    zebra_if_add(ifp);

#if 0				// XXX

    MfeaVif *vif = vif_find_by_name(ifp->name);

    if (vif == NULL)
    {
	XLOG_ERROR("unknown interface: %s", ifp->name);
	return;
    }

    /* XXX can this be done directly?
       vif->set_underlying_vif_up(false);
    */

    string error_msg;
    if (set_config_vif_flags(ifp->name, false, vif->is_p2p(),
			     vif->is_loopback(), vif->is_multicast_capable(),
			     vif->is_broadcast_capable(), false, vif->mtu(),
			     error_msg) != XORP_OK)
	XLOG_ERROR("set_config_vif_flags() failed: %s", error_msg.c_str());

    if (set_config_all_vifs_done(error_msg) != XORP_OK)
	XLOG_ERROR("set_config_all_vifs_done() failed: %s", error_msg.c_str());

#endif
}

void
ZebraMfeaNode::zebra_if_addr_add(const struct connected *c)
{
    if (c->address->family != MfeaNode::family())
	return;

    MfeaVif *mvif = vif_find_by_name(c->ifp->name);
    if (mvif == NULL)
    {
	XLOG_ERROR("can't add address to %s: unknown interface", c->ifp->name);
	return;
    }

    IPvX addr(c->address->family, &c->address->u.prefix);

    // check if the vif already has this address
    // XXX what if it has the address but subnet, broadcast, or peer
    // have changed?
    if (mvif->find_address(addr) != NULL)
	return;

    IPvXNet subnet(addr, c->address->prefixlen);
    IPvX broadcast(c->address->family);
    if (!CONNECTED_PEER(c) && c->destination)
	broadcast.copy_in(c->destination->family, &c->destination->u.prefix);
    IPvX peer(c->address->family);
    if (CONNECTED_PEER(c))
	peer.copy_in(c->destination->family, &c->destination->u.prefix);

    string error_msg;
    if (add_config_vif_addr(c->ifp->name, addr, subnet, broadcast,
			    peer, error_msg) != XORP_OK)
	XLOG_ERROR("add_config_vif_addr() failed: %s", error_msg.c_str());

    if (set_config_all_vifs_done(error_msg) != XORP_OK)
	XLOG_ERROR("set_config_all_vifs_done() failed: %s", error_msg.c_str());

    apply_config(c->ifp->name);
}

void
ZebraMfeaNode::zebra_if_addr_del(const struct connected *c)
{
    if (c->address->family != MfeaNode::family())
	return;

    IPvX addr(c->address->family, &c->address->u.prefix);

    string error_msg;
    if (delete_config_vif_addr(c->ifp->name, addr, error_msg) != XORP_OK)
	XLOG_ERROR("delete_config_vif_addr() failed: %s", error_msg.c_str());

    if (set_config_all_vifs_done(error_msg) != XORP_OK)
	XLOG_ERROR("set_config_all_vifs_done() failed: %s", error_msg.c_str());
}

void
ZebraMfeaNode::apply_config(const string &vif_name)
{
    if (_if_config.count(vif_name) == 0)
	return;

    ZebraMfeaVifConfig &config = _if_config[vif_name];

    if (!config.enabled.is_applied())
    {
	if (config.enabled.is_set() && config.enabled.get())
	{
	    string error_msg;
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
ZebraMfeaNode::clear_config(const string &vif_name)
{
    if (_if_config.count(vif_name) == 0)
	return;

    ZebraMfeaVifConfig &config = _if_config[vif_name];

    config.clear_all_applied();
}

#define MFEA_CLIENT_CALLBACK(cbclass, funcname, args...)		\
  do {									\
      ZebraMfeaClient *mfea_client =					\
	  find_client(dst_module_instance_name, dst_module_id, error_msg); \
      if (mfea_client == NULL)						\
      {									\
	  XLOG_UNREACHABLE();	/* XXX FOR DEBUGGING */			\
	  return XORP_ERROR;						\
      }									\
      new cbclass(*mfea_client, &ZebraMfeaClient::funcname, ## args);	\
      return XORP_OK;							\
  } while (0)

int
ZebraMfeaNode::proto_send(const string& dst_module_instance_name,
			  xorp_module_id dst_module_id, uint32_t vif_index,
			  const IPvX& src, const IPvX& dst,
			  int ip_ttl, int ip_tos, bool is_router_alert,
			  const uint8_t *sndbuf, size_t sndlen,
			  string& error_msg)
{
    MFEA_CLIENT_CALLBACK(MfeaClientProtoRecvCallback, proto_recv,
			 MfeaNode::module_name(), MfeaNode::module_id(),
			 vif_index, src, dst, ip_ttl, ip_tos, is_router_alert,
			 sndbuf, sndlen);
}

int
ZebraMfeaNode::signal_message_send(const string& dst_module_instance_name,
				   xorp_module_id dst_module_id,
				   int message_type, uint32_t vif_index,
				   const IPvX& src, const IPvX& dst,
				   const uint8_t *sndbuf, size_t sndlen)
{
    string error_msg;
    MFEA_CLIENT_CALLBACK(MfeaClientSignalMessageRecvCallback,
			 signal_message_recv, MfeaNode::module_name(),
			 MfeaNode::module_id(), message_type, vif_index,
			 src, dst, sndbuf, sndlen);
}

int
ZebraMfeaNode::send_add_config_vif(const string& dst_module_instance_name,
				   xorp_module_id dst_module_id,
				   const string& vif_name,
				   uint32_t vif_index)
{
    string error_msg;
    MFEA_CLIENT_CALLBACK(MfeaClientAddConfigVifCallback, add_config_vif,
			 vif_name, vif_index);
}

int
ZebraMfeaNode::send_delete_config_vif(const string& dst_module_instance_name,
				      xorp_module_id dst_module_id,
				      const string& vif_name)
{
    string error_msg;
    MFEA_CLIENT_CALLBACK(MfeaClientDeleteConfigVifCallback,
			 delete_config_vif, vif_name);
}

int
ZebraMfeaNode::send_add_config_vif_addr(const string& dst_module_instance_name,
					xorp_module_id dst_module_id,
					const string& vif_name,
					const IPvX& addr,
					const IPvXNet& subnet,
					const IPvX& broadcast,
					const IPvX& peer)
{
    string error_msg;
    MFEA_CLIENT_CALLBACK(MfeaClientAddConfigVifAddrCallback,
			 add_config_vif_addr, vif_name, addr, subnet,
			 broadcast, peer);
}

int
ZebraMfeaNode::send_delete_config_vif_addr(const string& dst_module_instance_name,
					   xorp_module_id dst_module_id,
					   const string& vif_name,
					   const IPvX& addr)
{
    string error_msg;
    MFEA_CLIENT_CALLBACK(MfeaClientDeleteConfigVifAddrCallback,
			 delete_config_vif_addr, vif_name, addr);
}

int
ZebraMfeaNode::send_set_config_vif_flags(const string& dst_module_instance_name,
					 xorp_module_id dst_module_id,
					 const string& vif_name,
					 bool is_pim_register,
					 bool is_p2p,
					 bool is_loopback,
					 bool is_multicast,
					 bool is_broadcast,
					 bool is_up,
					 uint32_t mtu)
{
    string error_msg;
    MFEA_CLIENT_CALLBACK(MfeaClientSetConfigVifFlagsCallback,
			 set_config_vif_flags, vif_name, is_pim_register,
			 is_p2p, is_loopback, is_multicast, is_broadcast,
			 is_up, mtu);
}

int
ZebraMfeaNode::send_set_config_all_vifs_done(const string& dst_module_instance_name,
					     xorp_module_id dst_module_id)
{
    string error_msg;
    MFEA_CLIENT_CALLBACK(MfeaClientSetConfigAllVifsDoneCallback,
			 set_config_all_vifs_done);
}

int
ZebraMfeaNode::dataflow_signal_send(const string& dst_module_instance_name,
				    xorp_module_id dst_module_id,
				    const IPvX& source_addr,
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
				    bool is_geq_upcall,
				    bool is_leq_upcall)
{
    string error_msg;
    MFEA_CLIENT_CALLBACK(MfeaClientSignalDataflowRecvCallback,
			 signal_dataflow_recv, source_addr, group_addr,
			 threshold_interval_sec, threshold_interval_usec,
			 measured_interval_sec, measured_interval_usec,
			 threshold_packets, threshold_bytes,
			 measured_packets, measured_bytes,
			 is_threshold_in_packets, is_threshold_in_bytes,
			 is_geq_upcall, is_leq_upcall);
}

#undef MFEA_CLIENT_CALLBACK

int
ZebraMfeaNode::raise_privileges()
{
    return ZebraRouterClient::raise_privileges();
}

int
ZebraMfeaNode::lower_privileges()
{
    return ZebraRouterClient::lower_privileges();
}
