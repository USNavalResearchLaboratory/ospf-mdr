// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#include "zebra_mfea_client.hh"
#include "zebra_mfea_client_callback.hh"


#define MFEA_CALLBACK(cbclass, funcname, args...)			\
  do {									\
      new cbclass(_zebra_mfea_node, &MfeaNode::funcname, ## args);	\
      return XORP_OK;							\
  } while (0)


int
ZebraMfeaClient::proto_send(const string& dst_module_instance_name,
			    xorp_module_id dst_module_id, uint32_t vif_index,
			    const IPvX& src, const IPvX& dst, int ip_ttl,
			    int ip_tos, bool is_router_alert,
			    const uint8_t *sndbuf, size_t sndlen,
			    string& error_msg)
{
    XLOG_ASSERT(dst_module_id == XORP_MODULE_MFEA);

    MFEA_CALLBACK(MfeaProtoRecvCallback, proto_recv,
		  _proto_unit.module_name(), _proto_unit.module_id(),
		  vif_index, src, dst, ip_ttl, ip_tos, is_router_alert,
		  sndbuf, sndlen);
}

int
ZebraMfeaClient::start_protocol_kernel_vif(uint32_t vif_index)
{
    MFEA_CALLBACK(MfeaStartProtocolVifCallback, start_protocol_vif,
		  _proto_unit.module_name(), _proto_unit.module_id(),
		  vif_index);
}

int
ZebraMfeaClient::stop_protocol_kernel_vif(uint32_t vif_index)
{
    MFEA_CALLBACK(MfeaStopProtocolVifCallback, stop_protocol_vif,
		  _proto_unit.module_name(), _proto_unit.module_id(),
		  vif_index);
}

int
ZebraMfeaClient::join_multicast_group(uint32_t vif_index, const IPvX& multicast_group)
{
    MFEA_CALLBACK(MfeaJoinMulticastGroupCallback, join_multicast_group,
		  _proto_unit.module_name(), _proto_unit.module_id(),
		  vif_index, multicast_group);
}

int
ZebraMfeaClient::leave_multicast_group(uint32_t vif_index,
				       const IPvX& multicast_group)
{
    MFEA_CALLBACK(MfeaLeaveMulticastGroupCallback, leave_multicast_group,
		  _proto_unit.module_name(), _proto_unit.module_id(),
		  vif_index, multicast_group);
}

int
ZebraMfeaClient::add_mfc(const IPvX& source, const IPvX& group,
			 uint32_t iif_vif_index, const Mifset& oiflist,
			 const Mifset& oiflist_disable_wrongvif,
			 uint32_t max_vifs_oiflist,
			 const IPvX& rp_addr)
{
    MFEA_CALLBACK(MfeaAddMfcCallback, add_mfc,
		  _proto_unit.module_name(), source, group,
		  iif_vif_index, oiflist, oiflist_disable_wrongvif,
		  max_vifs_oiflist, rp_addr);
}

int
ZebraMfeaClient::delete_mfc(const IPvX& source, const IPvX& group)
{
    MFEA_CALLBACK(MfeaDeleteMfcCallback, delete_mfc,
		  _proto_unit.module_name(), source, group);
}

int
ZebraMfeaClient::add_dataflow_monitor(const IPvX& source, const IPvX& group,
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
    TimeVal threshold_interval(threshold_interval_sec,
			       threshold_interval_usec);

    MFEA_CALLBACK(MfeaAddDataflowMonitorCallback, add_dataflow_monitor,
		  _proto_unit.module_name(), source, group,
		  threshold_interval, threshold_packets,
		  threshold_bytes, is_threshold_in_packets,
		  is_threshold_in_bytes, is_geq_upcall, is_leq_upcall,
		  rolling);
}

int
ZebraMfeaClient::delete_dataflow_monitor(const IPvX& source, const IPvX& group,
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
    TimeVal threshold_interval(threshold_interval_sec,
			       threshold_interval_usec);

    MFEA_CALLBACK(MfeaDeleteDataflowMonitorCallback, delete_dataflow_monitor,
		  _proto_unit.module_name(), source, group,
		  threshold_interval, threshold_packets,
		  threshold_bytes, is_threshold_in_packets,
		  is_threshold_in_bytes, is_geq_upcall, is_leq_upcall,
		  rolling);
}

int
ZebraMfeaClient::delete_all_dataflow_monitor(const IPvX& source,
					     const IPvX& group)
{
    MFEA_CALLBACK(MfeaDeleteAllDataflowMonitorCallback,
		  delete_all_dataflow_monitor, _proto_unit.module_name(),
		  source, group);
}

#undef MFEA_CALLBACK
