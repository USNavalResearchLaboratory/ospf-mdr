// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#include "zebra_router_module.h"

// #include "libxorp/xorp.h"
#include "libxorp/xlog.h"

#include "zebra_misc.hh"

extern "C" {
#include "command.h"
#include "vty.h"
}

#ifndef VNL
#define VNL VTY_NEWLINE
#endif

#define VTY_TERM vty::VTY_TERM


int
cli_process_command(ProtoNodeCli *pncli, const string& command_name,
		    const string& command_args, struct vty *vty)
{
    string ret_processor_name;
    string ret_cli_term_name;
    uint32_t ret_cli_session_id;
    string ret_command_output;

    XLOG_ASSERT(pncli != NULL);

    if (pncli->cli_process_command("", // processor_name
				  "", // cli_term_name
				  0, // cli_session_id
				  command_name,
				  command_args,
				  ret_processor_name,
				  ret_cli_term_name,
				  ret_cli_session_id,
				  ret_command_output) != XORP_OK)
    {
	vty_out(vty, "cli_process_command() failed: %s%s",
		ret_command_output.c_str(), VNL);
	return CMD_WARNING;
    }

    string::size_type start = 0;
    do {
	string::size_type end = ret_command_output.find("\n", start);
	if (end == string::npos)
	    end = ret_command_output.length();

	vty_out(vty, "%s",
                ret_command_output.substr(start, end - start).c_str());
	vty_out(vty, VNL);
	start = end + 1;
    } while (start < ret_command_output.length());

    return CMD_SUCCESS;
}
