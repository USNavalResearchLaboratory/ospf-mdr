// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef __ZEBRA_MFEA_CLIENT_CALLBACK_HH__
#define  __ZEBRA_MFEA_CLIENT_CALLBACK_HH__

#include "fea/mfea_node.hh"

#include "zebra_ipc_callback.hh"
#include "zebra_mfea_client.hh"


// classes for callbacks from a MfeaNode to ZebraMfeaClient methods

class MfeaClientProtoRecvCallback :
    public ZebraIpcCallbackError<ZebraMfeaClient,
				 typeof(&ZebraMfeaClient::proto_recv)> {
public:
    MfeaClientProtoRecvCallback(ZebraMfeaClient &mfea_client,
				typeof(&ZebraMfeaClient::proto_recv) method,
				const string &module_name,
				xorp_module_id module_id,
				uint32_t vif_index, const IPvX& src,
				const IPvX& dst, int ip_ttl, int ip_tos,
				bool is_router_alert, const uint8_t *rcvbuf,
				size_t rcvlen) :
	ZebraIpcCallbackError<ZebraMfeaClient,
			      typeof(&ZebraMfeaClient::proto_recv)>(mfea_client,
								    method,
								    mfea_client.eventloop()),
	_module_name(module_name), _module_id(module_id),
	_vif_index(vif_index), _src(src), _dst(dst),
	_ip_ttl(ip_ttl), _ip_tos(ip_tos),
	_is_router_alert(is_router_alert), _rcvlen(rcvlen)
    {
	_rcvbuf = new uint8_t[rcvlen];
	memcpy(_rcvbuf, rcvbuf, rcvlen);
    }
    virtual ~MfeaClientProtoRecvCallback()
    {
	delete[] _rcvbuf;
    }

    virtual void dispatch(string &error_msg)
    {
	(_node.*_method)(_module_name, _module_id, _vif_index,
			 _src, _dst, _ip_ttl, _ip_tos,
			 _is_router_alert, _rcvbuf, _rcvlen,
			 error_msg);
    }

protected:
    const string _module_name;
    const xorp_module_id _module_id;
    const uint32_t _vif_index;
    const IPvX _src;
    const IPvX _dst;
    const int _ip_ttl;
    const int _ip_tos;
    const bool _is_router_alert;
    uint8_t *_rcvbuf;
    const size_t _rcvlen;
};

class MfeaClientSignalMessageRecvCallback :
    public ZebraIpcCallback<ZebraMfeaClient,
			    typeof(&ZebraMfeaClient::signal_message_recv)> {
public:
    MfeaClientSignalMessageRecvCallback(ZebraMfeaClient &mfea_client,
					typeof(&ZebraMfeaClient::signal_message_recv) method,
					const string &module_name,
					xorp_module_id module_id,
					int message_type, uint32_t vif_index,
					const IPvX& src, const IPvX& dst,
					const uint8_t *rcvbuf, size_t rcvlen) :
	ZebraIpcCallback<ZebraMfeaClient,
			 typeof(&ZebraMfeaClient::signal_message_recv)>(mfea_client,
									method,
									mfea_client.eventloop()),
	_module_name(module_name), _module_id(module_id),
	_message_type(message_type), _vif_index(vif_index),
	_src(src), _dst(dst), _rcvlen(rcvlen)
    {
	_rcvbuf = new uint8_t[rcvlen];
	memcpy(_rcvbuf, rcvbuf, rcvlen);
    }
    virtual ~MfeaClientSignalMessageRecvCallback()
    {
	delete[] _rcvbuf;
    }

    virtual void dispatch()
    {
	(_node.*_method)(_module_name, _module_id, _message_type,
			 _vif_index, _src, _dst, _rcvbuf, _rcvlen);
    }

protected:
    const string _module_name;
    const xorp_module_id _module_id;
    const int _message_type;
    const uint32_t _vif_index;
    const IPvX _src;
    const IPvX _dst;
    uint8_t *_rcvbuf;
    const size_t _rcvlen;
};

class MfeaClientAddConfigVifCallback :
    public ZebraIpcCallbackError<ZebraMfeaClient,
				 typeof(&ZebraMfeaClient::add_config_vif)> {
public:
    MfeaClientAddConfigVifCallback(ZebraMfeaClient &mfea_client,
				   typeof(&ZebraMfeaClient::add_config_vif) method,
				   const string& vif_name, uint32_t vif_index) :
	ZebraIpcCallbackError<ZebraMfeaClient,
			      typeof(&ZebraMfeaClient::add_config_vif)>(mfea_client,
									method,
									mfea_client.eventloop()),
	_vif_name(vif_name), _vif_index(vif_index) {}

    virtual void dispatch(string &error_msg)
    {
	(_node.*_method)(_vif_name, _vif_index, error_msg);
    }

protected:
    const string _vif_name;
    const uint32_t _vif_index;
};

class MfeaClientDeleteConfigVifCallback :
    public ZebraIpcCallbackError<ZebraMfeaClient,
				 typeof(&ZebraMfeaClient::delete_config_vif)> {
public:
    MfeaClientDeleteConfigVifCallback(ZebraMfeaClient &mfea_client,
				      typeof(&ZebraMfeaClient::delete_config_vif) method,
				      const string& vif_name) :
	ZebraIpcCallbackError<ZebraMfeaClient,
			      typeof(&ZebraMfeaClient::delete_config_vif)>(mfea_client,
									   method,
									   mfea_client.eventloop()),
	_vif_name(vif_name) {}

    virtual void dispatch(string &error_msg)
    {
	(_node.*_method)(_vif_name, error_msg);
    }

protected:
    const string _vif_name;
};

class MfeaClientAddConfigVifAddrCallback :
    public ZebraIpcCallbackError<ZebraMfeaClient,
				 typeof(&ZebraMfeaClient::add_config_vif_addr)> {
public:
    MfeaClientAddConfigVifAddrCallback(ZebraMfeaClient &mfea_client,
				       typeof(&ZebraMfeaClient::add_config_vif_addr) method,
				       const string& vif_name,
				       const IPvX& addr,
				       const IPvXNet& subnet,
				       const IPvX& broadcast,
				       const IPvX& peer) :
	ZebraIpcCallbackError<ZebraMfeaClient,
			      typeof(&ZebraMfeaClient::add_config_vif_addr)>(mfea_client,
									     method,
									     mfea_client.eventloop()),
	_vif_name(vif_name), _addr(addr), _subnet(subnet),
	_broadcast(broadcast), _peer(peer) {}

    virtual void dispatch(string &error_msg)
    {
	(_node.*_method)(_vif_name, _addr, _subnet,
			 _broadcast, _peer, error_msg);
    }

protected:
    const string _vif_name;
    const IPvX _addr;
    const IPvXNet _subnet;
    const IPvX _broadcast;
    const IPvX _peer;
};

class MfeaClientDeleteConfigVifAddrCallback :
    public ZebraIpcCallbackError<ZebraMfeaClient,
				 typeof(&ZebraMfeaClient::delete_config_vif_addr)> {
public:
    MfeaClientDeleteConfigVifAddrCallback(ZebraMfeaClient &mfea_client,
					  typeof(&ZebraMfeaClient::delete_config_vif_addr) method,
					  const string& vif_name,
					  const IPvX& addr) :
	ZebraIpcCallbackError<ZebraMfeaClient,
			      typeof(&ZebraMfeaClient::delete_config_vif_addr)>(mfea_client,
										method,
										mfea_client.eventloop()),
	_vif_name(vif_name), _addr(addr) {}

    virtual void dispatch(string &error_msg)
    {
	(_node.*_method)(_vif_name, _addr, error_msg);
    }

protected:
    const string _vif_name;
    const IPvX _addr;
};

class MfeaClientSetConfigVifFlagsCallback :
    public ZebraIpcCallbackError<ZebraMfeaClient,
				 typeof(&ZebraMfeaClient::set_config_vif_flags)> {
public:
    MfeaClientSetConfigVifFlagsCallback(ZebraMfeaClient &mfea_client,
					typeof(&ZebraMfeaClient::set_config_vif_flags) method,
					const string& vif_name,
					bool is_pim_register, bool is_p2p,
					bool is_loopback, bool is_multicast,
					bool is_broadcast, bool is_up,
					uint32_t mtu) :
	ZebraIpcCallbackError<ZebraMfeaClient,
			      typeof(&ZebraMfeaClient::set_config_vif_flags)>(mfea_client,
									      method,
									      mfea_client.eventloop()),
	_vif_name(vif_name), _is_pim_register(is_pim_register),
	_is_p2p(is_p2p), _is_loopback(is_loopback),
	_is_multicast(is_multicast), _is_broadcast(is_broadcast),
	_is_up(is_up), _mtu(mtu) {}

    virtual void dispatch(string &error_msg)
    {
	(_node.*_method)(_vif_name, _is_pim_register, _is_p2p,
			 _is_loopback, _is_multicast, _is_broadcast,
			 _is_up, _mtu, error_msg);
    }

protected:
    const string _vif_name;
    const bool _is_pim_register;
    const bool _is_p2p;
    const bool _is_loopback;
    const bool _is_multicast;
    const bool _is_broadcast;
    const bool _is_up;
    const uint32_t _mtu;
};

class MfeaClientSetConfigAllVifsDoneCallback :
    public ZebraIpcCallbackError<ZebraMfeaClient,
				 typeof(&ZebraMfeaClient::set_config_all_vifs_done)> {
public:
    MfeaClientSetConfigAllVifsDoneCallback(ZebraMfeaClient &mfea_client,
					   typeof(&ZebraMfeaClient::set_config_all_vifs_done) method) :
	ZebraIpcCallbackError<ZebraMfeaClient,
			      typeof(&ZebraMfeaClient::set_config_all_vifs_done)>(mfea_client,
										  method,
										  mfea_client.eventloop())
    {}

    virtual void dispatch(string &error_msg)
    {
	(_node.*_method)(error_msg);
    }
};

class MfeaClientSignalDataflowRecvCallback :
    public ZebraIpcCallback<ZebraMfeaClient,
			    typeof(&ZebraMfeaClient::signal_dataflow_recv)> {
public:
    MfeaClientSignalDataflowRecvCallback(ZebraMfeaClient &mfea_client,
					 typeof(&ZebraMfeaClient::signal_dataflow_recv) method,
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
					 bool is_leq_upcall) :
	ZebraIpcCallback<ZebraMfeaClient,
			 typeof(&ZebraMfeaClient::signal_dataflow_recv)>(mfea_client,
									 method,
									 mfea_client.eventloop()),
	_source_addr(source_addr), _group_addr(group_addr),
	_threshold_interval_sec(threshold_interval_sec),
	_threshold_interval_usec(threshold_interval_usec),
	_measured_interval_sec(measured_interval_sec),
	_measured_interval_usec(measured_interval_usec),
	_threshold_packets(threshold_packets), _threshold_bytes(threshold_bytes),
	_measured_packets(measured_packets), _measured_bytes(measured_bytes),
	_is_threshold_in_packets(is_threshold_in_packets),
	_is_threshold_in_bytes(is_threshold_in_bytes),
	_is_geq_upcall(is_geq_upcall), _is_leq_upcall(is_leq_upcall) {}

    virtual void dispatch()
    {
	(_node.*_method)(_source_addr, _group_addr,
			 _threshold_interval_sec,
			 _threshold_interval_usec,
			 _measured_interval_sec, _measured_interval_usec,
			 _threshold_packets, _threshold_bytes,
			 _measured_packets, _measured_bytes,
			 _is_threshold_in_packets, _is_threshold_in_bytes,
			 _is_geq_upcall, _is_leq_upcall);
    }

protected:
    const IPvX _source_addr;
    const IPvX _group_addr;
    const uint32_t _threshold_interval_sec;
    const uint32_t _threshold_interval_usec;
    const uint32_t _measured_interval_sec;
    const uint32_t _measured_interval_usec;
    const uint32_t _threshold_packets;
    const uint32_t _threshold_bytes;
    const uint32_t _measured_packets;
    const uint32_t _measured_bytes;
    const bool _is_threshold_in_packets;
    const bool _is_threshold_in_bytes;
    const bool _is_geq_upcall;
    const bool _is_leq_upcall;
};


// classes for callbacks from a ZebraMfeaClient to MfeaNode methods

class MfeaProtoRecvCallback :
    public ZebraIpcCallbackError<MfeaNode, typeof(&MfeaNode::proto_recv)> {
public:
    MfeaProtoRecvCallback(MfeaNode &mfea_node,
			  typeof(&MfeaNode::proto_recv) method,
			  const string &module_name, xorp_module_id module_id,
			  uint32_t vif_index, const IPvX& src, const IPvX& dst,
			  int ip_ttl, int ip_tos, bool is_router_alert,
			  const uint8_t *rcvbuf, size_t rcvlen) :
	ZebraIpcCallbackError<MfeaNode,
			      typeof(&MfeaNode::proto_recv)>(mfea_node,
							     method,
							     mfea_node.eventloop()),
	_module_name(module_name), _module_id(module_id),
	_vif_index(vif_index), _src(src), _dst(dst),
	_ip_ttl(ip_ttl), _ip_tos(ip_tos),
	_is_router_alert(is_router_alert), _rcvlen(rcvlen)
    {
	_rcvbuf = new uint8_t[rcvlen];
	memcpy(_rcvbuf, rcvbuf, rcvlen);
    }
    virtual ~MfeaProtoRecvCallback()
    {
	delete[] _rcvbuf;
    }

    virtual void dispatch(string &error_msg)
    {
	(_node.*_method)(_module_name, _module_id, _vif_index,
			 _src, _dst, _ip_ttl, _ip_tos,
			 _is_router_alert, _rcvbuf, _rcvlen,
			 error_msg);
    }

protected:
    const string _module_name;
    const xorp_module_id _module_id;
    const uint32_t _vif_index;
    const IPvX _src;
    const IPvX _dst;
    const int _ip_ttl;
    const int _ip_tos;
    const bool _is_router_alert;
    uint8_t *_rcvbuf;
    const size_t _rcvlen;
};

class MfeaStartProtocolVifCallback :
    public ZebraIpcCallback<MfeaNode,
			    typeof(&MfeaNode::start_protocol_vif)> {
public:
    MfeaStartProtocolVifCallback(MfeaNode &mfea_node,
				 typeof(&MfeaNode::start_protocol_vif) method,
				 const string &module_name,
				 xorp_module_id module_id, uint32_t vif_index) :
	ZebraIpcCallback<MfeaNode,
			 typeof(&MfeaNode::start_protocol_vif)>(mfea_node,
								method,
								mfea_node.eventloop()),
	_module_name(module_name), _module_id(module_id),
	_vif_index(vif_index) {}

    void dispatch()
    {
	(_node.*_method)(_module_name, _module_id, _vif_index);
    }

protected:
    const string _module_name;
    const xorp_module_id _module_id;
    const uint32_t _vif_index;
};

typedef MfeaStartProtocolVifCallback MfeaStopProtocolVifCallback;

class MfeaJoinMulticastGroupCallback :
    public ZebraIpcCallback<MfeaNode,
			    typeof(&MfeaNode::join_multicast_group)> {
public:
    MfeaJoinMulticastGroupCallback(MfeaNode &mfea_node,
				   typeof(&MfeaNode::join_multicast_group) method,
				   const string &module_name,
				   xorp_module_id module_id,
				   uint32_t vif_index, const IPvX& group) :
	ZebraIpcCallback<MfeaNode,
			 typeof(&MfeaNode::join_multicast_group)>(mfea_node,
								  method,
								  mfea_node.eventloop()),
	_module_name(module_name), _module_id(module_id),
	_vif_index(vif_index), _group(group) {}

    void dispatch()
    {
	(_node.*_method)(_module_name, _module_id, _vif_index, _group);
    }

protected:
    const string _module_name;
    const xorp_module_id _module_id;
    const uint32_t _vif_index;
    const IPvX _group;
};

typedef MfeaJoinMulticastGroupCallback MfeaLeaveMulticastGroupCallback;

class MfeaAddMfcCallback :
    public ZebraIpcCallback<MfeaNode, typeof(&MfeaNode::add_mfc)> {

public:
    MfeaAddMfcCallback(MfeaNode &mfea_node,
		       typeof(&MfeaNode::add_mfc) method,
		       const string &module_name,
		       const IPvX& source, const IPvX& group,
		       uint32_t iif_vif_index, const Mifset& oiflist,
		       const Mifset& oiflist_disable_wrongvif,
		       uint32_t max_vifs_oiflist, const IPvX& rp_addr) :
	ZebraIpcCallback<MfeaNode,
			 typeof(&MfeaNode::add_mfc)>(mfea_node,
						     method,
						     mfea_node.eventloop()),
	_module_name(module_name), _source(source), _group(group),
	_iif_vif_index(iif_vif_index), _oiflist(oiflist),
	_oiflist_disable_wrongvif(oiflist_disable_wrongvif),
	_max_vifs_oiflist(max_vifs_oiflist), _rp_addr(rp_addr) {}

    void dispatch()
    {
	(_node.*_method)(_module_name, _source, _group,
			 _iif_vif_index, _oiflist,
			 _oiflist_disable_wrongvif,
			 _max_vifs_oiflist, _rp_addr);
    }

protected:
    const string _module_name;
    const IPvX _source;
    const IPvX _group;
    const uint32_t _iif_vif_index;
    const Mifset _oiflist;
    const Mifset _oiflist_disable_wrongvif;
    const uint32_t _max_vifs_oiflist;
    const IPvX _rp_addr;
};

class MfeaDeleteMfcCallback :
    public ZebraIpcCallback<MfeaNode, typeof(&MfeaNode::delete_mfc)> {
public:
    MfeaDeleteMfcCallback(MfeaNode &mfea_node,
			  typeof(&MfeaNode::delete_mfc) method,
			  const string &module_name,
			  const IPvX& source, const IPvX& group) :
	ZebraIpcCallback<MfeaNode,
			 typeof(&MfeaNode::delete_mfc)>(mfea_node,
							method,
							mfea_node.eventloop()),
	_module_name(module_name), _source(source), _group(group) {}

    void dispatch()
    {
	(_node.*_method)(_module_name, _source, _group);
    }

protected:
    const string _module_name;
    const IPvX _source;
    const IPvX _group;
};

class MfeaAddDataflowMonitorCallback :
    public ZebraIpcCallbackError<MfeaNode,
				 typeof(&MfeaNode::add_dataflow_monitor)> {
public:
    MfeaAddDataflowMonitorCallback(MfeaNode &mfea_node,
				   typeof(&MfeaNode::add_dataflow_monitor) method,
				   const string &module_name,
				   const IPvX& source, const IPvX& group,
				   const TimeVal& threshold_interval,
				   uint32_t threshold_packets,
				   uint32_t threshold_bytes,
				   bool is_threshold_in_packets,
				   bool is_threshold_in_bytes,
				   bool is_geq_upcall, bool is_leq_upcall,
				   bool rolling) :
	ZebraIpcCallbackError<MfeaNode,
			      typeof(&MfeaNode::add_dataflow_monitor)>(mfea_node,
								       method,
								       mfea_node.eventloop()),
	_module_name(module_name),
	_source(source), _group(group),
	_threshold_interval(threshold_interval),
	_threshold_packets(threshold_packets),
	_threshold_bytes(threshold_bytes),
	_is_threshold_in_packets(is_threshold_in_packets),
	_is_threshold_in_bytes(is_threshold_in_bytes),
	_is_geq_upcall(is_geq_upcall), _is_leq_upcall(is_leq_upcall),
	_rolling(rolling) {}

    void dispatch(string &error_msg)
    {
	(_node.*_method)(_module_name, _source, _group,
			 _threshold_interval, _threshold_packets,
			 _threshold_bytes, _is_threshold_in_packets,
			 _is_threshold_in_bytes, _is_geq_upcall,
			 _is_leq_upcall, _rolling, error_msg);
    }

protected:
    const string _module_name;
    const IPvX _source;
    const IPvX _group;
    const TimeVal _threshold_interval;
    const uint32_t _threshold_packets;
    const uint32_t _threshold_bytes;
    const bool _is_threshold_in_packets;
    const bool _is_threshold_in_bytes;
    const bool _is_geq_upcall;
    const bool _is_leq_upcall;
    const bool _rolling;
};

typedef MfeaAddDataflowMonitorCallback MfeaDeleteDataflowMonitorCallback;

class MfeaDeleteAllDataflowMonitorCallback :
    public ZebraIpcCallbackError<MfeaNode,
				 typeof(&MfeaNode::delete_all_dataflow_monitor)> {
public:
    MfeaDeleteAllDataflowMonitorCallback(MfeaNode &mfea_node,
					 typeof(&MfeaNode::delete_all_dataflow_monitor) method,
					 const string &module_name,
					 const IPvX& source,
					 const IPvX& group) :
	ZebraIpcCallbackError<MfeaNode,
			      typeof(&MfeaNode::delete_all_dataflow_monitor)>(mfea_node,
									      method,
									      mfea_node.eventloop()),
	_module_name(module_name), _source(source), _group(group) {}

    void dispatch(string &error_msg)
    {
	(_node.*_method)(_module_name, _source, _group, error_msg);
    }

protected:
    const string _module_name;
    const IPvX _source;
    const IPvX _group;
};

#endif  // __ZEBRA_MFEA_CLIENT_CALLBACK_HH__
