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

MtcRouter *mtc_simple_router_new();

MtcPeer *mtc_simple_router_add(MtcRouter *router, int fd, int close_fd);

void mtc_simple_peer_disconnect(MtcPeer *peer);

int mtc_simple_peer_is_connected(MtcPeer *peer);
