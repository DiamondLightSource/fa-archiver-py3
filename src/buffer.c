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
    /* Used to detect reader underflow. */
    size_t cycle_count;

    /* One reserved reader is supported: we will never overwrite the block it's
     * reading and a gap will be forced instead if necessary. */
    struct reader_state *reserved_reader;
};


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Miscellaneous support routines.                                           */


static size_t advance_index(struct buffer *buffer, size_t index)
{
    index += 1;
    if (index >= buffer->block_count)
        index = 0;
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
    bool running;                   // Used to interrupt reader
    bool gap_reported;              // Set once we've reported a gap
    size_t index_out;               // Next block to read
    size_t cycle_count;             // Buffer cycle count at last reading
};


struct reader_state *open_reader(struct buffer *buffer, bool reserved_reader)
{
    struct reader_state *reader = malloc(sizeof(struct reader_state));
    reader->buffer = buffer;
    reader->running = true;
    reader->gap_reported = false;

    LOCK(buffer->lock);
    reader->index_out = buffer->index_in;
    reader->cycle_count = buffer->cycle_count;
    if (reserved_reader)
    {
        ASSERT_OK(buffer->reserved_reader == NULL);
        buffer->reserved_reader = reader;
    }
    UNLOCK(buffer->lock);

    return reader;
}


void close_reader(struct reader_state *reader)
{
    struct buffer *buffer = reader->buffer;

    LOCK(buffer->lock);
    if (buffer->reserved_reader == reader)
        buffer->reserved_reader = NULL;
    UNLOCK(buffer->lock);

    free(reader);
}


const void *get_read_block(struct reader_state *reader, uint64_t *timestamp)
{
    struct buffer *buffer = reader->buffer;
    void *block;

    LOCK(buffer->lock);

    struct frame_info *frame_info = &buffer->frame_info[reader->index_out];
    /* Wait until one of the following conditions is satisfied:
     *  1. We're stopped by setting running to false
     *  2. The out and in indexes don't coincide
     *  3. We haven't reported a gap yet and the new frame starts a new gap. */
    while (reader->running  &&
           reader->index_out == buffer->index_in  &&
           (reader->gap_reported  ||  !frame_info->gap)  &&
           pwait_timeout(&buffer->lock, 2, 0))
        ;

    if (!reader->running)
        block = NULL;
    else if (frame_info->gap  &&  !reader->gap_reported)
        /* This block is preceded by a gap.  Return a gap indicator this time,
         * we'll return the block itself next time. */
        block = NULL;
    else if (reader->index_out == buffer->index_in)
    {
        /* If we get here there must have been a timeout.  This is definitely
         * not normal, log and treat as no data. */
        log_error("Timeout waiting for circular buffer");
        block = NULL;
    }
    else
    {
        block = get_buffer(buffer, reader->index_out);
        if (timestamp)
            *timestamp = frame_info->timestamp;
    }

    UNLOCK(buffer->lock);

    reader->gap_reported = block == NULL;
    return block;
}


void interrupt_reader(struct reader_state *reader)
{
    struct buffer *buffer = reader->buffer;
    LOCK(buffer->lock);
    reader->running = false;
    pbroadcast(&reader->buffer->lock);
    UNLOCK(buffer->lock);
}


/* Detects buffer underflow, returns true if ok. */
static bool check_underflow(
    struct reader_state *reader, size_t index_in, size_t cycle_count)
{
    /* Detect buffer underflow by inspecting the in and out pointers and
     * checking the buffer cycle count.  We can only be deceived if a full 2^32
     * cycles have ocurred since the last time we looked, but the pacing of
     * reading and writing eliminates that risk. */
    if (index_in == reader->index_out)
        /* Unmistakable collision! */
        return false;
    else if (index_in > reader->index_out)
        /* Out pointer ahead of in pointer.  We're ok if we're both on the same
         * cycle. */
        return cycle_count == reader->cycle_count;
    else
        /* Out pointer behind in pointer.  In this case the buffer should be one
         * step ahead of us. */
        return cycle_count == reader->cycle_count + 1;
}


bool release_read_block(struct reader_state *reader)
{
    /* Grab consistent snapshot of current buffer position. */
    size_t index_in, cycle_count;
    struct buffer *buffer = reader->buffer;
    LOCK(buffer->lock);
    index_in    = buffer->index_in;
    cycle_count = buffer->cycle_count;
    UNLOCK(buffer->lock);

    if (check_underflow(reader, index_in, cycle_count))
    {
        /* Normal case.  Advance to point to the next block. */
        reader->index_out = advance_index(buffer, reader->index_out);
        if (reader->index_out == 0)
            reader->cycle_count += 1;
        return true;
    }
    else
    {
        /* If we were underflowed then perform a complete reset of the read
         * stream.  Discard everything in the buffer and start again.  This
         * helps the writer which can rely on this. */
        reader->index_out = index_in;
        reader->cycle_count = cycle_count;
        reader->gap_reported = false;   // Strictly speaking, already set so!
        return false;
    }
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Writer routines.                                                          */

void *get_write_block(struct buffer *buffer)
{
    return get_buffer(buffer, buffer->index_in);
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
        /* Check for presence of blocking reserved reader. */
        blocked = buffer->reserved_reader  &&
            new_index == buffer->reserved_reader->index_out;
        if (blocked)
            /* Whoops.  Can't advance, instead force a gap and fail. */
            buffer->frame_info[buffer->index_in].gap = true;
        else
        {
            /* This is the normal case: fresh data to be stored. */
            buffer->frame_info[buffer->index_in].timestamp = timestamp;
            buffer->index_in = new_index;
            buffer->frame_info[new_index].gap = false;
            if (new_index == 0)
                buffer->cycle_count += 1;
        }
    }
    pbroadcast(&buffer->lock);
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
    return 1000000 * (uint64_t) ts->tv_sec + (uint64_t) ts->tv_nsec / 1000;
}

uint64_t get_timestamp(void)
{
    struct timespec ts;
    IGNORE(TEST_IO(clock_gettime(CLOCK_REALTIME, &ts)));
    return ts_to_microseconds(&ts);
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
    (*buffer)->cycle_count = 0;
    (*buffer)->reserved_reader = NULL;
    return
        TEST_NULL((*buffer)->frame_buffer)  &&
        TEST_NULL((*buffer)->frame_info);
}
