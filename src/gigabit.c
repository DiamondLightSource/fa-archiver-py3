/* Interface for data capture from Libera Gigabit Ethernet interface.
 *
 * Copyright (c) 2011 Eugene Tan, Australian Synchrotron,
 *      Michael Abbott, Diamond Light Source Ltd.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "error.h"

#include "fa_sniffer.h"
#include "sniffer.h"
#include "buffer.h"
#include "libera-grouping.h"

#include "gigabit.h"


/* This determines how much buffer space we put aside for gigabit ethernet
 * capture.  At up to 4096 bytes per message (up to 256 frames, 16 bytes per BPM
 * frame), a buffer count of 256 reserves about 1MB of buffer which is fine. */
#define BUFFER_COUNT    256

/* Timeout while waiting for gigabit ethernet. */
#define TIMEOUT_SECS    0
#define TIMEOUT_USECS   100000          // 100 ms
#define TIMEOUT_NSECS   (1000 * TIMEOUT_USECS)


static int gigabit_socket;
static size_t fa_frame_size;

static struct libera_payload payload_buffer[BUFFER_COUNT][LIBERAS_PER_DATAGRAM];
static struct mmsghdr mmsghdr[BUFFER_COUNT];
static struct iovec iovec[BUFFER_COUNT];


/* In preparation for using recvmmsg we need to prepare a mmsghdr array together
 * with the associated buffers. */
static bool prepare_gigabit_buffers(void)
{
    for (int i = 0; i < BUFFER_COUNT; i ++)
    {
        mmsghdr[i] = (struct mmsghdr) {
            .msg_hdr = (struct msghdr) {
                .msg_iov = &iovec[i],
                .msg_iovlen = 1,
            },
        };
        iovec[i] = (struct iovec) {
            .iov_base = &payload_buffer[i],
            .iov_len = sizeof(payload_buffer[i]),
        };
    }
    return true;
}


static void decode_frame(
    const struct libera_payload buffer[], size_t bytes_rx,
    struct fa_row *row)
{
    /* As the data can be quite sparse, zero initialise the row. */
    memset(row, 0, fa_frame_size);

    /* Decode the data */
    for (unsigned int i = 0; i < bytes_rx / LIBERA_BLOCK_SIZE; i ++)
    {
        const struct libera_payload *payload = &buffer[i];
        if (payload->status.valid)
        {
            unsigned int id = payload->status.libera_id;
            row->row[id].x = payload->x;
            row->row[id].y = payload->y;
        }
    }
}


static void decode_frames(
    struct mmsghdr message[], int count, struct fa_row block[])
{
    for (int i = 0; i < count; i ++)
    {
        decode_frame(payload_buffer[i], message[i].msg_len, block);
        block = (void *) block + fa_frame_size;
    }
}


static bool read_gigabit_block(
    struct fa_row block[], size_t block_size, uint64_t *timestamp)
{
    bool ok = true;
    unsigned int frames = (unsigned int) (block_size / fa_frame_size);
    while (ok  &&  frames > 0)
    {
        unsigned int to_read = frames <= BUFFER_COUNT ? frames : BUFFER_COUNT;
        struct timespec timeout = {
            .tv_sec = TIMEOUT_SECS,
            .tv_nsec = TIMEOUT_NSECS,
        };
        int frames_rx = recvmmsg(gigabit_socket, mmsghdr, to_read, 0, &timeout);
        *timestamp = get_timestamp();

        if (frames_rx > 0)
        {
            decode_frames(mmsghdr, frames_rx, block);
            frames -= (unsigned int) frames_rx;
            block = (void *) block + (size_t) frames_rx * fa_frame_size;
        }
        else if (frames_rx == -1  &&  errno == EAGAIN)
            /* Fail silently on timeout. */
            ok = false;
        else
            /* Log unexpected error. */
            ok = TEST_IO(-1);
    }
    return ok;
}


static bool open_gigabit_socket(void)
{
    struct sockaddr_in skaddr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(2048),
    };
    struct timeval rx_timeout = {
        .tv_sec = TIMEOUT_SECS,
        .tv_usec = TIMEOUT_USECS,
    };
    return
        TEST_IO(gigabit_socket = socket(PF_INET, SOCK_DGRAM, 0))  &&
        TEST_IO(setsockopt(
            gigabit_socket, SOL_SOCKET, SO_RCVTIMEO,
            &rx_timeout, sizeof(rx_timeout)))  &&
        TEST_IO(bind(
            gigabit_socket, (struct sockaddr *) &skaddr, sizeof(skaddr)));
}


static bool reset_gigabit(void)
{
    return
        TEST_IO(close(gigabit_socket))  &&         // Close the connection
        open_gigabit_socket();
}


static bool read_gigabit_status(struct fa_status *status)
{
    errno = 0;
    return FAIL_("Read status not suppported for gigabit");
}


static bool interrupt_gigabit(void)
{
    errno = 0;
    return FAIL_("Interrupt not suppported for gigabit");
}


static const struct sniffer_context sniffer_gigabit = {
    .reset = reset_gigabit,
    .read = read_gigabit_block,
    .status = read_gigabit_status,
    .interrupt = interrupt_gigabit,
};


const struct sniffer_context *initialise_gigabit(unsigned int fa_entry_count)
{
    fa_frame_size = fa_entry_count * FA_ENTRY_SIZE;
    bool ok =
        TEST_OK_(fa_entry_count >= LIBERAS_PER_DATAGRAM,
            "FA capture count too small")  &&
        prepare_gigabit_buffers()  &&
        open_gigabit_socket();
    return ok ? &sniffer_gigabit : NULL;
}
