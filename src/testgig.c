/* Simple program for testing gigabit ethernet. */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "error.h"
#include "parse.h"
#define LIBERA_GROUPING 0
#include "libera-grouping.h"


#define NS_S    1000000000      // 1e9 ns per second
#define NS_US   1000            // 1e3 ns per microsecond



/* Advances deadline by 100 us so that we can send packets at 10kHz. */
static void advance_deadline(struct timespec *deadline)
{
    deadline->tv_nsec += 100 * NS_US;    // 100 us
    if (deadline->tv_nsec >= NS_S)
    {
        deadline->tv_sec += 1;
        deadline->tv_nsec -= NS_S;
    }
}


static bool deadline_reached(
    const struct timespec *now,
    const struct timespec *deadline)
{
    return
        now->tv_sec > deadline->tv_sec  ||
        (now->tv_sec == deadline->tv_sec  &&
         now->tv_nsec >= deadline->tv_nsec);
}


static bool wait_for_deadline(const struct timespec *deadline)
{
    struct timespec now;
    return
        TEST_IO(clock_gettime(CLOCK_MONOTONIC, &now))  &&
        IF_(!deadline_reached(&now, deadline),
            TEST_IO(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                deadline, NULL)));
}


static void prepare_payload(
    struct libera_payload payload[], unsigned int bpm_count, unsigned int step)
{
    for (size_t i = 0; i < bpm_count; i ++)
        payload[i] = (struct libera_payload) {
            .sum = 0,
            .x = (int32_t) (i * step),
            .y = (int32_t) (-i * step),
            .counter = (uint16_t) step,
            .status = {
                .lock_status = 1,
                .libera_id = i & LIBERAS_ID_MASK,
                .valid = 1,
            },
        };
}

static bool send_payload(
    int sock, const struct libera_payload payload[],
    unsigned int bpm_count, const struct timespec *deadline)
{
    struct sockaddr_in skaddr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(0x7f000001),   // 127.0.0.1
        .sin_port = htons(2048),
    };
    return
        wait_for_deadline(deadline)  &&
        TEST_IO(sendto(sock, payload, bpm_count * LIBERA_BLOCK_SIZE, 0,
            (struct sockaddr_in *) &skaddr, sizeof(skaddr)));
}


static bool send_sequence(
    int sock, unsigned int bpm_count, unsigned int message_count)
{
    struct timespec deadline;
    bool ok = TEST_IO(clock_gettime(CLOCK_MONOTONIC, &deadline));
    for (unsigned int i = 0;
         ok && (message_count == 0 || i < message_count); i ++)
    {
        struct libera_payload payload[LIBERAS_PER_DATAGRAM];
        prepare_payload(payload, bpm_count, i);
        advance_deadline(&deadline);
        ok = send_payload(sock, payload, bpm_count, &deadline);
    }
    return ok;
}


static bool send_message(unsigned int bpm_count, unsigned int message_count)
{
    int sock;
    return
        TEST_IO(sock = socket(PF_INET, SOCK_DGRAM, 0))  &&
        send_sequence(sock, bpm_count, message_count);
}


struct args {
    unsigned int bpm_count;
    unsigned int message_count;
};


static bool parse_args(int argc, char *argv[], struct args *args)
{
    return
        TEST_OK_(argc > 1, "Must specify number of bpms in message")  &&
        DO_PARSE("bpm count", parse_uint, argv[1], &args->bpm_count)  &&
        TEST_OK_(
            0 < args->bpm_count  &&  args->bpm_count <= LIBERAS_PER_DATAGRAM,
            "Invalid number of bpms")  &&
        IF_(argc > 2,
            DO_PARSE("count", parse_uint, argv[2], &args->message_count));
}


int main(int argc, char *argv[])
{
    COMPILE_ASSERT(LIBERA_BLOCK_SIZE == 16);

    struct args args = {};
    bool ok =
        parse_args(argc, argv, &args)  &&
        send_message(args.bpm_count, args.message_count);
    return ok ? 0 : 1;
}
