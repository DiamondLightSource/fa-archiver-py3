#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
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
/* Size of each block in circular buffer. */
static int block_size;
/* Data reduction factor. */
static int decimation_factor;


static bool parse_config_file(const char *config_file)
{
    block_count = 10;    // Random sensible number
    decimation_factor = 8;
    block_size = fa_block_size / decimation_factor;
    return true;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Data processing. */


static void decimate_block(const void *block_in)
{
    void *block_out = get_write_block(decimation_buffer);
    if (block_in)
    {
        for (int i = 0;
             i * decimation_factor * FA_FRAME_SIZE < fa_block_size; i++)
        {
            struct fa_entry *row_in = (struct fa_entry *) (
                (char *) block_in + i * decimation_factor * FA_FRAME_SIZE);
            struct fa_entry *row_out = (struct fa_entry *) (
                (char *) block_out + i * FA_FRAME_SIZE);
            for (int j = 0; j < FA_ENTRY_COUNT; j ++)
                row_out[j] = row_in[j];
        }
    }
    release_write_block(decimation_buffer, block_in == NULL);
}

static void * decimation_thread(void *context)
{
    while (running)
    {
        const void *block = get_read_block(reader, NULL, NULL);
        decimate_block(block);
        if (block)
            release_read_block(reader);
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
        create_buffer(&decimation_buffer, block_size, block_count)  &&
        DO_(*buffer = decimation_buffer)  &&
        TEST_0(pthread_create(&decimate_id, NULL, decimation_thread, NULL));
}


void terminate_decimation(void)
{
    log_message("Closing decimation");
    running = false;
    stop_reader(reader);
    ASSERT_0(pthread_cancel(decimate_id));
    ASSERT_0(pthread_join(decimate_id, NULL));
    close_reader(reader);
}
