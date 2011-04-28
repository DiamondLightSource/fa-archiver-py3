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
#include "sniffer.h"
#include "parse.h"
#include "config_file.h"

#include "decimate.h"


/* A couple of workspace declarations. */
struct fa_entry_int64  { int64_t x, y; };
struct fa_entry_double { double  x, y; };
struct fa_row_int64  { struct fa_entry_int64  row[FA_ENTRY_COUNT]; };
struct fa_row_double { struct fa_entry_double row[FA_ENTRY_COUNT]; };


/* Incoming buffer of FA blocks and associated reader. */
static struct reader_state *reader;
static size_t fa_block_size;

/* Buffer of decimated blocks. */
static struct buffer *decimation_buffer;

/* Control flags for orderly shutdown of decimation thread. */
static bool running;
static pthread_t decimate_id;


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* CIC configuration. */


/* CIC configuration settings read from configuration file. */
static int decimation_factor;               // CIC decimation factor
static struct int_array history_length;     // Differentiation history lengths
static struct double_array compensation_filter; // Smoothes out overall response
static int filter_decimation = 1;           // Extra decimation at FIR stage
static int output_sample_count = 100;       // Samples per output block
static int output_block_count = 10;         // Total number of buffered blocks

/* Description of settings above to be read from configuration file. */
static const struct config_entry config_table[] = {
    CONFIG(decimation_factor,   parse_int),
    CONFIG(history_length,      parse_int_array),
    CONFIG(compensation_filter, parse_double_array),
    CONFIG(filter_decimation,   parse_int, OPTIONAL),
    CONFIG(output_sample_count, parse_int, OPTIONAL),
    CONFIG(output_block_count,  parse_int, OPTIONAL),
};


/* Workspace initialised from CIC configuration. */
static struct fa_row_int64 *cic_accumulators;
static struct fa_row_int64 **cic_histories;
static int *cic_history_index;

static struct fa_row_int64 *filter_buffer;
static double filter_scaling;
static int group_delay;


/* Called after successful parsing of the configuration file. */
static bool initialise_configuration(void)
{
    /* Some sanity checking on parameters. */
    bool ok =
        TEST_OK_(decimation_factor > 1, "Invalid decimation factor")  &&
        TEST_OK_(history_length.count > 0, "No CIC stages given")  &&
        TEST_OK_(compensation_filter.count > 0, "Empty compensation filter")  &&
        TEST_OK_(filter_decimation > 0, "Invalid filter decimation");
    if (!ok)
        return false;

    /* One accumulator for each stage. */
    cic_accumulators = calloc(
        history_length.count, sizeof(struct fa_row_int64));
    /* Array of history buffers for variable length comb stage. */
    cic_histories = calloc(history_length.count, sizeof(struct fa_row_int64 *));
    for (int i = 0; i < history_length.count; i ++)
        cic_histories[i] = calloc(
            history_length.data[i], sizeof(struct fa_row_int64));
    cic_history_index = calloc(history_length.count, sizeof(int));
    /* History buffer for compensation filter. */
    filter_buffer = calloc(
        compensation_filter.count, sizeof(struct fa_row_int64));

    /* Compute scaling factor for overall unit DC response and group delay for
     * the entire filter chain. */
    filter_scaling = 0;
    int filter_length = compensation_filter.count;
    for (int i = 0; i < compensation_filter.count; i ++)
        filter_scaling += compensation_filter.data[i];
    for (int i = 0; i < history_length.count; i ++)
    {
        filter_scaling *= decimation_factor * history_length.data[i];
        filter_length += history_length.data[i] - 1;
    }
    filter_scaling = 1 / filter_scaling;
    group_delay = filter_length / 2;

    return true;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Data processing. */

/* This counter tracks the decimation counter. */
static int decimation_counter;
/* Tracks position in the compensation filter buffer. */
static int filter_index;
/* Keeps count of output filter extra decimation. */
static int output_counter;


/* Helper routine for cycling index pointers: advances and returns true iff the
 * index has cycled. */
static bool advance_index(int *ix, int limit)
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
 * untouched. */
#define ACCUMULATE_ROW(accumulator, row_in) \
    do for (int i = 1; i < FA_ENTRY_COUNT; i ++) \
    { \
        (accumulator)->row[i].x += (row_in)->row[i].x; \
        (accumulator)->row[i].y += (row_in)->row[i].y; \
    } while(0)

/* Accumulates a single update into the accumulator array for the integrating
 * part of the CIC filter, returns the last row. */
static const struct fa_row_int64 * accumulate(const struct fa_row *row_in)
{
    /* The first stage converts 32 bit in into 64 bit intermediate results. */
    ACCUMULATE_ROW(&cic_accumulators[0], row_in);
    struct fa_row_int64 *last_row = &cic_accumulators[0];

    /* The remaining rows are all uniform. */
    for (int stage = 1; stage < history_length.count; stage ++)
    {
        ACCUMULATE_ROW(&cic_accumulators[stage], last_row);
        last_row = &cic_accumulators[stage];
    }
    return last_row;
}


/* Performs repeated comb filter of raw decimated data. */
static void comb(
    const struct fa_row_int64 *row_in, struct fa_row_int64 *row_out)
{
    for (int stage = 0; stage < history_length.count; stage ++)
    {
        struct fa_row_int64 *history =
            &cic_histories[stage][cic_history_index[stage]];
        advance_index(&cic_history_index[stage], history_length.data[stage]);

        for (int i = 1; i < FA_ENTRY_COUNT; i ++)
        {
            struct fa_entry_int64 in = row_in->row[i];
            row_out->row[i].x = in.x - history->row[i].x;
            row_out->row[i].y = in.y - history->row[i].y;
            history->row[i] = in;
        }
        row_in = row_out;
    }
}


/* Convolves compensation filter with the waiting output buffer. */
static void filter_output(struct fa_row *row_out)
{
    struct fa_row_double accumulator;
    memset(&accumulator, 0, sizeof(accumulator));
    for (int j = 0; j < compensation_filter.count; j ++)
    {
        double coeff = compensation_filter.data[j];
        struct fa_row_int64 *row =
            &filter_buffer[(filter_index + j) % compensation_filter.count];
        for (int i = 1; i < FA_ENTRY_COUNT; i ++)
        {
            accumulator.row[i].x += coeff * row->row[i].x;
            accumulator.row[i].y += coeff * row->row[i].y;
        }
    }

    for (int i = 1; i < FA_ENTRY_COUNT; i ++)
    {
        row_out->row[i].x = (int) (filter_scaling * accumulator.row[i].x);
        row_out->row[i].y = (int) (filter_scaling * accumulator.row[i].y);
    }
}


static void update_t0(struct fa_row *row_out, const struct fa_entry *t0)
{
    row_out->row[0].x = t0->x - group_delay;
    row_out->row[0].y = t0->y - group_delay;
}


static struct fa_row *block_out;
static int out_pointer;


/* Advance the output by one row or mark a gap in incoming data. */
static void advance_write_block(bool gap, uint64_t timestamp)
{
    if (advance_index(&out_pointer, output_sample_count)  ||  gap)
    {
        /* Ought to correct the timestamp here by the filter group delay and the
         * difference between the two data blocks. */
        release_write_block(decimation_buffer, gap, timestamp);
        block_out = get_write_block(decimation_buffer);

        /* In the presence of a gap we ought to reset all the filters. */
    }
}


/* CIC: repeated integration steps on every input sample, decimate by selected
 * decimation factor, comb filter of each output sample. */
static void decimate_block(const struct fa_row *block_in, uint64_t timestamp)
{
    int sample_count_in = fa_block_size / FA_FRAME_SIZE;
    for (int in = 0; in < sample_count_in; in ++)
    {
        const struct fa_entry *t0 = &block_in->row[0];
        const struct fa_row_int64 *row = accumulate(block_in++);

        if (advance_index(&decimation_counter, decimation_factor))
        {
            comb(row, &filter_buffer[filter_index]);
            advance_index(&filter_index, compensation_filter.count);

            if (advance_index(&output_counter, filter_decimation))
            {
                filter_output(&block_out[out_pointer]);
                update_t0(&block_out[out_pointer], t0);
                advance_write_block(false, timestamp);
            }
        }
    }
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Decimation control. */


static void * decimation_thread(void *context)
{
    block_out = get_write_block(decimation_buffer);

    while (running)
    {
        uint64_t timestamp;
        const struct fa_row *block_in =
            get_read_block(reader, NULL, &timestamp);
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


int get_decimation_factor(void)
{
    return decimation_factor * filter_decimation;
}


bool initialise_decimation(const char *config_file)
{
    return
        config_parse_file(
            config_file, config_table, ARRAY_SIZE(config_table))  &&
        initialise_configuration();
}


bool start_decimation(struct buffer *fa_buffer, struct buffer **buffer)
{
    fa_block_size = buffer_block_size(fa_buffer);
    reader = open_reader(fa_buffer, false);
    running = true;

    return
        create_buffer(&decimation_buffer,
            output_sample_count * FA_FRAME_SIZE, output_block_count)  &&
        DO_(*buffer = decimation_buffer)  &&
        TEST_0(pthread_create(&decimate_id, NULL, decimation_thread, NULL));
}


void terminate_decimation(void)
{
    log_message("Closing decimation");
    ASSERT_0(pthread_cancel(decimate_id));
    running = false;
    stop_reader(reader);
    ASSERT_0(pthread_join(decimate_id, NULL));
    close_reader(reader);
}
