/* Code to interface to fa_sniffer device. */

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <limits.h>

#include "error.h"
#include "buffer.h"

#include "sniffer.h"


static pthread_t sniffer_id;

static struct buffer *fa_block_buffer;
static const char *fa_sniffer_device;



static void dummy_data(void *data, int block_size)
{
    static uint32_t dummy_t = 0;
    struct fa_entry *output = data;
    unsigned int frame_count = block_size / FA_FRAME_SIZE;
    for (unsigned int i = 0; i < frame_count; i ++)
    {
        for (int j = 0; j < FA_ENTRY_COUNT; j ++)
        {
            if (j == 0)
            {
                output->x = dummy_t;
                output->y = dummy_t;
            }
            else
            {
                uint32_t int_phase = dummy_t * j * 7000;
                double phase = 2 * M_PI * int_phase / (double) ULONG_MAX;
                output->x = (int32_t) 50000 * sin(phase);
                output->y = (int32_t) 50000 * cos(phase);
            }
            output ++;
        }
        dummy_t += 1;
    }
    usleep(100 * frame_count);
}

static void * dummy_sniffer_thread(void *context)
{
    const size_t fa_block_size = buffer_block_size(fa_block_buffer);
    while (true)
    {
        void *buffer = get_write_block(fa_block_buffer);
        if (buffer == NULL)
        {
            log_message("dummy sniffer unable to write block");
            sleep(1);
        }
        else
        {
            dummy_data(buffer, fa_block_size);
            release_write_block(fa_block_buffer, false);
        }
    }
    return NULL;
}


static void * sniffer_thread(void *context)
{
    const size_t fa_block_size = buffer_block_size(fa_block_buffer);
    bool in_gap = false;    // Only report gap once
    int fa_sniffer;
    while (TEST_IO_(
        fa_sniffer = open(fa_sniffer_device, O_RDONLY),
        "Can't open sniffer device %s", fa_sniffer_device))
    {
        while (true)
        {
            void *buffer = get_write_block(fa_block_buffer);
            if (buffer == NULL)
            {
                /* Whoops: the archiver thread has fallen behind. */
                log_message("Sniffer unable to write block");
                break;
            }
            bool gap =
                read(fa_sniffer, buffer, fa_block_size) <
                    (ssize_t) fa_block_size;
            release_write_block(fa_block_buffer, gap);
            if (gap)
            {
                if (!in_gap)
                    log_message("Unable to read block");
                in_gap = true;
                break;
            }
            else if (in_gap)
            {
                log_message("Block read successfully");
                in_gap = false;
            }
        }

        close(fa_sniffer);

        /* Pause before retrying.  Ideally should poll sniffer card for
         * active network here. */
        sleep(1);
    }
    return NULL;
}



bool initialise_sniffer(struct buffer *buffer, const char * device_name)
{
    fa_block_buffer = buffer;
    fa_sniffer_device = device_name;
    return TEST_0(pthread_create(&sniffer_id, NULL,
        device_name == NULL ? dummy_sniffer_thread : sniffer_thread, NULL));
}

void terminate_sniffer(void)
{
    log_message("Waiting for sniffer...");
    pthread_cancel(sniffer_id);     // Ignore complaint if already halted
    ASSERT_0(pthread_join(sniffer_id, NULL));
    log_message("done");
}