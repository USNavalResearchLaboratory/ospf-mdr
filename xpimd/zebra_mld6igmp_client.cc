// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#include "zebra_mld6igmp_client.hh"
#include "zebra_mld6igmp_client_callback.hh"


#define MLD6IGMP_CALLBACK(cbclass, funcname, args...)		\
do {								\
    new cbclass(_zebra_mld6igmp_node, &Mld6igmpNode::funcname,	\
		## args);					\
    return XORP_OK;						\
} while (0)


int
ZebraMld6igmpClient::add_protocol_mld6igmp(uint32_t vif_index)
{
    MLD6IGMP_CALLBACK(Mld6igmpAddProtocolCallback, add_protocol,
		      _proto_unit.module_name(), _proto_unit.module_id(),
		      vif_index);
}

int
ZebraMld6igmpClient::delete_protocol_mld6igmp(uint32_t vif_index)
{
    MLD6IGMP_CALLBACK(Mld6igmpDeleteProtocolCallback, delete_protocol,
		      _proto_unit.module_name(), _proto_unit.module_id(),
		      vif_index);
}

#undef MLD6IGMP_CALLBACK
