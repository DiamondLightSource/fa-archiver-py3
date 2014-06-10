/* Data transposition.
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
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <sys/mman.h>

#include "error.h"
#include "fa_sniffer.h"
#include "mask.h"
#include "buffer.h"
#include "disk_writer.h"
#include "locking.h"
#include "disk.h"

#include "transform.h"


/* Allow up to 1ms delta before reporting a data capture gap. */
#define MAX_DELTA_T     1000

/* We skip this many old index blocks that are still within range.  This is a
 * simple heuristic to avoid early blocks being overwritten as we're reading
 * them. */
#define INDEX_SKIP      2


/* Archiver header with core parameter. */
static struct disk_header *header;
/* Archiver index. */
static struct data_index *data_index;
/* Area to write DD data. */
static struct decimated_data *dd_area;

/* EVR events are handled completely differently, if present.  Only active if
 * positive value assigned. */
static unsigned int events_fa_id;           // Input id or -1
static unsigned int events_fa_id_output;    // Output id or -1

/* Numbers of normal and decimated samples in a single input block. */
static unsigned int input_frame_count;
static unsigned int input_decimation_count;

/* This lock guards access to header->current_major_block, or to be precise,
 * enforces the invariant described here.  The transform thread has full
 * unconstrained access to this variable, but only updates it under this lock.
 * All major blocks other than current_major_block are valid for reading from
 * disk, the current block is either being worked on or being written to disk.
 * The request_read() function ensures that the previously current block is
 * written and therefore is available. */
DECLARE_LOCKING(transform_lock);

static size_t page_size;    // 4096



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Buffered IO support. */

/* Double-buffered block IO. */

static void *buffers[2];           // Two major buffers to receive data
static unsigned int current_buffer; // Index of buffer currently receiving data
static unsigned int fa_offset;     // Current sample count into current block
static unsigned int d_offset;      // Current decimated sample count


static inline struct fa_entry *fa_block(unsigned int id)
{
    return buffers[current_buffer] + fa_data_offset(header, fa_offset, id);
}


static inline struct decimated_data *d_block(unsigned int id)
{
    return buffers[current_buffer] + d_data_offset(header, d_offset, id);
}


/* Advances the offset pointer within an minor block by the number of bytes
 * written, returns true iff the block is now full. */
static bool advance_block(void)
{
    fa_offset += input_frame_count;
    d_offset += input_frame_count >> header->first_decimation_log2;
    return fa_offset >= header->major_sample_count;
}


/* Called if the block is to be discarded. */
static void reset_block(void)
{
    fa_offset = 0;
    d_offset = 0;
}


/* Writes the currently written major block to disk at the current offset. */
static void write_major_block(void)
{
    off64_t offset = (off64_t) header->major_data_start +
        (off64_t) header->current_major_block * header->major_block_size;
    schedule_write(offset, buffers[current_buffer], header->major_block_size);

    current_buffer = 1 - current_buffer;
    reset_block();
}


/* Initialises IO buffers for the given minor block size. */
static void initialise_io_buffer(void)
{
    for (unsigned int i = 0; i < 2; i ++)
        buffers[i] = valloc(header->major_block_size);

    current_buffer = 0;
    fa_offset = 0;
    d_offset = 0;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Block transpose. */

/* To make reading of individual BPMs more efficient (the usual usage) we
 * transpose frames into individual BPMs until we've assembled a complete
 * collection of disk blocks (determined by output_block_size). */


static void transpose_column(
    const struct fa_entry *input, struct fa_entry *output)
{
    for (unsigned int i = input_frame_count; i > 0; i--)
    {
        *output ++ = *input;
        input += header->fa_entry_count;
    }
}


/* Processes a single input block of FA sniffer frames.  Each BPM is written to
 * its own output block.  True is returned iff the transposed buffer set is full
 * and is ready to be written out. */
static void transpose_block(const void *read_block)
{
    /* For the moment forget about being too clever about the impact of
     * transposing data on the cache.  We copy one column at a time. */
    unsigned int written = 0;
    for (unsigned int id = 0; id < header->fa_entry_count; id ++)
    {
        if (test_mask_bit(&header->archive_mask, id))
        {
            transpose_column(
                read_block + FA_ENTRY_SIZE * id, fa_block(written));
            written += 1;
        }
    }
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Support for variance calculation. */

/* To avoid overflow and to support the incremental calculation of variance we
 * need to use a long accumulator.  Unfortunately on 32 bit systems we have no
 * intrinsic support for 128 bit integers, so we need some conditional
 * compilation here.
 *    The only operations we need are accumulation (of 64 and 128 bit
 * intermediates) and shifting out the result. */

#ifdef __i386__
/* Only have 64 bit integers, need to emulate the 128 bit accumulator. */

struct uint128 { uint64_t low; uint64_t high; };
typedef struct uint128 uint128_t;

/* It's really rather annoying, there doesn't seem to be any good way other than
 * resorting to the assembler below to efficiently compute with 128 bit numbers.
 * The principal problem is that the carry is inaccessible.  The alternative
 * trick of writing:
 *      acc->low += val; if (acc->low < val) acc->high += 1;
 * generates horrible code.
 *
 * Some notes on the assembler constraints, because the documentation can be a
 * bit opaque and some of the interactions are quite subtle:
 *
 *  1.  The form of the asm statement is
 *          __asm__(<code> : <outputs> : <inputs> : <effects>)
 *  2.  We must at least specify "m"(*acc) otherwise the code can end up being
 *      discarded (as having no significant side effects).
 *  3.  The "=&r"(t) output assigns a temporary register.  The & ensures that
 *      this register doesn't overlap with any of the input registers.
 *  4.  The register modifier = is used for an output which is written without
 *      being read, + is used for an output which is also read. */

static void accum128_64(uint128_t *acc, uint64_t val)
{
    __asm__(
        "addl   %[vall], 0(%[acc])" "\n\t"
        "adcl   %[valh], 4(%[acc])" "\n\t"
        "adcl   $0, 8(%[acc])" "\n\t"
        "adcl   $0, 12(%[acc])"
        :
        : [acc] "r" (acc), "m" (*acc),
          [vall] "r" ((uint32_t) val), [valh] "r" ((uint32_t) (val >> 32))
        : "cc" );
}

static void accum128_128(uint128_t *acc, const uint128_t *val)
{
    int t;
    __asm__(
        "movl   0(%[val]), %[t]" "\n\t"
        "addl   %[t], 0(%[acc])" "\n\t"
        "movl   4(%[val]), %[t]" "\n\t"
        "adcl   %[t], 4(%[acc])" "\n\t"
        "movl   8(%[val]), %[t]" "\n\t"
        "adcl   %[t], 8(%[acc])" "\n\t"
        "movl   12(%[val]), %[t]" "\n\t"
        "adcl   %[t], 12(%[acc])"
        : [t] "=&r" (t), "+m" (*acc)
        : [acc] "r" (acc), [val] "r" (val), "m" (*val)
        : "cc" );
}

static uint64_t sr128(uint128_t *acc, unsigned int shift)
{
    return (acc->low >> shift) | (acc->high << (64 - shift));
}

#else
/* Assume built-in 128 bit integers. */

typedef __uint128_t uint128_t;

static void accum128_64(uint128_t *acc, uint64_t val)
{
    *acc += val;
}

static void accum128_128(uint128_t *acc, const uint128_t *val)
{
    *acc += *val;
}

static uint64_t sr128(uint128_t *acc, unsigned int shift)
{
    return (uint64_t) (*acc >> shift);
}

#endif


/* The calculation of variance is really rather delicate, as it is enormously
 * susceptible to numerical problems.  The "proper" way to compute variance is
 * using the formula
 *      var = SUM((x[i] - m)^2) / N   where  m = mean(x) = SUM(x[i]) / N  .
 * This approach isn't so great when dealing with a stream of data, which we
 * have in the case of double decimation, as we need to pass over the dataset
 * twice.  The alternative calulcation is:
 *      var = SUM(x[i]^2) / N - m^2  ,
 * but this is *very* demanding on the intermediate values, particularly if the
 * result is to be accurate when m is large.  In this application x[i] is 32
 * bits, N maybe up to 16 bits, and so we need around 80 bits for the sum, hence
 * the use of 128 bits for the accumulator. */

static int32_t compute_std(uint128_t *acc, int64_t sum, unsigned int shift)
{
    /* It's sufficiently accurate and actually faster to change over to floating
     * point arithmetic at this point. */
    double mean = (double) sum / (double) (1 << shift);
    double var  = (double) sr128(acc, shift) - mean * mean;
    /* Note that rounding errors still allow var in the range -1..0, so need to
     * truncate these to zero. */
    return var > 0 ? (int32_t) sqrt(var) : 0;
}


/* Accumulator for generating decimated data. */
struct fa_accum {
    int32_t minx, maxx, miny, maxy;
    int64_t sumx, sumy;
    uint128_t sum_sq_x, sum_sq_y;
};

static void initialise_accum(struct fa_accum *acc)
{
    memset(acc, 0, sizeof(struct fa_accum));
    acc->minx = INT32_MAX;
    acc->maxx = INT32_MIN;
    acc->miny = INT32_MAX;
    acc->maxy = INT32_MIN;
}

static void accum_xy(struct fa_accum *acc, const struct fa_entry *input)
{
    int32_t x = input->x;
    int32_t y = input->y;
    if (x < acc->minx)   acc->minx = x;
    if (acc->maxx < x)   acc->maxx = x;
    if (y < acc->miny)   acc->miny = y;
    if (acc->maxy < y)   acc->maxy = y;
    acc->sumx += x;
    acc->sumy += y;
    accum128_64(&acc->sum_sq_x, (uint64_t) ((int64_t) x * x));
    accum128_64(&acc->sum_sq_y, (uint64_t) ((int64_t) y * y));
}

static void accum_accum(struct fa_accum *result, const struct fa_accum *input)
{
    if (input->minx < result->minx)   result->minx = input->minx;
    if (result->maxx < input->maxx)   result->maxx = input->maxx;
    if (input->miny < result->miny)   result->miny = input->miny;
    if (result->maxy < input->maxy)   result->maxy = input->maxy;
    result->sumx += input->sumx;
    result->sumy += input->sumy;
    accum128_128(&result->sum_sq_x, &input->sum_sq_x);
    accum128_128(&result->sum_sq_y, &input->sum_sq_y);
}

static void compute_result(
    struct fa_accum *acc, unsigned int shift, struct decimated_data *result)
{
    result->min.x = acc->minx;
    result->max.x = acc->maxx;
    result->min.y = acc->miny;
    result->max.y = acc->maxy;
    result->mean.x = (int32_t) (acc->sumx >> shift);
    result->mean.y = (int32_t) (acc->sumy >> shift);
    result->std.x = compute_std(&acc->sum_sq_x, acc->sumx, shift);
    result->std.y = compute_std(&acc->sum_sq_y, acc->sumy, shift);
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Event set decimation. */

/* For an (x,y) pair representing a bit mask of events we use a different
 * approach to decimation.  For the moment we'll just compute the bit-wise or of
 * all the bits. */

/* Converts a column of event codes into an aggregated event code.  For the
 * moment all we do is accumulate into the mean. */
static void decimate_events(
    const struct fa_entry *input, struct decimated_data *output,
    struct fa_accum *double_accum, unsigned int N_log2)
{
    memset(output, 0, sizeof(*output));
    for (unsigned int i = 0; i < 1U << N_log2; i ++)
    {
        output->mean.x |= input->x;
        output->mean.y |= input->y;
        input += header->fa_entry_count;
    }

    /* Duplicate result to other fields for now to avoid confusion. */
    output->min = output->mean;
    output->max = output->mean;
    output->std = output->mean;

    /* Accumulate result into fa_accum, using bottom bits of sumx, sumy. */
    double_accum->sumx |= output->mean.x;
    double_accum->sumy |= output->mean.y;
}

static void compute_events_result(
    struct fa_accum *acc, struct decimated_data *result)
{
    result->mean.x = (int32_t) acc->sumx;
    result->mean.y = (int32_t) acc->sumy;

    result->min = result->mean;
    result->max = result->mean;
    result->std = result->mean;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Single data decimation. */


/* Array of result accumulators for double decimation. */
static struct fa_accum *double_accumulators;


/* Converts a column of N FA entries into a single entry by computing the mean,
 * min, max and standard deviation of the column. */
static void decimate_column_one(
    const struct fa_entry *input, struct decimated_data *output,
    struct fa_accum *double_accum, unsigned int N_log2)
{
    struct fa_accum accum;
    initialise_accum(&accum);

    for (unsigned int i = 0; i < 1U << N_log2; i ++)
    {
        accum_xy(&accum, input);
        input += header->fa_entry_count;
    }
    compute_result(&accum, N_log2, output);

    accum_accum(double_accum, &accum);
}


static void decimate_column(
    unsigned int id,
    const struct fa_entry *input, struct decimated_data *output,
    struct fa_accum *double_accums)
{
    for (unsigned int i = 0; i < input_decimation_count; i ++)
    {
        if (id == events_fa_id)
            decimate_events(
                input, output, double_accums, header->first_decimation_log2);
        else
            decimate_column_one(
                input, output, double_accums, header->first_decimation_log2);
        input += header->fa_entry_count << header->first_decimation_log2;
        output += 1;
    }
}

static void decimate_block(const void *read_block)
{
    unsigned int written = 0;
    for (unsigned int id = 0; id < header->fa_entry_count; id ++)
    {
        if (test_mask_bit(&header->archive_mask, id))
        {
            decimate_column(
                id, read_block + FA_ENTRY_SIZE * id, d_block(written),
                &double_accumulators[written]);
            written += 1;
        }
    }
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Double data decimation. */

/* Current offset into DD data area. */
static unsigned int dd_offset;
/* Count of IDs being stored. */
static unsigned int output_id_count;
/* Array of previously madvised addresses. */
static void **madvise_array;



/* In this case we work on decimated data sorted in the d_block and we write to
 * the in memory DD block. */
static void double_decimate_block(void)
{
    struct decimated_data *output = dd_area + dd_offset;
    unsigned int decimation_log2 =
        header->first_decimation_log2 + header->second_decimation_log2;

    for (unsigned int i = 0; i < output_id_count; i ++)
    {
        if (i == events_fa_id_output)
            compute_events_result(&double_accumulators[i], output);
        else
            compute_result(&double_accumulators[i], decimation_log2, output);
        initialise_accum(&double_accumulators[i]);
        output += header->dd_total_count;
    }

    dd_offset = (dd_offset + 1) % header->dd_total_count;
}


static void reset_double_decimation(void)
{
    dd_offset = header->current_major_block * header->dd_sample_count;
    for (unsigned int i = 0; i < output_id_count; i ++)
        initialise_accum(&double_accumulators[i]);
}


/* This is called at the start of a round of block processing to give the system
 * notice to ensure that all the pages required for writing double decimation
 * values are in memory.  This should be called after dd_offset has been
 * advanced. */
static void madvise_double_decimation(void)
{
    struct decimated_data *output = dd_area + dd_offset;
    for (unsigned int i = 0; i < output_id_count; i ++)
    {
        void *dd_address = (void *) ((uintptr_t) output & -page_size);
        output += header->dd_total_count;
        if (dd_address != madvise_array[i])
        {
            IGNORE(TEST_IO(madvise(dd_address, page_size, MADV_WILLNEED)));
            madvise_array[i] = dd_address;
        }
    }
}


/* Helper function to compute offset into output structure corresponding to
 * given input id.  Returns -1 if no entry found. */
static unsigned int input_id_to_output(
    const struct filter_mask *mask, unsigned int input_id)
{
    unsigned int output_id = (unsigned int) -1;
    for (unsigned int id = 0; id < header->fa_entry_count; id ++)
    {
        if (test_mask_bit(mask, id))
            output_id += 1;
        if (id == input_id)
            return output_id;
    }
    return (unsigned int) -1;
}

static void initialise_double_decimation(void)
{
    output_id_count =
        count_mask_bits(&header->archive_mask, header->fa_entry_count);
    events_fa_id_output =
        input_id_to_output(&header->archive_mask, events_fa_id);
    double_accumulators = calloc(output_id_count, sizeof(struct fa_accum));
    madvise_array = calloc(output_id_count, sizeof(void *));
    reset_double_decimation();

    madvise_double_decimation();
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Index maintenance. */

/* Number of timestamps we expect to see in a single major block. */
static int timestamp_count;
/* Array of timestamps for timestamp estimation, stored relative to the first
 * timestamp. */
static uint64_t first_timestamp;
static int *timestamp_array;
/* Current index into the timestamp array as we fill it. */
static unsigned int timestamp_index = 0;


/* Adds a minor block to the timestamp array. */
static void index_minor_block(const void *block, uint64_t timestamp)
{
    if (timestamp_index == 0)
    {
        first_timestamp = timestamp;
        /* For the very first index record the first id 0 field. */
        data_index[header->current_major_block].id_zero =
            (uint32_t) ((const struct fa_entry *) block)[0].x;
    }

    timestamp_array[timestamp_index] = (int) (timestamp - first_timestamp);
    timestamp_index += 1;
}


/* Called after the index has been updated to ensure that changes are written to
 * disk.  If we omit this and power is lost then it is quite likely that the
 * header and index can be very behind! */
static void flush_index(uint32_t current_block)
{
    uintptr_t page_mask = ~((uintptr_t) page_size - 1);
    void *index_address = (void *) (
        (uintptr_t) &data_index[current_block] & page_mask);
    IGNORE(
        TEST_IO(msync(header, DISK_HEADER_SIZE, MS_ASYNC))  &&
        TEST_IO(msync(index_address, page_size, MS_ASYNC)));
}


/* Called when a major block is complete, complete the index entry. */
static void advance_index(void)
{
    /* Fit a straight line through the timestamps and compute the timestamp at
     * the beginning of the segment. */
    int64_t sum_x = 0;
    int64_t sum_xt = 0;
    for (int i = 0; i < timestamp_count; i ++)
    {
        int t = 2*i - timestamp_count + 1;
        int64_t x = timestamp_array[i];
        sum_xt += x * t;
        sum_x  += x;
    }
    /* Compute sum_t2 = SUM t**2 through the iteration above, but unwinding the
     * algebra and using summation formulae
     *      SUM_{i=1..N} i   = N(N+1)/2
     *      SUM_{i=1..N} i*i = N(N+1)(2N+1)/6
     * we get sum_t2 = N(N*N-1)/3 . */
    int64_t sum_t2 =
        ((int64_t) timestamp_count * timestamp_count - 1) *
        timestamp_count / 3;

    struct data_index *ix = &data_index[header->current_major_block];
    /* Duration is "slope" calculated from fit above over an interval of
     * 2*timestamp_count. */
    ix->duration = (uint32_t) (2 * timestamp_count * sum_xt / sum_t2);
    /* Starting timestamp is computed at t=-timestamp_count-1 from centre. */
    ix->timestamp = first_timestamp +
        (uint64_t) (
            sum_x / timestamp_count - (timestamp_count + 1) * sum_xt / sum_t2);

    /* For the last duration we run an IIR to smooth out the bumps in our
     * timestamp calculations.  This gives us another digit or so. */
    header->last_duration = (uint32_t) round(
        ix->duration * header->timestamp_iir +
        header->last_duration * (1 - header->timestamp_iir));

    /* All done, advance the block index and reset our index. */
    uint32_t current_block = header->current_major_block;
    header->current_major_block =
        (header->current_major_block + 1) % header->major_block_count;
    timestamp_index = 0;

    /* Flush index and header to disk. */
    flush_index(current_block);
}


/* Discard work so far, called when we see a gap. */
static void reset_index(void)
{
    timestamp_index = 0;
}


static void initialise_index(void)
{
    timestamp_count = (int) (header->major_sample_count / input_frame_count);
    timestamp_array = malloc(sizeof(int) * (size_t) timestamp_count);
    timestamp_index = 0;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Interlocked access. */

/* Binary search to find major block corresponding to timestamp.  Note that the
 * high block is never inspected, which is just as well, as the current block is
 * invariably invalid.
 *     Returns the index of the latest valid block with a starting timestamp no
 * later than the target timestamp.  If the archive is empty may return an
 * invalid index, this is recognised by comparing the result with current. */
static unsigned int binary_search(uint64_t timestamp, bool *first_block)
{
    unsigned int N = header->major_block_count;
    unsigned int current = header->current_major_block;
    unsigned int start = (current + 1 + INDEX_SKIP) % N;
    unsigned int low = start;
    unsigned int high = current;
    while ((low + 1) % N != high)
    {
        unsigned int mid;
        if (low < high)
            mid = (low + high) / 2;
        else
            mid = ((low + high + N) / 2) % N;
        if (timestamp < data_index[mid].timestamp)
            high = mid;
        else
            low = mid;
    }

    /* To improve error reporting identify whether this is the first block. */
    if (first_block)
        *first_block = data_index[low].duration == 0  ||  low == start;
    /* Blocks with zero duration represent the start of the archive, so don't
     * return one of these.  Unless the archive is completely empty the result
     * will still be a valid block.  We don't worry about coping with an empty
     * archive, so long as we don't crash! */
    return data_index[low].duration == 0 ? high : low;
}


/* Converts timestamp to timestamp of closest block. */
uint64_t timestamp_to_index_ts(uint64_t timestamp)
{
    uint64_t result;
    LOCK(transform_lock);
    result = data_index[binary_search(timestamp, NULL)].timestamp;
    UNLOCK(transform_lock);
    return result;
}


/* Looks up timestamp and returns the block and offset into that block of the
 * "nearest" block.  If skip_gap is set it is possible that *block_out becomes
 * invalid, which must be checked by comparing with header->current_major_block,
 * but otherwise (except in the transient case of a completely empty archive)
 * the block is guaranteed to be valid. */
static void timestamp_to_block(
    uint64_t timestamp, bool skip_gap, bool *first_block,
    unsigned int *block_out, unsigned int *offset)
{
    unsigned int block = binary_search(timestamp, first_block);
    uint64_t block_start = data_index[block].timestamp;
    unsigned int duration = data_index[block].duration;
    unsigned int block_size = header->major_sample_count;
    if (timestamp < block_start)
        /* Timestamp precedes block, must mean that this is the earliest block
         * in the archive, so just start at the beginning of this block. */
        *offset = 0;
    else if (timestamp - block_start < duration)
        /* The normal case, return the offset of the selected timestamp into the
         * current block. */
        *offset = (unsigned int) (
            (timestamp - block_start) * block_size / duration);
    else if (skip_gap)
    {
        /* Timestamp falls off this block but precedes the next.  This will be
         * due to a data gap which we skip.  Caller must check validity of the
         * returned block in this case. */
        block = (block + 1) % header->major_block_count;
        *offset = 0;
        if (first_block)
            *first_block = false;
    }
    else
        /* Data gap after this block but skipping disabled.  Point to the last
         * data point in the block instead. */
        *offset = block_size - 1;
    *block_out = block;
}


/* Computes the number of samples available from the given block:offset to the
 * current end of the archive. */
static uint64_t compute_samples(unsigned int block, unsigned int offset)
{
    unsigned int current = header->current_major_block;
    unsigned int N = header->major_block_count;
    unsigned int block_count =
        current >= block ? current - block : N - block + current;
    unsigned int block_size = header->major_sample_count;
    return (uint64_t) block_count * block_size - offset;
}


bool timestamp_to_start(
    uint64_t timestamp, bool all_data, uint64_t *samples_available,
    unsigned int *block, unsigned int *offset)
{
    bool ok;
    LOCK(transform_lock);

    bool first_block;
    timestamp_to_block(timestamp, true, &first_block, block, offset);
    ok =
        TEST_OK_(
            *block != header->current_major_block, "Start time too late")  &&
        TEST_OK_(all_data  ||  data_index[*block].timestamp <= timestamp,
            first_block ? "Start time too early" : "Start time in data gap");
    if (ok)
        *samples_available = compute_samples(*block, *offset);

    UNLOCK(transform_lock);
    return ok;
}


bool timestamp_to_end(
    uint64_t timestamp, bool all_data, unsigned int start_block,
    unsigned int *block, unsigned int *offset)
{
    uint64_t end_timestamp;
    unsigned int current;

    LOCK(transform_lock);

    current = header->current_major_block;
    timestamp_to_block(timestamp, false, NULL, block, offset);
    struct data_index *ix = &data_index[*block];
    end_timestamp = ix->timestamp + ix->duration;

    UNLOCK(transform_lock);

    return
        TEST_OK_(all_data  ||  timestamp <= end_timestamp,
            "End time too late")  &&
        TEST_OK_(
            /* This test is a little tricky: checking that end comes no earlier
             * than start, but need to take wraparound into account.
             * Essentially can only see end below start if the gap (current
             * block) lies inbetween. */
            *block >= start_block  ||
            (*block < current  &&  current < start_block),
            "No data in selected range");
}



bool find_gap(bool check_id0, unsigned int *start, unsigned int *blocks)
{
    struct data_index *ix = &data_index[*start];
    uint64_t timestamp = ix->timestamp + ix->duration;
    uint32_t id_zero   = ix->id_zero + header->major_sample_count;
    while (*blocks > 1)
    {
        *blocks -= 1;
        *start += 1;
        if (*start == header->major_block_count)
            *start = 0;

        ix = &data_index[*start];
        int64_t delta_t = (int64_t) (ix->timestamp - timestamp);
        if ((check_id0  &&  ix->id_zero != id_zero)  ||
            delta_t < -MAX_DELTA_T  ||  MAX_DELTA_T < delta_t)
            return true;

        timestamp = ix->timestamp + ix->duration;
        id_zero = ix->id_zero + header->major_sample_count;
    }
    return false;
}


const struct data_index *read_index(unsigned int ix)
{
    return &data_index[ix];
}

const struct disk_header *get_header(void)
{
    return header;
}

const struct decimated_data *get_dd_area(void)
{
    return dd_area;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Top level control. */


/* Processes a single block of raw frames read from the internal circular
 * buffer, transposing for efficient read and generating decimations as
 * appropriate.  Schedules write to disk as appropriate when buffer is full
 * enough. */
void process_block(const void *block, uint64_t timestamp)
{
    if (block)
    {
        index_minor_block(block, timestamp);
        transpose_block(block);
        decimate_block(block);
        bool must_write = advance_block();
        unsigned int decimation = 1U << (
            header->first_decimation_log2 + header->second_decimation_log2);
        if ((fa_offset & (decimation - 1)) == 0)
            double_decimate_block();
        if (must_write)
        {
            LOCK(transform_lock);
            write_major_block();
            advance_index();
            UNLOCK(transform_lock);

            madvise_double_decimation();
        }
    }
    else
    {
        /* If we see a gap in the block then discard all the work we've done so
         * far. */
        reset_block();
        reset_index();
        reset_double_decimation();
    }
}


void initialise_transform(
    struct disk_header *header_, struct data_index *data_index_,
    struct decimated_data *dd_area_, unsigned int events_fa_id_)
{
    header = header_;
    data_index = data_index_;
    dd_area = dd_area_;
    events_fa_id = events_fa_id_;

    input_frame_count =
        header->input_block_size / header->fa_entry_count / FA_ENTRY_SIZE;
    input_decimation_count = input_frame_count >> header->first_decimation_log2;

    page_size = (size_t) sysconf(_SC_PAGESIZE);
    initialise_double_decimation();
    initialise_io_buffer();
    initialise_index();
}
