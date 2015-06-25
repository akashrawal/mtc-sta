/* simple_router.c
 * A simple MtcRouter implementation that connects to peers 
 * over stream socket links
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

#include <mtc0-sta/simple_router_declares.h>
#include <mtc0-sta/simple_router_defines.h>


typedef struct _MtcSimplePeer MtcSimplePeer;

struct _MtcSimplePeer
{
	MtcPeer parent;
	
	MtcRing peer_ring;
	MtcLink *link;
	MtcEventBackend *backend;
};

typedef struct _MtcSimpleRouter MtcSimpleRouter;

struct _MtcSimpleRouter
{
	MtcRouter parent;
	
	MtcRing peers;
	
	struct
	{
		struct event_base *base;
		MtcEventMgr *mgr;
		MtcEventBackend *backend;
		MtcSimplePeer *peer;
	} sync_cache;
	
	MtcLinkAsyncFlush *flush;
};

//Peer ring management

#define mtc_simple_peer_from_ring(ring) \
	((MtcSimplePeer *) \
		(MTC_PTR_ADD((ring), - offsetof(MtcSimplePeer, peer_ring))))

static void mtc_simple_peer_insert
	(MtcSimplePeer *peer, MtcSimpleRouter *router)
{
	//RULE: only constructor calls this
	MtcRing *sentinel = &(router->peers), *ring = &(peer->peer_ring);
	
	ring->next = sentinel;
	ring->prev = sentinel->prev;
	ring->next->prev = ring;
	ring->prev->next = ring;
}

static void mtc_simple_peer_remove(MtcSimplePeer *peer)
{
	//RULE: only close() and discard() can (and should) remove peer 
	//from router
	MtcRing *ring = &(peer->peer_ring);
	
	ring->next->prev = ring->prev;
	ring->prev->next = ring->next;
	ring->next = ring;
	ring->prev = ring;
}

static void mtc_simple_router_init_ring(MtcSimpleRouter *self)
{
	MtcRing *sentinel = &(self->peers);
	sentinel->next = sentinel;
	sentinel->prev = sentinel;
}

//Sync cache management
static void mtc_simple_router_init_sync_cache(MtcSimpleRouter *self)
{
	self->sync_cache.base = event_base_new();
	self->sync_cache.mgr = mtc_lev_event_mgr_new
		(self->sync_cache.base, 1);
	self->sync_cache.backend = NULL;
	self->sync_cache.peer = NULL;
}

static void mtc_simple_router_clear_sync_cache
	(MtcSimpleRouter *self, MtcSimplePeer *clear_for)
{
	if (self->sync_cache.peer == clear_for)
	{
		mtc_event_backend_destroy(self->sync_cache.backend);
		self->sync_cache.backend = NULL;
		self->sync_cache.peer = NULL;
	}
}

static void mtc_simple_router_set_sync_cache
	(MtcSimpleRouter *self, MtcSimplePeer *peer)
{
	if (self->sync_cache.peer != peer)
	{
		if (self->sync_cache.peer)
		{
			mtc_event_backend_destroy(self->sync_cache.backend);
			self->sync_cache.backend = NULL;
			self->sync_cache.peer = NULL;
		}
		if (peer)
		{
			self->sync_cache.peer = peer;
			self->sync_cache.backend = mtc_event_mgr_back
				(self->sync_cache.mgr, (MtcEventSource *) 
				mtc_link_get_event_source(peer->link));
		}
	}
}

static void mtc_simple_router_destroy_sync_cache
	(MtcSimpleRouter *self)
{
	mtc_simple_router_set_sync_cache(self, NULL);
	mtc_event_mgr_unref(self->sync_cache.mgr);
}

//IO management
static void mtc_simple_peer_set_backend
	(MtcSimplePeer *peer, MtcEventMgr *mgr)
{
	if (peer->backend)
	{
		mtc_event_backend_destroy(peer->backend);
		peer->backend = NULL;
	}
	if (mgr)
	{
		MtcLinkEventSource *source 
			= mtc_link_get_event_source(peer->link);
			
		peer->backend = mtc_event_mgr_back
			(mgr, (MtcEventSource *) source);
	}
}

static void mtc_simple_peer_close(MtcSimplePeer *peer)
{
	if (peer->link)
	{
		MtcSimpleRouter *router = (MtcSimpleRouter *) 
			mtc_peer_get_router(peer);
		
		mtc_simple_router_clear_sync_cache(router, peer);
		
		mtc_simple_peer_remove(peer);
		
		mtc_link_async_flush_add(router->flush, peer->link);
		mtc_link_unref(peer->link);
		peer->link = NULL;
	}
}

static void mtc_simple_peer_discard(MtcSimplePeer *peer)
{
	if (peer->link)
	{
		MtcSimpleRouter *router = (MtcSimpleRouter *) 
			mtc_peer_get_router(peer);
		
		mtc_simple_router_clear_sync_cache(router, peer);
		
		mtc_simple_peer_remove(peer);
		
		mtc_simple_peer_set_backend(peer, NULL);
		mtc_link_set_events_enabled(peer->link, 0);
		mtc_link_unref(peer->link);
		peer->link = NULL;
	}
}

static void mtc_simple_peer_broken_respond(MtcSimplePeer *peer)
{
	mtc_simple_peer_discard(peer);
	
	mtc_peer_reset((MtcPeer *) peer);
}

static void mtc_simple_peer_deliver
	(MtcSimplePeer *peer, MtcLinkInData in_data)
{
	MtcSimpleMail mail;
	
	if (in_data.stop)
		mtc_simple_peer_broken_respond(peer);
	
	//Deserialize the mail
	if (MtcSimpleMail__deserialize(in_data.msg, &mail) < 0)
	{
		mtc_simple_peer_broken_respond(peer);
		return;
	}
	
	//Deliver mail
	mtc_router_deliver(mtc_peer_get_router(peer), mail.dest,
		(MtcPeer *) peer, mail.ret, mail.payload);
	
	//Free data
	MtcSimpleMail__free(&mail);
}

static void mtc_simple_peer_received_cb
	(MtcLink *link, MtcLinkInData in_data, void *data)
{
	MtcSimplePeer *peer = (MtcSimplePeer *) data;
	
	mtc_simple_peer_deliver(peer, in_data);
}

static void mtc_simple_peer_broken_cb
	(MtcLink *link, void *data)
{
	MtcSimplePeer *peer = (MtcSimplePeer *) data;
	
	mtc_simple_peer_broken_respond(peer);
}

static void mtc_simple_peer_setup_events(MtcSimplePeer *peer)
{
	//RULE: called by constructor
	MtcLinkEventSource *source = mtc_link_get_event_source(peer->link);
	
	source->received = mtc_simple_peer_received_cb;
	source->broken = mtc_simple_peer_broken_cb;
	source->stopped = mtc_simple_peer_broken_cb;
	source->data = (void *) peer;
	
	mtc_link_set_events_enabled(peer->link, 1);
}


//Implementation vtable

static void mtc_simple_router_set_event_mgr
	(MtcRouter *router, MtcEventMgr *mgr)
{
	MtcSimpleRouter *self = (MtcSimpleRouter *) router;
	
	MtcSimplePeer *iter;
	MtcRing *r, *sentinel = &(self->peers);
	
	for (r = sentinel->next; r != sentinel; r = r->next)
	{
		iter = mtc_simple_peer_from_ring(r);
		
		mtc_simple_peer_set_backend(iter, mgr);
	}
}

static void mtc_simple_peer_sendto(MtcPeer *p, 
	MtcMBlock addr,
	MtcDest *reply_dest, MtcMsg *payload)
{
	MtcSimplePeer *peer = (MtcSimplePeer *) p;
	MtcSimpleMail mail;
	MtcMsg *mail_msg;
	
	//Don't send if disposed
	if (! peer->link)
	{
		mtc_peer_reset(p);
		return;
	}
	
	//Prepare mail
	mail.dest = addr;
	mtc_rcmem_ref(addr.mem);
	if (reply_dest)
	{
		mail.ret = mtc_dest_get_addr(reply_dest);
	}
	else
	{
		mail.ret.mem = NULL;
		mail.ret.size = 0;
	}
	mail.payload = payload;
	mtc_msg_ref(payload);
	
	//Serialize
	mail_msg = MtcSimpleMail__serialize(&mail);
	
	//Send mail
	mtc_link_queue(peer->link, mail_msg, 0);
	
	//Free
	mtc_msg_unref(mail_msg);
	MtcSimpleMail__free(&mail);
}

static int mtc_simple_peer_sync_io_step(MtcPeer *p)
{
	MtcSimplePeer *peer = (MtcSimplePeer *) p;
	MtcSimpleRouter *router = (MtcSimpleRouter *) 
		mtc_peer_get_router(peer);
	int status;
	
	mtc_simple_router_set_sync_cache(router, peer);
	
	status = event_base_loop(router->sync_cache.base, EVLOOP_ONCE);
	
	if (status < 0)
		return -1;
	else if (peer->link)
		return 0;
	else
		return -1;
}

static void mtc_simple_peer_destroy(MtcPeer *p)
{
	MtcSimplePeer *peer = (MtcSimplePeer *) p;
	
	mtc_simple_peer_close(peer);
	
	mtc_peer_destroy(p);
	
	mtc_free(p);
}

static void mtc_simple_router_destroy(MtcRouter *router)
{
	MtcSimpleRouter *self = (MtcSimpleRouter *) router;
	
	MtcRing *sentinel = &(self->peers);
	
	if (sentinel->next != sentinel)
		mtc_error("Peers still remaining with router in destruction");
	
	mtc_link_async_flush_unref(self->flush);
	
	mtc_simple_router_destroy_sync_cache(self);
}

MtcRouterVTable	mtc_simple_router_vtable =
{
	mtc_simple_router_set_event_mgr,
	mtc_simple_peer_sendto,
	mtc_simple_peer_sync_io_step,
	mtc_simple_peer_destroy,
	mtc_simple_router_destroy
};

MtcRouter *mtc_simple_router_new()
{
	MtcSimpleRouter *self = (MtcSimpleRouter *) mtc_router_create
		(sizeof(MtcSimpleRouter), &mtc_simple_router_vtable);
		
	mtc_simple_router_init_ring(self);
	self->flush = mtc_link_async_flush_new();
	mtc_simple_router_init_sync_cache(self);
	
	return (MtcRouter *) self;
}

MtcPeer *mtc_simple_router_add(MtcRouter *router, int fd, int close_fd)
{
	MtcSimpleRouter *self = (MtcSimpleRouter *) router;
	
	MtcSimplePeer *peer = (MtcSimplePeer *)
		mtc_alloc(sizeof(MtcSimplePeer));
	
	//Parent's constructor
	mtc_peer_init((MtcPeer *) peer, router);
	
	//Create link
	peer->link = mtc_fd_link_new(fd, fd);
	mtc_fd_link_set_close_fd(peer->link, fd);
	mtc_fd_set_blocking(fd, 0);
	
	//Add to router
	mtc_simple_peer_insert(peer, self);
	
	//Setup events
	peer->backend = NULL;
	mtc_simple_peer_setup_events(peer);
	mtc_simple_peer_set_backend
		(peer, mtc_router_get_event_mgr(router));
	
	return (MtcPeer *) peer;
}

void mtc_simple_peer_disconnect(MtcPeer *p)
{
	MtcSimplePeer *peer = (MtcSimplePeer *) p;
	
	mtc_simple_peer_broken_respond(peer);
}

int mtc_simple_peer_is_connected(MtcPeer *p)
{
	MtcSimplePeer *peer = (MtcSimplePeer *) p;
	
	return peer->link ? 1 : 0;
}

