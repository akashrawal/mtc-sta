/* fd_link.h
 * MtcLink implementation that uses file descriptors
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

int mtc_fd_set_blocking(int fd, int val)


/**Creates a new link that works with file descriptors.
 * \param in_fd The file descriptor to receive from.
 * \param out_fd The file descriptor to send to. Can be as
 *               same as in_fd.
 * \return A new link.
 */
MtcLink *mtc_fd_link_new(int out_fd, int in_fd);

/**Gets the file descriptor used for sending.
 * \param link The link
 * \return a file descriptor
 */
int mtc_fd_link_get_out_fd(MtcLink *link);

/**Gets the file descriptor used for receiving.
 * \param link The link
 * \return a file descriptor
 */
int mtc_fd_link_get_in_fd(MtcLink *link);

/**Gets whether the file descriptors will be closed when the link 
 * is destroyed.
 * \param link The link
 * \return 1 if the file descriptor should be closed on destruction, 
 *         0 otherwise.
 */
int mtc_fd_link_get_close_fd(MtcLink *link);

/**Sets whether the file descriptors will be closed when the link 
 * is destroyed.
 * \param link The link
 * \param val 1 if the file descriptor should be closed on destruction, 
 *            0 otherwise.
 */
void mtc_fd_link_set_close_fd(MtcLink *link, int val);


