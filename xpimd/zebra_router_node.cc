// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#include "zebra_router_node.hh"

extern "C" {
#include "command.h"
#include "vty.h"
}

static ZebraRouterNode *_zrouter = NULL;

// interface node
static struct cmd_node interface_node = {
    INTERFACE_NODE,
    "%s(config-if)# ",
    1 				// vtysh
};

// debug node
static struct cmd_node debug_node = {
    DEBUG_NODE,
    "",
    1				// vtysh
};


void
ZebraRouterNode::zebra_start()
{
    bool redist[ZEBRA_ROUTE_MAX];

    /* get routes of all type from zebra */
    for (int i = 0; i < ZEBRA_ROUTE_MAX; i++)
	redist[i] = true;

    return ZebraRouter::zebra_start(redist, false);
}

void
ZebraRouterNode::zebra_rid_update(const struct prefix *rid)
{
    for (list<ZebraRidUpdateCb>::const_iterator it =
	     _rid_update_cblist.begin();
	 it != _rid_update_cblist.end(); ++it)
    {
	const ZebraRidUpdateCb &cb = *it;
	cb->dispatch(rid);
    }
}

void
ZebraRouterNode::dispatch_zebra_if_cblist(const list<ZebraIfCb> &cblist,
					  const struct interface *ifp)

{
    for (list<ZebraIfCb>::const_iterator it = cblist.begin();
	 it != cblist.end(); ++it)
    {
	const ZebraIfCb &cb = *it;
	cb->dispatch(ifp);
    }
}

void
ZebraRouterNode::dispatch_zebra_if_addr_cblist(const list<ZebraIfAddrCb> &cblist,
					       const struct connected *c)

{
    for (list<ZebraIfAddrCb>::const_iterator it = cblist.begin();
	 it != cblist.end(); ++it)
    {
	const ZebraIfAddrCb &cb = *it;
	cb->dispatch(c);
    }
}

void
ZebraRouterNode::dispatch_zebra_ipv4_route_cblist(const list<ZebraIpv4RtCb> &cblist,
						  const struct prefix_ipv4 *p,
						  u_char numnexthop,
						  const struct in_addr *nexthop,
						  const u_int32_t *ifindex,
						  u_int32_t metric)
{
    for (list<ZebraIpv4RtCb>::const_iterator it = cblist.begin();
	 it != cblist.end(); ++it)
    {
	const ZebraIpv4RtCb &cb = *it;
	cb->dispatch(p, numnexthop, nexthop, ifindex, metric);
    }
}

#ifdef HAVE_IPV6
void
ZebraRouterNode::dispatch_zebra_ipv6_route_cblist(const list<ZebraIpv6RtCb> &cblist,

						  const struct prefix_ipv6 *p,
						  u_char numnexthop,
						  const struct in6_addr *nexthop,
						  const u_int32_t *ifindex,
						  u_int32_t metric)
{
    for (list<ZebraIpv6RtCb>::const_iterator it = cblist.begin();
	 it != cblist.end(); ++it)
    {
	const ZebraIpv6RtCb &cb = *it;
	cb->dispatch(p, numnexthop, nexthop, ifindex, metric);
    }
}
#endif	// HAVE_IPV6

void
ZebraRouterNode::zebra_command_init()
{
    XLOG_ASSERT(_zrouter == NULL);

    _zrouter = this;

    // install the interface node
    install_element(CONFIG_NODE, &interface_cmd);
    install_element(CONFIG_NODE, &no_interface_cmd);
    install_node(&interface_node, config_write_zrouter_interface);
    install_default(INTERFACE_NODE); // add the default commands (exit, etc.)
    install_element(INTERFACE_NODE, &interface_desc_cmd);
    install_element(INTERFACE_NODE, &no_interface_desc_cmd);

    // install the debug node
    install_node(&debug_node, config_write_zrouter_debug);
    install_default(DEBUG_NODE); // add the default commands (exit, etc.)
}

int
ZebraRouterNode::zebra_config_write_interface(struct vty *vty)
{
    int r = CMD_SUCCESS;
    for (list<ZebraConfigWriteCb>::const_iterator it =
	     _config_write_interface_cblist.begin();
	 it != _config_write_interface_cblist.end(); ++it)
    {
	const ZebraConfigWriteCb &cb = *it;

	int tmp = cb->dispatch(vty);
	if (tmp != CMD_SUCCESS)
	    r = tmp;
    }

    return r;
}

int
ZebraRouterNode::zebra_config_write_debug(struct vty *vty)
{
    int r = CMD_SUCCESS;
    for (list<ZebraConfigWriteCb>::const_iterator it =
	     _config_write_debug_cblist.begin();
	 it != _config_write_debug_cblist.end(); ++it)
    {
	const ZebraConfigWriteCb &cb = *it;

	int tmp = cb->dispatch(vty);
	if (tmp != CMD_SUCCESS)
	    r = tmp;
    }

    return r;
}

// interface configuration write
int
config_write_zrouter_interface(struct vty *vty)
{
    XLOG_ASSERT(_zrouter != NULL);

    return _zrouter->zebra_config_write_interface(vty);
}

// debug configuration write
int
config_write_zrouter_debug(struct vty *vty)
{
    XLOG_ASSERT(_zrouter != NULL);

    return _zrouter->zebra_config_write_debug(vty);
}
