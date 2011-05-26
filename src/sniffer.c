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


/* Abstraction of sniffer device interface so we can implement debug versions of
 * the sniffer. */
struct sniffer_context
{
    bool (*initialise)(const char *source_name);
    void (*reset)(void);
    bool (*read)(struct fa_row *block, size_t block_size);
};


static void * sniffer_thread(void *context)
{
    struct sniffer_context *sniffer = context;
    const size_t fa_block_size = buffer_block_size(fa_block_buffer);
    bool in_gap = false;    // Only report gap once
    while (true)
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
            bool gap = !sniffer->read(buffer, fa_block_size);

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
        sniffer->reset();
    }
    return NULL;
}



/* Standard sniffer using true sniffer device. */

static const char *fa_sniffer_device;
static int fa_sniffer;

static bool initialise_sniffer_device(const char *device_name)
{
    fa_sniffer_device = device_name;
    return TEST_IO_(
        fa_sniffer = open(fa_sniffer_device, O_RDONLY),
        "Can't open sniffer device %s", fa_sniffer_device);
}

static void reset_sniffer_device(void)
{
    IGNORE(TEST_IO_(
        fa_sniffer = open(fa_sniffer_device, O_RDONLY),
        "Can't open sniffer device %s", fa_sniffer_device));
}

static bool read_sniffer_device(struct fa_row *buffer, size_t block_size)
{
    bool ok = read(fa_sniffer, buffer, block_size) == (ssize_t) block_size;
    if (!ok)
        close(fa_sniffer);
    return ok;
}

struct sniffer_context sniffer_device = {
    .initialise = initialise_sniffer_device,
    .reset = reset_sniffer_device,
    .read = read_sniffer_device,
};


/* Dummy sniffer using replay data. */

static void reset_replay(void) { ASSERT_FAIL(); }

struct sniffer_context sniffer_replay = {
    .initialise = initialise_replay,
    .reset = reset_replay,
    .read = read_replay_block,
};



static struct sniffer_context *sniffer_context;
static pthread_t sniffer_id;

bool initialise_sniffer(
    struct buffer *buffer, const char *device_name, bool replay)
{
    fa_block_buffer = buffer;
    sniffer_context = replay ? &sniffer_replay : &sniffer_device;
    return sniffer_context->initialise(device_name);
}

bool start_sniffer(bool boost_priority)
{
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
            &sniffer_id, &attr, sniffer_thread, sniffer_context),
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
