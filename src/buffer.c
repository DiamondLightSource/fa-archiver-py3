/* FA archiver memory buffer.
 *
 * Handles the central memory buffer.
 *
 * Copyright (c) 2011 Michael Abbott, Diamond Light Source Ltd.
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

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "list.h"
#include "error.h"
#include "locking.h"

#include "buffer.h"


struct frame_info {
    /* True if this frame is a gap and contains no true data, false if the
     * associated frame in frame_buffer[] is valid. */
    bool gap;
    /* Timestamp for completion of this frame. */
    uint64_t timestamp;
};


struct buffer
{
    /* Size of individual buffer blocks. */
    size_t block_size;
    /* Number of blocks in buffer. */
    size_t block_count;
    /* Contiguous array of blocks, page aligned to work nicely with unbuffered
     * direct disk IO. */
    void *frame_buffer;
    /* Frame information including gap marks and timestamps. */
    struct frame_info *frame_info;

    /* Lock and pthread signal structure. */
    struct locking lock;

    /* Write pointer. */
    size_t index_in;
    /* Flag to halt writes for debugging. */
    bool write_blocked;

    /* Lists of readers. */
    struct list_head all_readers;
    struct list_head reserved_readers;
};


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Miscellaneous support routines.                                           */


static size_t advance_index(struct buffer *buffer, size_t index)
{
    index += 1;
    if (index >= buffer->block_count)
        index -= buffer->block_count;
    return index;
}

static void *get_buffer(struct buffer *buffer, size_t index)
{
    return buffer->frame_buffer + index * buffer->block_size;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Reader routines.                                                          */

struct reader_state
{
    struct buffer *buffer;          // Associated buffer
    size_t index_out;               // Next block to read
    bool underflowed;               // Set if buffer overrun for this reader
    bool running;                   // Used to halt reader
    bool gap_reported;              // Set once we've reported a gap
    int backlog;                    // Gap between read and write pointer
    struct list_head list_entry;    // Links all active readers together
    struct list_head reserved_entry;    // and all reserved readers
};


/* Iterators for the two lists of readers. */
#define for_all_readers(buffer, reader) \
    list_for_each_entry( \
        struct reader_state, list_entry, reader, &buffer->all_readers)
#define for_reserved_readers(buffer, reader) \
    list_for_each_entry( \
        struct reader_state, reserved_entry, reader, &buffer->reserved_readers)


/* Updates the backlog count.  This is computed as the maximum number of
 * unread frames from the write pointer to our read pointer.  As we're only
 * interested in the maximum value, this only needs to be updated when frames
 * are written. */
static void update_backlog(struct reader_state *reader)
{
    int backlog = reader->buffer->index_in - reader->index_out;
    if (backlog < 0)
        backlog += reader->buffer->block_count;

    if (backlog > reader->backlog)
        reader->backlog = backlog;
}


struct reader_state *open_reader(struct buffer *buffer, bool reserved_reader)
{
    struct reader_state *reader = malloc(sizeof(struct reader_state));
    reader->buffer = buffer;
    reader->underflowed = false;
    reader->gap_reported = false;
    reader->backlog = 0;
    reader->running = true;
    INIT_LIST_HEAD(&reader->reserved_entry);

    LOCK(buffer->lock);
    reader->index_out = buffer->index_in;
    list_add_tail(&reader->list_entry, &buffer->all_readers);
    if (reserved_reader)
        list_add_tail(&reader->reserved_entry, &buffer->reserved_readers);
    UNLOCK(buffer->lock);

    return reader;
}


void close_reader(struct reader_state *reader)
{
    struct buffer *buffer = reader->buffer;

    LOCK(buffer->lock);
    list_del(&reader->list_entry);
    list_del(&reader->reserved_entry);
    UNLOCK(buffer->lock);

    free(reader);
}


const void *get_read_block(
    struct reader_state *reader, int *backlog, uint64_t *timestamp)
{
    struct buffer *buffer = reader->buffer;
    void *block;
    LOCK(buffer->lock);
    if (reader->underflowed)
    {
        /* If we were underflowed then perform a complete reset of the read
         * stream.  Discard everything in the block and start again.  This
         * helps the writer which can rely on this.  We'll also start by
         * reporting a synthetic gap. */
        reader->index_out = buffer->index_in;
        reader->underflowed = false;
        block = NULL;
    }
    else
    {
        struct frame_info *frame_info = &buffer->frame_info[reader->index_out];
        /* Wait until one of the following conditions is satisfied:
         *  1. We're stopped by setting running to false
         *  2. The out and in indexes don't coincide
         *  3. The we haven't reported a gap yet and the new frame starts a new
         *     gap. */
        while (reader->running  &&
               reader->index_out == buffer->index_in  &&
               (reader->gap_reported  ||  !frame_info->gap)  &&
               pwait_timeout(&buffer->lock, 2, 0))
            ;

        if (!reader->running)
            block = NULL;
        else if (frame_info->gap  &&  !reader->gap_reported)
            /* This block is preceded by a gap.  Return a gap indicator this
             * time, we'll return the block itself next time. */
            block = NULL;
        else if (reader->index_out == buffer->index_in)
        {
            /* If we get here there must have been a timeout.  This is
             * definitely not normal, log and treat as no data. */
            log_error("Timeout waiting for circular buffer");
            block = NULL;
        }
        else
        {
            block = get_buffer(buffer, reader->index_out);
            if (timestamp)
                *timestamp = frame_info->timestamp;
        }
    }

    if (backlog)
    {
        *backlog = reader->backlog * buffer->block_size;
        reader->backlog = 0;
    }
    UNLOCK(buffer->lock);

    reader->gap_reported = block == NULL;
    return block;
}


void stop_reader(struct reader_state *reader)
{
    struct buffer *buffer = reader->buffer;
    LOCK(buffer->lock);
    reader->running = false;
    psignal(&buffer->lock);
    UNLOCK(buffer->lock);
}


bool release_read_block(struct reader_state *reader)
{
    struct buffer *buffer = reader->buffer;
    bool underflow;
    LOCK(buffer->lock);
    reader->index_out = advance_index(buffer, reader->index_out);
    underflow = reader->underflowed;
    UNLOCK(buffer->lock);
    return !underflow;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Writer routines.                                                          */

void *get_write_block(struct buffer *buffer)
{
    return get_buffer(buffer, buffer->index_in);
}


/* Checks for the presence of a blocking reserved reader. */
static bool check_blocking_readers(struct buffer *buffer, size_t index_in)
{
    for_reserved_readers(buffer, reader)
        if (index_in == reader->index_out)
            return true;
    return false;
}


/* Let all readers know if they've suffered an underflow. */
static void notify_underflow(struct buffer *buffer, size_t index_in)
{
    for_all_readers(buffer, reader)
    {
        if (index_in == reader->index_out)
            /* Whoops.  We've collided with a reader.  Mark the reader as
             * underflowed. */
            reader->underflowed = true;
        else
            update_backlog(reader);
    }
}


bool release_write_block(struct buffer *buffer, bool gap, uint64_t timestamp)
{
    gap = gap || buffer->write_blocked;     // Allow blocking override
    bool blocked = false;

    LOCK(buffer->lock);
    if (gap)
        /* If we're deliberately writing a gap there's nothing more to do. */
        buffer->frame_info[buffer->index_in].gap = true;
    else
    {
        /* If a gap isn't forced we might still have to make one if we can't
         * actually advance. */
        size_t new_index = advance_index(buffer, buffer->index_in);
        blocked = check_blocking_readers(buffer, new_index);
        if (blocked)
            buffer->frame_info[buffer->index_in].gap = true;
        else
        {
            /* This is the normal case: fresh data to be stored.  Advance the
             * write pointer and ensure everybody knows. */
            buffer->frame_info[buffer->index_in].timestamp = timestamp;
            buffer->index_in = new_index;
            buffer->frame_info[new_index].gap = false;
            notify_underflow(buffer, new_index);
        }
    }
    psignal(&buffer->lock);
    UNLOCK(buffer->lock);

    return !blocked;
}


void enable_buffer_write(struct buffer *buffer, bool enabled)
{
    buffer->write_blocked = !enabled;
}

bool buffer_write_enabled(struct buffer *buffer)
{
    return !buffer->write_blocked;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

size_t buffer_block_size(struct buffer *buffer)
{
    return buffer->block_size;
}

size_t reader_block_size(struct reader_state *reader)
{
    return buffer_block_size(reader->buffer);
}

uint64_t ts_to_microseconds(const struct timespec *ts)
{
    return 1000000 * (uint64_t) ts->tv_sec + ts->tv_nsec / 1000;
}


bool create_buffer(
    struct buffer **buffer, size_t block_size, size_t block_count)
{
    if (!TEST_NULL(*buffer = malloc(sizeof(struct buffer))))
        return false;

    (*buffer)->block_size = block_size;
    (*buffer)->block_count = block_count;
    /* The frame buffer must be page aligned, because we're going to write to
     * disk with direct I/O. */
    (*buffer)->frame_buffer = valloc(block_count * block_size);
    (*buffer)->frame_info = calloc(block_count, sizeof(struct frame_info));
    initialise_locking(&(*buffer)->lock);
    (*buffer)->index_in = 0;
    (*buffer)->write_blocked = false;
    INIT_LIST_HEAD(&(*buffer)->all_readers);
    INIT_LIST_HEAD(&(*buffer)->reserved_readers);
    return
        TEST_NULL((*buffer)->frame_buffer)  &&
        TEST_NULL((*buffer)->frame_info);
}
