/* simple_server.c
 * A simple server listening for connections on a socket
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

//MtcPeerList

typedef struct _MtcPeerRing MtcPeerRing;
struct _MtcPeerRing
{
	MtcPeerRing *next, *prev;
};

typedef struct 
{
	MtcPeerRing ring;
	MtcPeer *peer;
	MtcPeerResetNotify notify;
} MtcPeerHolder;

struct _MtcPeerSet
{
	int refcount;
	
	MtcPeerRing sentinel;
	MtcRouter *simple_router;
};

static void mtc_peer_holder_destroy(MtcPeerHolder *holder)
{
	MtcPeerRing *ring = &(holder->ring);
	
	ring->next->prev = ring->prev;
	ring->prev->next = ring->next;
	
	mtc_peer_reset_notify_remove(&(holder->notify));
	mtc_peer_unref(holder->peer);
	mtc_free(holder);
}

static void mtc_peer_holder_reset_notify(MtcPeerResetNotify *notify)
{
	MtcPeerHolder *holder = mtc_encl_struct
		(notify, MtcPeerHolder, notify);
	
	mtc_peer_holder_destroy(holder);
}

void mtc_peer_set_add(MtcPeerSet *peer_set, int fd, int close_fd)
{
	//Create new holder
	MtcPeerHolder *holder = (MtcPeerHolder *) 
		mtc_alloc(sizeof(MtcPeerHolder));
	MtcPeerRing *ring, *sentinel;
	
	//Add to ring
	sentinel = &(peer_set->sentinel);
	ring = &(holder->ring);
	ring->next = sentinel;
	ring->prev = sentinel->prev;
	ring->next->prev = ring;
	ring->prev->next = ring;
	
	//Initialize
	holder->peer = mtc_simple_router_add(peer_set->simple_router, fd, close_fd);
	holder->notify.cb = mtc_peer_holder_reset_notify;
	mtc_peer_add_reset_notify(holder->peer, &(holder->notify));
}


MtcPeerSet *mtc_peer_set_new(MtcRouter *simple_router)
{
	MtcPeerSet *peer_set;
	MtcPeerRing *sentinel;
	
	peer_set = (MtcPeerSet *) mtc_alloc(sizeof(MtcPeerSet));
	
	peer_set->refcount = 1;
	sentinel = &(peer_set->sentinel);
	sentinel->next = sentinel->prev = sentinel;
	peer_set->simple_router = simple_router;
	mtc_router_ref(simple_router);
	
	return peer_set;
}

void mtc_peer_set_ref(MtcPeerSet *peer_set)
{
	peer_set->refcount++;
}

void mtc_peer_set_unref(MtcPeerSet *peer_set)
{
	peer_set->refcount--;
	
	if (peer_set->refcount <= 0)
	{
		MtcPeerRing *sentinel = &(peer_set->sentinel);
		while (sentinel->next != sentinel)
			mtc_peer_holder_destroy((MtcPeerHolder *) sentinel->next);
		
		mtc_router_unref(peer_set->simple_router);
		
		mtc_free(peer_set);
	}
}


//MtcSimpleListener

static void mtc_simple_listener_event
	(MtcEventSource *source, MtcEventFlags event)
{
	MtcSimpleListener *listener = (MtcSimpleListener *) source;
	
	if ((event & MTC_EVENT_CHECK) && listener->active)
	{
		int fd;
		
		fd = accept(listener->test.fd, NULL, NULL);
		
		if (fd == -1)
		{
			if (MTC_IO_TEMP_ERROR(errno))
				mtc_error("accept(): %s", strerror(errno));
			return;
		}
		
		(* listener->accepted) (listener, fd);
	}
}

static MtcEventSourceVTable mtc_simple_listener_vtable =
{
	mtc_simple_listener_event,
	MTC_EVENT_CHECK
};

MtcSimpleListener *mtc_simple_listener_new(int svr_fd)
{
	MtcSimpleListener *listener = (MtcSimpleListener *) mtc_alloc
		(sizeof(MtcSimpleListener));
	
	//Parent class
	mtc_event_source_init
		((MtcEventSource *) listener, &mtc_simple_listener_vtable);
	
	//Data members
	listener->refcount = 1;
	listener->active = 0;
	listener->close_fd = 0;
	mtc_event_test_pollfd_init(&(listener->test), svr_fd, MTC_POLLIN);
	mtc_fd_set_blocking(svr_fd, 0);
	listener->peer_set = NULL;
	
	listener->accepted = NULL;
	listener->data = NULL;
	
	return listener;
}

void mtc_simple_listener_ref(MtcSimpleListener *listener)
{
	listener->refcount++;
}

void mtc_simple_listener_unref(MtcSimpleListener *listener)
{
	listener->refcount--;
	if (listener->refcount <= 0)
	{
		if (listener->close_fd)
			close(listener->test.fd);
		if (listener->peer_set)
			mtc_peer_set_unref(listener->peer_set);
		
		mtc_event_source_destroy((MtcEventSource *) listener);
		
		mtc_free(listener);
	}
}

void mtc_simple_listener_set_active
	(MtcSimpleListener *listener, int val)
{
	if ((! listener->active) && val)
	{
		listener->active = 1;
		mtc_event_source_prepare
			((MtcEventSource *) listener, 
			(MtcEventTest *) &(listener->test));
	}
	else if (listener->active && (! val))
	{
		listener->active = 0;
		mtc_event_source_prepare
			((MtcEventSource *) listener, NULL);
	}
}

void mtc_simple_listener_set_close_fd
	(MtcSimpleListener *listener, int val)
{
	listener->close_fd = val ? 1 : 0;
}

static void mtc_simple_listener_add_peer_cb
	(MtcSimpleListener *listener, int fd)
{
	mtc_peer_set_add(listener->peer_set, fd, 1);
}

void mtc_simple_listener_set_peer_set
	(MtcSimpleListener *listener, MtcPeerSet *peer_set)
{
	listener->accepted = mtc_simple_listener_add_peer_cb;
	listener->peer_set = peer_set;
	listener->data = NULL;
	mtc_peer_set_ref(peer_set);
	mtc_simple_listener_set_active(listener, 1);
}

void mtc_simple_listener_unset_peer_set
	(MtcSimpleListener *listener)
{
	mtc_simple_listener_set_active(listener, 0);
	mtc_peer_set_unref(listener->peer_set);
	listener->peer_set = NULL;
	listener->accepted = NULL;
	listener->data = NULL;
}
