// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef __ZEBRA_MLD6IGMP_CLIENT_HH__
#define  __ZEBRA_MLD6IGMP_CLIENT_HH__

#include "zebra_mld6igmp_client_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"

#include "libproto/proto_unit.hh"

#include "zebra_mld6igmp_node.hh"


class ZebraMld6igmpClient {

public:

    ZebraMld6igmpClient(ProtoUnit &proto_unit,
			ZebraMld6igmpNode &zebra_mld6igmp_node) :
	_proto_unit(proto_unit), _zebra_mld6igmp_node(zebra_mld6igmp_node)
    {
	if (zebra_mld6igmp_node.add_client(proto_unit.module_name(), *this) !=
	    XORP_OK)
	    XLOG_ERROR("ZebraMld6igmpNode::add_client() failed");
    }
    virtual ~ZebraMld6igmpClient()
    {
	if (_zebra_mld6igmp_node.delete_client(_proto_unit.module_name()) !=
	    XORP_OK)
	    XLOG_ERROR("ZebraMld6igmpNode::delete_client() failed");
    }

    EventLoop &eventloop()
    {
	return _zebra_mld6igmp_node.Mld6igmpNode::eventloop();
    }

    // communication from a Mld6igmpNode to a ZebraMld6igmpClient
    // (receive methods)
    virtual int add_membership(uint32_t vif_index, const IPvX& source,
			       const IPvX& group) = 0;
    virtual int delete_membership(uint32_t vif_index, const IPvX& source,
				  const IPvX& group) = 0;

    // communication from a ZebraMld6igmpClient to a Mld6igmpNode
    // (send methods)
    int add_protocol_mld6igmp(uint32_t vif_index);
    int delete_protocol_mld6igmp(uint32_t vif_index);

protected:

    ProtoUnit &_proto_unit;
    ZebraMld6igmpNode &_zebra_mld6igmp_node;
};

#endif	// __ZEBRA_MLD6IGMP_CLIENT_HH__
