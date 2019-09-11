// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef __ZEBRA_MFEA_NODE_HH__
#define __ZEBRA_MFEA_NODE_HH__

#include <map>
#include <string>

#include "fea/mfea_node.hh"
#include "fea/mfea_node_cli.hh"

#include "zebra_router_config.hh"
#include "zebra_router_client.hh"
#include "zebra_server_node.hh"


class ZebraMfeaVifConfig : public ZebraVifConfig {

public:

    void clear_all_applied() const;

    ZebraConfigVal<bool> enabled;
};

class ZebraMfeaClient;

class ZebraMfeaNode : public MfeaNode, public MfeaNodeCli,
		      public ZebraServerNode<ZebraMfeaClient>,
		      public ZebraRouterClient {

public:

    ZebraMfeaNode(int family, xorp_module_id module_id,
		  EventLoop &eventloop, ZebraRouterNode &zebra_router_node);

    ~ZebraMfeaNode();

    int start (string& error_msg);
    int stop (string& error_msg);

    int add_vif(const Vif& vif, string& error_msg);

    bool try_start_vif(const string& name);

    ZebraMfeaVifConfig &get_if_config(const string &name);
    void del_if_config(const string &name);

    // ZebraRouterClient methods
    void init();
    void terminate();

    const char* zebra_ipstr() const;
    const char* zebra_protostr() const;

    int zebra_config_write_interface(struct vty *vty);
    int zebra_config_write_debug(struct vty *vty);

    void zebra_client_register();
    void zebra_client_unregister();

    void zebra_if_add(const struct interface *ifp);
    void zebra_if_del(const struct interface *ifp);
    void zebra_if_up(const struct interface *ifp);
    void zebra_if_down(const struct interface *ifp);
    void zebra_if_addr_add(const struct connected *c);
    void zebra_if_addr_del(const struct connected *c);

    // ProtoNode methods
    int proto_send(const string& dst_module_instance_name,
		   xorp_module_id dst_module_id, uint32_t vif_index,
		   const IPvX& src, const IPvX& dst,
		   int ip_ttl, int ip_tos, bool is_router_alert,
		   const uint8_t *sndbuf, size_t sndlen,
		   string& error_msg);
    int signal_message_send(const string& dst_module_instance_name,
			    xorp_module_id dst_module_id,
			    int message_type, uint32_t vif_index,
			    const IPvX& src, const IPvX& dst,
			    const uint8_t *sndbuf, size_t sndlen);

    // MfeaNode methods
    int send_add_config_vif(const string& dst_module_instance_name,
			    xorp_module_id dst_module_id,
			    const string& vif_name,
			    uint32_t vif_index);
    int send_delete_config_vif(const string& dst_module_instance_name,
			       xorp_module_id dst_module_id,
			       const string& vif_name);
    int send_add_config_vif_addr(const string& dst_module_instance_name,
				 xorp_module_id dst_module_id,
				 const string& vif_name,
				 const IPvX& addr,
				 const IPvXNet& subnet,
				 const IPvX& broadcast,
				 const IPvX& peer);
    int send_delete_config_vif_addr(const string& dst_module_instance_name,
				    xorp_module_id dst_module_id,
				    const string& vif_name,
				    const IPvX& addr);
    int send_set_config_vif_flags(const string& dst_module_instance_name,
				  xorp_module_id dst_module_id,
				  const string& vif_name,
				  bool is_pim_register,
				  bool is_p2p,
				  bool is_loopback,
				  bool is_multicast,
				  bool is_broadcast,
				  bool is_up,
				  uint32_t mtu);
    int send_set_config_all_vifs_done(const string& dst_module_instance_name,
				      xorp_module_id dst_module_id);
    int dataflow_signal_send(const string& dst_module_instance_name,
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
			     bool is_leq_upcall);

    int raise_privileges();
    int lower_privileges();

protected:

    void zebra_command_init();
    void apply_config(const string &vif_name);
    void clear_config(const string &vif_name);

private:

    map<string, ZebraMfeaVifConfig> _if_config;
    bool _terminated;
};

#endif	// __ZEBRA_MFEA_NODE_HH__
