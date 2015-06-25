/* simple_router.h
 * A simple MtcRouter implementation that connects to peers 
 * over stream sockets
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

//TODO: Documentation

/**Creates a new simple router
 * \return a new simple router
 */
MtcRouter *mtc_simple_router_new();

/**Adds a new connection to simple router
 * \param router A simple router
 * \param fd A connection
 * \param close_fd 1 to close the file descriptor 
 *        when peer is destroyed, 0 otherwise
 */
MtcPeer *mtc_simple_router_add(MtcRouter *router, int fd, int close_fd);

/**Closes connection to the peer. 
 * \param peer A peer belonging to simple router
 */
void mtc_simple_peer_disconnect(MtcPeer *peer);

/**Gets whether the peer has an active connection.
 * \param peer A peer belonging to simple router
 */
int mtc_simple_peer_is_connected(MtcPeer *peer);
