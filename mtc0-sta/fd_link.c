/* fd_link.c
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

#include "common.h"

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>

//TODO: Refactor
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
static void mtc_reader_init(MtcReader *self, void *mem, size_t len, int fd)
{
	self->mem = mem;
	self->len = len;
	self->fd  = fd;
}

//Reads some data. Returns no. of bytes remaining to be read, or one of 
//MtcIOStatus in case of error.
static MtcIOStatus mtc_reader_read(MtcReader *self)
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
static void mtc_reader_v_init(MtcReaderV *self, struct iovec *blocks, int n_blocks, int fd)
{
	self->blocks = blocks;
	self->n_blocks = n_blocks;
	self->fd = fd;
}

//Reads some data. 
static MtcIOStatus mtc_reader_v_read(MtcReaderV *self)
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


/*-------------------------------------------------------------------*/
//MtcFDLink


//Internals

//Common header for all types of data used on MtcFDLink
//No padding to be assumed
typedef struct
{
	//Magic values
	char m, t, c, zero;
	
	//No. of blocks inside a message.
	//MSB is one if link is to be stopped after receiving this
	uint32_t size;
	
	//'block size index' (BSI)
	//Indicates size of each extra block followed by the actual data.
	uint32_t data[];
} MtcHeader;

//Structure containing only useful data elements from MtcHeader
typedef struct 
{
	uint32_t size, data_1;
	int stop;
} MtcHeaderData;

//Type for the buffer for message
typedef struct {uint64_t data[2]; } MtcHeaderBuf;

//Macro to calculate size of the header
#define mtc_header_sizeof(size) (8 + (4 * (size)))

//Macro to calculate minimum size of the header
//It is the size of data that is read first by MtcFDLink which tells
//about size of the rest of the header
#define mtc_header_min_size (mtc_header_sizeof(1))


//serialize the header for a message
void mtc_header_write
	(MtcHeaderBuf *buf, MtcMBlock *blocks, uint32_t n_blocks, int stop)
{
	char *buf_c = (char *) buf;
	char *data_iter, *data_lim;
	uint32_t size;
	
	buf_c[0] = 'M';
	buf_c[1] = 'T';
	buf_c[2] = 'C';
	buf_c[3] = 0;
	
	size = n_blocks;
	if (stop)
		size |= (1 << 31);
	mtc_uint32_copy_to_le(buf_c + 4, &size);
	
	data_iter = buf_c + 8;
	data_lim = data_iter + (n_blocks * 4);
	for (; data_iter < data_lim; data_iter += 4, blocks++)
	{
		uint32_t size = blocks->size;
		mtc_uint32_copy_to_le(data_iter, &size);
	}
}

//Deserialize the message header
//returns zero for format errors
int mtc_header_read(MtcHeaderBuf *buf, MtcHeaderData *res)
{
	char *buf_c = (char *) buf;
	uint32_t size;
	
	if (buf_c[0] != 'M' 
		|| buf_c[1] != 'T'
		|| buf_c[2] != 'C'
		|| buf_c[3] != 0)
	{
		return 0;
	}
	
	mtc_uint32_copy_from_le(buf_c + 4, &size);
	res->size = size & (~(((uint32_t) 1) << 31));
	res->stop = size >> 31;
	
	if (res->size == 0)
		return 0;
	
	mtc_uint32_copy_from_le(buf_c + 8, &(res->data_1));
	
	return 1;
}


//Stores information about current reading status
typedef enum 
{
	//Starting
	MTC_FD_LINK_INIT_READ = 0,
	//Reading header
	MTC_FD_LINK_HDR = 1,
	//Reading message with only main block
	MTC_FD_LINK_SIMPLE = 2,
	//Reading message's block size index
	MTC_FD_LINK_IDX = 3,
	//Reading message data
	MTC_FD_LINK_DATA = 4
} MtcFDLinkReadStatus;

//Data to be sent
typedef struct _MtcFDLinkSendJob MtcFDLinkSendJob;
struct _MtcFDLinkSendJob
{
	MtcFDLinkSendJob *next;
	
	MtcMsg *msg;
	
	int stop_flag;
	unsigned int n_blocks;
	
	//The header data
	MtcHeaderBuf hdr;
};

#define MTC_IOV_MIN 16

//A link that operates on file descriptor
typedef struct
{
	MtcLink parent;
	
	//Underlying file descriptors
	int out_fd, in_fd;
	
	//Whether to close file descriptors
	int close_fd;
	
	//Stuff for sending
	struct
	{
		struct iovec *mem;
		int alen, start, len, ulim, clip;
	} iov;
	struct
	{
		MtcFDLinkSendJob *head, *tail;
	} jobs;
	
	//Stuff for receiving
	MtcReader reader;
	MtcReaderV reader_v;
	MtcFDLinkReadStatus read_status;
	MtcHeaderData header_data;
	void *mem; //< buffer for BSI and IO vector
	MtcMsg *msg; //< Message structure
	
	//Event loop integration
	MtcEventTestPollFD tests[2];
	
	//fcntl cache
	struct 
	{
		int in, out;
	} flcache;
	
	//Preallocated buffers
	MtcHeaderBuf header;
} MtcFDLink;

//Functions to manage IO vector
static struct iovec *mtc_fd_link_alloc_iov
	(MtcFDLink *self, int n_blocks)
{
	struct iovec *res;
	
	if (self->iov.start + self->iov.len + n_blocks > self->iov.alen)
	{
		//Resize if IO vector is not sufficiently large.
		int new_alen, i;
		struct iovec *new_iov, *src;
		
		new_alen = self->iov.alen * 2;
		new_iov = (struct iovec *) mtc_alloc
			(sizeof(struct iovec) * new_alen);
		
		src = self->iov.mem + self->iov.start;
		for (i = 0; i < self->iov.len; i++)
			new_iov[i] = src[i];
		
		mtc_free(self->iov.mem);
		self->iov.mem = new_iov;
		self->iov.start = 0;
		self->iov.alen = new_alen;
	}
	
	//Allocate
	res = self->iov.mem + self->iov.start + self->iov.len;
	self->iov.len += n_blocks;
	
	return res;
}

static int mtc_fd_link_pop_iov(MtcFDLink *self, int n_bytes)
{
	int n_blocks = 0;
	struct iovec *vector;
	
	vector = self->iov.mem + self->iov.start;
	
	//Count finished blocks and update unfinished block
	while (n_blocks < self->iov.len ? n_bytes >= vector->iov_len : 0)
	{
		n_bytes -= vector->iov_len;
		n_blocks++;
		vector++;
	}
	if (n_bytes > 0)
	{
		vector->iov_base = MTC_PTR_ADD(vector->iov_base, n_bytes);
		vector->iov_len -= n_bytes;
	}
	
	//Update IO vector
	self->iov.start += n_blocks;
	self->iov.len -= n_blocks;
	if (self->iov.clip >= 0)
	{
		self->iov.clip -= n_blocks;
		if (self->iov.clip < 0)
			mtc_error("Assertion failure");
	}
	
	//Collapse IO vector if necessary
	if (self->iov.alen > MTC_IOV_MIN
		&& self->iov.len <= self->iov.alen * 0.25)
	{
		int new_alen, i;
		struct iovec *new_iov, *src;
		
		new_alen = self->iov.alen / 2;
		new_iov = (struct iovec *) mtc_alloc
			(sizeof(struct iovec) * new_alen);
		
		src = self->iov.mem + self->iov.start;
		for (i = 0; i < self->iov.len; i++)
			new_iov[i] = src[i];
		
		mtc_free(self->iov.mem);
		self->iov.mem = new_iov;
		self->iov.start = 0;
		self->iov.alen = new_alen;
	}
	else if (self->iov.start >= self->iov.alen / 2)
	{
		int i;
		struct iovec *src;
		
		src = self->iov.mem + self->iov.start;
		for (i = 0; i < self->iov.len; i++)
			self->iov.mem[i] = src[i];
		
		self->iov.start = 0;
	}
	
	return n_blocks;
}

static void mtc_fd_link_init_iov(MtcFDLink *self)
{
	self->iov.mem = (struct iovec *) 
		mtc_alloc(sizeof(struct iovec) * MTC_IOV_MIN);
	self->iov.alen = MTC_IOV_MIN;
	self->iov.start = 0;
	self->iov.len = 0;
	self->iov.clip = -1;
}

//Schedules a message to be sent through the link.
static void mtc_fd_link_queue
	(MtcLink *link, MtcMsg *msg, int stop)
{	
	MtcFDLink *self = (MtcFDLink *) link;
	
	MtcFDLinkSendJob *job;
	MtcMBlock *blocks;
	uint32_t n_blocks;
	uint32_t hdr_len; 
	uint32_t i;
	struct iovec *iov;
	
	mtc_msg_ref(msg);
	
	//Get the data to be sent
	n_blocks = mtc_msg_get_n_blocks(msg);
	blocks = mtc_msg_get_blocks(msg);
	
	//Allocate a new structure...
	hdr_len = mtc_header_sizeof(n_blocks);
	job = (MtcFDLinkSendJob *) mtc_alloc
		(sizeof(MtcFDLinkSendJob) - sizeof(MtcHeaderBuf) + hdr_len);
	
	//Initialize job
	if (self->jobs.head)
		self->jobs.tail->next = job;
	else 
		self->jobs.head = job;
	self->jobs.tail = job;
	job->next = NULL;
	job->msg = msg;
	job->stop_flag = stop ? 1 : 0;
	job->n_blocks = n_blocks + 1;
	mtc_header_write(&(job->hdr), blocks, n_blocks, stop);
	
	//Fill data into IOV
	iov = mtc_fd_link_alloc_iov(self, n_blocks + 1);
	iov[0].iov_base = &(job->hdr);
	iov[0].iov_len = hdr_len;
	iov++;
	for (i = 0; i < n_blocks; i++)
	{
		iov[i].iov_base = blocks[i].mem;
		iov[i].iov_len = blocks[i].size;
	}
	
	//Setup stop
	if (stop)
	{
		if (self->iov.clip < 0)
			self->iov.clip = self->iov.len;
	}
}

//Determines whether mtc_link_send will try to send any data.
static int mtc_fd_link_can_send(MtcLink *link)
{
	MtcFDLink *self = (MtcFDLink *) link;
	
	if (self->iov.clip > 0)
		return 1;
	else if (self->iov.clip == 0)
		return 0;
	else if (self->iov.len > 0)
		return 1;
	else
		return 0;
}

//Tries to send all queued data
static MtcLinkIOStatus mtc_fd_link_send(MtcLink *link)
{
	MtcFDLink *self = (MtcFDLink *) link;
	
	int blocks_out = 0;
	ssize_t bytes_out;
	int repeat_count;
	
	//Run the operation
	for (repeat_count = 0; ; repeat_count++)
	{
		struct iovec *vector;
		int n_blocks;
		int repeat = 0;
		
		n_blocks = self->iov.clip >= 0 ? self->iov.clip : self->iov.len;
		vector = self->iov.mem + self->iov.start;
		
		if (n_blocks > 0)
		{
			if (self->iov.ulim > 0 && n_blocks > self->iov.ulim)
			{
				n_blocks = self->iov.ulim;
				repeat = 1;
			}
			bytes_out = writev(self->out_fd, vector, n_blocks);
		}
		else
			bytes_out = 0;
		
		//Handle errors
		if (bytes_out < 0)
		{
			repeat = 0;
			if (! repeat_count)
			{
				if (MTC_IO_TEMP_ERROR(errno))
					return MTC_LINK_IO_TEMP;
				else
					return MTC_LINK_IO_FAIL;
			}
		}
		else
			blocks_out += mtc_fd_link_pop_iov(self, bytes_out);
		
		if (! repeat)
			break;
	}
	
	//Garbage collection
	{
		MtcFDLinkSendJob *iter, *next;
		for (iter = self->jobs.head; iter; iter = next)
		{
			next = iter->next;
			
			if (blocks_out < iter->n_blocks)
				break;
			
			blocks_out -= iter->n_blocks;
			mtc_msg_unref(iter->msg);
			mtc_free(iter);
		}
		
		self->jobs.head = iter;
		if (iter)
			iter->n_blocks -= blocks_out;
		else
			self->jobs.tail = NULL;
		
		if (self->jobs.head && self->iov.len)
			mtc_error("Assertion failure");
	}
	
	//Next stop signal and return status
	if (self->iov.clip == 0)
	{
		MtcFDLinkSendJob *iter;
		int counter = 0;
		
		for (iter = self->jobs.head; iter; iter = iter->next)
		{
			counter += iter->n_blocks;
			if (iter->stop_flag)
				break;
		}
		
		if (iter)
			self->iov.clip = counter;
		else
			self->iov.clip = -1;
		
		return MTC_LINK_IO_STOP;
	}
	else if (self->jobs.head)
		return MTC_LINK_IO_TEMP;
	else
		return MTC_LINK_IO_OK;
}

//Tries to receive a message or a signal.
static MtcLinkIOStatus mtc_fd_link_receive
	(MtcLink *link, MtcLinkInData *data)
{
	MtcFDLink *self = (MtcFDLink *) link;
	MtcIOStatus io_res = MTC_IO_OK;
	MtcHeaderData *header = &(self->header_data);
	MtcMBlock *blocks;
	
	switch (self->read_status)
	{
	case MTC_FD_LINK_INIT_READ:
		//Prepare the reader to read header
		mtc_reader_init(&(self->reader),
			(void *) &(self->header), mtc_header_min_size, self->in_fd);
		
		//Save status
		self->read_status = MTC_FD_LINK_HDR;
		
		//Fallthrough
	case MTC_FD_LINK_HDR:
		//Try to read generic header.
		io_res = mtc_reader_read(&(self->reader));
		
		//If reading not finished bail out.
		if (io_res != MTC_IO_OK)
			break;
		
		//Now start reading...
		
		//Convert all byte orders
		if(! mtc_header_read(&(self->header), header))
		{
			mtc_warn("Invalid header on link %p, breaking the link.",
			         self);
			return MTC_LINK_IO_FAIL;
		}
		
		//For message the program continues
		
		//Check size of main block
		if (! header->data_1)
		{
			mtc_warn("Size of main memory block is zero "
					 "for message received on link %p, breaking link",
					 self);
			return MTC_LINK_IO_FAIL;
		}
		
		//If the message has extra blocks follow another way
		if (header->size != 1)
			goto complex_msg;
			
		//For message having only main block, continue...
		
		//Try to create an allocated message
		self->msg = mtc_msg_try_new_allocd(header->data_1, 0, NULL);
		if (! self->msg)
		{
			mtc_warn("Failed to allocate message structure "
			         "for message received on link %p, breaking link.",
			         self);
			return MTC_LINK_IO_FAIL;
		}
		
		//Prepare the reader to read the message main block.
		//There's only one block.
		blocks = mtc_msg_get_blocks(self->msg);
		mtc_reader_init
			(&(self->reader), blocks->mem, blocks->size, self->in_fd);
		
		//Save status
		self->read_status = MTC_FD_LINK_SIMPLE;
		
		//Fallthrough
	case MTC_FD_LINK_SIMPLE:
		//Read message main block
		io_res = mtc_reader_read(&(self->reader));
		
		//If reading not finished bail out.
		if (io_res != MTC_IO_OK)
			break;
		
		//Message reading finished, jump to end of switch
		//there is code to return the message
		goto return_msg;
		
	complex_msg:
		//we now need to read the block size index.
		//but we will also need to read message data in future.
		//Allocate memory large enough for both purposes
		//but don't crash if it fails.
		{
			//I think this is large enough
			size_t alloc_size = header->size * sizeof(struct iovec);
			self->mem = mtc_tryalloc(alloc_size);
			if (! self->mem)
			{
				mtc_warn("Memory allocation failed for %ld bytes "
						 "for message received on link %p, breaking link",
						 (long) alloc_size, self);
				return MTC_LINK_IO_FAIL;
			}
		}
		
		//Prepare the reader to read BSI
		mtc_reader_init
				(&(self->reader), 
				self->mem, sizeof(uint32_t) * (header->size - 1), 
				self->in_fd);
	
		//Save status
		self->read_status = MTC_FD_LINK_IDX;
		
		//Fallthrough
	case MTC_FD_LINK_IDX:
		//Read the index
		io_res = mtc_reader_read(&(self->reader));
		
		//If reading not finished bail out.
		if (io_res != MTC_IO_OK)
			break;
		
		//Assertions and byte order conversion
		{
			uint32_t *iter, *lim;
			
			iter = (uint32_t *) self->mem;
			lim = iter + header->size - 1;
			
			for (; iter < lim; iter++)
			{
				*iter = mtc_uint32_from_le(*iter);
				if (! *iter)
				{
					mtc_warn("In block size index received on link %p, "
					         "element %ld has value 0, breaking link.",
					         self, 
					         (long) (iter - (uint32_t *) self->mem));
					return MTC_LINK_IO_FAIL;
				}
			}
		}
		
		//Create message reader
		self->msg = mtc_msg_try_new_allocd
			(header->data_1, header->size - 1, (uint32_t *) self->mem);
		if (! self->msg)
		{
			mtc_warn("Failed to allocate message structure "
			         "for message received on link %p, breaking link.",
			         self);
			return MTC_LINK_IO_FAIL;
		}
		
		//Prepare reader_v to read message data
		{
			MtcMBlock *blocks, *blocks_lim;
			struct iovec *vector;
			
			//First create the IO vector. 
			//There are atleast two memory blocks.
			vector = (struct iovec *) self->mem;
			blocks = mtc_msg_get_blocks(self->msg);
			blocks_lim = blocks + header->size;
			
			while (blocks < blocks_lim)
			{
				vector->iov_base = blocks->mem;
				vector->iov_len = blocks->size;
				
				blocks++;
				vector++;
			}
			
			//Now prepare
			mtc_reader_v_init(&(self->reader_v),
							  (struct iovec *) self->mem,
							  header->size,
							  self->in_fd);
		}
		
		//Save status
		self->read_status = MTC_FD_LINK_DATA;
		
		//Fallthrough
	case MTC_FD_LINK_DATA:
		//Try to read message data
		io_res = (ssize_t) mtc_reader_v_read(&(self->reader_v));
		
		//If reading not finished bail out.
		if (io_res != MTC_IO_OK)
			break;
		
		//Message reading finished
		
		//Free the memory allocated for IO vector
		mtc_free(self->mem);
		self->mem = NULL;
		
	return_msg:
		//Return data
		data->msg = self->msg;
		data->stop = header->stop;
		
		//Reset status
		self->read_status = MTC_FD_LINK_INIT_READ;
		self->msg = NULL;
		
		return MTC_LINK_IO_OK;
	}
	
	if ((io_res == MTC_IO_SEVERE) || (io_res == MTC_IO_EOF))
		return MTC_LINK_IO_FAIL;
	else
		return MTC_LINK_IO_TEMP;
}

//Event management

/*RULES: 
 * - self->tests should be kept updated at all times.
 * - mtc_event_source_prepare() to be called with self->tests 
 * whenever mtc_link_get_events_enabled(self) and NULL otherwise
 */

#define define_out_idx \
	int out_idx = (self->in_fd == self->out_fd ? 0 : 1)


static void mtc_fd_link_calc_events
	(MtcFDLink *self, int *events)
{
	MtcLink *link = (MtcLink *) self;
	define_out_idx;
	
	events[0] = 0;
	events[1] = 0;
	
	if (mtc_link_get_in_status(link) == MTC_LINK_STATUS_OPEN)
		events[0] |= MTC_POLLIN;
	
	if ((mtc_link_get_out_status(link) == MTC_LINK_STATUS_OPEN) 
		&& (mtc_fd_link_can_send(link)))
		events[out_idx] |= MTC_POLLOUT;
}

static void mtc_fd_link_event_source_event
	(MtcEventSource *source, MtcEventFlags flags)
{
	MtcLinkEventSource *ev = (MtcLinkEventSource *) source;
	MtcLink *link = ev->link;
	MtcFDLink *self = (MtcFDLink *) link;
	
	mtc_link_ref(link);
	
	define_out_idx;
	int status;
	MtcLinkInData in_data;
	
	if (flags & MTC_EVENT_CHECK)
	{
		//Error condition
		if (self->tests[0].revents & (MTC_POLLERR | MTC_POLLHUP | MTC_POLLNVAL)
			|| self->tests[out_idx].revents & (MTC_POLLERR | MTC_POLLHUP | MTC_POLLNVAL))
		{
			mtc_link_break((MtcLink *) self);
			if (mtc_link_get_events_enabled(link))
				(* ev->broken)((MtcLink *) self, ev->data);
			goto end;
		}
		
		//Nonblocking
		mtc_fd_link_set_blocking((MtcLink *) self, 0);
		
		//Sending
		if (self->tests[out_idx].revents & (MTC_POLLOUT))
		{
			status = mtc_link_send((MtcLink *) self);
			if (status == MTC_LINK_IO_STOP)
			{
				if (mtc_link_get_events_enabled(link))
					(* ev->stopped)((MtcLink *) self, ev->data);
			}
			else if (status == MTC_LINK_IO_FAIL)
			{
				if (mtc_link_get_events_enabled(link))
					(* ev->broken)((MtcLink *) self, ev->data);
				goto end;
			}
		}
		
		//Receiving
		if (self->tests[0].revents & (MTC_POLLIN))
		{
			while (1)
			{
				status = mtc_link_receive((MtcLink *) self, &in_data);
				
				if (status == MTC_LINK_IO_OK)
				{
					if (mtc_link_get_events_enabled(link))
						(* ev->received) 
							((MtcLink *) self, in_data, ev->data);
					mtc_msg_unref(in_data.msg);
				}
				else if (status == MTC_LINK_IO_FAIL)
				{
					if (mtc_link_get_events_enabled(link))
						(* ev->broken)((MtcLink *) self, ev->data);
					goto end;
				}
				else
				{
					break;
				}
			}
		}
	}
	
end:
	mtc_link_unref(link);
}


static void mtc_fd_link_set_events_enabled
	(MtcLink *link, int value)
{
	MtcFDLink *self = (MtcFDLink *) link;
	MtcEventSource *source = (MtcEventSource *) 
		mtc_link_get_event_source(link);
	
	if (! mtc_link_is_broken(link))
	{
		if (value)
		{
			mtc_event_source_prepare
				(source, (MtcEventTest *) self->tests);
		}
		else
		{
			mtc_event_source_prepare
				(source, (MtcEventTest *) NULL);
		}
	}
}

static void mtc_fd_link_action_hook(MtcLink *link)
{
	MtcFDLink *self = (MtcFDLink *) link;
	int events[2];
	
	MtcLinkEventSource *source = mtc_link_get_event_source(link);
	
	if (mtc_link_is_broken(link))
	{
		if (mtc_link_get_events_enabled(link))
		{
			mtc_event_source_prepare((MtcEventSource *) source, NULL);
		}
		return;
	}
	
	mtc_fd_link_calc_events(self, events);
	
	if ((events[0] != self->tests[0].events) 
		|| (events[1] != self->tests[1].events))
	{
		if (mtc_link_get_events_enabled(link))
		{
			mtc_event_source_prepare((MtcEventSource *) source, NULL);
		}
		self->tests[0].events = events[0];
		self->tests[1].events = events[1];
		if (mtc_link_get_events_enabled(link))
		{
			mtc_event_source_prepare
				((MtcEventSource *) source, 
				(MtcEventTest *) self->tests);
		}
	}
}

static void mtc_fd_link_init_event(MtcFDLink *self)
{
	define_out_idx;
	int events[2];
	MtcEventTest constdata = 
		{NULL, {'p', 'o', 'l', 'l', 'f', 'd', '\0', '\0'}};
	
	//Initialize test data
	mtc_fd_link_calc_events(self, events);
	self->tests[0].parent = constdata;
	self->tests[0].fd = self->in_fd;
	self->tests[0].events = events[0];
	self->tests[0].revents = 0;
	self->tests[1].parent = constdata;
	self->tests[1].fd = self->out_fd;
	self->tests[1].events = events[1];
	self->tests[1].revents = 0;
	
	if (out_idx == 1)
	{
		self->tests[0].parent.next = (MtcEventTest *) self->tests + 1;
	}
}

static void mtc_fd_link_finalize(MtcLink *link)
{
	MtcFDLink *self = (MtcFDLink *) link;
	MtcFDLinkSendJob *iter, *next;
	
	//Destroy IO vector and all jobs.
	mtc_free(self->iov.mem);
	for (iter = self->jobs.head; iter; iter = next)
	{
		next = iter->next;
		
		mtc_msg_unref(iter->msg);
		mtc_free(iter);
	}
	
	//Destroy the 'additional' buffer
	if (self->mem)
	{
		mtc_free(self->mem);
		self->mem = NULL;
	}
	
	//Destroy partially read message if any
	if (self->msg)
	{
		mtc_msg_unref(self->msg);
		self->msg = NULL;
	}
	
	//Close file descriptors
	if (self->close_fd)
	{
		close(self->in_fd);
		if (self->in_fd != self->out_fd)
			close(self->out_fd);
	}
}

//VTable
const static MtcLinkVTable mtc_fd_link_vtable = {
	mtc_fd_link_queue,
	mtc_fd_link_can_send,
	mtc_fd_link_send,
	mtc_fd_link_receive,
	mtc_fd_link_set_events_enabled,
	{
		mtc_fd_link_event_source_event
	},
	mtc_fd_link_action_hook,
	mtc_fd_link_finalize
};

//Constructor
MtcLink *mtc_fd_link_new(int out_fd, int in_fd)
{
	MtcFDLink *self;
	
	self = (MtcFDLink *) mtc_link_create
			(sizeof(MtcFDLink), &mtc_fd_link_vtable);
	
	//Set file descriptors
	self->out_fd = out_fd;
	self->in_fd = in_fd;
	self->close_fd = 0;
	
	//Initialize sending data
	mtc_fd_link_init_iov(self);
	self->jobs.head = NULL;
	self->jobs.tail = NULL;
	self->iov.ulim = sysconf(_SC_IOV_MAX);
	
	//Initialize reading data
	self->read_status = MTC_FD_LINK_INIT_READ;
	self->mem = self->msg = NULL;
	
	//Clear fcntl cache 
	mtc_fd_link_clear_fcntl_cache((MtcLink *) self);
	
	//Initialize events
	mtc_fd_link_init_event(self);
	
	return (MtcLink *) self;
}

int mtc_fd_link_get_out_fd(MtcLink *link)
{
	MtcFDLink *self = (MtcFDLink *) link;
	
	if (link->vtable != &mtc_fd_link_vtable)
		mtc_error("%p is not MtcFDLink", link);
		
	return self->out_fd;
}

int mtc_fd_link_get_in_fd(MtcLink *link)
{
	MtcFDLink *self = (MtcFDLink *) link;
	
	if (link->vtable != &mtc_fd_link_vtable)
		mtc_error("%p is not MtcFDLink", link);
		
	return self->in_fd;
}

int mtc_fd_link_get_close_fd(MtcLink *link)
{
	MtcFDLink *self = (MtcFDLink *) link;
	
	if (link->vtable != &mtc_fd_link_vtable)
		mtc_error("%p is not MtcFDLink", link);
	
	return self->close_fd;
}

void mtc_fd_link_set_close_fd(MtcLink *link, int val)
{
	MtcFDLink *self = (MtcFDLink *) link;
	
	if (link->vtable != &mtc_fd_link_vtable)
		mtc_error("%p is not MtcFDLink", link);
	
	self->close_fd = (val ? 1 : 0);
}

static int mtc_set_blocking_cached(int fd, int val, int cache)
{
	int new_cache;
	
	//Get older flags if required
	if (cache < 0)
	{
		if ((cache = fcntl(fd, F_GETFL, 0)) < 0)
			mtc_error("fcntl(%d, F_GETFL, 0): %s", 
				fd, strerror(errno));
	}
	
	//Compute new flags
	if (val)
		new_cache = cache & (~ O_NONBLOCK);
	else
		new_cache = cache | O_NONBLOCK;
		
	//Apply
	if (new_cache != cache)
	{
		if (fcntl(fd, F_SETFL, new_cache) < 0)
			mtc_error("fcntl(%d, F_SETFL, new_cache): %s", 
				fd, strerror(errno));
	}
	
	return new_cache;
}

void mtc_fd_link_set_blocking(MtcLink *link, int val)
{
	MtcFDLink *self = (MtcFDLink *) link;
	
	self->flcache.in = mtc_set_blocking_cached
		(self->in_fd, val, self->flcache.in);
	
	//Same for out_fd
	if (self->in_fd != self->out_fd)
	{
		self->flcache.out = mtc_set_blocking_cached
			(self->out_fd, val, self->flcache.out);
	}
}

void mtc_fd_link_clear_fcntl_cache(MtcLink *link)
{
	MtcFDLink *self = (MtcFDLink *) link;
	
	self->flcache.in = -1;
	self->flcache.out = -1;
}

