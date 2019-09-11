// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef _ZEBRA_ROUTER_CLIENT_HH_
#define _ZEBRA_ROUTER_CLIENT_HH_

#include "zebra_router_node.hh"


class ZebraRouterClient {

public:

    ZebraRouterClient(ZebraRouterNode &zebra_router_node) :
	_zebra_router_node(zebra_router_node) {}

    virtual void init() = 0;
    virtual void terminate() = 0;

    virtual void zebra_client_init()
    {
	zebra_client_register();
	_zebra_router_node.add_config_write_interface_cb(callback(this,
								  &ZebraRouterClient::zebra_config_write_interface));
	_zebra_router_node.add_config_write_debug_cb(callback(this,
							      &ZebraRouterClient::zebra_config_write_debug));
	zebra_command_init();
    }
    virtual void zebra_client_terminate()
    {
	zebra_client_unregister();
    }

    virtual const char* zebra_ipstr() const = 0;
    virtual const char* zebra_protostr() const = 0;
    virtual const char* xorp_protostr() const {return zebra_protostr();}

    virtual int zebra_config_write_interface(struct vty *vty) = 0;
    virtual int zebra_config_write_debug(struct vty *vty) = 0;

protected:

    virtual void zebra_client_register() = 0;
    virtual void zebra_client_unregister() = 0;
    virtual void zebra_command_init() = 0;

    int raise_privileges()
    {
	return _zebra_router_node.raise_privileges();
    }

    int lower_privileges ()
    {
	return _zebra_router_node.lower_privileges();
    }

    ZebraRouterNode &_zebra_router_node;
};

#endif	// _ZEBRA_ROUTER_CLIENT_HH_
