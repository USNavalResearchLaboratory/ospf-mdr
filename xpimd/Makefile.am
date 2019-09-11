AUTOMAKE_OPTIONS = subdir-objects

sbin_PROGRAMS = xpimd

xpimd_CPPFLAGS = $(AM_CFLAGS) -std=gnu++03 -DSYSCONFDIR=\"$(sysconfdir)/\" \
	-I$(top_srcdir)/lib -Ixorp

xpimd_LDFLAGS = -Wl,--allow-multiple-definition

# noinst_HEADERS = 

xpimd_SOURCES =					\
	main.cc					\
	zebra_client.cc				\
	zebra_client.hh				\
	zebra_client_module.h			\
	zebra_ipc_callback.hh			\
	zebra_mfea_client.cc			\
	zebra_mfea_client.hh			\
	zebra_mfea_client_callback.hh		\
	zebra_mfea_client_module.h		\
	zebra_mfea_command.cc			\
	zebra_mfea_module.h			\
	zebra_mfea_node.cc			\
	zebra_mfea_node.hh			\
	zebra_misc.cc				\
	zebra_misc.hh				\
	zebra_mld6igmp_client.cc		\
	zebra_mld6igmp_client.hh		\
	zebra_mld6igmp_client_callback.hh	\
	zebra_mld6igmp_client_module.h		\
	zebra_mld6igmp_command.cc		\
	zebra_mld6igmp_module.h			\
	zebra_mld6igmp_node.cc			\
	zebra_mld6igmp_node.hh			\
	zebra_pim_command.cc			\
	zebra_pim_module.h			\
	zebra_pim_node.cc			\
	zebra_pim_node.hh			\
	zebra_router.cc				\
	zebra_router.hh				\
	zebra_router_client.hh			\
	zebra_router_config.hh			\
	zebra_router_module.h			\
	zebra_router_node.cc			\
	zebra_router_node.hh			\
	zebra_server_node.hh			\
	zebra_thread.cc				\
	zebra_thread.hh				\
	zebra_thread_module.h

# XORP MFEA files
xpimd_SOURCES +=				\
	xorp/fea/kernel_utils.hh		\
	xorp/fea/mfea_config.cc			\
	xorp/fea/mfea_dataflow.cc		\
	xorp/fea/mfea_dataflow.hh		\
	xorp/fea/mfea_kernel_messages.hh	\
	xorp/fea/mfea_module.h			\
	xorp/fea/mfea_mrouter.cc		\
	xorp/fea/mfea_mrouter.hh		\
	xorp/fea/mfea_node.cc			\
	xorp/fea/mfea_node.hh			\
	xorp/fea/mfea_node_cli.cc		\
	xorp/fea/mfea_node_cli.hh		\
	xorp/fea/mfea_osdep.hh			\
	xorp/fea/mfea_proto_comm.cc		\
	xorp/fea/mfea_proto_comm.hh		\
	xorp/fea/mfea_vif.cc			\
	xorp/fea/mfea_vif.hh

# XORP MLD/IGMP files
xpimd_SOURCES +=				\
	xorp/mld6igmp/igmp_proto.h		\
	xorp/mld6igmp/mld6_proto.h		\
	xorp/mld6igmp/mld6igmp_config.cc	\
	xorp/mld6igmp/mld6igmp_group_record.cc	\
	xorp/mld6igmp/mld6igmp_group_record.hh	\
	xorp/mld6igmp/mld6igmp_module.h		\
	xorp/mld6igmp/mld6igmp_node.cc		\
	xorp/mld6igmp/mld6igmp_node.hh		\
	xorp/mld6igmp/mld6igmp_node_cli.cc	\
	xorp/mld6igmp/mld6igmp_node_cli.hh	\
	xorp/mld6igmp/mld6igmp_proto.cc		\
	xorp/mld6igmp/mld6igmp_source_record.cc	\
	xorp/mld6igmp/mld6igmp_source_record.hh	\
	xorp/mld6igmp/mld6igmp_vif.cc		\
	xorp/mld6igmp/mld6igmp_vif.hh

# XORP PIM files
xpimd_SOURCES +=					\
	xorp/pim/pim_bsr.cc				\
	xorp/pim/pim_bsr.hh				\
	xorp/pim/pim_config.cc				\
	xorp/pim/pim_mfc.cc				\
	xorp/pim/pim_mfc.hh				\
	xorp/pim/pim_module.h				\
	xorp/pim/pim_mre.cc				\
	xorp/pim/pim_mre.hh				\
	xorp/pim/pim_mre_assert.cc			\
	xorp/pim/pim_mre_data.cc			\
	xorp/pim/pim_mre_join_prune.cc			\
	xorp/pim/pim_mre_register.cc			\
	xorp/pim/pim_mre_rpf.cc				\
	xorp/pim/pim_mre_task.cc			\
	xorp/pim/pim_mre_task.hh			\
	xorp/pim/pim_mre_track_state.cc			\
	xorp/pim/pim_mre_track_state.hh			\
	xorp/pim/pim_mrib_table.cc			\
	xorp/pim/pim_mrib_table.hh			\
	xorp/pim/pim_mrt.cc				\
	xorp/pim/pim_mrt.hh				\
	xorp/pim/pim_mrt_mfc.cc				\
	xorp/pim/pim_mrt_task.cc			\
	xorp/pim/pim_nbr.cc				\
	xorp/pim/pim_nbr.hh				\
	xorp/pim/pim_node.cc				\
	xorp/pim/pim_node.hh				\
	xorp/pim/pim_node_cli.cc			\
	xorp/pim/pim_node_cli.hh			\
	xorp/pim/pim_proto.h				\
	xorp/pim/pim_proto_assert.cc			\
	xorp/pim/pim_proto_assert.hh			\
	xorp/pim/pim_proto_bootstrap.cc			\
	xorp/pim/pim_proto_cand_rp_adv.cc		\
	xorp/pim/pim_proto_graft.cc			\
	xorp/pim/pim_proto_graft_ack.cc			\
	xorp/pim/pim_proto_hello.cc			\
	xorp/pim/pim_proto_join_prune.cc		\
	xorp/pim/pim_proto_join_prune_message.cc	\
	xorp/pim/pim_proto_join_prune_message.hh	\
	xorp/pim/pim_proto_register.cc			\
	xorp/pim/pim_proto_register_stop.cc		\
	xorp/pim/pim_rp.cc				\
	xorp/pim/pim_rp.hh				\
	xorp/pim/pim_scope_zone_table.cc		\
	xorp/pim/pim_scope_zone_table.hh		\
	xorp/pim/pim_vif.cc				\
	xorp/pim/pim_vif.hh

# XORP library files
xpimd_SOURCES +=				\
	xorp/libcomm/comm_api.h			\
	xorp/libcomm/comm_module.h		\
	xorp/libcomm/comm_private.h		\
	xorp/libcomm/comm_sock.c		\
	xorp/libcomm/comm_user.c		\
	xorp/libproto/checksum.c		\
	xorp/libproto/checksum.h		\
	xorp/libproto/libproto_module.h		\
	xorp/libproto/packet.cc			\
	xorp/libproto/packet.hh			\
	xorp/libproto/proto_node.hh		\
	xorp/libproto/proto_node_cli.cc		\
	xorp/libproto/proto_node_cli.hh		\
	xorp/libproto/proto_register.cc		\
	xorp/libproto/proto_register.hh		\
	xorp/libproto/proto_state.cc		\
	xorp/libproto/proto_state.hh		\
	xorp/libproto/proto_unit.cc		\
	xorp/libproto/proto_unit.hh		\
	xorp/libxorp/c_format.cc		\
	xorp/libxorp/c_format.hh		\
	xorp/libxorp/callback.cc		\
	xorp/libxorp/callback.hh		\
	xorp/libxorp/callback_debug.hh		\
	xorp/libxorp/callback_nodebug.hh	\
	xorp/libxorp/clock.cc			\
	xorp/libxorp/clock.hh			\
	xorp/libxorp/config_param.hh		\
	xorp/libxorp/debug.h			\
	xorp/libxorp/ether_compat.h		\
	xorp/libxorp/eventloop.cc		\
	xorp/libxorp/eventloop.hh		\
	xorp/libxorp/exceptions.cc		\
	xorp/libxorp/exceptions.hh		\
	xorp/libxorp/heap.cc			\
	xorp/libxorp/heap.hh			\
	xorp/libxorp/ioevents.hh		\
	xorp/libxorp/ipnet.hh			\
	xorp/libxorp/ipv4.cc			\
	xorp/libxorp/ipv4.hh			\
	xorp/libxorp/ipv4net.hh			\
	xorp/libxorp/ipv6.cc			\
	xorp/libxorp/ipv6.hh			\
	xorp/libxorp/ipv6net.hh			\
	xorp/libxorp/ipvx.cc			\
	xorp/libxorp/ipvx.hh			\
	xorp/libxorp/ipvxnet.hh			\
	xorp/libxorp/libxorp_module.h		\
	xorp/libxorp/mac.hh			\
	xorp/libxorp/minitraits.hh		\
	xorp/libxorp/random.h			\
	xorp/libxorp/range.hh			\
	xorp/libxorp/ref_ptr.cc			\
	xorp/libxorp/ref_ptr.hh			\
	xorp/libxorp/round_robin.cc		\
	xorp/libxorp/round_robin.hh		\
	xorp/libxorp/safe_callback_obj.hh	\
	xorp/libxorp/selector.cc		\
	xorp/libxorp/selector.hh		\
	xorp/libxorp/service.cc			\
	xorp/libxorp/service.hh			\
	xorp/libxorp/status_codes.h		\
	xorp/libxorp/task.cc			\
	xorp/libxorp/task.hh			\
	xorp/libxorp/time_slice.cc		\
	xorp/libxorp/time_slice.hh		\
	xorp/libxorp/timer.cc			\
	xorp/libxorp/timer.hh			\
	xorp/libxorp/timeval.hh			\
	xorp/libxorp/token.cc			\
	xorp/libxorp/token.hh			\
	xorp/libxorp/utility.c			\
	xorp/libxorp/utility.h			\
	xorp/libxorp/utils.hh			\
	xorp/libxorp/vif.cc			\
	xorp/libxorp/vif.hh			\
	xorp/libxorp/xlog.c			\
	xorp/libxorp/xlog.h			\
	xorp/libxorp/xorp.h			\
	xorp/libxorp/xorp_osdep_begin.h		\
	xorp/libxorp/xorp_osdep_end.h		\
	xorp/libxorp/xorp_osdep_mid.h		\
	xorp/libxorp/xorpfd.hh			\
	xorp/mrt/buffer.c			\
	xorp/mrt/buffer.h			\
	xorp/mrt/include/ip_mroute.h		\
	xorp/mrt/include/netinet/pim.h		\
	xorp/mrt/max_vifs.h			\
	xorp/mrt/mifset.hh			\
	xorp/mrt/mrib_table.cc			\
	xorp/mrt/mrib_table.hh			\
	xorp/mrt/mrt.hh				\
	xorp/mrt/mrt_module.h			\
	xorp/mrt/multicast_defs.h		\
	xorp/mrt/netstream_access.h		\
	xorp/mrt/random.c			\
	xorp/mrt/random.h

xpimd_LDADD = ../lib/libzebra.la @LIBCAP@

examplesdir = $(exampledir)
dist_examples_DATA = xpimd.conf.sample
