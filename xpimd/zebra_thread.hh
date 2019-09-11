// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#ifndef _ZEBRA_THREAD_H_
#define _ZEBRA_THREAD_H_

#include "zebra_thread_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"

/*
  include these before zebra.h to work around the C++ include problem
  for nd6.h mentioned below
*/
#ifdef HAVE_NET_IF_VAR_H
#include <net/if_var.h>
#endif
#ifdef HAVE_NETINET6_IN6_VAR_H
#include <netinet6/in6_var.h>
#endif
#ifdef HAVE_NETINET6_ND6_H
#ifdef HAVE_BROKEN_CXX_NETINET6_ND6_H
// XXX: a hack needed if <netinet6/nd6.h> is not C++ friendly
#define prf_ra in6_prflags::prf_ra
#endif
#include <netinet6/nd6.h>
#endif

extern "C" {
#include "zebra.h"
#include "thread.h"
}

#include "zebra_router.hh"

typedef int (*zthread_func_t)(struct thread *);

class ZthreadCb {

public:

    ZthreadCb(zthread_func_t func, void *arg)
    {
	memset(&_thread, 0, sizeof(_thread));
	_thread.func = func;
	_thread.arg = arg;
	_thread.data = this;
    }

    virtual ~ZthreadCb() {}

    struct thread *thread() {return &_thread;}

    void cbdelete()
    {
	_thread.func(&_thread);
	delete this;
    }

    virtual void cancel() = 0;

    void canceldelete()
    {
	cancel();
	delete this;
    }

protected:

    struct thread _thread;
};

class ZthreadEventCb : public ZthreadCb {

public:

    ZthreadEventCb(EventLoop &eventloop,
		   zthread_func_t func, void *arg, int val) :
	ZthreadCb(func, arg),
	_xorptask(eventloop.new_oneoff_task(callback(this, &ZthreadEventCb::cbdelete)))
    {
	_thread.u.val = val;
    }

    // this is needed to make XORP's callback() function happy
    void cbdelete() {ZthreadCb::cbdelete();}

    void cancel() {_xorptask.unschedule();}

private:

    XorpTask _xorptask;
};

class ZthreadTimerCb : public ZthreadCb {

public:

    ZthreadTimerCb(EventLoop &eventloop, long waitsec,
		   zthread_func_t func, void *arg) :
	ZthreadCb(func, arg),
	_xorptimer(eventloop.new_oneoff_after(TimeVal(waitsec, 0),
					      callback(this, &ZthreadTimerCb::cbdelete)))
    {}

    // this is needed to make XORP's callback() function happy
    void cbdelete() {ZthreadCb::cbdelete();}

    void cancel() {_xorptimer.unschedule();}

private:

    XorpTimer _xorptimer;
};

class ZthreadIOEventCb : public ZthreadCb {

public:

    ZthreadIOEventCb(EventLoop &eventloop, int fd, IoEventType iotype,
		     zthread_func_t func, void *arg) :
	ZthreadCb(func, arg), _eventloop(eventloop),
	_xorpfd(XorpFd(fd)), _iotype(iotype)
    {
	_thread.u.fd = (int)_xorpfd;
	IoEventCb cb = callback(this, &ZthreadIOEventCb::ioevent_cbdelete);
	if (_eventloop.add_ioevent_cb(_xorpfd, _iotype, cb) == false)
	    XLOG_FATAL("add_ioevent_cb() failed: read fd = %d",
		       (int)_xorpfd);
    }

    void ioevent_cbdelete(XorpFd fd, IoEventType iotype)
    {
	if (_eventloop.remove_ioevent_cb(fd, iotype) == false)
	    XLOG_FATAL("remove_ioevent_cb() failed: fd = %d; iotype = %d",
		       (int)fd, iotype);
	cbdelete();
    }

    void cancel()
    {
	if (_eventloop.remove_ioevent_cb(_xorpfd, _iotype) == false)
	    XLOG_FATAL("remove_ioevent_cb() failed: fd = %d; iotype = %d",
		       (int)_xorpfd, _iotype);
    }

private:

    EventLoop &_eventloop;
    XorpFd _xorpfd;
    IoEventType _iotype;
};

extern "C" {
struct thread *funcname_thread_add_read(struct thread_master *m,
					zthread_func_t, void *arg, int fd,
					const char *funcname);
struct thread *funcname_thread_add_write(struct thread_master *m,
					 zthread_func_t, void *arg, int fd,
					 const char *funcname);
struct thread *funcname_thread_add_event(struct thread_master *m,
					 zthread_func_t, void *arg, int val,
					 const char *funcname);
struct thread *funcname_thread_add_timer(struct thread_master *m,
					 zthread_func_t, void *arg, long timer,
					 const char *funcname);
struct thread *funcname_thread_add_timer_msec(struct thread_master *m,
					      zthread_func_t func, void *arg,
					      long msec, const char *funcname);
struct thread *funcname_thread_add_background(struct thread_master *m,
					      zthread_func_t func, void *arg,
					      long msecdelay,
					      const char *funcname);
struct thread *funcname_thread_execute(struct thread_master *m,
				       zthread_func_t func, void *arg,
				       int val,  const char *funcname);
void thread_cancel(struct thread *);
unsigned int thread_cancel_event(struct thread_master *m, void *arg);
struct thread *thread_fetch(struct thread_master *m, struct thread *fetch);
void thread_call(struct thread *thread);
unsigned long thread_timer_remain_second(struct thread *thread);
int thread_should_yield(struct thread *thread);
}

#endif	// _ZEBRA_THREAD_H_
