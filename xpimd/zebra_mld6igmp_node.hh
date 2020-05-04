// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef __ZEBRA_MLD6IGMP_NODE_HH__
#define __ZEBRA_MLD6IGMP_NODE_HH__

#include <map>
#include <string>
#include <set>

#include "mld6igmp/mld6igmp_node.hh"
#include "mld6igmp/mld6igmp_node_cli.hh"

#include "zebra_router_config.hh"
#include "zebra_router_client.hh"
#include "zebra_server_node.hh"
#include "zebra_mfea_client.hh"


class ZebraMld6igmpVifConfig : public ZebraVifConfig {

public:

    void clear_all_applied() const;

    ZebraConfigVal<bool> enabled;
    ZebraConfigVal<int> proto_version;
    ZebraConfigVal<bool> ip_router_alert_option_check;
    ZebraConfigVal<TimeVal> query_interval;
    ZebraConfigVal<TimeVal> query_last_member_interval;
    ZebraConfigVal<TimeVal> query_response_interval;
    ZebraConfigVal<uint32_t> robust_count;
    set<ZebraConfigVal<IPvXNet> > alternative_subnets;
};

class ZebraMld6igmpClient;

class ZebraMld6igmpNode : public Mld6igmpNode, public Mld6igmpNodeCli,
			  public ZebraServerNode<ZebraMld6igmpClient>,
			  public ZebraRouterClient,
			  public ZebraMfeaClient {

public:

    ZebraMld6igmpNode(int family, xorp_module_id module_id,
		      EventLoop &eventloop, ZebraRouterNode &zebra_router_node,
		      ZebraMfeaNode &zebra_mfea_node);
    ~ZebraMld6igmpNode();

    int start (string& error_msg);
    int stop (string& error_msg);

    int add_vif(const Vif& vif, string& error_msg);
    int	add_vif_addr(const string& vif_name, const IPvX& addr,
		     const IPvXNet& subnet_addr, const IPvX& broadcast_addr,
		     const IPvX& peer_addr, string& error_msg);
    int delete_vif(const string& vif_name, string& error_msg);

    bool try_start_vif(const string& name);

    ZebraMld6igmpVifConfig &get_if_config(const string &name);
    void del_if_config(const string &name);

    // ZebraRouterClient methods
    void init();
    void terminate();

    const char* zebra_ipstr() const;
    const char* zebra_protostr() const;
    const char* xorp_protostr() const;

    int zebra_config_write_interface(struct vty *vty);
    int zebra_config_write_debug(struct vty *vty);

    void zebra_client_register();
    void zebra_client_unregister();

    void zebra_ipv4_route_add(const struct prefix_ipv4 *p,
			      u_char numnexthop,
			      const struct in_addr *nexthop,
			      const u_int32_t *ifindex,
			      u_int32_t metric);
    void zebra_ipv4_route_del(const struct prefix_ipv4 *p,
			      u_char numnexthop,
			      const struct in_addr *nexthop,
			      const u_int32_t *ifindex,
			      u_int32_t metric);
#ifdef HAVE_IPV6_MULTICAST
    void zebra_ipv6_route_add(const struct prefix_ipv6 *p,
			      u_char numnexthop,
			      const struct in6_addr *nexthop,
			      const u_int32_t *ifindex,
			      u_int32_t metric);
    void zebra_ipv6_route_del(const struct prefix_ipv6 *p,
			      u_char numnexthop,
			      const struct in6_addr *nexthop,
			      const u_int32_t *ifindex,
			      u_int32_t metric);
#endif	// HAVE_IPV6_MULTICAST

    // ProtoNode methods
    int proto_send(const string& dst_module_instance_name,
		   xorp_module_id dst_module_id, uint32_t vif_index,
		   const IPvX& src, const IPvX& dst,
		   int ip_ttl, int ip_tos, bool is_router_alert,
		   const uint8_t *sndbuf, size_t sndlen,
		   string& error_msg);

    // ZebraMfeaClient methods
    int	proto_recv(const string& src_module_instance_name,
		   xorp_module_id src_module_id,
		   uint32_t vif_index, const IPvX& src, const IPvX& dst,
		   int ip_ttl, int ip_tos, bool is_router_alert,
		   const uint8_t *rcvbuf, size_t rcvlen, string& error_msg);
    int	signal_message_recv(const string& src_module_instance_name,
			    xorp_module_id src_module_id,
			    int message_type, uint32_t vif_index,
			    const IPvX& src, const IPvX& dst,
			    const uint8_t *rcvbuf,  size_t rcvlen);
    int add_config_vif(const string& vif_name, uint32_t vif_index,
		       string& error_msg);
    int delete_config_vif(const string& vif_name, string& error_msg);
    int add_config_vif_addr(const string& vif_name, const IPvX& addr,
			    const IPvXNet& subnet, const IPvX& broadcast,
			    const IPvX& peer, string& error_msg);
    int delete_config_vif_addr(const string& vif_name, const IPvX& addr,
			       string& error_msg);
    int set_config_vif_flags(const string& vif_name, bool is_pim_register,
			     bool is_p2p, bool is_loopback, bool is_multicast,
			     bool is_broadcast, bool is_up, uint32_t mtu,
			     string& error_msg);
    int set_config_all_vifs_done(string& error_msg);
    int signal_dataflow_recv(const IPvX& source_addr, const IPvX& group_addr,
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
			     bool is_geq_upcall, bool is_leq_upcall);

    // Mld6igmpNode methods
    int start_protocol_kernel_vif(uint32_t vif_index);
    int stop_protocol_kernel_vif(uint32_t vif_index);
    int join_multicast_group(uint32_t vif_index, const IPvX& multicast_group);
    int leave_multicast_group(uint32_t vif_index, const IPvX& multicast_group);
    void mfea_register_startup();
    void mfea_register_shutdown();
    int send_add_membership(const string& dst_module_instance_name,
			    xorp_module_id dst_module_id, uint32_t vif_index,
			    const IPvX& source, const IPvX& group);
    int send_delete_membership(const string& dst_module_instance_name,
			       xorp_module_id dst_module_id, uint32_t vif_index,
			       const IPvX& source, const IPvX& group);

protected:

    void zebra_command_init();

    void apply_config(const string &vif_name);
    void clear_config(const string &vif_name);

private:

    map<string, ZebraMld6igmpVifConfig> _if_config;
    bool _terminated;
};

#endif	// __ZEBRA_MLD6IGMP_NODE_HH__
