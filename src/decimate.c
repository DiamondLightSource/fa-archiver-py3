/* Filtered decimation of live FA data by CIC + FIR.
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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <memory.h>
#include <pthread.h>
#include <math.h>

#include "error.h"
#include "buffer.h"
#include "fa_sniffer.h"
#include "parse.h"
#include "config_file.h"

#include "decimate.h"


/* Workspace declarations. */
struct fa_entry_int64  { int64_t x, y; };
struct fa_entry_double { double  x, y; };
struct fa_row_int64 { struct fa_entry_int64 row[0]; };


/* Incoming buffer of FA blocks and associated reader. */
static struct reader_state *reader;
static size_t fa_block_size;
static unsigned int fa_entry_count;
static size_t sizeof_row_int64;

/* Used for event mask accumulation, only active if events_fa_id != -1. */
static unsigned int events_fa_id;
static struct fa_entry accumulated_events;

/* Buffer of decimated blocks. */
static struct buffer *decimation_buffer;

/* Control flags for orderly shutdown of decimation thread. */
static bool running;
static pthread_t decimate_id;



/* Macros for indexing a pointers to arrays of fa_row and fa_row_int64
 * structures. */
#define INDEX_ROW(const, base, offset) \
    ((const struct fa_row *) ( \
        (const void *) (base) + (offset) * fa_entry_count * FA_ENTRY_SIZE))
#define INDEX_ROW64(base, offset) \
    ((struct fa_row_int64 *) ((void *) (base) + (offset) * sizeof_row_int64))


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* CIC configuration. */


/* CIC configuration settings read from configuration file. */
static unsigned int decimation_factor;          // CIC decimation factor
static struct uint_array comb_orders;           // Array of comb orders
static struct double_array compensation_filter; // Smoothes out overall response
static unsigned int filter_decimation = 1;      // Extra decimation at FIR stage
static unsigned int output_sample_count = 100;  // Samples per output block
static unsigned int output_block_count = 50;    // Number of buffered blocks

/* Description of settings above to be read from configuration file. */
static const struct config_entry config_table[] = {
    CONFIG(decimation_factor,   parse_uint),
    CONFIG(comb_orders,         parse_uint_array),
    CONFIG(compensation_filter, parse_double_array),
    CONFIG(filter_decimation,   parse_uint, OPTIONAL),
    CONFIG(output_sample_count, parse_uint, OPTIONAL),
    CONFIG(output_block_count,  parse_uint, OPTIONAL),
};


/* Workspace initialised from CIC configuration. */
static unsigned int cic_order;
/* One accumulator for each order. */
static struct fa_row_int64 *cic_accumulators;
/* The comb histories are moderately complicated: for each number N[M] =
 * comb_orders.data[M-1] for M = 1..comb_orders.count we have an array of NxM
 * histories and an index cycling from 0 to M-1. */
static struct fa_row_int64 **comb_histories;
static unsigned int *comb_history_index;

static struct fa_row_int64 *filter_buffer;
static double filter_scaling;
static unsigned int group_delay;


/* Called after successful parsing of the configuration file. */
static bool initialise_configuration(void)
{
    /* Accumulate the CIC order from the orders of all the combs. */
    cic_order = 0;
    for (unsigned int i = 0; i < comb_orders.count; i ++)
        cic_order += comb_orders.data[i];

    /* Some sanity checking on parameters. */
    bool ok =
        TEST_OK_(decimation_factor > 1, "Invalid decimation factor")  &&
        TEST_OK_(cic_order > 0, "No CIC stages given")  &&
        TEST_OK_(compensation_filter.count > 0, "Empty compensation filter")  &&
        TEST_OK_(filter_decimation > 0, "Invalid filter decimation");
    if (!ok)
        return false;

    /* One accumulator for each stage. */
    cic_accumulators = calloc(cic_order, sizeof_row_int64);
    /* Array of history buffers for variable length comb stage. */
    comb_histories = calloc(comb_orders.count, sizeof(struct fa_row_int64 *));
    for (unsigned int i = 0; i < comb_orders.count; i ++)
        comb_histories[i] = calloc(
            comb_orders.data[i] * (i + 1), sizeof_row_int64);
    comb_history_index = calloc(comb_orders.count, sizeof(unsigned int));
    /* History buffer for compensation filter. */
    filter_buffer = calloc(compensation_filter.count, sizeof_row_int64);

    /* Compute scaling factor for overall unit DC response and group delay for
     * the entire filter chain. */
    filter_scaling = 0;
    unsigned int filter_length = 1 +
        (compensation_filter.count - 1) * decimation_factor;
    for (unsigned int i = 0; i < compensation_filter.count; i ++)
        filter_scaling += compensation_filter.data[i];
    for (unsigned int i = 0; i < comb_orders.count; i ++)
    {
        for (unsigned int j = 0; j < comb_orders.data[i]; j ++)
            filter_scaling *= decimation_factor * (i + 1);
        filter_length +=
            comb_orders.data[i] * ((i + 1) * decimation_factor - 1);
    }
    filter_scaling = 1 / filter_scaling;
    group_delay = filter_length / 2;

    return true;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Data processing. */

/* This counter tracks the decimation counter. */
static unsigned int decimation_counter;
/* Tracks position in the compensation filter buffer. */
static unsigned int filter_index;
/* Keeps count of output filter extra decimation. */
static unsigned int output_counter;


/* Helper routine for cycling index pointers: advances and returns true iff the
 * index has cycled. */
static bool advance_index(unsigned int *ix, unsigned int limit)
{
    *ix += 1;
    if (*ix >= limit)
    {
        *ix = 0;
        return true;
    }
    else
        return false;
}


/* Helper macro for repeated pattern in accumulate().  Note that t0 is
 * untouched.  Needs to be a macro because we call this twice, once with 32-bit
 * row_in and the second time with 64-bit. */
#define ACCUMULATE_ROW(accumulator, row_in) \
    do for (unsigned int i = 1; i < fa_entry_count; i ++) \
    { \
        (accumulator)->row[i].x += (row_in)->row[i].x; \
        (accumulator)->row[i].y += (row_in)->row[i].y; \
    } while(0)


/* Accumulates a single update into the accumulator array for the integrating
 * part of the CIC filter, returns the last row. */
static const struct fa_row_int64 *accumulate(const struct fa_row *row_in)
{
    /* The first stage converts 32 bit in into 64 bit intermediate results. */
    struct fa_row_int64 *accumulator = cic_accumulators;
    ACCUMULATE_ROW(accumulator, row_in);
    struct fa_row_int64 *last_row = accumulator;

    /* The remaining rows are all uniform. */
    for (unsigned int stage = 1; stage < cic_order; stage ++)
    {
        accumulator = INDEX_ROW64(accumulator, 1);
        ACCUMULATE_ROW(accumulator, last_row);
        last_row = accumulator;
    }
    return last_row;
}


/* Performs repeated comb filter of raw decimated data. */
static void comb(
    const struct fa_row_int64 *row_in, struct fa_row_int64 *row_out)
{
    for (unsigned int order = 0; order < comb_orders.count; order ++)
    {
        unsigned int N = comb_orders.data[order];
        struct fa_row_int64 *history =
            INDEX_ROW64(comb_histories[order], N * comb_history_index[order]);
        advance_index(&comb_history_index[order], order + 1);

        for (unsigned int n = 0; n < N; n ++)
        {
            for (unsigned int i = 1; i < fa_entry_count; i ++)
            {
                struct fa_entry_int64 in = row_in->row[i];
                row_out->row[i].x = in.x - history->row[i].x;
                row_out->row[i].y = in.y - history->row[i].y;
                history->row[i] = in;
            }

            /* A couple of tricks here.  On the first pass through row_in and
             * row_out are different, but on all subsequent loops we're
             * recycling the same result location -- better for the cache.
             * Also, we arrange the histories so that this simple stepping
             * through works correctly. */
            row_in = row_out;
            history = INDEX_ROW64(history, 1);
        }
    }
}


/* Convolves compensation filter with the waiting output buffer. */
static void filter_output(struct fa_row *row_out)
{
    size_t sizeof_row_double = fa_entry_count * sizeof(struct fa_entry_double);
    struct fa_entry_double *accumulator = alloca(sizeof_row_double);
    memset(accumulator, 0, sizeof_row_double);

    for (unsigned int j = 0; j < compensation_filter.count; j ++)
    {
        double coeff = compensation_filter.data[j];
        struct fa_row_int64 *row =
            INDEX_ROW64(filter_buffer,
                (filter_index + j) % compensation_filter.count);
        for (unsigned int i = 1; i < fa_entry_count; i ++)
        {
            accumulator[i].x += coeff * (double) row->row[i].x;
            accumulator[i].y += coeff * (double) row->row[i].y;
        }
    }

    for (unsigned int i = 1; i < fa_entry_count; i ++)
    {
        row_out->row[i].x = (int) (filter_scaling * accumulator[i].x);
        row_out->row[i].y = (int) (filter_scaling * accumulator[i].y);
    }
}


static void update_t0(struct fa_row *row_out, const struct fa_entry *t0)
{
    row_out->row[0].x = t0->x - (int) group_delay;
    row_out->row[0].y = t0->y - (int) group_delay;
}

static void combine_events(const struct fa_row *row_in)
{
    if (events_fa_id < fa_entry_count)
    {
        accumulated_events.x |= row_in->row[events_fa_id].x;
        accumulated_events.y |= row_in->row[events_fa_id].y;
    }
}

static void update_events(struct fa_row *row_out)
{
    if (events_fa_id < fa_entry_count)
    {
        row_out->row[events_fa_id] = accumulated_events;
        accumulated_events = (struct fa_entry) { 0, 0 };
    }
}


static struct fa_row *block_out;
static unsigned int out_pointer;


/* Advance the output by one row or mark a gap in incoming data. */
static void advance_write_block(bool gap, uint64_t timestamp)
{
    if (advance_index(&out_pointer, output_sample_count)  ||  gap)
    {
        /* Ought to correct the timestamp here by the filter group delay and the
         * difference between the two data blocks. */
        IGNORE(TEST_OK(release_write_block(decimation_buffer, gap, timestamp)));
        block_out = get_write_block(decimation_buffer);

        /* In the presence of a gap we ought to reset all the filters. */
    }
}


/* CIC: repeated integration steps on every input sample, decimate by selected
 * decimation factor, comb filter of each output sample. */
static void decimate_block(const struct fa_row *block_in, uint64_t timestamp)
{
    unsigned int sample_count_in = (unsigned int) (
        fa_block_size / fa_entry_count / FA_ENTRY_SIZE);
    for (unsigned int in = 0; in < sample_count_in; in ++)
    {
        const struct fa_entry *t0 = &block_in->row[0];
        const struct fa_row_int64 *row = accumulate(block_in);
        combine_events(block_in);
        block_in = INDEX_ROW(const, block_in, 1);

        if (advance_index(&decimation_counter, decimation_factor))
        {
            comb(row, INDEX_ROW64(filter_buffer, filter_index));
            advance_index(&filter_index, compensation_filter.count);

            if (advance_index(&output_counter, filter_decimation))
            {
                struct fa_row *row_out = INDEX_ROW(, block_out, out_pointer);
                filter_output(row_out);
                update_t0(row_out, t0);
                update_events(row_out);
                advance_write_block(false, timestamp);
            }
        }
    }
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Decimation control. */


static void *decimation_thread(void *context)
{
    block_out = get_write_block(decimation_buffer);

    while (running)
    {
        uint64_t timestamp;
        const struct fa_row *block_in = get_read_block(reader, &timestamp);
        if (block_in)
        {
            decimate_block(block_in, timestamp);
            release_read_block(reader);
        }
        else
            /* Mark a gap if can't get a read block. */
            advance_write_block(true, timestamp);
    }
    return NULL;
}


unsigned int get_decimation_factor(void)
{
    return decimation_factor * filter_decimation;
}


bool initialise_decimation(
    const char *config_file, struct buffer *fa_buffer, struct buffer **buffer,
    unsigned int _fa_entry_count, unsigned int _events_fa_id)
{
    fa_block_size = buffer_block_size(fa_buffer);
    fa_entry_count = _fa_entry_count;
    events_fa_id = _events_fa_id;
    sizeof_row_int64  = sizeof(struct fa_entry_int64) * fa_entry_count;

    return
        config_parse_file(
            config_file, config_table, ARRAY_SIZE(config_table))  &&
        initialise_configuration()  &&
        DO_(reader = open_reader(fa_buffer, false))  &&
        create_buffer(&decimation_buffer,
            output_sample_count * fa_entry_count * FA_ENTRY_SIZE,
            output_block_count)  &&
        DO_(*buffer = decimation_buffer);
}


bool start_decimation(void)
{
    running = true;
    return TEST_0(pthread_create(&decimate_id, NULL, decimation_thread, NULL));
}


void terminate_decimation(void)
{
    log_message("Closing decimation");
    ASSERT_0(pthread_cancel(decimate_id));
    running = false;
    interrupt_reader(reader);
    ASSERT_0(pthread_join(decimate_id, NULL));
    close_reader(reader);
}
