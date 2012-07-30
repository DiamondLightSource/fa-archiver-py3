/* Support for shared buffer pool.
 *
 * Copyright (c) 2012 Michael Abbott, Diamond Light Source Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Contact:
 *      Dr. Michael Abbott,
 *      Diamond Light Source Ltd,
 *      Diamond House,
 *      Chilton,
 *      Didcot,
 *      Oxfordshire,
 *      OX11 0DE
 *      michael.abbott@diamond.ac.uk
 */

/* Buffer pool.  This is a pool of buffers which are shared out to clients.  The
 * limited number of buffers is one way of regulating the number of clients and
 * averts overloading the server by making excessive demands. */


/* This structure is used to communicate with the buffer pool. */
struct read_buffers {
    unsigned int count;         // Number of allocated buffers
    void **buffers;             // Array of allocated buffers
};

#define ALLOCATE_READ_BUFFERS(bufs) \
    struct read_buffers bufs = { .count = 0, .buffers = NULL }


/* Allocates the requested number of buffers, fails if none available. */
bool lock_buffers(struct read_buffers *buffers, unsigned int count);
/* Releases previously allocated buffer block.  Safe to call if count==0. */
void unlock_buffers(struct read_buffers *buffers);

/* Called at startup to initialise the buffer pool. */
void initialise_buffer_pool(size_t buffer_size, unsigned int count);

/* Records the size of buffers allocated by lock_buffers(). */
extern size_t pooled_buffer_size;




/* Buffered socket writes using pooled buffers.  Used for both buffered block
 * writes and delayed writes. */

struct write_buffer {
    int file;                       // File to write to, -1 if no write wanted
    unsigned int current_buffer;    // Buffer currently in  use
    size_t *out_pointers;           // One out pointer for each buffer
    struct read_buffers buffers;    // The buffers themselves
};


#define ALLOCATE_WRITE_BUFFER(buffer, scon) \
    struct write_buffer buffer = { .file = scon }

/* Helper macro for writing a single item. */
#define BUFFER_ITEM(buffer, item)    write_buffer(buffer, &item, sizeof(item))


/* Allocates write buffer.  Assumes the buffer has been correctly zero
 * initialised with ALLOCATE_WRITE_BUFFER. */
bool allocate_write_buffer(struct write_buffer *buffer, unsigned int count);
/* Must be called to release buffer. */
void release_write_buffer(struct write_buffer *buffer);

/* Ensures all data in buffer is transmitted. */
bool flush_buffer(struct write_buffer *buffer);
/* Writes given data to buffer, flushing buffer to make room as required. */
bool write_buffer(struct write_buffer *buffer, const void *data, size_t length);

/* Returns pointer to internal buffer of length at least min_length.  Data is
 * written if necessary, and the actual length available is returned. */
void *get_buffer(
    struct write_buffer *buffer, size_t min_length, size_t *length);
/* After calling get_buffer() this should be called to mark the given length as
 * having been written. */
void release_buffer(struct write_buffer *buffer, size_t length);

/* Writes from buffer_in to buffer_out, to be used when buffer_in has been used
 * as a delay buffer. */
bool write_delayed_buffer(
    struct write_buffer *buffer_in, struct write_buffer *buffer_out);
