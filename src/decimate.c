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
static int decimation_factor;               // Overall data reduction
static struct int_array history_length;     // Differentiation history lengths
static struct double_array compensation_filter; // Smoothes out overall response
static int output_sample_count = 100;       // Samples per output block
static int output_block_count = 10;         // Total number of buffered blocks

/* Description of settings above to be read from configuration file. */
static const struct config_entry config_table[] = {
    CONFIG(decimation_factor,   parse_int),
    CONFIG(history_length,      parse_int_array),
    CONFIG(compensation_filter, parse_double_array),
    CONFIG(output_sample_count, parse_int, OPTIONAL),
    CONFIG(output_block_count,  parse_int, OPTIONAL),
};


/* Workspace initialised from CIC configuration. */
static struct fa_row *cic_accumulators;
static struct fa_row **cic_histories;
static int *cic_history_index;

static struct fa_row *filter_buffer;
static double filter_scaling;


/* Called after successful parsing of the configuration file. */
static bool initialise_configuration(void)
{
    /* Some sanity checking on parameters. */
    bool ok =
        TEST_OK_(decimation_factor > 1, "Invalid decimation factor")  &&
        TEST_OK_(history_length.count > 0, "No CIC stages given")  &&
        TEST_OK_(compensation_filter.count > 0, "Empty compensation filter");
    if (!ok)
        return false;

    /* One accumulator for each stage. */
    cic_accumulators = calloc(history_length.count, FA_FRAME_SIZE);
    /* Array of history buffers for variable length differentiation stage. */
    cic_histories = calloc(history_length.count, sizeof(struct fa_row *));
    for (int i = 0; i < history_length.count; i ++)
        cic_histories[i] = calloc(history_length.data[i], FA_FRAME_SIZE);
    cic_history_index = calloc(history_length.count, sizeof(int));
    /* History buffer for compensation filter. */
    filter_buffer = calloc(compensation_filter.count, FA_FRAME_SIZE);

    /* Compute scaling factor for overall unit DC response for the entire filter
     * chain. */
    filter_scaling = 0;
    for (int i = 0; i < compensation_filter.count; i ++)
        filter_scaling += compensation_filter.data[i];
    for (int i = 0; i < history_length.count; i ++)
        filter_scaling *= decimation_factor * history_length.data[i];
    filter_scaling = 1 / filter_scaling;

    return true;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Data processing. */

/* This counter tracks the decimation counter. */
static int decimation_counter;
/* Tracks position in the compensation filter buffer. */
static int filter_index;


/* Accumulates a single update into the accumulator array for the integrating
 * part of the CIC filter, returns the last row. */
static const struct fa_row * accumulate(const struct fa_row *row_in)
{
    for (int stage = 0; stage < history_length.count; stage ++)
    {
        struct fa_row *accumulator = &cic_accumulators[stage];
        for (int i = 0; i < FA_ENTRY_COUNT; i ++)
        {
            accumulator->row[i].x += row_in->row[i].x;
            accumulator->row[i].y += row_in->row[i].y;
        }
        row_in = accumulator;
    }
    return row_in;
}


/* Performs repeated differentiation of raw decimated data. */
static void differentiate(const struct fa_row *row_in, struct fa_row *row_out)
{
    for (int stage = 0; stage < history_length.count; stage ++)
    {
        struct fa_row *history =
            &cic_histories[stage][cic_history_index[stage]];
        cic_history_index[stage] =
            (cic_history_index[stage] + 1) % history_length.data[stage];

        for (int i = 0; i < FA_ENTRY_COUNT; i ++)
        {
            struct fa_entry in = row_in->row[i];
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
    filter_index += 1;
    if (filter_index >= compensation_filter.count)
        filter_index = 0;

    double accumulator[FA_ENTRY_COUNT][2];
    memset(&accumulator, 0, sizeof(accumulator));
    for (int j = 0; j < compensation_filter.count; j ++)
    {
        double coeff = compensation_filter.data[j];
        struct fa_row *row =
            &filter_buffer[(filter_index + j) % compensation_filter.count];
        for (int i = 0; i < FA_ENTRY_COUNT; i ++)
        {
            accumulator[i][0] += coeff * row->row[i].x;
            accumulator[i][1] += coeff * row->row[i].y;
        }
    }

    for (int i = 0; i < FA_ENTRY_COUNT; i ++)
    {
        row_out->row[i].x = (int) (filter_scaling * accumulator[i][0]);
        row_out->row[i].y = (int) (filter_scaling * accumulator[i][1]);
    }
}


static struct fa_row *block_out;
static int out_pointer;


/* Advance the output by one row or mark a gap in incoming data. */
static void advance_write_block(bool gap, struct timespec *ts)
{
    out_pointer += 1;
    if (gap  ||  out_pointer >= output_sample_count)
    {
        release_write_block(decimation_buffer, gap);
        out_pointer = 0;
        block_out = get_write_block(decimation_buffer);
    }
}


/* CIC: repeated integration steps on every input sample, decimate by selected
 * decimation factor, differentiation of each output sample. */
static void decimate_block(const struct fa_row *block_in, struct timespec *ts)
{
    int sample_count_in = fa_block_size / FA_FRAME_SIZE;
    for (int in = 0; in < sample_count_in; in ++)
    {
        const struct fa_row *row = accumulate(block_in++);

        decimation_counter += 1;
        if (decimation_counter >= decimation_factor)
        {
            decimation_counter = 0;
            differentiate(row, &filter_buffer[filter_index]);
            filter_output(&block_out[out_pointer]);
            advance_write_block(false, ts);
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
        struct timespec ts;
        const struct fa_row *block_in = get_read_block(reader, NULL, &ts);
        if (block_in)
        {
            decimate_block(block_in, &ts);
            release_read_block(reader);
        }
        else
            /* Mark a gap if can't get a read block. */
            advance_write_block(true, &ts);
    }
    return NULL;
}


int get_decimation_factor(void)
{
    return decimation_factor;
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
