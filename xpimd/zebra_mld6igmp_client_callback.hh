// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef __ZEBRA_MLD6IGMP_CLIENT_CALLBACK_HH__
#define __ZEBRA_MLD6IGMP_CLIENT_CALLBACK_HH__

#include "mld6igmp/mld6igmp_node.hh"

#include "zebra_ipc_callback.hh"
#include "zebra_mld6igmp_client.hh"


// classes for callbacks from a Mld6igmpNode to ZebraMld6igmpClient methods

class Mld6igmpClientAddMembershipCallback :
    public ZebraIpcCallback<ZebraMld6igmpClient,
			    typeof(&ZebraMld6igmpClient::add_membership)> {
public:
    Mld6igmpClientAddMembershipCallback(ZebraMld6igmpClient &mld6igmp_client,
					typeof(&ZebraMld6igmpClient::add_membership) method,
					uint32_t vif_index, const IPvX& source,
					const IPvX& group) :
	ZebraIpcCallback<ZebraMld6igmpClient,
			 typeof(&ZebraMld6igmpClient::add_membership)>(mld6igmp_client,
								       method,
								       mld6igmp_client.eventloop()),
	_vif_index(vif_index), _source(source), _group(group) {}

    virtual void dispatch()
    {
	(_node.*_method)(_vif_index, _source, _group);
    }

protected:
    const uint32_t _vif_index;
    const IPvX _source;
    const IPvX _group;
};

typedef Mld6igmpClientAddMembershipCallback Mld6igmpClientDeleteMembershipCallback;


// classes for callbacks from a ZebraMld6igmpClient to Mld6igmpNode methods

class Mld6igmpAddProtocolCallback :
    public ZebraIpcCallback<Mld6igmpNode,
			    typeof(&Mld6igmpNode::add_protocol)> {
public:
    Mld6igmpAddProtocolCallback(Mld6igmpNode &mld6igmp_node,
				typeof(&Mld6igmpNode::add_protocol) method,
				const string &module_name,
				xorp_module_id module_id, uint32_t vif_index) :
	ZebraIpcCallback<Mld6igmpNode,
			 typeof(&Mld6igmpNode::add_protocol)>(mld6igmp_node,
							      method,
							      mld6igmp_node.eventloop()),
	_module_name(module_name), _module_id(module_id),
	_vif_index(vif_index) {}

    virtual void dispatch()
    {
	(_node.*_method)(_module_name, _module_id, _vif_index);
    }

protected:
    const string _module_name;
    const xorp_module_id _module_id;
    const uint32_t _vif_index;
};

typedef Mld6igmpAddProtocolCallback Mld6igmpDeleteProtocolCallback;

#endif	// __ZEBRA_MLD6IGMP_CLIENT_CALLBACK_HH__
