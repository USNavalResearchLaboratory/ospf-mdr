// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef _ZEBRA_MISC_HH_
#define _ZEBRA_MISC_HH_

#include "libproto/proto_node_cli.hh"

int cli_process_command(ProtoNodeCli *pncli, const string& command_name,
			const string& command_args, struct vty *vty);

#endif	// _ZEBRA_MISC_HH_
