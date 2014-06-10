/* Utility to prepare a file for use as an archive area by the FA sniffer
 * archiver application.
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
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

#include "error.h"
#include "fa_sniffer.h"
#include "mask.h"
#include "disk.h"
#include "parse.h"



/* An experiment shows that a disk block transfer size of 512K is optimal in the
 * sense of being the largest single block transfer size. */
#define K               1024
#define FA_BLOCK_SIZE   (512 * K)    // Default block size for device IO


/*****************************************************************************/
/*                               Option Parsing                              */
/*****************************************************************************/

static char *argv0;

static const char *file_name;
static bool file_size_given = false;
static uint64_t file_size;
static struct filter_mask archive_mask;
static uint32_t input_block_size = 512 * K;
static uint32_t major_sample_count = 65536;
static uint32_t first_decimation = 64;
static uint32_t second_decimation = 256;
static double sample_frequency = 10072.4;
static bool dry_run = false;
static bool quiet_allocate = false;
static uint32_t fa_entry_count = 256;
static double timestamp_iir = 0.1;

/* Options for read only operation. */
static bool read_only = false;
static bool do_validate = true;
static bool dump_header = true;
static bool dump_index = false;
static unsigned int dump_start = 0;
static unsigned int dump_end = UINT_MAX;
static bool do_lock = true;
static bool convert_timestamps = false;


static void usage(void)
{
    printf(
"Usage: %s [<options>] <capture-mask> <file-name>\n"
"or:    %s -H [<H-options>] <file-name>\n"
"\n"
"Prepares or reinitalises a disk file <file-name> for use as an FA sniffer\n"
"archive unless -H is given.  The given <file-name> can be a block device or\n"
"an ordinary file.  The BPMs specified in <capture-mask> will be captured to\n"
"disk.\n"
"\n"
"The following options can be given:\n"
"   -s:  Specify size of file.  The file will be resized to the given size\n"
"        all disk blocks allocated.  Optional if the file already exists.\n"
"   -N:  Specify number of FA entries in a single block, default is 256.\n"
"   -I:  Specify input block size for reads from FA sniffer device.  The\n"
"        default value is %"PRIu32" bytes.\n"
"   -M:  Specify number of samples in a single capture to disk.  The default\n"
"        value is %"PRIu32".\n"
"   -d:  Specify first decimation factor.  The default value is %"PRIu32".\n"
"   -D:  Specify second decimation factor.  The default value is %"PRIu32".\n"
"   -f:  Specify nominal sample frequency.  The default is %.1fHz.\n"
"   -T:  Specify timestamp IIR factor.  The default is %g.\n"
"   -n   Print file header but don't actually write anything.\n"
"   -q   Use faster but quiet mechanism for allocating file buffer.\n"
"\n"
"File size can be followed by one of K, M, G or T to specify sizes in\n"
"kilo, mega, giga or terabytes, and similarly block sizes can be followed\n"
"by one of K or M.\n"
"\n"
"If instead -H is given then the file header will be printed.  This can be\n"
"followed by the following options:\n"
"   -f   Bypass header validation and display even if appears invalid.\n"
"   -d   Dump index.  This can generate a lot of data, or -s/-e can be used.\n"
"   -s:  Offset of first index block to dump.\n"
"   -e:  Offset of last index block to dump.\n"
"   -n   Don't actually dump the header.\n"
"   -u   Don't lock the archive while dumping index.  Allows dumping of live.\n"
"        archive but can produce inconsistent results over write boundary.\n"
"   -t   Show timestamps in human readable form.\n"
        , argv0, argv0,
        input_block_size, major_sample_count,
        first_decimation, second_decimation,
        sample_frequency, timestamp_iir);
}


static bool process_opts(int *argc, char ***argv)
{
    argv0 = (*argv)[0];
    bool ok = true;
    while (ok)
    {
        switch (getopt(*argc, *argv, "+hs:N:I:M:d:D:f:T:nq"))
        {
            case 'h':
                usage();
                exit(0);
            case 's':
                ok = DO_PARSE("file size", parse_size64, optarg, &file_size);
                file_size_given = true;
                break;
            case 'N':
                ok = DO_PARSE("FA entry count",
                    parse_uint32, optarg, &fa_entry_count);
                break;
            case 'I':
                ok = DO_PARSE("input block size",
                    parse_size32, optarg, &input_block_size);
                break;
            case 'M':
                ok = DO_PARSE("major sample count",
                    parse_uint32, optarg, &major_sample_count);
                break;
            case 'd':
                ok = DO_PARSE("first decimation",
                    parse_size32, optarg, &first_decimation);
                break;
            case 'D':
                ok = DO_PARSE("second decimation",
                    parse_size32, optarg, &second_decimation);
                break;
            case 'f':
                ok = DO_PARSE("sample frequency",
                    parse_double, optarg, &sample_frequency);
                break;
            case 'T':
                ok = DO_PARSE("timestamp IIR",
                    parse_double, optarg, &timestamp_iir);
                break;
            case 'n':   dry_run = true;                             break;
            case 'q':   quiet_allocate = true;                      break;
            case '?':
            default:
                fprintf(stderr, "Try `%s -h` for usage\n", argv0);
                return false;
            case -1:
                *argc -= optind;
                *argv += optind;
                return true;
        }
    }
    return false;
}


static bool process_H_opts(int *argc, char ***argv)
{
    argv0 = (*argv)[0];
    bool ok = true;
    while (ok)
    {
        switch (getopt(*argc, *argv, "+Hfdnuts:e:"))
        {
            case 'H':   break;      // Expected this one, ignore
            case 'f':   do_validate = false;                        break;
            case 'd':   dump_index = true;                          break;
            case 'n':   dump_header = false;                        break;
            case 'u':   do_lock = false;                            break;
            case 't':   convert_timestamps = true;                  break;
            case 's':
                ok = DO_PARSE("start block", parse_uint, optarg, &dump_start);
                break;
            case 'e':
                ok = DO_PARSE("end block", parse_uint, optarg, &dump_end);
                break;
            case '?':
            default:
                fprintf(stderr, "Try `%s -h` for usage\n", argv0);
                return false;
            case -1:
                *argc -= optind;
                *argv += optind;
                return true;
        }
    }
    return false;
}


static bool process_args(int argc, char **argv)
{
    read_only = argc >= 2  &&  strncmp(argv[1], "-H", 2) == 0;

    if (read_only)
        return
            process_H_opts(&argc, &argv)  &&
            TEST_OK_(argc == 1, "Wrong number of arguments")  &&
            DO_(file_name = argv[0]);
    else
        return
            process_opts(&argc, &argv)  &&
            TEST_OK_(argc == 2, "Wrong number of arguments")  &&
            DO_PARSE("capture mask",
                parse_mask, argv[0], fa_entry_count, &archive_mask)  &&
            DO_(file_name = argv[1]);
}


/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/

#define PROGRESS_INTERVAL   16


/* Resets the index area to zeros, even if the rest of the file is untouched.
 * This is necessary for a consistent freshly initialised database. */
static bool reset_index(int file_fd, size_t index_data_size)
{
    void *data_index;
    bool ok =
        TEST_NULL(data_index = valloc(index_data_size))  &&
        DO_(memset(data_index, 0, index_data_size))  &&
        TEST_write(file_fd, data_index, index_data_size);
    free(data_index);
    return ok;
}

static bool prepare_new_header(struct disk_header *header)
{
    return
        initialise_header(header,
            &archive_mask, file_size,
            input_block_size, major_sample_count,
            first_decimation, second_decimation, sample_frequency,
            timestamp_iir, fa_entry_count)  &&
        DO_(print_header(stdout, header));
}

static bool write_new_header(int file_fd, size_t *written)
{
    struct disk_header *header;
    bool ok =
        TEST_NULL(header = valloc(DISK_HEADER_SIZE))  &&
        DO_(memset(header, 0, DISK_HEADER_SIZE))  &&
        prepare_new_header(header)  &&
        TEST_IO(lseek(file_fd, 0, SEEK_SET))  &&
        TEST_write(file_fd, header, DISK_HEADER_SIZE)  &&
        reset_index(file_fd, header->index_data_size)  &&
        DO_(*written = DISK_HEADER_SIZE + header->index_data_size);
    free(header);
    return ok;
}


static void show_progress(unsigned int n, unsigned int final_n)
{
    const char *progress = "|/-\\";
    if (n % PROGRESS_INTERVAL == 0)
    {
        printf("%c %9d (%5.2f%%)\r",
            progress[(n / PROGRESS_INTERVAL) % 4], n,
            100.0 * (double) n / final_n);
        fflush(stdout);
    }
}


/* Verbose and slower near equivalent to posix_fallocate(). */
static bool fill_zeros(int file_fd, size_t written)
{
    uint32_t block_size = 512*K;
    void *zeros = valloc(block_size);
    memset(zeros, 0, block_size);

    uint64_t size_left = file_size - written;
    unsigned int final_n = (unsigned int) (size_left / block_size);
    bool ok = true;
    for (unsigned int n = 0; ok  &&  size_left >= block_size;
         size_left -= block_size, n += 1)
    {
        ok = TEST_write(file_fd, zeros, block_size);
        show_progress(n, final_n);
    }
    if (ok  &&  size_left > 0)
        ok = TEST_write(file_fd, zeros, (size_t) size_left);
    printf("\n");
    free(zeros);
    return ok;
}


static void print_timestamp(time_t timestamp)
{
    struct tm tm;
    gmtime_r(&timestamp, &tm);
    printf("%04d-%02d-%02d %02d:%02d:%02d ",
        1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
}


static bool do_dump_index(int file_fd, struct disk_header *header)
{
    /* Dumping the index isn't altogether straightforward -- we need to map it
     * into memory first! */
    struct data_index *data_index = NULL;
    bool ok =
        IF_(do_lock, lock_archive(file_fd))  &&
        TEST_IO(
            data_index = mmap(NULL, header->index_data_size,
                PROT_READ, MAP_SHARED, file_fd,
                (off64_t) header->index_data_start));
    if (ok)
    {
        unsigned int block_count = header->major_block_count;
        if (dump_start > block_count)
            dump_start = block_count;
        if (dump_end > block_count)
            dump_end = block_count;
        struct data_index *last_block =
            &data_index[dump_start > 0 ? dump_start - 1 : block_count - 1];
        for (unsigned int i = dump_start; i < dump_end; i ++)
        {
            struct data_index *block = &data_index[i];
            printf("%6u: ", i);
            if (convert_timestamps)
                print_timestamp((time_t) (block->timestamp / 1000000));
            printf("%10"PRIu64".%06"PRIu64" / %7u / %9u",
                block->timestamp / 1000000, block->timestamp % 1000000,
                block->duration, block->id_zero);
            if (i == header->current_major_block)
                printf(" <<<<<<<<<<<<<<<");
            else
            {
                uint64_t delta_t = block->timestamp - last_block->timestamp;
                printf(" => %1"PRIu64".%06"PRIu64" / %u",
                    delta_t / 1000000, delta_t % 1000000,
                    block->id_zero - last_block->id_zero);
            }
            printf("\n");
            last_block = block;
        }
        TEST_IO(munmap(data_index, header->index_data_size));
    }
    return ok;
}


/* Read an existing header and report. */
static bool prepare_read_only(void)
{
    int file_fd;
    struct disk_header header;
    return
        TEST_IO_(file_fd = open(file_name, O_RDONLY),
            "Unable to read file \"%s\"", file_name)  &&
        FINALLY(
            TEST_read(file_fd, &header, sizeof(header))  &&
            IF_(do_validate,
                get_filesize(file_fd, &file_size)  &&
                validate_header(&header, file_size))  &&
            IF_(dump_header, DO_(print_header(stdout, &header)))  &&
            IF_(dump_index, do_dump_index(file_fd, &header)),

            // Close opened file
            TEST_IO(close(file_fd)));
}


/* Prepare dummy header and report what would be written. */
static bool prepare_dry_run(void)
{
    int file_fd;
    struct disk_header header = {};
    return
        IF_(!file_size_given,
            TEST_IO_(file_fd = open(file_name, O_RDONLY),
                "Unable to open archive \"%s\"", file_name)  &&
            FINALLY(
                get_filesize(file_fd, &file_size),

                TEST_IO(close(file_fd))))  &&
        prepare_new_header(&header);
}


/* Actually do the work of creating a header and initialising the data store (if
 * required). */
static bool prepare_create(void)
{
    int file_fd;
    int open_flags =
        (file_size_given ? O_CREAT | O_TRUNC : 0) |
        (quiet_allocate ? 0 : O_DIRECT) | O_WRONLY;
    size_t written;
    return
        TEST_IO_(file_fd = open(file_name, open_flags, 0664),
            "Unable to write to file \"%s\"", file_name)  &&
        FINALLY(
            lock_archive(file_fd)  &&
            IF_(!file_size_given,
                get_filesize(file_fd, &file_size))  &&
            write_new_header(file_fd, &written)  &&
            IF_(file_size_given,
                IF_ELSE(quiet_allocate,
                    /* posix_fallocate is marginally faster but shows no sign of
                     * progress. */
                    TEST_0(posix_fallocate(
                        file_fd, (off64_t) written,
                        (off64_t) (file_size - written))),
                    /* If we use full_zeros we can show progress to the user. */
                    fill_zeros(file_fd, written))),

        TEST_IO(close(file_fd)));
}

int main(int argc, char **argv)
{
    if (!process_args(argc, argv))
        /* For argument errors return 1. */
        return 1;

    bool ok;
    if (read_only)
        ok = prepare_read_only();
    else if (dry_run)
        ok = prepare_dry_run();
    else
        ok = prepare_create();

    return ok ? 0 : 2;
}
