/* Subscription to live FA data stream
 *
 * Copyright (c) 2012 Michael Abbott, Diamond Light Source Ltd.
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

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <inttypes.h>
#include <string.h>

#include "error.h"
#include "fa_sniffer.h"
#include "mask.h"
#include "buffer.h"
#include "parse.h"
#include "socket_server.h"
#include "reader.h"
#include "disk.h"
#include "transform.h"
#include "decimate.h"

#include "subscribe.h"


/* Block buffer for full resolution FA data. */
static struct buffer *fa_block_buffer;
/* Block buffer for decimated FA data. */
static struct buffer *decimated_buffer;


#define WRITE_BUFFER_SIZE       (1 << 16)



/* Same options as for reader. */
enum send_timestamp {
    SEND_NOTHING = 0,               // Don't send timestamp with data
    SEND_BASIC,                     // Send timestamp at start of data
    SEND_EXTENDED                   // Send timestamp with each data block
};

/* Result of parsing a subscribe command. */
struct subscribe_parse {
    struct filter_mask mask;        // List of FA ids to be subscribed
    enum send_timestamp send_timestamp; // Timestamp options
    bool want_t0;                   // Set if T0 should be sent
    bool uncork;                    // Set if stream should be uncorked
    bool decimated;                 // Source of data (FA or decimated)
};


static bool parse_options(const char **string, struct subscribe_parse *parse)
{
    parse->send_timestamp =
        read_char(string, 'T') ?    //        "TE"          "T"             ""
            read_char(string, 'E') ? SEND_EXTENDED : SEND_BASIC : SEND_NOTHING;
    parse->want_t0   = read_char(string, 'Z');
    parse->uncork    = read_char(string, 'U');
    parse->decimated = read_char(string, 'D');
    return
        TEST_OK_(!parse->decimated  ||  decimated_buffer != NULL,
            "Decimated data not available");
}

/* A subscribe request is a filter mask followed by options:
 *
 *  subscription = "S" filter-mask options
 *  options = [ "T" [ "E" ]] [ "Z" ] [ "U" ] [ "D" ]
 *
 * The options have the following meanings:
 *
 *  T   Start subscription stream with timestamp
 *  TE  Send extended timestamps
 *  Z   Start subscription stream with t0
 *  U   Uncork data stream
 *  D   Want decimated data stream
 *
 * If TZ is specified then the timestamp is sent first before T0.
 * If TEZ is specified then T0 is sent with each timestamp. */
static bool parse_subscription(
    const char **string, unsigned int fa_entry_count,
    struct subscribe_parse *parse)
{
    return
        parse_char(string, 'S')  &&
        parse_mask(string, fa_entry_count, &parse->mask)  &&
        parse_options(string, parse);
}


/* Sends header according to selected options.  The transmitted data optionally
 * begins with the timestamp and T0 values, in that order, if requested. */
static bool send_header(
    int scon, struct subscribe_parse *parse,
    size_t block_size, uint64_t timestamp, const uint32_t *id0)
{
    if (parse->send_timestamp == SEND_EXTENDED)
    {
        struct extended_timestamp_header header = {
            .block_size = (uint32_t) block_size,
            .offset = 0 };
        return TEST_write(scon, &header, sizeof(header));
    }
    else
        return
            IF_(parse->send_timestamp == SEND_BASIC,
                TEST_write(scon, &timestamp, sizeof(uint64_t)))  &&
            IF_(parse->want_t0,
                TEST_write(scon, id0, sizeof(uint32_t)));
}


static bool send_extended_timestamp(
    int scon, bool want_t0, bool decimated,
    size_t block_size, uint64_t timestamp, uint32_t id0)
{
    const struct disk_header *header = get_header();

    /* Compute an estimate of the duration of this block. */
    unsigned int decimation = decimated ? get_decimation_factor() : 1;
    uint32_t duration = (uint32_t) (
        block_size * decimation * header->last_duration /
        header->major_sample_count);
    timestamp -= duration;      // timestamp is after *last* point

#define TS_ERROR "Unable to write timestamp block"
    if (want_t0)
    {
        struct extended_timestamp_id0 extended_timestamp = {
            .timestamp = timestamp,
            .duration = duration,
            .id_zero = id0 };
        return TEST_write_(
            scon, &extended_timestamp, sizeof(extended_timestamp), TS_ERROR);
    }
    else
    {
        struct extended_timestamp extended_timestamp = {
            .timestamp = timestamp,
            .duration = duration };
        return TEST_write_(
            scon, &extended_timestamp, sizeof(extended_timestamp), TS_ERROR);
    }
}


/* Copies a single FA frame taking the mask into account.
 * 'from' should point to a completely populated frame, 'to' will contain X,Y
 * pairs in ascending numerical order for bits set in mask. */
static void copy_frame(
    struct fa_entry *to, const struct fa_entry *from,
    const struct filter_mask *mask, unsigned int fa_entry_count)
{
    for (unsigned int i = 0; i < fa_entry_count / 8; i ++)  // 8 bits at a time
    {
        uint8_t m = mask->mask[i];
        for (unsigned int j = 0; j < 8; j ++)
        {
            if ((m >> j) & 1)
                *to++ = *from;
            from += 1;
        }
    }
}


/* Takes copy of masked frames to buffer. */
static void copy_frames(
    void *buffer, const void *block,
    const struct filter_mask *mask, unsigned int fa_entry_count,
    unsigned int count)
{
    size_t out_frame_size =
        count_mask_bits(mask, fa_entry_count) * FA_ENTRY_SIZE;
    size_t in_frame_size = fa_entry_count * FA_ENTRY_SIZE;

    for (unsigned int i = 0; i < count; i ++)
    {
        copy_frame(buffer, block, mask, fa_entry_count);
        buffer += out_frame_size;
        block += in_frame_size;
    }
}


/* Sends data for subscription until something fails, typically either the data
 * source is interrupted or the client disconnects. */
static bool send_subscription(
    int scon, struct reader_state *reader,
    struct subscribe_parse *parse, unsigned int fa_entry_count,
    const void *block, uint64_t timestamp)
{
    unsigned int block_size = (unsigned int) (
        reader_block_size(reader) / fa_entry_count / FA_ENTRY_SIZE);
    unsigned int id_count = count_mask_bits(&parse->mask, fa_entry_count);
    size_t buffer_size = block_size * FA_ENTRY_SIZE * id_count;

    bool ok =
        send_header(scon, parse, block_size, timestamp, block)  &&
        IF_(parse->uncork, set_socket_cork(scon, false));

    while (ok)
    {
        /* Grab a copy of the data in the buffer. */
        char buffer[buffer_size];
        copy_frames(buffer, block, &parse->mask, fa_entry_count, block_size);
        uint32_t id0 = *(const uint32_t *) block;

        ok =
            /* See if the data is clean, or if we've underrun. */
            TEST_OK_(release_read_block(reader), "Write underrun to client")  &&
            /* Write the data if it's clean. */
            IF_(parse->send_timestamp == SEND_EXTENDED,
                send_extended_timestamp(
                    scon, parse->want_t0, parse->decimated,
                    block_size, timestamp, id0))  &&
            TEST_write_(scon, buffer, buffer_size, "Unable to write frame")  &&
            /* Get the next block. */
            TEST_NULL_(
                block = get_read_block(reader, &timestamp),
                "Gap in subscribed data");
    }
    return ok;
}


/* A subscription is a command of the form S<mask> where <mask> is a mask
 * specification as described in mask.h.  The default mask is empty. */
bool process_subscribe(int scon, const char *client_name, const char *buf)
{
    unsigned int fa_entry_count = get_header()->fa_entry_count;

    push_error_handling();

    /* Parse the incoming request. */
    struct subscribe_parse parse;
    if (!DO_PARSE("subscription",
            parse_subscription, buf, fa_entry_count, &parse))
        return report_socket_error(scon, client_name, false);

    /* See if we can start the subscription, report the final status to the
     * caller. */
    struct reader_state *reader = open_reader(
        parse.decimated ? decimated_buffer : fa_block_buffer, false);
    uint64_t timestamp;
    const void *block = get_read_block(reader, &timestamp);
    bool start_ok = TEST_NULL_(block, "No data currently available");
    bool ok = report_socket_error(scon, client_name, start_ok);

    /* Send the requested subscription if all is well. */
    if (start_ok  &&  ok)
        ok = send_subscription(
            scon, reader, &parse, fa_entry_count, block, timestamp);

    close_reader(reader);

    return ok;
}


void initialise_subscribe(struct buffer *fa_buffer, struct buffer *decimated)
{
    fa_block_buffer = fa_buffer;
    decimated_buffer = decimated;
}
