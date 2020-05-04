// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef __ZEBRA_PIM_NODE_HH__
#define __ZEBRA_PIM_NODE_HH__

#include <map>
#include <string>
#include <set>

#include "pim/pim_node.hh"
#include "pim/pim_node_cli.hh"
#include "pim/pim_mfc.hh"

#include "zebra_router_config.hh"
#include "zebra_router_client.hh"
#include "zebra_mfea_client.hh"
#include "zebra_mld6igmp_client.hh"


class ZebraBsrCandidateConfig : public ZebraConfig {

public:

    ZebraBsrCandidateConfig(const IPvXNet &scope_zone_id, bool is_scope_zone,
			    const string& vif_name, const IPvX& vif_addr,
			    uint8_t bsr_priority, uint8_t hash_mask_len);
    ZebraBsrCandidateConfig(const IPvXNet &scope_zone_id, bool is_scope_zone);

    bool operator<(const ZebraBsrCandidateConfig& other) const;

    IPvXNet _scope_zone_id;
    bool _is_scope_zone;
    string _vif_name;
    IPvX _vif_addr;
    uint8_t _bsr_priority;
    uint8_t _hash_mask_len;
};

class ZebraRpCandidateConfig : public ZebraConfig {

public:

    ZebraRpCandidateConfig(const IPvXNet& group_prefix, bool is_scope_zone,
			   const string& vif_name, const IPvX& vif_addr,
			   uint8_t rp_priority, uint16_t rp_holdtime);
    ZebraRpCandidateConfig(const IPvXNet& group_prefix, bool is_scope_zone);

    bool operator<(const ZebraRpCandidateConfig& other) const;

    IPvXNet _group_prefix;
    bool _is_scope_zone;
    string _vif_name;
    IPvX _vif_addr;
    uint8_t _rp_priority;
    uint8_t _rp_holdtime;
};

// this could inherit from SourceGroup in xorp/mrt/mrt.hh
class ZebraStaticMembership {

public:

    ZebraStaticMembership(const IPvX &source, const IPvX &group);

    const IPvX &source() const;
    const IPvX &group() const;

    bool operator<(const ZebraStaticMembership &other) const;

private:

    IPvX _source;
    IPvX _group;
};

class ZebraPimVifConfig : public ZebraVifConfig {

public:

    void clear_all_applied() const;

    ZebraConfigVal<bool> enabled;
    ZebraConfigVal<int> proto_version;
    ZebraConfigVal<bool> passive;
    ZebraConfigVal<bool> ip_router_alert_option_check;
    ZebraConfigVal<uint16_t> hello_triggered_delay;
    ZebraConfigVal<uint16_t> hello_period;
    ZebraConfigVal<uint16_t> hello_holdtime;
    ZebraConfigVal<uint32_t> dr_priority;
    ZebraConfigVal<uint16_t> propagation_delay;
    ZebraConfigVal<uint16_t> override_interval;
    ZebraConfigVal<bool> is_tracking_support_disabled;
    ZebraConfigVal<bool> accept_nohello_neighbors;
    ZebraConfigVal<uint16_t> join_prune_period;
    set<ZebraConfigVal<IPvXNet> > alternative_subnets;
    set<ZebraConfigVal<ZebraStaticMembership> > static_memberships;
};

class ZebraPimNode : public PimNode, public PimNodeCli,
		     public ZebraRouterClient, public ZebraMfeaClient,
		     public ZebraMld6igmpClient {

public:

    ZebraPimNode(int			family,
		 xorp_module_id		module_id,
		 EventLoop&		eventloop,
		 ZebraRouterNode	&zebra_router_node,
		 ZebraMfeaNode		&zebra_mfea_node,
		 ZebraMld6igmpNode	&zebra_mld6igmp_node);
    ~ZebraPimNode();

    int start (string& error_msg);
    int stop (string& error_msg);

    int add_vif(const Vif& vif, string& error_msg);
    int	add_vif_addr(const string& vif_name, const IPvX& addr,
		     const IPvXNet& subnet_addr,
		     const IPvX& broadcast_addr,
		     const IPvX& peer_addr,
		     bool& should_send_pim_hello,
		     string& error_msg);
    int delete_vif(const string& vif_name, string& error_msg);

    bool try_start_vif(const string& name);

    ZebraPimVifConfig &get_if_config(const string &vif_name);
    void del_if_config(const string &vif_name);

    void set_pending_rp_update();

    int add_cand_bsr_config(const IPvXNet& scope_zone_id,
			    bool is_scope_zone,
			    const string& vif_name,
			    const IPvX& vif_addr,
			    uint8_t bsr_priority,
			    uint8_t hash_mask_len,
			    string& error_msg);
    int delete_cand_bsr_config(const IPvXNet& scope_zone_id,
			       bool is_scope_zone, string& error_msg);

    int add_cand_rp_config(const IPvXNet& group_prefix,
			   bool is_scope_zone,
			   const string& vif_name,
			   const IPvX& vif_addr,
			   uint8_t rp_priority,
			   uint16_t rp_holdtime,
			   string& error_msg);
    int delete_cand_rp_config(const IPvXNet& group_prefix,
			      bool is_scope_zone,
			      const string& vif_name,
			      const IPvX& vif_addr,
			      string& error_msg);

    int set_register_source_config(const string& vif_name, string& error_msg);
    int clear_register_source_config(string& error_msg);

    int add_static_membership(const string &vif_name, const IPvX &source,
			      const IPvX &group, string &error_msg);
    int delete_static_membership(const string &vif_name, const IPvX &source,
				 const IPvX &group, string &error_msg);

    // ZebraRouterClient methods
    void init();
    void terminate();

    const char* zebra_ipstr() const;
    const char* zebra_protostr() const;
    const char* xorp_protostr() const;

    void zebra_config_write(struct vty *vty);
    int zebra_config_write_interface(struct vty *vty);
    void zebra_config_write_interface(struct vty *vty,
				      const PimVif *vif);
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

    // ZebraMld6igmpClient methods
    int add_membership(uint32_t vif_index, const IPvX& source,
		       const IPvX& group);
    int delete_membership(uint32_t vif_index, const IPvX& source,
			  const IPvX& group);

    // PimNode methods
    int start_protocol_kernel_vif(uint32_t vif_index);
    int stop_protocol_kernel_vif(uint32_t vif_index);
    int join_multicast_group(uint32_t vif_index, const IPvX& multicast_group);
    int leave_multicast_group(uint32_t vif_index, const IPvX& multicast_group);
    void mfea_register_startup();
    void mfea_register_shutdown();
    int add_mfc_to_kernel(const PimMfc& pim_mfc);
    int delete_mfc_from_kernel(const PimMfc& pim_mfc);
    int add_dataflow_monitor(const IPvX& source_addr,
			     const IPvX& group_addr,
			     uint32_t threshold_interval_sec,
			     uint32_t threshold_interval_usec,
			     uint32_t threshold_packets,
			     uint32_t threshold_bytes,
			     bool is_threshold_in_packets,
			     bool is_threshold_in_bytes,
			     bool is_geq_upcall,
			     bool is_leq_upcall,
			     bool rolling);
    int delete_dataflow_monitor(const IPvX& source_addr,
				const IPvX& group_addr,
				uint32_t threshold_interval_sec,
				uint32_t threshold_interval_usec,
				uint32_t threshold_packets,
				uint32_t threshold_bytes,
				bool is_threshold_in_packets,
				bool is_threshold_in_bytes,
				bool is_geq_upcall,
				bool is_leq_upcall,
				bool rolling);
    int delete_all_dataflow_monitor(const IPvX& source_addr,
				    const IPvX& group_addr);
    int add_protocol_mld6igmp(uint32_t vif_index);
    int delete_protocol_mld6igmp(uint32_t vif_index);

protected:

    void zebra_command_init();
    void apply_config(const string &vif_name);
    void clear_config(const string &vif_name);
    void check_static_rp(const string& vif_name);

private:

    map<string, ZebraPimVifConfig> _if_config;
    bool _pending_rp_update;
    set<ZebraBsrCandidateConfig> _cand_bsrs;
    set<ZebraRpCandidateConfig> _cand_rps;
    ZebraConfigVal<string> _register_source_vif_name;
    bool _terminated;
};

#endif	// __ZEBRA_PIM_NODE_HH__
