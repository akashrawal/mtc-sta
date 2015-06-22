/* io.h
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

//This is an internal module.

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>


#ifndef EWOULDBLOCK
#define MTC_IO_TEMP_ERROR(e) ((e == EAGAIN) || (e == EINTR))
#else
#if (EAGAIN == EWOULDBLOCK)
#define MTC_IO_TEMP_ERROR(e) ((e == EAGAIN) || (e == EINTR))
#else
#define MTC_IO_TEMP_ERROR(e) ((e == EAGAIN) || (e == EWOULDBLOCK) || (e == EINTR))
#endif
#endif

//Return status for IO operation
typedef enum
{	
	//No error
	MTC_IO_OK = 0,
	//Temporary error
	MTC_IO_TEMP = -1,
	//End-of-file encountered while reading
	MTC_IO_EOF = -2,
	//Irrecoverable error
	MTC_IO_SEVERE = -3
} MtcIOStatus;

//A simple reader
typedef struct 
{
	void *mem;
	size_t len;
	int fd;
} MtcReader;

//Another simple reader but calls readv()
typedef struct 
{
	struct iovec *blocks;
	int n_blocks;
	int fd;
} MtcReaderV;

//Initializes the reader
void mtc_reader_init(MtcReader *self, void *mem, size_t len, int fd);

//Reads some data. Returns no. of bytes remaining to be read, or one of 
//MtcIOStatus in case of error.
MtcIOStatus mtc_reader_read(MtcReader *self);

//Initializes the reader
void mtc_reader_v_init(MtcReaderV *self, struct iovec *blocks, int n_blocks, int fd);

//Reads some data. 
MtcIOStatus mtc_reader_v_read(MtcReaderV *self);

//File descriptor utilities

//Sets whether IO operations on fd should block
//val<0 means do nothing
int mtc_fd_set_blocking(int fd, int val);

