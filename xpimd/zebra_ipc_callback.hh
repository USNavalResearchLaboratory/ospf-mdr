// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef __ZEBRA_IPC_CALLBACK_HH__
#define __ZEBRA_IPC_CALLBACK_HH__

#include "libxorp/callback.hh"
#include "libxorp/eventloop.hh"


template <class T, class M>
class ZebraIpcCallback {
public:
    ZebraIpcCallback(T &node, M method, EventLoop &eventloop) :
	_node(node), _method(method),
	_xorptask(eventloop.new_oneoff_task(callback(this,
						     &ZebraIpcCallback::calldelete)))
    {}
    virtual ~ZebraIpcCallback() {}
    virtual void calldelete()
    {
	dispatch();
	delete this;
    }
    virtual void dispatch() = 0;

protected:
    T &_node;
    const M _method;

private:
    XorpTask _xorptask;
};

template <class T, class M>
class ZebraIpcCallbackError {
public:
    ZebraIpcCallbackError(T &node, M method, EventLoop &eventloop) :
	_node(node), _method(method),
	_xorptask(eventloop.new_oneoff_task(callback(this,
						     &ZebraIpcCallbackError::calldelete)))
    {}
    virtual ~ZebraIpcCallbackError() {}
    virtual void calldelete()
    {
	string error_msg;
	dispatch(error_msg);
	delete this;
    }
    virtual void dispatch(string &error_msg) = 0;

protected:
    T &_node;
    const M _method;

private:
    XorpTask _xorptask;
};

#endif	// __ZEBRA_IPC_CALLBACK_HH__
