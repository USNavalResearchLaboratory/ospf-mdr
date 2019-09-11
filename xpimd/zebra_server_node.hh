// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef _ZEBRA_SERVER_NODE_HH_
#define _ZEBRA_SERVER_NODE_HH_

#include <map>
#include <string>


template <class T>
class ZebraServerNode {

public:

    int add_client(const string &module_name, T &client_node)
    {
	if (_client_map.count(module_name) != 0)
	{
	    XLOG_WARNING("%s: module instance already exists: %s",
			 __func__, module_name.c_str());
	    return XORP_ERROR;
	}
	_client_map[module_name] = &client_node;
	return XORP_OK;
    }

    int delete_client(const string &module_name)
    {
	if (_client_map.erase(module_name) == 0)
	{
	    XLOG_WARNING("%s: module instance not found: %s",
			 __func__, module_name.c_str());
	    return XORP_ERROR;
	}
	return XORP_OK;
    }

    T *find_client(const string& module_name,
		   xorp_module_id module_id, string& error_msg)
    {
	typename map<string, T *>::const_iterator iter;
	iter = _client_map.find(module_name);
	if (iter == _client_map.end())
	{
	    error_msg = "module not found: " + module_name;
	    return NULL;
	}
	return (*iter).second;
    }

private:

    map<string, T *> _client_map;
};

#endif	// _ZEBRA_SERVER_NODE_HH_
