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
 
/**
 * \addtogroup mtc_simple_server
 * \{
 * 
 * This module can help you set up a simple server 
 * based on simple router.
 */

//MtcPeerSet

///A collection of incoming connections.
typedef struct _MtcPeerSet MtcPeerSet;

/**Creates a new collection
 * \param simple_router A simple router to add peers to
 * \return A new collection
 */
MtcPeerSet *mtc_peer_set_new(MtcRouter *simple_router);

/**Increments the reference count by 1
 * \param peer_set an MtcPeerSet
 */
void mtc_peer_set_ref(MtcPeerSet *peer_set);

/**Decrements the reference count by 1
 * \param peer_set an MtcPeerSet
 */
void mtc_peer_set_unref(MtcPeerSet *peer_set);

/**Adds a new peer to underlying router
 * \param peer_set an MtcPeerSet
 * \param fd A socket file descriptor
 * \param close_fd Whether to close file descriptor when 
 *        corresponding peer is destroyed.
 */
void mtc_peer_set_add(MtcPeerSet *peer_set, int fd, int close_fd);


//MtcSimpleListener

///A listener that asynchronously accepts connections to a 
///socket.
typedef struct _MtcSimpleListener MtcSimpleListener;

struct _MtcSimpleListener
{
	MtcEventSource parent;
	
	int refcount;
	int active;
	int close_fd;
	MtcEventTestPollFD test;
	MtcPeerSet *peer_set;
	
	/**Callback to be called when a connection is accepted
	 * \param listener The listener object
	 * \param fd Newly accepted connection
	 */
	void (*accepted) (MtcSimpleListener *listener, int fd);
	///User data
	void *data;
};

/**Ceates a new listener listening for connections on given
 * file descriptor
 * \param svr_fd A file descriptor. bind() and listen() must be 
 *               already called on it.
 * \return a new listener
 */
MtcSimpleListener *mtc_simple_listener_new(int svr_fd);

/**Increments the reference count by 1. 
 * \param listener The listener object
 */
void mtc_simple_listener_ref(MtcSimpleListener *listener);

/**Decrements the reference count by 1. 
 * \param listener The listener object
 */
void mtc_simple_listener_unref(MtcSimpleListener *listener);

/**Sets whether the event source is active, i.e. 
 * listening for connections
 * \param listener The listener object
 * \param val Nonzero to accept new connections, 0 to not
 */
void mtc_simple_listener_set_active
	(MtcSimpleListener *listener, int val);

/**Gets whether the event source is active, i.e. 
 * listening for connections
 * \param listener The listener object
 * \return Nonzero to accept new connections, 0 to not
 */
#define mtc_simple_listener_get_active(listener) \
	((int) ((listener)->active))

/**Sets whether file descriptor is closed when listener is destroyed.
 * \param listener The listener object
 * \param val Nonzero to close the file descriptor, 0 to not
 */ 
void mtc_simple_listener_set_close_fd
	(MtcSimpleListener *listener, int val);

/**Gets whether file descriptor is closed when listener is destroyed.
 * \param listener The listener object
 * \return Nonzero to close the file descriptor, 0 to not
 */ 
#define mtc_simple_listener_get_close_fd(listener) \
	((int) ((listener)->close_fd))

/**Adjusts callback functions so that newly accepted connections are
 * forwarded straight to the provided collection.
 * \param listener The listener object
 * \param peer_set An MtcPeerSet.
 *        A strong reference will be held on it,
 *        which can be released by mtc_simple_listener_unset_peer_set()
 */ 
void mtc_simple_listener_set_peer_set
	(MtcSimpleListener *listener, MtcPeerSet *peer_set);

/**Removes MtcPeerSet set by mtc_simple_listener_set_peer_set().
 * \param listener The listener object
 */
void mtc_simple_listener_unset_peer_set
	(MtcSimpleListener *listener);

/**
 * \}
 */
