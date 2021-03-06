/* event.h
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
 
/**\addtogroup mtc_lev_event
 * \{
 * 
 * A libevent based implementation for MTC event-driven framework.
 */

/**Creates a new libevent based event backend manager.
 * \param base As returned from event_base_new()
 * \param destroy_base 1 to destroy base when event manager is
 *               destroyed, 0 otherwise
 * \return A new libevent based event backend manager.
 */
MtcEventMgr *mtc_lev_event_mgr_new
	(struct event_base *base, int destroy_base);

/**
 * \}
 */
