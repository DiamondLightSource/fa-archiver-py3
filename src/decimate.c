#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>
#include <pthread.h>

#include "error.h"
#include "buffer.h"
#include "sniffer.h"

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
/* CIC configuration settings read from configuration file. */

/* Number of blocks in circular buffer. */
static int block_count;
/* Number of samples in output block. */
static int output_sample_count;
/* Data reduction factor. */
static int decimation_factor;

static int cic_stage_count = 3;
static struct fa_row *cic_accumulators;
static int cic_history_length[] = { 2, 1, 1 };
static int *cic_history_index;
static struct fa_row **cic_histories;

static int filter_length = 1;
static struct fa_row *filter_buffer;
static double compensation_filter[] = { 1 };
static double filter_scaling;


static bool parse_config_file(const char *config_file)
{
    block_count = 10;    // Random sensible number
    decimation_factor = 5;
    output_sample_count = fa_block_size / decimation_factor / FA_FRAME_SIZE;
    cic_accumulators = calloc(cic_stage_count, FA_FRAME_SIZE);
    cic_histories = calloc(cic_stage_count, sizeof(struct fa_row *));
    for (int i = 0; i < cic_stage_count; i++)
        cic_histories[i] = calloc(cic_history_length[i], FA_FRAME_SIZE);
    cic_history_index = calloc(cic_stage_count, sizeof(int));
    filter_buffer = calloc(filter_length, FA_FRAME_SIZE);
//     compensation_filter = calloc(filter_length, sizeof(double));

    filter_scaling = 0;
    for (int i = 0; i < filter_length; i ++)
        filter_scaling += compensation_filter[i];
    for (int i = 0; i < cic_stage_count; i ++)
        filter_scaling *= decimation_factor * cic_history_length[i];
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
    for (int stage = 0; stage < cic_stage_count; stage ++)
    {
        struct fa_row *accumulator = &cic_accumulators[stage];
        for (int i = 1; i < FA_ENTRY_COUNT; i ++)
        {
            accumulator->row[i].x += row_in->row[i].x;
            accumulator->row[i].y += row_in->row[i].y;
        }
        row_in = accumulator;
    }
    return row_in;
}

static void decimate(const struct fa_row *row_in, struct fa_row *row_out)
{
    for (int stage = 0; stage < cic_stage_count; stage ++)
    {
        struct fa_row *history =
            &cic_histories[stage][cic_history_index[stage]];
        cic_history_index[stage] =
            (cic_history_index[stage] + 1) % cic_history_length[stage];

        for (int i = 1; i < FA_ENTRY_COUNT; i ++)
        {
            struct fa_entry in = row_in->row[i];
            row_out->row[i].x = in.x - history->row[i].x;
            row_out->row[i].y = in.y - history->row[i].y;
            history->row[i] = in;
        }
        row_in = row_out;
    }
}


static void filter_output(struct fa_row *row_out)
{
    filter_index += 1;
    if (filter_index >= filter_length)
        filter_index = 0;

    double accumulator[FA_ENTRY_COUNT][2];
    memset(&accumulator, 0, sizeof(accumulator));
    for (int j = 0; j < filter_length; j ++)
    {
        double coeff = compensation_filter[j];
        struct fa_row *row = &filter_buffer[(filter_index + j) % filter_length];
        for (int i = 1; i < FA_ENTRY_COUNT; i ++)
        {
            accumulator[i][0] += coeff * row->row[i].x;
            accumulator[i][1] += coeff * row->row[i].y;
        }
    }

    for (int i = 1; i < FA_ENTRY_COUNT; i ++)
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
            decimate(row, &filter_buffer[filter_index]);
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


bool initialise_decimation(
    const char *config_file, struct buffer *fa_buffer, struct buffer **buffer)
{
    fa_block_size = buffer_block_size(fa_buffer);
    reader = open_reader(fa_buffer, false);
    running = true;

    return
        parse_config_file(config_file)  &&
        create_buffer(&decimation_buffer,
            output_sample_count * FA_FRAME_SIZE, block_count)  &&
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
