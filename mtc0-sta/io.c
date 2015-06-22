/* io.c
 * IO utilities
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

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>


//Initializes the reader
void mtc_reader_init(MtcReader *self, void *mem, size_t len, int fd)
{
	self->mem = mem;
	self->len = len;
	self->fd  = fd;
}

//Reads some data. Returns no. of bytes remaining to be read, or one of 
//MtcIOStatus in case of error.
MtcIOStatus mtc_reader_read(MtcReader *self)
{
	ssize_t bytes_read;
	
	//Precaution
	if (! self->len)
		return 0;
	
	//Read some data
	bytes_read = read(self->fd, self->mem, self->len);
	
	//Error checking
	if (bytes_read < 0)
	{
		if (MTC_IO_TEMP_ERROR(errno))
			return MTC_IO_TEMP;
		else
			return MTC_IO_SEVERE;
	}
	else if (bytes_read == 0)
		return MTC_IO_EOF;
	else
	{
		//Successful read.
		//Update and return status
		self->mem = MTC_PTR_ADD(self->mem, bytes_read);
		self->len -= bytes_read;
		
		if (self->len)
			return MTC_IO_TEMP;
		else
			return MTC_IO_OK;
	}
}

//Initializes the reader
void mtc_reader_v_init(MtcReaderV *self, struct iovec *blocks, int n_blocks, int fd)
{
	self->blocks = blocks;
	self->n_blocks = n_blocks;
	self->fd = fd;
}

//Reads some data. 
MtcIOStatus mtc_reader_v_read(MtcReaderV *self)
{
	ssize_t bytes_read;
	
	//Precaution
	if (! self->n_blocks)
		return 0;
	
	//Read some data
	bytes_read = readv(self->fd, self->blocks, self->n_blocks);
	
	//Handle errors
	if (bytes_read < 0)
	{
		if (MTC_IO_TEMP_ERROR(errno))
			return MTC_IO_TEMP;
		else
			return MTC_IO_SEVERE;
	}
	else if (bytes_read == 0)
		return MTC_IO_EOF;
	else
	{
		while (bytes_read ? (self->blocks->iov_len <= bytes_read) : 0)
		{
			bytes_read -= self->blocks->iov_len;
			self->blocks++;
			self->n_blocks--;
		}
		
		//Update the last partially read block
		if (bytes_read)
		{
			self->blocks->iov_base 
				= MTC_PTR_ADD(self->blocks->iov_base, bytes_read);
			self->blocks->iov_len -= bytes_read;
		}
		
		//return
		if (self->n_blocks > 0)
			return MTC_IO_TEMP;
		else
			return MTC_IO_OK;
	}
}

//File descriptor utilities

//Sets whether IO operations on fd should block
//val<0 means do nothing
int mtc_fd_set_blocking(int fd, int val)
{
	int stat_flags, res;
	
	//Get flags
	stat_flags = fcntl(fd, F_GETFL, 0);
	if (stat_flags < 0)
		mtc_error("Failed to get file descriptor flags for file descriptor %d: %s",
		          fd, strerror(errno));
	
	//Get current status
	res = !(stat_flags & O_NONBLOCK);
	
	//Set non-blocking
	if (val > 0)
		stat_flags &= (~O_NONBLOCK);
	else if (val == 0)
		stat_flags |= O_NONBLOCK;
	else
		return res;
	
	//Set back the flags
	if (fcntl(fd, F_SETFL, stat_flags) < 0)
		mtc_error("Failed to set file descriptor flags for file descriptor %d: %s",
		          fd, strerror(errno));
	
	return res;
}

