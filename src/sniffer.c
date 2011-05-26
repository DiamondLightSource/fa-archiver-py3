/* Code to interface to fa_sniffer device. */

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
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
#include "replay.h"


/* This is where the sniffer data will be written. */
static struct buffer *fa_block_buffer;

struct sniffer_context
{
    bool (*start)(void);
    bool (*read_block)(struct fa_row *block, size_t block_size);
};


static void * sniffer_thread(void *context)
{
    struct sniffer_context *sniffer = context;
    const size_t fa_block_size = buffer_block_size(fa_block_buffer);
    bool in_gap = false;    // Only report gap once
    while (sniffer->start())
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
            bool gap = !sniffer->read_block(buffer, fa_block_size);

            /* Get the time this block was written.  This is close enough to the
             * completion of the FA sniffer read to be a good timestamp for the
             * last frame. */
            struct timespec ts;
            ASSERT_IO(clock_gettime(CLOCK_REALTIME, &ts));
            release_write_block(fa_block_buffer, gap, ts_to_microseconds(&ts));
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

        /* Pause before retrying.  Ideally should poll sniffer card for
         * active network here. */
        sleep(1);
    }
    return NULL;
}



/* Standard sniffer using true sniffer device. */

static const char *fa_sniffer_device;
static int fa_sniffer;

static bool start_sniffer(void)
{
    return TEST_IO_(
        fa_sniffer = open(fa_sniffer_device, O_RDONLY),
        "Can't open sniffer device %s", fa_sniffer_device);
}

static bool read_sniffer_block(struct fa_row *buffer, size_t block_size)
{
    bool ok = read(fa_sniffer, buffer, block_size) == (ssize_t) block_size;
    if (!ok)
        close(fa_sniffer);
    return ok;
}

struct sniffer_context sniffer_device = {
    .start = start_sniffer,
    .read_block = read_sniffer_block,
};


/* Dummy sniffer using replay data. */
static bool start_replay(void) { return true; }

struct sniffer_context sniffer_replay = {
    .start = start_replay,
    .read_block = read_replay_block,
};



static pthread_t sniffer_id;

bool initialise_sniffer(
    struct buffer *buffer, const char * device_name, bool boost_priority)
{
    fa_block_buffer = buffer;
    fa_sniffer_device = device_name;
    pthread_attr_t attr;
    return
        TEST_0(pthread_attr_init(&attr))  &&
        IF_(boost_priority,
            /* If requested boost the thread priority and configure FIFO
             * scheduling to ensure that this thread gets absolute maximum
             * priority. */
            TEST_0(pthread_attr_setinheritsched(
                &attr, PTHREAD_EXPLICIT_SCHED))  &&
            TEST_0(pthread_attr_setschedpolicy(&attr, SCHED_FIFO))  &&
            TEST_0(pthread_attr_setschedparam(
                &attr, &(struct sched_param) { .sched_priority = 1 })))  &&
        TEST_0_(pthread_create(
            &sniffer_id, &attr, sniffer_thread,
            device_name == NULL ? &sniffer_replay : &sniffer_device),
            "Priority boosting requires real time thread support")  &&
        TEST_0(pthread_attr_destroy(&attr));
}

void terminate_sniffer(void)
{
    log_message("Waiting for sniffer...");
    pthread_cancel(sniffer_id);     // Ignore complaint if already halted
    ASSERT_0(pthread_join(sniffer_id, NULL));
    log_message("done");
}
