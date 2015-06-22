/* event.c
 * libevent based backend for MtcEventMgr
 * 
 * Copyright 2013 Akash Rawal
 * This file is part of MTC-Standalone.
 * 
 * MTC-Standalone is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * MTC-Standalone is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with MTC-Standalone.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"

//Translator between libevent and MtcEventTestPollFD events
const static struct {int mtc_flag, lev_flag;} translator[] = {
				{MTC_POLLIN,   EV_READ},
				{MTC_POLLOUT,  EV_WRITE}};
const static int n_flags = 2;

static short mtc_poll_to_lev(int events)
{
	int i, res = EV_PERSIST;
	
	for (i = 0; i < n_flags; i++)
		if (events & translator[i].mtc_flag)
			res |= translator[i].lev_flag;
	
	return res;
}

static int mtc_poll_from_lev(short events)
{
	int i, res = 0;
	
	for (i = 0; i < n_flags; i++)
		if (events & translator[i].lev_flag)
			res |= translator[i].mtc_flag;
	
	return res;
}

//Structures
typedef struct
{
	MtcEventMgr parent;
	
	struct event_base *base;
	int destroy_base;
} MtcLevEventMgr;

typedef struct _MtcLevTest MtcLevTest;

typedef struct 
{
	MtcEventBackend parent;
	
	MtcLevTest *lev_tests;
	MtcLevEventMgr *lmgr;
} MtcLevEventBackend;

struct _MtcLevTest
{
	MtcLevTest *next;
	
	int refcount;
	MtcEventTest *test;
	MtcLevEventBackend *backend;
	
	struct event *ev;
};

//MtcLevTest

static void mtc_lev_test_dispose(MtcLevTest *lev_test)
{
	if (lev_test->test)
	{
		event_del(lev_test->ev);
		event_free(lev_test->ev);
		lev_test->test = NULL;
		lev_test->ev = NULL;
	}
}

static int mtc_lev_test_is_disposed(MtcLevTest *lev_test)
{
	return lev_test->test ? 0 : 1;
}

static void mtc_lev_test_ref(MtcLevTest *lev_test)
{
	lev_test->refcount++;
}

static void mtc_lev_test_unref(MtcLevTest *lev_test)
{
	lev_test->refcount--;
	if (lev_test->refcount <= 0)
	{
		mtc_lev_test_dispose(lev_test);
		mtc_free(lev_test);
	}
}

static void mtc_lev_test_cb(evutil_socket_t fd, short events, void *arg)
{
	MtcLevTest *lev_test = (MtcLevTest *) arg;
	
	MtcEventTestPollFD *fd_test = (MtcEventTestPollFD *) lev_test->test;
	
	mtc_lev_test_ref(lev_test);
	fd_test->revents = mtc_poll_from_lev(events);
	mtc_event_backend_event
		((MtcEventBackend *) lev_test->backend, MTC_EVENT_CHECK);
	if (! mtc_lev_test_is_disposed(lev_test))
		fd_test->revents = 0;
	mtc_lev_test_unref(lev_test);
}

static MtcLevTest *mtc_lev_test_new
	(MtcLevEventBackend *backend, MtcEventTest *test)
{
	MtcLevTest *lev_test;
	
	if (mtc_event_test_check_name(test, MTC_EVENT_TEST_POLLFD))
	{
		MtcEventTestPollFD *fd_test = (MtcEventTestPollFD *) test;
		
		lev_test = (MtcLevTest *) mtc_alloc
			(sizeof(MtcLevTest) + event_get_struct_event_size());
		
		lev_test->refcount = 1;
		lev_test->next = NULL;
		lev_test->test = test;
		lev_test->backend = backend;
		lev_test->ev = event_new(backend->lmgr->base, fd_test->fd, 
			mtc_poll_to_lev(fd_test->events), 
			mtc_lev_test_cb, lev_test);
		event_add(lev_test->ev, NULL);
		
		fd_test->revents = 0;
	}
	else
	{
		mtc_error("Event test \"%s\" not supported", test->name);
	}
	
	return lev_test;
}

//MtcLevEventBackend
static void mtc_lev_event_backend_purge(MtcLevEventBackend *backend)
{
	MtcLevTest *lev_test, *next;
	
	for (lev_test = backend->lev_tests; lev_test; lev_test = next)
	{
		next = lev_test->next;
		mtc_lev_test_dispose(lev_test);
		mtc_lev_test_unref(lev_test);
	}
	backend->lev_tests = NULL;
}

static void mtc_lev_event_backend_init
	(MtcEventBackend *b,  MtcEventMgr *mgr)
{
	MtcLevEventBackend *backend = (MtcLevEventBackend *) b;
	
	mtc_event_mgr_ref(mgr);
	backend->lmgr = (MtcLevEventMgr *) mgr;
	backend->lev_tests = NULL;
}

static void mtc_lev_event_backend_destroy(MtcEventBackend *b)
{
	MtcLevEventBackend *backend = (MtcLevEventBackend *) b;
	
	mtc_lev_event_backend_purge(backend);
	mtc_event_mgr_unref((MtcEventMgr *) backend->lmgr);
}

static void mtc_lev_event_backend_prepare
	(MtcEventBackend *b, MtcEventTest *tests)
{
	MtcLevEventBackend *backend = (MtcLevEventBackend *) b;
	MtcLevTest *lev_test, *list = NULL;
	MtcEventTest *test_iter;
	
	mtc_lev_event_backend_purge(backend);
	
	for (test_iter = tests; test_iter; test_iter = test_iter->next)
	{
		lev_test = mtc_lev_test_new(backend, test_iter);
		lev_test->next = list;
		list = lev_test;
	}
	backend->lev_tests = list;
}

//MtcLevEventMgr
static void mtc_lev_event_mgr_destroy(MtcEventMgr *mgr)
{
	MtcLevEventMgr *lmgr = (MtcLevEventMgr *) mgr;
	
	if (lmgr->destroy_base)
	{
		event_base_free(lmgr->base);
	}
	
	mtc_event_mgr_destroy(mgr);
	
	mtc_free(mgr);
}

MtcEventBackendVTable mtc_lev_event_vtable = 
{
	sizeof(MtcLevEventBackend),
	mtc_lev_event_backend_init,
	mtc_lev_event_backend_destroy,
	mtc_lev_event_backend_prepare,
	
	mtc_lev_event_mgr_destroy
};

MtcEventMgr *mtc_lev_event_mgr_new
	(struct event_base *base, int destroy_base)
{
	MtcLevEventMgr *lmgr;
	
	lmgr = (MtcLevEventMgr *) mtc_alloc(sizeof(MtcLevEventMgr));
	
	mtc_event_mgr_init((MtcEventMgr *) lmgr, &mtc_lev_event_vtable);
	
	lmgr->base = base;
	lmgr->destroy_base = destroy_base;
	
	return (MtcEventMgr *) lmgr;
}
