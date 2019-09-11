// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright 2014 The Boeing Company

#include "zebra_thread_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/callback.hh"
#include "libxorp/eventloop.hh"

#include "zebra_thread.hh"
#include "zebra_router.hh"

struct thread *
funcname_thread_add_read(struct thread_master *m, zthread_func_t func,
			 void *arg, int fd, const char *funcname)
{
    ZebraRouter *zr = (ZebraRouter *)m->data;
    return zr->zebra_thread_add_read(func, arg, fd);
}

struct thread *
funcname_thread_add_write(struct thread_master *m, zthread_func_t func,
			  void *arg, int fd, const char *funcname)
{
    ZebraRouter *zr = (ZebraRouter *)m->data;
    return zr->zebra_thread_add_write(func, arg, fd);
}

struct thread *
funcname_thread_add_event(struct thread_master *m, zthread_func_t func,
			  void *arg, int val, const char *funcname)
{
    ZebraRouter *zr = (ZebraRouter *)m->data;
    return zr->zebra_thread_add_event(func, arg, val);
}

struct thread *
funcname_thread_add_timer(struct thread_master *m, zthread_func_t func,
			  void *arg, long waitsec, const char *funcname)
{
    ZebraRouter *zr = (ZebraRouter *)m->data;
    return zr->zebra_thread_add_timer(func, arg, waitsec);
}

void thread_cancel(struct thread *thread)
{
    ZthreadCb *zcb = (ZthreadCb *)thread->data;
    zcb->canceldelete();
}

struct thread *
funcname_thread_add_timer_msec(struct thread_master *m, zthread_func_t func,
			       void *arg, long msec, const char *funcname)
{
    XLOG_UNREACHABLE();		// XXX
    return NULL;
}

struct thread *
funcname_thread_add_background(struct thread_master *m, zthread_func_t func,
			       void *arg, long msecdelay, const char *funcname)
{
    XLOG_UNREACHABLE();		// XXX
    return NULL;
}

struct thread *
funcname_thread_execute(struct thread_master *m, zthread_func_t func,
			void *arg, int val,  const char *funcname)
{
    XLOG_UNREACHABLE();		// XXX
    return NULL;
}

unsigned int
thread_cancel_event(struct thread_master *m, void *arg)
{
    XLOG_UNREACHABLE();		// XXX
    return 0;
}

struct thread *
thread_fetch(struct thread_master *m, struct thread *fetch)
{
    XLOG_UNREACHABLE();		// XXX
    return NULL;
}

void
thread_call(struct thread *thread)
{
    XLOG_UNREACHABLE();		// XXX
}

unsigned long
thread_timer_remain_second(struct thread *thread)
{
    XLOG_UNREACHABLE();		// XXX
    return 0;
}

int
thread_should_yield(struct thread *thread)
{
    XLOG_UNREACHABLE();		// XXX
    return 0;
}
