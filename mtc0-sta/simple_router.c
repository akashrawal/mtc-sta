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

//TODO: Cross-check effect of 'dispose' for every function

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
	//RULE: only dispose() can (and should) remove peer 
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

static void mtc_simple_peer_discard(MtcSimplePeer *peer)
{
	if (peer->link)
	{
		mtc_simple_peer_remove(peer);
		mtc_simple_peer_set_backend(peer, NULL);
		mtc_link_set_events_enabled(peer->link, 0);
		
		mtc_link_unref(peer->link);
		peer->link = NULL;
	}
}

static void mtc_simple_peer_dispose(MtcSimplePeer *peer)
{
	mtc_simple_peer_discard(peer);
	
	mtc_peer_reset((MtcPeer *) peer);
}

static void mtc_simple_peer_deliver
	(MtcSimplePeer *peer, MtcLinkInData in_data)
{
	MtcSimpleMail mail;
	
	if (in_data.stop)
		mtc_simple_peer_dispose(peer);
	
	//Deserialize the mail
	if (MtcSimpleMail__deserialize(in_data.msg, &mail) < 0)
	{
		mtc_simple_peer_dispose(peer);
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
	
	mtc_simple_peer_dispose(peer);
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

//TODO: Implement using events after implementing
static int mtc_simple_router_sync_io_step
	(MtcRouter *router, MtcPeer *p)
{
	//MtcSimpleRouter *self = (MtcSimpleRouter *) router;
	MtcSimplePeer *peer = (MtcSimplePeer *) p;
	MtcLinkInData in_data;
	MtcLinkIOStatus status;
	
	if (! peer)
		mtc_error("Synchronous reception from all peers is not "
			"supported at this moment\n");
	
	//Do nothing if disposed
	if (! peer->link)
		return -1;
	
	mtc_fd_link_set_blocking(peer->link, 1);
	
	status = mtc_link_send(peer->link);
	
	if (status != MTC_LINK_IO_OK)
		goto fail;
	
	status = mtc_link_receive(peer->link, &in_data);
	
	if (status != MTC_LINK_IO_OK)
		goto fail;
	
	mtc_simple_peer_deliver(peer, in_data);
	
	return 0;
fail:
	if (status != MTC_LINK_IO_TEMP)
		mtc_simple_peer_dispose(peer);

	return -1;
}

static void mtc_simple_peer_destroy(MtcPeer *p)
{
	MtcSimplePeer *peer = (MtcSimplePeer *) p;
	
	mtc_simple_peer_discard(peer);
	
	mtc_peer_destroy(p);
	
	mtc_free(p);
}

static void mtc_simple_router_destroy(MtcRouter *router)
{
	MtcSimpleRouter *self = (MtcSimpleRouter *) router;
	
	MtcRing *sentinel = &(self->peers);
	
	if (sentinel->next != sentinel)
		mtc_error("Peers still remaining with router in destruction");
}

MtcRouterVTable	mtc_simple_router_vtable =
{
	mtc_simple_router_set_event_mgr,
	mtc_simple_peer_sendto,
	mtc_simple_router_sync_io_step,
	mtc_simple_peer_destroy,
	mtc_simple_router_destroy
};

MtcRouter *mtc_simple_router_new()
{
	MtcSimpleRouter *self = (MtcSimpleRouter *) mtc_router_create
		(sizeof(MtcSimpleRouter), &mtc_simple_router_vtable);
		
	mtc_simple_router_init_ring(self);
	
	return (MtcRouter *) self;
}

MtcPeer *mtc_simple_router_add(MtcRouter *router, int fd)
{
	MtcSimpleRouter *self = (MtcSimpleRouter *) router;
	
	MtcSimplePeer *peer = (MtcSimplePeer *)
		mtc_alloc(sizeof(MtcSimplePeer));
	
	//Parent's constructor
	mtc_peer_init((MtcPeer *) peer, router);
	
	//Create link
	peer->link = mtc_fd_link_new(fd, fd);
	
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
	
	mtc_simple_peer_dispose(peer);
}

int mtc_simple_peer_is_connected(MtcPeer *p)
{
	MtcSimplePeer *peer = (MtcSimplePeer *) p;
	
	return peer->link ? 1 : 0;
}

