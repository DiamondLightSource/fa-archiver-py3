/* Implements reading from disk.
 *
 * Copyright (c) 2011 Michael Abbott, Diamond Light Source Ltd.
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
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>

#include "error.h"
#include "fa_sniffer.h"
#include "mask.h"
#include "parse.h"
#include "buffer.h"
#include "locking.h"
#include "disk.h"
#include "transform.h"
#include "disk_writer.h"
#include "socket_server.h"
#include "list.h"
#include "pool.h"

#include "reader.h"

#define K   1024


/* Each connection opens its own file handle on the archive.  This is the
 * archive file. */
static const char *archive_filename;
static unsigned int fa_entry_count;         // Read from header at startup



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Reading from disk: general support. */


struct iter_mask {
    unsigned int count;
    uint16_t index[MAX_FA_ENTRY_COUNT];
};


struct reader {
    /* Reads the requested block from archive into buffer, samples_per_fa_block
     * samples will be returned:
     *  archive         File handle of archive to read
     *  block           Major block to start reading
     *  id              FA id to read
     *  *buffer         Data written here, must be correct size */
    bool (*read_block)(
        int archive, unsigned int block, unsigned int id, void *buffer);
    /* Writes the given lines from a list of buffers to an output buffer:
     *  line_count      Number of samples to be written
     *  field_count     Number of FA ids per sample
     *  read_buffers    Array of buffers, one for each FA id
     *  offset          Starting offset into buffer of first sample to write
     *  data_mask       Mask of decimated fields to write (ignored for FA)
     *  output          Data buffer to be written to */
    void (*write_lines)(
        unsigned int line_count, unsigned int field_count,
        struct read_buffers *read_buffers, unsigned int offset,
        unsigned int data_mask, void *output);
    /* The size of a single output value.  For decimated data the output size
     * depends on the selected data mask, which is of course meaningless for FA
     * data. */
    size_t (*output_size)(unsigned int data_mask);

    unsigned int decimation_log2;       // FA samples per read sample
    unsigned int samples_per_fa_block;  // Samples in a single FA block
};


/* Converts an external mask into indexes into the archive. */
static bool mask_to_archive(
    const struct filter_mask *mask, struct iter_mask *iter)
{
    const struct disk_header *header = get_header();
    unsigned int ix = 0;
    unsigned int n = 0;
    bool ok = true;
    for (unsigned int i = 0; ok  &&  i < fa_entry_count; i ++)
    {
        if (test_mask_bit(mask, i))
        {
            ok = TEST_OK_(test_mask_bit(&header->archive_mask, i),
                "BPM %d not in archive", i);
            iter->index[n] = (uint16_t) ix;
            n += 1;
        }
        if (test_mask_bit(&header->archive_mask, i))
            ix += 1;
    }
    iter->count = n;
    return ok;
}


static unsigned int round_up(uint64_t a, uint64_t b)
{
    return (unsigned int) ((a + b - 1) / b);
}


/* Checks that the run of samples from (ix_start,offset) has no gaps.  Here the
 * start is an index block, but the offset is an offset in data points. */
static bool check_run(
    const struct reader *reader, bool check_id0,
    unsigned int ix_start, unsigned int offset, uint64_t samples)
{
    /* Compute the total number of index blocks that will need to be read. */
    unsigned int blocks = round_up(
        offset + samples, reader->samples_per_fa_block);
    unsigned int blocks_requested = blocks;
    /* Check whether they represent a contiguous data block. */
    return TEST_OK_(!find_gap(check_id0, &ix_start, &blocks),
        "Only %"PRIu64" contiguous samples available",
        (uint64_t) (blocks_requested - blocks) *
            reader->samples_per_fa_block - offset);
}


/* Computes a requested number of samples from an end date. */
static bool compute_end_samples(
    const struct reader *reader,
    uint64_t end, unsigned int start_block, unsigned int start_offset,
    bool all_data, uint64_t *samples)
{
    const struct disk_header *header = get_header();
    unsigned int end_block, end_offset;

    bool ok =
        timestamp_to_end(
            end, all_data, start_block, &end_block, &end_offset)  &&
        TEST_OK(start_block != end_block  ||  start_offset <= end_offset);
    if (ok)
    {
        /* Convert the two block and offset counts into a total FA count. */
        if (end_block < start_block)
            end_block += header->major_block_count;
        uint64_t fa_samples =
            (uint64_t) header->major_sample_count * (end_block - start_block) +
            end_offset - start_offset;

        /* Finally convert FA samples to requested samples. */
        *samples = fa_samples >> reader->decimation_log2;
        ok = TEST_OK_(*samples > 0, "No samples in selected range");
    }
    return ok;
}


/* Given start and an optional end timestamp computes the starting block and
 * first sample offset.  If an end timestamp is given it is used to compute the
 * number of samples.  Both *samples and *offset are in units for the
 * appropriate data to be read. */
static bool compute_start(
    const struct reader *reader,
    uint64_t start, uint64_t end, bool all_data,
    uint64_t *samples, unsigned int *ix_block, unsigned int *offset)
{
    uint64_t available;
    return
        /* Convert requested timestamp into a starting index block and FA offset
         * into that block. */
        timestamp_to_start(start, all_data, &available, ix_block, offset)  &&
        IF_(end != 0,
            TEST_OK_(start < end, "Time range runs backwards")  &&
            compute_end_samples(
                reader, end, *ix_block, *offset, all_data, samples))  &&
        /* Convert offset and available counts into numbers appropriate for our
         * current data type. */
        DO_(available >>= reader->decimation_log2;
            *offset   >>= reader->decimation_log2)  &&
        /* Check the requested data set is valid and available. */
        IF_ELSE(all_data,
            // Truncate to available data if necessary
            IF_(*samples > available, DO_(*samples = available)),
            // Otherwise ensure all requested data available
            TEST_OK_(*samples <= available,
                "Only %"PRIu64" samples of %"PRIu64" requested available",
                available, *samples));
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Timestamp support. */


enum send_timestamp {
    SEND_NOTHING = 0,               // Don't send timestamp with data
    SEND_BASIC,                     // Send timestamp at start of data
    SEND_EXTENDED,                  // Send timestamp with each data block
    SEND_AT_END,                    // Aggregate timestamp data and send at end
};


struct ts_buffer {
    uint32_t count;                 // Number of timestamps actually written
    bool send_id0;                  // Set if id0s is in use
    /* To help the client out even further, we send the timestamps, durations
     * and option id0 values separately. */
    struct write_buffer timestamps;
    struct write_buffer durations;
    struct write_buffer id0s;
};


#define ALLOCATE_TS_BUFFER(buf) \
    struct ts_buffer buf = { \
        .count = 0, \
        .timestamps = { .file = -1 }, \
        .durations  = { .file = -1 }, \
        .id0s       = { .file = -1 }, \
    }


/* When sending timestamps at the end we have to first of all allocate a buffer
 * large enough to accomodate all the timestamps we're going to generate.  As
 * this could in fact end up being a lot of data we use the preallocated buffer
 * pool for this. */
static bool allocate_timestamp_buffer(
    enum send_timestamp send_timestamp, bool send_id0,
    struct ts_buffer *ts_buffer, unsigned int samples_per_block, uint64_t count)
{
    ts_buffer->send_id0 = send_id0;
    if (send_timestamp == SEND_AT_END)
    {
        /* We only need a rough estimate here, so long as we don't
         * underestimate.  Simplest to say one timestamp per complete block plus
         * one each extra for partial blocks at each end. */
        size_t ts_count = 2 + count / samples_per_block;
        unsigned int timestamp_blocks =
            round_up(ts_count, pooled_buffer_size / sizeof(uint64_t));
        unsigned int duration_blocks =
            round_up(ts_count, pooled_buffer_size / sizeof(uint32_t));
        return
            allocate_write_buffer(&ts_buffer->timestamps, timestamp_blocks)  &&
            allocate_write_buffer(&ts_buffer->durations,  duration_blocks)  &&
            IF_(send_id0,
                allocate_write_buffer(&ts_buffer->id0s, duration_blocks));
    }
    else
        return true;
}


/* When sending a timestamp with each major block ("extended timestamps") we
 * start by send a header with block information. */
static bool send_timestamp_header(
    enum send_timestamp send_timestamp, bool send_id0,
    struct write_buffer *buffer, const struct reader *reader,
    unsigned int ix_block, unsigned int offset)
{
    const struct data_index *data_index = read_index(ix_block);
    uint32_t id0 = data_index->id_zero + offset;

    switch (send_timestamp)
    {
        case SEND_NOTHING:
            if (send_id0)
                return BUFFER_ITEM(buffer, id0);
            else
                return true;

        case SEND_BASIC:
        {
            /* For basic timestamps we just send the timestamp of the first
             * sample at the head of the data, possibly followed by id0. */
            uint64_t timestamp =
                data_index->timestamp +
                /* A note on this calculation: both ix_offset and duration both
                 * comfortably fit into 32 bits, so this is a sensible way of
                 * computing the timestamp within the selected block. */
                (uint64_t) offset * data_index->duration /
                    reader->samples_per_fa_block;
            return
                BUFFER_ITEM(buffer, timestamp)  &&
                IF_(send_id0, BUFFER_ITEM(buffer, id0));
        }

        case SEND_EXTENDED:
        case SEND_AT_END:
        {
            /* Extended timestamps are sent for each block.  Start the data with
             * a format description. */
            struct extended_timestamp_header header = {
                .block_size = reader->samples_per_fa_block,
                .offset = offset };
            return BUFFER_ITEM(buffer, header);
        }

        default: ASSERT_FAIL(); // Nonsense.  Will not happen
    }
}


/* For extended timestamps we write the timestamp and duration at the head of
 * each block, or possibly delayed to the end of the transfer. */
static bool send_extended_timestamp(
    enum send_timestamp send_timestamp,
    struct ts_buffer *ts_buffer, struct write_buffer *buffer,
    unsigned int ix_block)
{
    const struct data_index *data_index = read_index(ix_block);
    ts_buffer->count += 1;
    switch (send_timestamp)
    {
        case SEND_EXTENDED:
            return
                BUFFER_ITEM(buffer, data_index->timestamp)  &&
                BUFFER_ITEM(buffer, data_index->duration)  &&
                IF_(ts_buffer->send_id0,
                    BUFFER_ITEM(buffer, data_index->id_zero));
        case SEND_AT_END:
            return
                BUFFER_ITEM(&ts_buffer->timestamps, data_index->timestamp)  &&
                BUFFER_ITEM(&ts_buffer->durations,  data_index->duration)  &&
                IF_(ts_buffer->send_id0,
                    BUFFER_ITEM(&ts_buffer->id0s, data_index->id_zero));
        default:
            return true;
    }
}


/* Writes the complete timestamp buffer. */
static bool write_timestamp_buffer(
    struct ts_buffer *ts_buffer, struct write_buffer *out_buffer)
{
    return
        BUFFER_ITEM(out_buffer, ts_buffer->count)  &&
        write_delayed_buffer(&ts_buffer->timestamps, out_buffer)  &&
        write_delayed_buffer(&ts_buffer->durations, out_buffer)  &&
        IF_(ts_buffer->send_id0,
            write_delayed_buffer(&ts_buffer->id0s, out_buffer));
}


static void release_timestamp_buffer(struct ts_buffer *ts_buffer)
{
    release_write_buffer(&ts_buffer->timestamps);
    release_write_buffer(&ts_buffer->durations);
    if (ts_buffer->send_id0)
        release_write_buffer(&ts_buffer->id0s);
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Data transfer control. */

/* Result of parsing a read command. */
struct read_parse {
    struct filter_mask read_mask;   // List of BPMs to be read
    uint64_t samples;               // Requested number of samples
    uint64_t start;                 // Data start (in microseconds into epoch)
    uint64_t end;                   // Data end (alternative to count)
    const struct reader *reader;    // Interpretation of data source
    unsigned int data_mask;         // Data mask for D and DD data
    bool send_sample_count;         // Send sample count at start
    bool send_all_data;             // Don't bail out if insufficient data
    enum send_timestamp send_timestamp; // Send timestamp configuration
    bool send_id0;                  // Send id0 (with timestamp data)
    bool only_contiguous;           // Only contiguous data acceptable
    bool check_id0;                 // Consider id0 gap as a gap
};


static bool transfer_data(
    const struct read_parse *parse, struct read_buffers *read_buffers,
    int archive, struct write_buffer *out_buffer, struct iter_mask *iter,
    struct ts_buffer *ts_buffer,
    unsigned int ix_block, unsigned int offset, uint64_t count)
{
    const struct reader *reader = parse->reader;
    const struct disk_header *header = get_header();
    size_t line_size_out = iter->count * reader->output_size(parse->data_mask);

    bool ok = true;
    while (ok  &&  count > 0)
    {
        ok = send_extended_timestamp(
            parse->send_timestamp, ts_buffer, out_buffer, ix_block);

        /* Read a single timeframe for each id from the archive.  This is
         * normally a single large disk IO block per BPM id. */
        for (unsigned int i = 0; ok  &&  i < iter->count; i ++)
            ok = reader->read_block(
                archive, ix_block, iter->index[i], read_buffers->buffers[i]);

        /* Transpose the read data into output lines and write out in buffer
         * sized chunks. */
        unsigned int samples_read = reader->samples_per_fa_block;
        while (ok  &&  offset < samples_read  &&  count > 0)
        {
            /* Ensure we get enough workspace to write a least a single line!
             * Alas, can fail if writing fails. */
            size_t buf_length;
            void *line_buffer =
                get_buffer(out_buffer, line_size_out, &buf_length);
            ok = line_buffer != NULL;
            if (!ok)
                break;

            /* Enough lines to fill the write buffer, so long as we don't write
             * more than requested and we don't exhaust the read blocks. */
            unsigned int line_count =
                (unsigned int) (buf_length / line_size_out);
            if (count < line_count)
                line_count = (unsigned int) count;
            if (offset + line_count > samples_read)
                line_count = samples_read - offset;

            reader->write_lines(
                line_count, iter->count,
                read_buffers, offset, parse->data_mask, line_buffer);
            release_buffer(out_buffer, line_count * line_size_out);

            count -= line_count;
            offset += line_count;
        }

        ix_block += 1;
        if (ix_block >= header->major_block_count)
            ix_block = 0;
        offset = 0;
    }

    return ok  &&
        IF_(parse->send_timestamp == SEND_AT_END,
            write_timestamp_buffer(ts_buffer, out_buffer));
}


static bool read_data(
    int scon, const char *client_name, const struct read_parse *parse)
{
    unsigned int ix_block, offset;      // Index of first point to send
    struct iter_mask iter = { 0 };      // List of IDs to read
    int archive = -1;                   // Archive file for reading FA or D data
    uint64_t samples = parse->samples;  // Number of samples to return

    /* Three lots of buffers from the pool: read buffers, write buffer and an
     * optional timestamp buffer. */
    ALLOCATE_READ_BUFFERS(read_buffers);    // Array of buffers, one for each ID
    ALLOCATE_WRITE_BUFFER(out_buffer, scon);  // Buffered writes
    ALLOCATE_TS_BUFFER(ts_buffer);      // For timestamps at end

    bool ok =
        /* Convert timestamps into index block, offset and sample count. */
        compute_start(
            parse->reader, parse->start, parse->end, parse->send_all_data,
            &samples, &ix_block, &offset)  &&
        /* If contiguous data requested ensure there are no gaps. */
        IF_(parse->only_contiguous,
            check_run(parse->reader,
                parse->check_id0, ix_block, offset, samples))  &&
        /* Prepare the iteration mask for efficient data delivery. */
        mask_to_archive(&parse->read_mask, &iter)  &&
        /* Capture all the buffers needed.  This can fail if there are too many
         * readers trying to run at once. */
        lock_buffers(&read_buffers, iter.count)  &&
        allocate_write_buffer(&out_buffer, 1)  &&
        allocate_timestamp_buffer(
            parse->send_timestamp, parse->send_id0, &ts_buffer,
            parse->reader->samples_per_fa_block, samples)  &&
        /* Finally we're ready to go. */
        TEST_IO(archive = open(archive_filename, O_RDONLY));
    bool write_ok = report_socket_error(scon, client_name, ok);

    if (ok  &&  write_ok)
    {
        write_ok =
            IF_(parse->send_sample_count,
                BUFFER_ITEM(&out_buffer, samples))  &&
            send_timestamp_header(
                parse->send_timestamp, parse->send_id0, &out_buffer,
                parse->reader, ix_block, offset)  &&
            transfer_data(
                parse, &read_buffers, archive, &out_buffer,
                &iter, &ts_buffer, ix_block, offset, samples)  &&
            flush_buffer(&out_buffer);
    }

    release_timestamp_buffer(&ts_buffer);
    release_write_buffer(&out_buffer);
    unlock_buffers(&read_buffers);
    if (archive != -1)
        TEST_IO(close(archive));

    return write_ok;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Format specific definitions. */

/* Forward declaration of the three reader definitions. */
static struct reader fa_reader;
static struct reader d_reader;
static struct reader dd_reader;


static bool read_fa_block(
    int archive, unsigned int major_block, unsigned int id, void *block)
{
    const struct disk_header *header = get_header();
    size_t fa_block_size = FA_ENTRY_SIZE * header->major_sample_count;
    off64_t offset = (off64_t) (
        header->major_data_start +
        (uint64_t) header->major_block_size * major_block +
        fa_block_size * id);
    return
        DO_(request_read())  &&
        TEST_IO(lseek(archive, offset, SEEK_SET))  &&
        TEST_read(archive, block, fa_block_size);
}

static bool read_d_block(
    int archive, unsigned int major_block, unsigned int id, void *block)
{
    const struct disk_header *header = get_header();
    size_t fa_block_size = FA_ENTRY_SIZE * header->major_sample_count;
    size_t d_block_size =
        sizeof(struct decimated_data) * header->d_sample_count;
    off64_t offset = (off64_t) (
        header->major_data_start +
        (uint64_t) header->major_block_size * major_block +
        header->archive_mask_count * fa_block_size +
        d_block_size * id);
    return
        DO_(request_read())  &&
        TEST_IO(lseek(archive, offset, SEEK_SET))  &&
        TEST_read(archive, block, d_block_size);
}

static bool read_dd_block(
    int archive, unsigned int major_block, unsigned int id, void *block)
{
    const struct disk_header *header = get_header();
    size_t offset =
        header->dd_total_count * id +
        dd_reader.samples_per_fa_block * major_block;

    const struct decimated_data *dd_area = get_dd_area();
    memcpy(block, dd_area + offset,
        sizeof(struct decimated_data) * dd_reader.samples_per_fa_block);
    return true;
}


static void fa_write_lines(
    unsigned int line_count, unsigned int field_count,
    struct read_buffers *read_buffers, unsigned int offset,
    unsigned int data_mask, void *p)
{
    struct fa_entry *output = (struct fa_entry *) p;
    for (unsigned int l = 0; l < line_count; l ++)
    {
        for (unsigned int i = 0; i < field_count; i ++)
            *output++ = ((struct fa_entry *) read_buffers->buffers[i])[offset];
        offset += 1;
    }
}

static void d_write_lines(
    unsigned int line_count, unsigned int field_count,
    struct read_buffers *read_buffers, unsigned int offset,
    unsigned int data_mask, void *p)
{
    struct fa_entry *output = (struct fa_entry *) p;
    for (unsigned int l = 0; l < line_count; l ++)
    {
        for (unsigned int i = 0; i < field_count; i ++)
        {
            /* Each input buffer is an array of decimated_data structures which
             * we index by offset, but we then cast this to an array of fa_entry
             * structures to allow the individual fields to be selected by the
             * data_mask. */
            struct fa_entry *input = (struct fa_entry *)
                &((struct decimated_data *) read_buffers->buffers[i])[offset];
            if (data_mask & 1)  *output++ = input[0];
            if (data_mask & 2)  *output++ = input[1];
            if (data_mask & 4)  *output++ = input[2];
            if (data_mask & 8)  *output++ = input[3];
        }
        offset += 1;
    }
}


static size_t fa_output_size(unsigned int data_mask)
{
    return FA_ENTRY_SIZE;
}

/* For decimated data the data mask selects individual data fields that are
 * going to be emitted, so we count them here. */
static size_t d_output_size(unsigned int data_mask)
{
    unsigned int count =
        ((data_mask >> 0) & 1) + ((data_mask >> 1) & 1) +
        ((data_mask >> 2) & 1) + ((data_mask >> 3) & 1);
    return count * FA_ENTRY_SIZE;
}


static struct reader fa_reader = {
    .read_block = read_fa_block,
    .write_lines = fa_write_lines,
    .output_size = fa_output_size,
    .decimation_log2 = 0,
};

static struct reader d_reader = {
    .read_block = read_d_block,
    .write_lines = d_write_lines,
    .output_size = d_output_size,
};

static struct reader dd_reader = {
    .read_block = read_dd_block,
    .write_lines = d_write_lines,
    .output_size = d_output_size,
};



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Read request parsing. */

/* A read request specifies the following:
 *
 *  Data source: normal FA data, decimated or double decimated data.
 *  For decimated data, a field mask is specifed
 *  Mask of BPM ids to be retrieved
 *  Data start point as a timestamp
 *
 * The syntax is very simple (no spaces allowed):
 *
 *  read-request = "R" source "M" filter-mask start end options
 *  source = "F" | "D" [ "D" ] [ "F" data-mask ]
 *  data-mask = integer
 *  start = time-or-seconds
 *  end = "N" samples | "E" time-or-seconds
 *  time-or-seconds = "T" date-time | "S" seconds [ "." nanoseconds ]
 *  samples = integer
 *  options = [ "N" ] [ "A" ] [ "T" [ "E" | "A" ] ] [ "Z" ] [ "C" [ "Z" ] ]
 *
 * The options can only appear in the order given and have the following
 * meanings:
 *
 *  N   Send sample count as part of data stream
 *  A   Send all data there is, even if samples is too large or starts too early
 *  T   Send timestamp at head of dataset
 *  TE  Send extended timestamp at head of dataset and with each major block
 *  TA  Send extended timestamp at head and tail of dataset
 *  Z   Send id0 with data at the same time as the timestamp (or at start)
 *  C   Ensure no gaps in selected dataset, fail if any
 *  CZ  Include gaps generated by id0 in gap check
 */

/* source = "F" | "D" [ "D" ] [ "F" data-mask ] . */
static bool parse_source(const char **string, struct read_parse *parse)
{
    if (read_char(string, 'F'))
    {
        parse->reader = &fa_reader;
        return true;
    }
    else if (read_char(string, 'D'))
    {
        parse->data_mask = 15;      // Default to all fields if no mask
        if (read_char(string, 'D'))
            parse->reader = &dd_reader;
        else
            parse->reader = &d_reader;
        if (read_char(string, 'F'))
            return
                parse_uint(string, &parse->data_mask)  &&
                TEST_OK_(0 < parse->data_mask  &&  parse->data_mask <= 15,
                    "Invalid decimated data fields: %x", parse->data_mask);
        else
            return true;
    }
    else
        return FAIL_("Invalid source specification");
}


/* time-or-seconds = "T" date-time | "S" seconds [ "." nanoseconds ] . */
static bool parse_time_or_seconds(const char **string, uint64_t *microseconds)
{
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 0 };
    bool ok;
    if (read_char(string, 'T'))
        ok = parse_datetime(string, &ts);
    else if (read_char(string, 'S'))
        ok = parse_seconds(string, &ts);
    else
        ok = FAIL_("Expected T or S for timestamp");
    *microseconds = ts_to_microseconds(&ts);
    return ok  &&  TEST_OK_(ts.tv_sec > 0, "Timestamp ridiculously early");
}


/* end = "N" samples | "E" time-or-seconds . */
static bool parse_end(const char **string, uint64_t *end, uint64_t *samples)
{
    *end = 0;
    *samples = 0;
    if (read_char(string, 'N'))
        return
            parse_uint64(string, samples)  &&
            TEST_OK_(*samples > 0, "No samples requested");
    else if (read_char(string, 'E'))
        return parse_time_or_seconds(string, end);
    else
        return FAIL_("Expected count or end time");
}


/* options = [ "N" ] [ "A" ] [ "T" [ "E" | "A" ] ] [ "C" ] [ "Z" ] . */
static bool parse_options(const char **string, struct read_parse *parse)
{
    parse->send_sample_count = read_char(string, 'N');
    parse->send_all_data     = read_char(string, 'A');
    parse->send_timestamp =
        read_char(string, 'T') ?
            read_char(string, 'E') ? SEND_EXTENDED :    // TE
            read_char(string, 'A') ? SEND_AT_END :      // TA
        SEND_BASIC :                                    // T
        SEND_NOTHING;
    parse->send_id0          = read_char(string, 'Z');
    parse->only_contiguous   = read_char(string, 'C');
    parse->check_id0 = parse->only_contiguous && read_char(string, 'Z');
    return true;
}


/* read-request = "R" source "M" filter-mask start end options . */
static bool parse_read_request(const char **string, struct read_parse *parse)
{
    return
        parse_char(string, 'R')  &&
        parse_source(string, parse)  &&
        parse_char(string, 'M')  &&
        parse_mask(string, fa_entry_count, &parse->read_mask)  &&
        parse_time_or_seconds(string, &parse->start)  &&
        parse_end(string, &parse->end, &parse->samples)  &&
        parse_options(string, parse);
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Read processing. */


/* Convert timestamp into block index. */
bool process_read(int scon, const char *client_name, const char *buf)
{
    struct read_parse parse;
    push_error_handling();      // Popped by report_socket_error()
    if (DO_PARSE("read request", parse_read_request, buf, &parse))
        return read_data(scon, client_name, &parse);
    else
        return report_socket_error(scon, client_name, false);
}


bool initialise_reader(const char *archive)
{
    const struct disk_header *header = get_header();

    archive_filename = archive;
    fa_entry_count = header->fa_entry_count;

    /* Initialise dynamic part of reader structures. */
    fa_reader.samples_per_fa_block  = header->major_sample_count;

    d_reader.decimation_log2        = header->first_decimation_log2;
    d_reader.samples_per_fa_block   = header->d_sample_count;

    dd_reader.decimation_log2 =
        header->first_decimation_log2 + header->second_decimation_log2;
    dd_reader.samples_per_fa_block  = header->dd_sample_count;

    /* Make the buffer size large enough for a complete FA major block for one
     * BPM id, allocate enough buffers to allow one user to capture a complete
     * set of ids. */
    initialise_buffer_pool(
        FA_ENTRY_SIZE * header->major_sample_count, fa_entry_count);
    return true;
}
