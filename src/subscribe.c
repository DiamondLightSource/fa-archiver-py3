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
    struct filter_mask mask;        // List of BPMs to be subscribed
    struct buffer *buffer;          // Source of data (FA or decimated)
    enum send_timestamp send_timestamp; // Timestamp options
    bool want_t0;                   // Set if T0 should be sent
    bool uncork;                    // Set if stream should be uncorked
};


static bool parse_options(const char **string, struct subscribe_parse *parse)
{
    parse->send_timestamp =
        read_char(string, 'T') ?    //        "TE"          "T"             ""
            read_char(string, 'E') ? SEND_EXTENDED : SEND_BASIC : SEND_NOTHING;
    parse->want_t0 = read_char(string, 'Z');
    parse->uncork = read_char(string, 'U');
    if (read_char(string, 'D'))
        parse->buffer = decimated_buffer;
    else
        parse->buffer = fa_block_buffer;
    return
        TEST_NULL_(parse->buffer, "Decimated data not available")  &&
        TEST_OK_(
            parse->send_timestamp != SEND_EXTENDED  ||
            parse->buffer != decimated_buffer,
            "Extended timestamps not available for decimated data");
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


/* Sends header according to selected options. */
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
    int scon, bool want_t0,
    size_t block_size, uint64_t timestamp, const uint32_t *id0)
{
    const struct disk_header *header = get_header();
    /* This duration calculation is why we can't do decimated data with extended
     * timestamps -- just don't have the value to deliver!  Actually, it's not a
     * great match to the data anyway, but that's another problem... */
    uint32_t duration = (uint32_t) (
        header->last_duration / (header->major_sample_count / block_size));
    timestamp -= duration;      // timestamp is after *last* point

    const char *on_error = "Unable to write timestamp block";
    if (want_t0)
    {
        struct extended_timestamp_id0 extended_timestamp = {
            .timestamp = timestamp,
            .duration = duration,
            .id_zero = *id0 };
        return TEST_write_(
            scon, &extended_timestamp, sizeof(extended_timestamp), on_error);
    }
    else
    {
        struct extended_timestamp extended_timestamp = {
            .timestamp = timestamp,
            .duration = duration };
        return TEST_write_(
            scon, &extended_timestamp, sizeof(extended_timestamp), on_error);
    }
}


/* Copies a single FA frame taking the mask into account, returns the number
 * of bytes copied into the target buffer (will be 8*count_mask_bits(mask)).
 * 'from' should point to a completely populated frame, 'to' will contain X,Y
 * pairs in ascending numerical order for bits set in mask. */
static int copy_frame(
    void *to, const void *from,
    const struct filter_mask *mask, unsigned int fa_entry_count)
{
    const int32_t *from_p = from;
    int32_t *to_p = to;
    int copied = 0;
    for (unsigned int i = 0; i < fa_entry_count / 8; i ++)  // 8 bits at a time
    {
        uint8_t m = mask->mask[i];
        for (unsigned int j = 0; j < 8; j ++)
        {
            if ((m >> j) & 1)
            {
                *to_p++ = from_p[0];
                *to_p++ = from_p[1];
                copied += 8;
            }
            from_p += 2;
        }
    }
    return copied;
}


/* Writes the selected number of masked frames to the given file, returning
 * false if writing fails. */
static bool write_frames(
    int file, const struct filter_mask *mask, unsigned int fa_entry_count,
    const void *frame, unsigned int count)
{
    size_t out_frame_size =
        count_mask_bits(mask, fa_entry_count) * FA_ENTRY_SIZE;
    while (count > 0)
    {
        char buffer[WRITE_BUFFER_SIZE];
        size_t buffered = 0;
        while (count > 0  &&  buffered + out_frame_size <= WRITE_BUFFER_SIZE)
        {
            copy_frame(buffer + buffered, frame, mask, fa_entry_count);
            frame = frame + FA_ENTRY_SIZE * fa_entry_count;
            buffered += out_frame_size;
            count -= 1;
        }

        size_t written = 0;
        while (buffered > 0)
        {
            ssize_t wr;
            if (!TEST_IO_(wr = write(file, buffer + written, buffered),
                    "Unable to write frame"))
                return false;
            written  += (size_t) wr;
            buffered -= (size_t) wr;
        }
    }
    return true;
}


static bool send_subscription(
    int scon, struct reader_state *reader, uint64_t timestamp,
    struct subscribe_parse *parse, unsigned int fa_entry_count,
    const void **block)
{
    /* The transmitted block optionally begins with the timestamp and T0 values,
     * in that order, if requested. */
    unsigned int block_size = (unsigned int) (
        reader_block_size(reader) / fa_entry_count / FA_ENTRY_SIZE);
    bool ok =
        send_header(scon, parse, block_size, timestamp, *block)  &&
        IF_(parse->uncork, set_socket_cork(scon, false));

    while (ok)
    {
        ok = FINALLY(
            IF_(parse->send_timestamp == SEND_EXTENDED,
                send_extended_timestamp(
                    scon, parse->want_t0, block_size, timestamp, *block))  &&
            write_frames(
                scon, &parse->mask, fa_entry_count, *block, block_size),

            // Always do this, even if write_frames fails.
            TEST_OK_(release_read_block(reader), "Write underrun to client"));
        if (ok)
            ok = TEST_NULL_(
                *block = get_read_block(reader, &timestamp),
                "Gap in subscribed data");
        else
            *block = NULL;
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
    struct reader_state *reader = open_reader(parse.buffer, false);
    uint64_t timestamp;
    const void *block = get_read_block(reader, &timestamp);
    bool start_ok = TEST_NULL_(block, "No data currently available");
    bool ok = report_socket_error(scon, client_name, start_ok);

    /* Send the requested subscription if all is well. */
    if (start_ok  &&  ok)
        ok = send_subscription(
            scon, reader, timestamp, &parse, fa_entry_count, &block);

    /* Clean up resources.  Rather important to get this right, as this can
     * happen many times. */
    if (block != NULL)
        release_read_block(reader);
    close_reader(reader);

    return ok;
}


void initialise_subscribe(struct buffer *fa_buffer, struct buffer *decimated)
{
    fa_block_buffer = fa_buffer;
    decimated_buffer = decimated;
}
