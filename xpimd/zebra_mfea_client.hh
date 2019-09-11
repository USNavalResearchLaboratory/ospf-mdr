// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef __ZEBRA_MFEA_CLIENT_HH__
#define  __ZEBRA_MFEA_CLIENT_HH__

#include "zebra_mfea_client_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"

#include "libproto/proto_unit.hh"

#include "zebra_mfea_node.hh"


class ZebraMfeaClient {

public:

    ZebraMfeaClient(ProtoUnit &proto_unit, ZebraMfeaNode &zebra_mfea_node) :
	_proto_unit(proto_unit), _zebra_mfea_node(zebra_mfea_node)
    {
	if (zebra_mfea_node.add_client(proto_unit.module_name(), *this) !=
	    XORP_OK)
	    XLOG_ERROR("ZebraMfeaNode::add_client() failed");
    }
    virtual ~ZebraMfeaClient()
    {
	if (_zebra_mfea_node.delete_client(_proto_unit.module_name()) !=
	    XORP_OK)
	    XLOG_ERROR("ZebraMfeaNode::delete_client() failed");
    }

    EventLoop &eventloop() {return _zebra_mfea_node.eventloop();}

    // communication from a MfeaNode to a ZebraMfeaClient (receive methods)
    virtual int proto_recv(const string& src_module_instance_name,
			   xorp_module_id src_module_id,
			   uint32_t vif_index,
			   const IPvX& src,
			   const IPvX& dst,
			   int ip_ttl,
			   int ip_tos,
			   bool is_router_alert,
			   const uint8_t *rcvbuf,
			   size_t rcvlen,
			   string& error_msg) = 0;
    virtual int signal_message_recv(const string& src_module_instance_name,
				    xorp_module_id src_module_id,
				    int message_type,
				    uint32_t vif_index,
				    const IPvX& src,
				    const IPvX& dst,
				    const uint8_t *rcvbuf,
				    size_t rcvlen) = 0;
    virtual int add_config_vif(const string& vif_name, uint32_t vif_index,
			       string& error_msg) = 0;
    virtual int delete_config_vif(const string& vif_name,
				  string& error_msg) = 0;
    virtual int add_config_vif_addr(const string& vif_name, const IPvX& addr,
				    const IPvXNet& subnet,
				    const IPvX& broadcast, const IPvX& peer,
				    string& error_msg) = 0;
    virtual int delete_config_vif_addr(const string& vif_name,
				       const IPvX& addr,
				       string& error_msg) = 0;
    virtual int set_config_vif_flags(const string& vif_name,
				     bool is_pim_register, bool is_p2p,
				     bool is_loopback, bool is_multicast,
				     bool is_broadcast, bool is_up,
				     uint32_t mtu, string& error_msg) = 0;
    virtual int set_config_all_vifs_done(string& error_msg) = 0;
    virtual int signal_dataflow_recv(const IPvX& source_addr,
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
				     bool is_leq_upcall) = 0;

    // communication from a ZebraMfeaClient to a MfeaNode (send methods)
    int proto_send(const string& dst_module_instance_name,
		   xorp_module_id dst_module_id, uint32_t vif_index,
		   const IPvX& src, const IPvX& dst, int ip_ttl, int ip_tos,
		   bool is_router_alert, const uint8_t *sndbuf, size_t sndlen,
		   string& error_msg);
    int start_protocol_kernel_vif(uint32_t vif_index);
    int stop_protocol_kernel_vif(uint32_t vif_index);
    int join_multicast_group(uint32_t vif_index, const IPvX& multicast_group);
    int leave_multicast_group(uint32_t vif_index, const IPvX& multicast_group);
    int add_mfc(const IPvX& source, const IPvX& group,
		uint32_t iif_vif_index, const Mifset& oiflist,
		const Mifset& oiflist_disable_wrongvif,
		uint32_t max_vifs_oiflist,
		const IPvX& rp_addr);
    int delete_mfc(const IPvX& source, const IPvX& group);
    int add_dataflow_monitor(const IPvX& source, const IPvX& group,
			     uint32_t threshold_interval_sec,
			     uint32_t threshold_interval_usec,
			     uint32_t threshold_packets,
			     uint32_t threshold_bytes,
			     bool is_threshold_in_packets,
			     bool is_threshold_in_bytes,
			     bool is_geq_upcall,
			     bool is_leq_upcall,
			     bool rolling);
    int delete_dataflow_monitor(const IPvX& source, const IPvX& group,
				uint32_t threshold_interval_sec,
				uint32_t threshold_interval_usec,
				uint32_t threshold_packets,
				uint32_t threshold_bytes,
				bool is_threshold_in_packets,
				bool is_threshold_in_bytes,
				bool is_geq_upcall,
				bool is_leq_upcall,
				bool rolling);
    int delete_all_dataflow_monitor(const IPvX& source, const IPvX& group);

    // These aren't callbacks because protocols must register with the
    // MFEA before the MFEA learns about network interfaces in order
    // to get notified about them
    void mfea_register_startup()
    {
	if (_zebra_mfea_node.add_protocol(_proto_unit.module_name(),
					  _proto_unit.module_id()) != XORP_OK)
	    XLOG_ERROR("MfeaNode::add_protocol() failed");
    }
    void mfea_register_shutdown()
    {
	if (_zebra_mfea_node.delete_protocol(_proto_unit.module_name(),
					     _proto_unit.module_id()) !=
	    XORP_OK)
	    XLOG_ERROR("MfeaNode::delete_protocol() failed");
    }

protected:

    ProtoUnit &_proto_unit;
    ZebraMfeaNode &_zebra_mfea_node;
};

#endif	//  __ZEBRA_MFEA_CLIENT_HH__
