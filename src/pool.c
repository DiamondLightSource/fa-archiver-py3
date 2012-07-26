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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include "error.h"
#include "list.h"
#include "locking.h"

#include "pool.h"


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Buffer pool. */

DECLARE_LOCKING(buffer_lock);

struct pool_entry {
    struct pool_entry *next;
    char buffer[];
};

static unsigned int pool_size;
static struct pool_entry *buffer_pool = NULL;

size_t pooled_buffer_size;



bool lock_buffers(struct read_buffers *buffers, unsigned int count)
{
    bool ok;
    LOCK(buffer_lock);
    ok = TEST_OK_(count <= pool_size, "Read too busy");
    if (ok)
    {
        pool_size -= count;
        buffers->buffers = malloc(count * sizeof(void *));
        buffers->count = count;
        for (unsigned int i = 0; i < count; i ++)
        {
            struct pool_entry *entry = buffer_pool;
            buffer_pool = entry->next;
            buffers->buffers[i] = entry->buffer;
        }
    }
    UNLOCK(buffer_lock);
    return ok;
}


void unlock_buffers(struct read_buffers *buffers)
{
    LOCK(buffer_lock);
    for (unsigned int i = 0; i < buffers->count; i ++)
    {
        /* Recover the pool_entry address and chain back onto free list. */
        struct pool_entry *entry =
            container_of(buffers->buffers[i], struct pool_entry, buffer[0]);
        entry->next = buffer_pool;
        buffer_pool = entry;
    }
    pool_size += buffers->count;
    UNLOCK(buffer_lock);
    free(buffers->buffers);
}


void initialise_buffer_pool(size_t buffer_size, unsigned int count)
{
    pooled_buffer_size = buffer_size;
    for (unsigned int i = 0; i < count; i ++)
    {
        struct pool_entry *entry =
            malloc(sizeof(struct pool_entry) + buffer_size);
        entry->next = buffer_pool;
        buffer_pool = entry;
    }
    pool_size = count;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Write buffers. */

bool allocate_write_buffer(struct write_buffer *buffer, unsigned int count)
{
    return
        lock_buffers(&buffer->buffers, count)  &&
        TEST_NULL(buffer->out_pointers = calloc(count, sizeof(size_t)));
}


void release_write_buffer(struct write_buffer *buffer)
{
    unlock_buffers(&buffer->buffers);
    free(buffer->out_pointers);
}


bool flush_buffer(struct write_buffer *buffer)
{
    ASSERT_OK(buffer->buffers.count == 1  &&  buffer->file >= 0);
    return IF_(buffer->out_pointers[0] > 0,
        TEST_write_(
            buffer->file, buffer->buffers.buffers[0], buffer->out_pointers[0],
            "Error writing to client")  &&
        DO_(buffer->out_pointers[0] = 0));
}


/* Helper routine to ensure length bytes free in buffer by flushing or advancing
 * buffer if necessary. */
static bool ensure_buffer(struct write_buffer *buffer, size_t length)
{
    unsigned int current = buffer->current_buffer;
    if (length + buffer->out_pointers[current] > pooled_buffer_size)
    {
        ASSERT_OK(length <= pooled_buffer_size);
        if (current + 1 < buffer->buffers.count)
            return DO_(buffer->current_buffer += 1);
        else
            return flush_buffer(buffer);
    }
    else
        return true;
}


bool write_buffer(struct write_buffer *buffer, const void *data, size_t length)
{
    if (ensure_buffer(buffer, length))
    {
        unsigned int current = buffer->current_buffer;
        void *target =
            buffer->buffers.buffers[current] + buffer->out_pointers[current];
        memcpy(target, data, length);
        buffer->out_pointers[current] += length;
        return true;
    }
    else
        return false;
}


void *get_buffer(struct write_buffer *buffer, size_t min_length, size_t *length)
{
    if (ensure_buffer(buffer, min_length))
    {
        unsigned int current = buffer->current_buffer;
        void *target =
            buffer->buffers.buffers[current] + buffer->out_pointers[current];
        *length = pooled_buffer_size - buffer->out_pointers[current];
        return target;
    }
    else
        return NULL;
}


void release_buffer(struct write_buffer *buffer, size_t length)
{
    buffer->out_pointers[buffer->current_buffer] += length;
}


bool write_delayed_buffer(
    struct write_buffer *buffer_in, struct write_buffer *buffer_out)
{
    bool ok = true;
    for (unsigned int i = 0; ok  &&  i <= buffer_in->current_buffer; i ++)
        ok = write_buffer(buffer_out,
            buffer_in->buffers.buffers[i], buffer_in->out_pointers[i]);
    return ok;
}
