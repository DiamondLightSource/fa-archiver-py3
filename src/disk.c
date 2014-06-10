/* Common routines for disk access.
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
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <errno.h>
#include <math.h>

#include "error.h"
#include "fa_sniffer.h"
#include "mask.h"

#include "disk.h"


static size_t round_to_page(size_t block_size)
{
    size_t page_size = (size_t) sysconf(_SC_PAGESIZE);
    return page_size * ((block_size + page_size - 1) / page_size);
}

static bool page_aligned(uint64_t offset, const char *description)
{
    size_t page_size = (size_t) sysconf(_SC_PAGESIZE);
    return TEST_OK_(offset % page_size == 0,
        "Bad page alignment for %s at %"PRIu64, description, offset);
}

static bool test_power_of_2(size_t value, const char *name)
{
    return TEST_OK_((value & -value) == value, "%s must be a power of 2", name);
}

static uint32_t uint_log2(uint32_t value)
{
    uint32_t result = 0;
    while (value >>= 1)
        result += 1;
    return result;
}


bool initialise_header(
    struct disk_header *header,
    struct filter_mask *archive_mask,
    uint64_t file_size,
    uint32_t input_block_size,
    uint32_t major_sample_count,
    uint32_t first_decimation,
    uint32_t second_decimation,
    double sample_frequency,
    double timestamp_iir,
    uint32_t fa_entry_count)
{
    uint32_t archive_mask_count = count_mask_bits(archive_mask, fa_entry_count);

    /* Header signature. */
    memset(header, 0, sizeof(*header));
    strncpy(header->signature, DISK_SIGNATURE, sizeof(header->signature));
    header->version = DISK_VERSION;

    /* Capture parameters. */
    copy_mask(&header->archive_mask, archive_mask);
    header->archive_mask_count = archive_mask_count;
    header->first_decimation_log2 = uint_log2(first_decimation);
    header->second_decimation_log2 = uint_log2(second_decimation);
    header->input_block_size = input_block_size;
    header->fa_entry_count = fa_entry_count;
    header->timestamp_iir = timestamp_iir;

    /* Compute the fixed size parameters describing the data layout. */
    header->major_sample_count = major_sample_count;
    header->d_sample_count = header->major_sample_count / first_decimation;
    header->dd_sample_count = header->d_sample_count / second_decimation;
    header->major_block_size = (uint32_t) (
        archive_mask_count * (
            header->major_sample_count * FA_ENTRY_SIZE +
            header->d_sample_count * sizeof(struct decimated_data)));

    /* Computing the total number of samples (we count in major blocks) is a
     * little tricky, as we have to fit everything into file_size including
     * all the auxiliary data structures.  What makes things more tricky is
     * that both the index and DD data areas are rounded up to a multiple of
     * page size, so simple division won't quite do the trick. */
    uint64_t data_size = file_size - DISK_HEADER_SIZE;
    uint32_t index_block_size = sizeof(struct data_index);
    uint32_t dd_block_size = (uint32_t) (
        header->dd_sample_count * archive_mask_count *
        sizeof(struct decimated_data));
    /* Start with a simple estimate by division. */
    uint32_t major_block_count =
        (uint32_t) (data_size / (
            index_block_size + dd_block_size + header->major_block_size));
    uint32_t index_data_size =
        (uint32_t) round_to_page(major_block_count * index_block_size);
    uint64_t dd_data_size =
        round_to_page((size_t) major_block_count * dd_block_size);
    /* Now incrementally reduce the major block count until we're good.  In
     * fact, this is only going to happen once at most. */
    while (index_data_size + dd_data_size +
           major_block_count * header->major_block_size > data_size)
    {
        major_block_count -= 1;
        index_data_size =
            (uint32_t) round_to_page(major_block_count * index_block_size);
        dd_data_size = round_to_page(major_block_count * dd_block_size);
    }

    /* Finally we can compute the data layout. */
    header->index_data_start = DISK_HEADER_SIZE;
    header->index_data_size = index_data_size;
    header->dd_data_start = header->index_data_start + index_data_size;
    header->dd_data_size = dd_data_size;
    header->dd_total_count = header->dd_sample_count * major_block_count;
    header->major_data_start = header->dd_data_start + dd_data_size;
    header->major_block_count = major_block_count;
    header->total_data_size =
        header->major_data_start +
        (uint64_t) major_block_count * header->major_block_size;

    header->current_major_block = 0;
    /* Compute the nominal time, in microseconds, to capture an entire major
     * block. */
    header->last_duration = (uint32_t) round(
        header->major_sample_count * 1e6 / sample_frequency);

    errno = 0;      // Suppresses invalid errno report from TEST_OK_ failures
    return
        test_power_of_2(first_decimation, "First decimation")  &&
        test_power_of_2(second_decimation, "Second decimation")  &&
        test_power_of_2(major_sample_count, "Major sample count")  &&
        TEST_OK_(major_sample_count >= first_decimation * second_decimation,
            "Major sample count must be no smaller than decimation count")  &&
        validate_header(header, file_size);
}


static bool validate_version(struct disk_header *header)
{
    return
        TEST_OK_(
            strncmp(header->signature, DISK_SIGNATURE,
                sizeof(header->signature)) == 0,
            "Invalid header signature")  &&
        TEST_OK_(header->version == DISK_VERSION,
            "Invalid header version %u, expected %u",
            header->version, DISK_VERSION);
}


bool validate_header(struct disk_header *header, uint64_t file_size)
{
    COMPILE_ASSERT(sizeof(struct disk_header) <= DISK_HEADER_SIZE);

    size_t fa_frame_size = header->fa_entry_count * FA_ENTRY_SIZE;
    uint32_t input_sample_count =
        (uint32_t) (header->input_block_size / fa_frame_size);
    uint32_t first_decimation  = 1U << header->first_decimation_log2;
    uint32_t second_decimation = 1U << header->second_decimation_log2;
    unsigned int archive_mask_count;
    errno = 0;      // Suppresses invalid error report from TEST_OK_ failures
    return
        /* Basic header validation. */
        validate_version(header)  &&

        TEST_OK_(header->fa_entry_count <= MAX_FA_ENTRY_COUNT,
            "FA entry count %"PRIu32" too large", header->fa_entry_count)  &&
        DO_(archive_mask_count =
            count_mask_bits(&header->archive_mask, header->fa_entry_count))  &&

        /* Data capture parameter validation. */
        TEST_OK_(
            archive_mask_count == header->archive_mask_count,
            "Inconsistent archive mask: %d != %"PRIu32,
                archive_mask_count, header->archive_mask_count)  &&
        TEST_OK_(header->archive_mask_count > 0, "Empty capture mask")  &&
        TEST_OK_(header->total_data_size <= file_size,
            "Data size in header larger than file size: %"PRIu64" > %"PRIu64,
            header->total_data_size, file_size)  &&
        test_power_of_2(header->fa_entry_count, "FA entry count")  &&
        TEST_OK_(header->fa_entry_count <= MAX_FA_ENTRY_COUNT,
            "FA entry count too large")  &&

        /* Data parameter validation. */
        TEST_OK_(
            header->d_sample_count << header->first_decimation_log2 ==
                header->major_sample_count,
            "Invalid first decimation: %"PRIu32" * %"PRIu32" != %"PRIu32,
                header->d_sample_count, first_decimation,
                header->major_sample_count)  &&
        TEST_OK_(
            header->dd_sample_count << header->second_decimation_log2 ==
            header->d_sample_count,
            "Invalid second decimation: %"PRIu32" * %"PRIu32" != %"PRIu32,
                header->dd_sample_count, second_decimation,
                header->d_sample_count)  &&
        TEST_OK_(
            header->archive_mask_count * (
                header->major_sample_count * FA_ENTRY_SIZE +
                header->d_sample_count * sizeof(struct decimated_data)) ==
            header->major_block_size,
            "Invalid major block size: "
            "%"PRIu32" * (%"PRIu32" * %zd + %"PRIu32" * %zd) != %"PRIu32,
                header->archive_mask_count,
                header->major_sample_count, FA_ENTRY_SIZE,
                header->d_sample_count, sizeof(struct decimated_data),
                header->major_block_size)  &&
        TEST_OK_(
            header->major_block_count * sizeof(struct data_index) <=
            header->index_data_size,
            "Invalid index block size: %"PRIu32" * %zd > %"PRIu32,
                header->major_block_count, sizeof(struct data_index),
                header->index_data_size)  &&
        TEST_OK_(
            header->dd_sample_count * header->major_block_count ==
                header->dd_total_count,
            "Invalid total DD count: %"PRIu32" * %"PRIu32" != %"PRIu32,
                header->dd_sample_count, header->major_block_count,
                header->dd_total_count)  &&
        TEST_OK_(
            header->dd_total_count * header->archive_mask_count *
                sizeof(struct decimated_data) <= header->dd_data_size,
            "DD area too small: %"PRIu32" * %"PRIu32" * %zd > %"PRIu64,
                header->dd_total_count, header->archive_mask_count,
                sizeof(struct decimated_data), header->dd_data_size)  &&

        TEST_OK_(
            0 < header->timestamp_iir  &&  header->timestamp_iir <= 1,
            "Invalid timetampt IIR: %g\n", header->timestamp_iir)  &&

        /* Check page alignment. */
        page_aligned(header->index_data_size, "index size")  &&
        page_aligned(header->dd_data_size, "DD size")  &&
        page_aligned(header->major_block_size, "major block")  &&
        page_aligned(header->index_data_start, "index area")  &&
        page_aligned(header->dd_data_start, "DD data area")  &&
        page_aligned(header->major_data_start, "major data area")  &&

        /* Check data areas. */
        TEST_OK_(header->index_data_start >= DISK_HEADER_SIZE,
            "Unexpected index data start: %"PRIu64" < %d",
            header->index_data_start, DISK_HEADER_SIZE)  &&
        TEST_OK_(
            header->dd_data_start >=
            header->index_data_start + header->index_data_size,
            "Unexpected DD data start: %"PRIu64" < %"PRIu64" + %"PRIu32,
                header->dd_data_start,
                header->index_data_start, header->index_data_size)  &&
        TEST_OK_(
            header->major_data_start >=
            header->dd_data_start + header->dd_data_size,
            "Unexpected major data start: %"PRIu64" < %"PRIu64" + %"PRIu64,
                header->major_data_start,
                header->dd_data_start, header->dd_data_size)  &&
        TEST_OK_(
            header->total_data_size >=
            header->major_data_start +
            (uint64_t) header->major_block_count * header->major_block_size,
            "Data area too small for data: "
            "%"PRIu64" < %"PRIu64" + %"PRIu32" * %"PRIu32,
                header->total_data_size,
                header->major_data_start,
                header->major_block_count, header->major_block_size)  &&
        TEST_OK_(
            header->index_data_size >=
            header->major_block_count * sizeof(struct data_index),
            "Index area too small: %"PRIu32" < %"PRIu32" * %zd",
                header->index_data_size,
                header->major_block_count, sizeof(struct data_index))  &&

        /* Major data layout validation. */
        TEST_OK_(
            header->first_decimation_log2 > 0  &&
            header->second_decimation_log2 > 0,
            "Decimation too small: %"PRIu32", %"PRIu32,
                first_decimation, second_decimation)  &&
        TEST_OK_(
            header->major_sample_count > 1, "Output block size too small")  &&
        TEST_OK_(header->major_block_count > 1, "Data file too small")  &&
        TEST_OK_(
            header->input_block_size % fa_frame_size == 0,
            "Input block size doesn't match frame size: %"PRIu32", %d",
                header->input_block_size, fa_frame_size == 0)  &&
        TEST_OK_(
            header->major_sample_count % input_sample_count == 0,
            "Input and major block sizes don't match: %"PRIu32", %d",
                header->major_sample_count, input_sample_count)  &&

        TEST_OK_(header->current_major_block < header->major_block_count,
            "Invalid current index: %"PRIu32" >= %"PRIu32,
            header->current_major_block, header->major_block_count);
}


void print_header(FILE *out, struct disk_header *header)
{
    char mask_string[RAW_MASK_BYTES+1];
    char format_string[256];
    if (!validate_version(header))
        fprintf(out,
            "WARNING: Header validation failed, data below will be invalid\n");
    format_raw_mask(&header->archive_mask, header->fa_entry_count, mask_string);
    if (!format_readable_mask(
            &header->archive_mask, header->fa_entry_count,
            format_string, sizeof(format_string)))
        sprintf(format_string, "...");
    double sample_frequency =
        header->major_sample_count * 1e6 / header->last_duration;
    uint64_t total_sample_count =
        (uint64_t) header->major_block_count * header->major_sample_count;
    uint32_t first_decimation  = 1U << header->first_decimation_log2;
    uint32_t second_decimation = 1U << header->second_decimation_log2;
    double seconds = (double) total_sample_count / sample_frequency;
    size_t fa_frame_size = header->fa_entry_count * FA_ENTRY_SIZE;
    fprintf(out,
        "FA sniffer archive: %.7s, v%d.\n"
        "Archiving: %s\n    BPMS: %s\n"
        "Decimation %"PRIu32", %"PRIu32" => %"PRIu32", recording %u BPMs\n"
        "Input block size = %"PRIu32" bytes, %zu frames, "
            "%"PRIu32" samples per frame\n"
        "Major block size = %"PRIu32" bytes, %"PRIu32" samples\n"
        "Total size = %"PRIu32" major blocks = %"PRIu64" samples"
            " = %"PRIu64" bytes\n"
        "    Duration: %d hours, %d minutes, %.1f seconds (f_s = %.2f)\n"
        "Index data from %"PRIu64" for %"PRIu32" bytes\n"
        "DD data starts %"PRIu64" for %"PRIu64" bytes, %"PRIu32" samples,"
            " %"PRIu32" per block\n"
        "FA+D data from %"PRIu64", %"PRIu32" decimated samples per block\n"
        "Last duration: %"PRIu32" us, or %lg Hz.  Current index: %"PRIu32"\n",
        header->signature, header->version,
        mask_string, format_string,
        first_decimation, second_decimation,
            first_decimation * second_decimation, header->archive_mask_count,
        header->input_block_size, header->input_block_size / fa_frame_size,
            header->fa_entry_count,
        header->major_block_size, header->major_sample_count,
        header->major_block_count, total_sample_count, header->total_data_size,
        (int) seconds / 3600, ((int) seconds / 60) % 60, fmod(seconds, 60),
            sample_frequency,
        header->index_data_start, header->index_data_size,
        header->dd_data_start, header->dd_data_size, header->dd_total_count,
            header->dd_sample_count,
        header->major_data_start, header->d_sample_count,
        header->last_duration,
            1e6 * header->major_sample_count / (double) header->last_duration,
            header->current_major_block);
}


bool lock_archive(int disk_fd)
{
    return TEST_IO_(flock(disk_fd, LOCK_EX | LOCK_NB),
        "Unable to lock archive for access: already running?");
}


bool get_filesize(int disk_fd, uint64_t *file_size)
{
    /* First try blocksize, if that fails try stat: the first works on a
     * block device, the second on a regular file. */
    if (ioctl(disk_fd, BLKGETSIZE64, file_size) == 0)
        return true;
    else
    {
        struct stat st;
        return
            TEST_IO(fstat(disk_fd, &st))  &&
            DO_(*file_size = (uint64_t) st.st_size)  &&
            TEST_OK_(*file_size > 0,
                "Zero file size.  Maybe stat failed?");
    }
}
