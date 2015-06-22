/* simple_server.h
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
 
//TODO: documentation

//MtcPeerSet

typedef struct _MtcPeerSet MtcPeerSet;

MtcPeerSet *mtc_peer_set_new(MtcRouter *simple_router);

void mtc_peer_set_ref(MtcPeerSet *peer_set);

void mtc_peer_set_unref(MtcPeerSet *peer_set);

void mtc_peer_set_add(MtcPeerSet *peer_set, int fd, int close_fd);


//MtcSimpleListener

typedef struct _MtcSimpleListener MtcSimpleListener;

struct _MtcSimpleListener
{
	MtcEventSource parent;
	
	int refcount;
	int active;
	int close_fd;
	MtcEventTestPollFD test;
	MtcPeerSet *peer_set;
	
	void (*accepted) (MtcSimpleListener *listener, int fd);
	void *data;
};

MtcSimpleListener *mtc_simple_listener_new(int svr_fd);

void mtc_simple_listener_ref(MtcSimpleListener *listener);

void mtc_simple_listener_unref(MtcSimpleListener *listener);

void mtc_simple_listener_set_active
	(MtcSimpleListener *listener, int val);

#define mtc_simple_listener_get_active(listener) \
	((int) ((listener)->active))

void mtc_simple_listener_set_close_fd
	(MtcSimpleListener *listener, int val);

#define mtc_simple_listener_get_close_fd(listener) \
	((int) ((listener)->close_fd))

void mtc_simple_listener_set_peer_set
	(MtcSimpleListener *listener, MtcPeerSet *peer_set);

void mtc_simple_listener_unset_peer_set
	(MtcSimpleListener *listener);
