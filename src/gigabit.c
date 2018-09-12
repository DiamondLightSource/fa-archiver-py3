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

#include "error.h"

#include "fa_sniffer.h"
#include "sniffer.h"
#include "buffer.h"
#include "libera-grouping.h"

#include "gigabit.h"


static int gigabit_socket;
static size_t fa_frame_size;


/* Receives and decodes a single Libera datagram from the gigabit ethernet
 * interface.  */
static bool read_datagram(struct fa_entry *row)
{
    COMPILE_ASSERT(LIBERA_BLOCK_SIZE == 16);

    /* As reading can be quite sparse, zero initialise the row. */
    memset(row, 0, fa_frame_size);

    /* Read a datagram from the socket. */
    struct libera_payload buffer[LIBERAS_PER_DATAGRAM];
    ssize_t bytes_rx;
    bool ok =
        TEST_IO(
            bytes_rx = recvfrom(
                gigabit_socket, buffer, sizeof(buffer), 0, NULL, NULL))  &&
        TEST_OK_(bytes_rx > 0, "Gigabit socket closed");
    if (!ok)
        return false;

    /* Decode the data */
    for (unsigned int i = 0; i < (size_t) bytes_rx / LIBERA_BLOCK_SIZE; i ++)
    {
        struct libera_payload *payload = &buffer[i];
        if (payload->status.valid)
        {
            unsigned int id = payload->status.libera_id;
            row[id].x = payload->x;
            row[id].y = payload->y;
        }
    }
    return true;
}


static bool read_gigabit_block(
    struct fa_row *block, size_t block_size, uint64_t *timestamp)
{
    bool ok = true;
    for (unsigned int i = 0; ok  &&  i < block_size / fa_frame_size; i ++)
    {
        ok = read_datagram(block->row);
        block = (void *) block + fa_frame_size;
    }
    *timestamp = get_timestamp();
    return ok;
}


static bool open_gigabit_socket(void)
{
    struct sockaddr_in skaddr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(2048),
    };
    return
        TEST_IO(gigabit_socket = socket(PF_INET, SOCK_DGRAM, 0))  &&
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
    return FAIL_("Read status not suppported for gigabit");
}


static bool interrupt_gigabit(void)
{
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
        open_gigabit_socket();
    return ok ? &sniffer_gigabit : NULL;
}
