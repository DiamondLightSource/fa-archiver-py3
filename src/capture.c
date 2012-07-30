/* Command line tool to capture stream of FA sniffer data to file.
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
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include "error.h"
#include "fa_sniffer.h"
#include "mask.h"
#include "matlab.h"
#include "parse.h"
#include "reader.h"


#define DEFAULT_SERVER      "fa-archiver.diamond.ac.uk"
#define BUFFER_SIZE         (1 << 16)
#define PROGRESS_INTERVAL   (1 << 18)

/* Minimum server protocol supported.  We just can't talk to older servers. */
#define SERVER_MAJOR_VERSION   1
#define SERVER_MINOR_VERSION   1


enum data_format { DATA_FA, DATA_D, DATA_DD };

/* Command line parameters. */
static int port = 8888;
static const char *server_name = DEFAULT_SERVER;
static const char *output_filename = NULL;
static struct filter_mask capture_mask;
static bool matlab_format = true;
static bool squeeze_matlab = true;
static bool continuous_capture = false;
static bool start_specified = false;
static struct timespec start;
static bool end_specified = false;
static struct timespec end;
static uint64_t sample_count = 0;
static enum data_format data_format = DATA_FA;
static unsigned int data_mask = 1;
static bool show_progress = true;
static bool request_contiguous = false;
static const char *data_name = "data";
static bool all_data = false;
static bool check_id0 = false;
static bool offset_matlab_times = true;
static bool subtract_day_zero = false;
static bool save_id0 = false;

/* Archiver parameters read from archiver during initialisation. */
static double sample_frequency;
static unsigned int first_decimation;
static unsigned int second_decimation;
static unsigned int major_version, minor_version;
static unsigned int fa_entry_count = 256;

static FILE *output_file;


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Server connection core. */


/* Macro for reading a single item from stream. */
#define READ_ITEM(stream, item) \
    (fread(&item, sizeof(item), 1, stream) == 1)


/* Connnects to the server. */
static bool connect_server(FILE **stream)
{
    struct sockaddr_in s_in = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };
    struct hostent *hostent;
    int sock;
    return
        TEST_NULL_(
            hostent = gethostbyname(server_name),
            "Unable to resolve server \"%s\"", server_name)  &&
        DO_(memcpy(
            &s_in.sin_addr.s_addr, hostent->h_addr,
            (size_t) hostent->h_length))  &&
        TEST_IO(sock = socket(AF_INET, SOCK_STREAM, 0))  &&
        TEST_IO_(
            connect(sock, (struct sockaddr *) &s_in, sizeof(s_in)),
            "Unable to connect to server %s:%d", server_name, port)  &&
        TEST_NULL(*stream = fdopen(sock, "r+"));
}


/* Reads a complete (short) response from the server until end of input, fails
 * if buffer overflows (or any other reason). */
static bool read_response(FILE *stream, char *buf, size_t buflen)
{
    size_t rx = fread(buf, 1, buflen - 1, stream);
    buf[rx] = '\0';
    return
        TEST_OK_(rx < buflen, "Read buffer exhausted")  &&
        TEST_OK(!ferror(stream))  &&
        TEST_OK_(rx > 0, "No response from server");
}



/* Parses version string of the form <int>.<int> */
static bool parse_version(const char **string)
{
    return
        parse_uint(string, &major_version)  &&
        parse_char(string, '.')  &&
        parse_uint(string, &minor_version);
}


/* Parses expected server response to CFdDVK command: should be four newline
 * terminated numbers and a version string. */
static bool parse_archive_parameters(const char **string)
{
    return
        parse_double(string, &sample_frequency)  &&
        parse_char(string, '\n')  &&
        parse_uint(string, &first_decimation)  &&
        parse_char(string, '\n')  &&
        parse_uint(string, &second_decimation)  &&
        parse_char(string, '\n')  &&
        parse_version(string)  &&
        parse_char(string, '\n')  &&
        parse_uint(string, &fa_entry_count)  &&
        parse_char(string, '\n');
}

/* Interrogates server for the nominal sample frequency and the decimation
 * factors. */
static bool read_archive_parameters(void)
{
    FILE *stream;
    char buffer[64];
    return
        connect_server(&stream)  &&
        TEST_OK(fprintf(stream, "CFdDVK\n") > 0)  &&
        FINALLY(
            read_response(stream, buffer, sizeof(buffer)),
            // Finally, whether read_response succeeds
            TEST_OK(fclose(stream) == 0))  &&
        DO_PARSE("server response", parse_archive_parameters, buffer)  &&
        TEST_OK_(
            major_version > SERVER_MAJOR_VERSION ||
            minor_version >= SERVER_MINOR_VERSION,
            "Server protocol mismatch, server %d.%d less than expected %d.%d",
            major_version, minor_version,
            SERVER_MAJOR_VERSION, SERVER_MINOR_VERSION);
}


/* Returns configured decimation factor. */
static unsigned int get_decimation(void)
{
    switch (data_format)
    {
        case DATA_DD:   return first_decimation * second_decimation;
        case DATA_D:    return first_decimation;
        case DATA_FA:   return 1;
        default:        return 0;   // Not going to happen
    }
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Argument parsing. */


static void usage(char *argv0)
{
    printf(
"Usage: %s [options] <capture-mask> [<samples>]\n"
"\n"
"Captures sniffer frames from the FA archiver, either reading historical data\n"
"(if -b, -s or -t is given) or live continuous data (if -C is specified).\n"
"\n"
"<capture-mask> specifies precisely which BPM ids will be captured.\n"
"The mask is specified as a comma separated sequence of ranges or BPM ids\n"
"where a range is two BPM ids separated by a hyphen, ie:\n"
"    mask = id [ \"-\" id ] [ \",\" mask ]\n"
"For example, 1-168 specifies all arc BPMs.\n"
"\n"
"<samples> specifies how many samples will be captured or the sample time in\n"
"seconds (if the number ends in s).  This must be specified when reading\n"
"historical data (-b, -s or -t) unless a range of times has been specified\n"
"with these options.  If <samples> is not specified with continuous capture\n"
"(-C) capture must be interrupted with ctrl-C.\n"
"\n"
"If historical data is wanted one of the following must be specified:\n"
"   -s:  Specify start, as a date and time in ISO 8601 date time format (with\n"
"        fractional seconds allowed).  Use a trailing Z for UTC time.\n"
"   -t:  Specify start as a time of day today, or yesterday if Y added to\n"
"        the end, in format hh:mm:ss[Y], interpreted as a local time.\n"
"   -b:  Specify start as a time in the past as hh:mm:ss\n"
"For each of these flags a range of times separated by ~ can be specified\n"
"instead of giving a sample count.\n"
"\n"
"Alternatively, continuous capture of live data can be specified:\n"
"   -C   Request continuous capture from live data stream\n"
"\n"
"The following options can be given:\n"
"\n"
"   -o:  Save output to specified file, otherwise stream to stdout\n"
"   -f:  Specify data format, can be -fF for FA (the default), -fd[mask] for\n"
"        single decimated data, or -fD[mask] for double decimated data, where\n"
"        [mask] is an optional data mask, default value 15 (all fields).\n"
"        Decimated data is only available for archived data.\n"
"           The bits in the data mask correspond to decimated fields:\n"
"            1 => mean, 2 => min, 4 => max, 8 => standard deviation\n"
"   -a   Capture all available data even if too much requested.  Otherwise\n"
"        capture fails if more data requested than present in archive.\n"
"   -R   Save in raw format, otherwise the data is saved in matlab format\n"
"   -c   Forbid any gaps in the captured sequence, contiguous data only\n"
"   -z   Check for gaps in ID0 data when checking for gaps, otherwise ignored\n"
"   -k   Keep extra dimensions in matlab values\n"
"   -n:  Specify name of data array (default is \"%s\")\n"
"   -S:  Specify archive server to read from (default is\n"
"            %s)\n"
"   -p:  Specify port to connect to on server (default is %d)\n"
"   -q   Suppress display of progress of capture on stderr\n"
"   -Z   Use UTC timestamps for matlab timestamps, otherwise local time is\n"
"        used including any local daylight saving offset.\n"
"   -d   Subtract the day from the matlab timestamp vector.\n"
"   -T   Save \"id0\" communication controller timestamp information.\n"
"\n"
"Note that if matlab format is specified and no sample count is specified\n"
"(interrupted continuous capture or range of times given) then output must be\n"
"directed to a file, otherwise the capture count in the result will be\n"
"invalid.\n"
    , argv0, data_name, server_name, port);
}


/* Computes the offset from local time to UTC.  This is needed to fix up matlab
 * timestamps. */
static time_t local_time_offset(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    return timegm(&tm) - now;
}


/* Returns seconds at midnight this morning for time of day relative timestamp
 * specification.  This uses the current timezone. */
static time_t midnight_today(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_sec = 0;
    tm.tm_min = 0;
    tm.tm_hour = 0;
    time_t midnight;
    ASSERT_IO(midnight = mktime(&tm));
    return midnight;
}


/* Parses time in hh:mm:ss[Y] format representing time today or yesterday. */
static bool parse_today(const char **string, struct timespec *ts)
{
    return
        parse_time(string, ts)  &&
        DO_(ts->tv_sec += midnight_today())  &&
        IF_(read_char(string, 'Y'),
            DO_(ts->tv_sec -= 24 * 3600));
}


#define COMPARE(x, y) ((x) < (y) ? -1 : (x) == (y) ? 0 : 1)

/* Returns +ve number if ts1 > ts2, -ve if ts1 < ts2, 0 if ts1 == ts2. */
static int compare_ts(struct timespec *ts1, struct timespec *ts2)
{
    if (ts1->tv_sec == ts2->tv_sec)
        return COMPARE(ts1->tv_nsec, ts2->tv_nsec);
    else
        return COMPARE(ts1->tv_sec, ts2->tv_sec);
}


/* Parses data format description in format
 *  F | d[n] | D[n]
 * where n is a three bit data mask. */
static bool parse_data_format(const char **string, enum data_format *format)
{
    if (read_char(string, 'F'))
    {
        *format = DATA_FA;
        return true;
    }
    else
    {
        if (read_char(string, 'd'))
            *format = DATA_D;
        else if (read_char(string, 'D'))
            *format = DATA_DD;
        else
            return FAIL_("Invalid data format");

        if (**string == '\0')
        {
            data_mask = 15;         // Read all fields by default
            return true;
        }
        else
            return
                parse_uint(string, &data_mask)  &&
                TEST_OK_(0 < data_mask  &&  data_mask <= 15,
                    "Invalid data mask");
    }
}


/* Parses time of day in hh:mm:ss interpreted as time before now. */
static bool parse_before(const char **string, struct timespec *ts)
{
    return
        parse_time(string, ts)  &&
        DO_(ts->tv_sec = time(NULL) - ts->tv_sec);
}


/* Parses one or two timestamps of the same format possibly separated by ~. */
static bool parse_interval(
    const char **string,
    bool (*parser)(const char **string, struct timespec *ts))
{
    return
        parser(string, &start)  &&
        DO_(start_specified = true)  &&
        IF_(read_char(string, '~'),
            parser(string, &end)  &&
            DO_(end_specified = true));
}


/* Parse wrapper for parsing a timestamp or timestamp interval. */
static bool parse_start(
    bool (*parser)(const char **string, struct timespec *ts),
    const char *string)
{
    return
        TEST_OK_(!start_specified, "Start already specified")  &&
        DO_PARSE("start time", parse_interval, string, parser);
}


/* Parses command line switches, updates *argc, *argv to point first remaining
 * argument. */
static bool parse_opts(int *argc, char ***argv)
{
    char *argv0 = (*argv)[0];
    char *argv0slash = strrchr(argv0, '/');
    if (argv0slash != NULL)
        argv0 = argv0slash + 1;

    bool ok = true;
    while (ok)
    {
        switch (getopt(*argc, *argv, "+hRCo:aS:qckn:zZdTs:t:b:p:f:"))
        {
            case 'h':   usage(argv0);                               exit(0);
            case 'R':   matlab_format = false;                      break;
            case 'C':   continuous_capture = true;                  break;
            case 'o':   output_filename = optarg;                   break;
            case 'a':   all_data = true;                            break;
            case 'S':   server_name = optarg;                       break;
            case 'q':   show_progress = false;                      break;
            case 'c':   request_contiguous = true;                  break;
            case 'k':   squeeze_matlab = false;                     break;
            case 'n':   data_name = optarg;                         break;
            case 'z':   check_id0 = true;                           break;
            case 'Z':   offset_matlab_times = false;                break;
            case 'd':   subtract_day_zero = true;                   break;
            case 'T':   save_id0 = true;                            break;
            case 's':   ok = parse_start(parse_datetime, optarg);   break;
            case 't':   ok = parse_start(parse_today, optarg);      break;
            case 'b':   ok = parse_start(parse_before, optarg);     break;
            case 'p':
                ok = DO_PARSE("server port", parse_int, optarg, &port);
                break;
            case 'f':
                ok = DO_PARSE("data format",
                    parse_data_format, optarg, &data_format);
                break;
            default:
                fprintf(stderr, "Try `capture -h` for usage\n");
                return false;
            case -1:
                *argc -= optind;
                *argv += optind;
                return true;
        }
    }
    return false;
}


/* Parses a sample count as either a number of samples or a duration in seconds.
 * Must be called after decimation parameters have been read from server. */
static bool parse_samples(const char **string, uint64_t *result)
{
    bool ok = parse_uint64(string, result);
    if (ok)
    {
        double duration = (double) *result; // In case it's a duration after all
        bool seconds = **string == '.';
        if (seconds)
        {
            long nsec;
            ok = parse_nanoseconds(string, &nsec)  &&  parse_char(string, 's');
            duration += 1e-9 * (double) nsec;
        }
        else
            seconds = read_char(string, 's');
        if (ok  &&  seconds)
            *result = (uint64_t) round(
                duration * sample_frequency / get_decimation());
    }

    return ok  &&  TEST_OK_(*result > 0, "Zero sample count");
}


/* Parses all command line arguments. */
static bool parse_args(int argc, char **argv)
{
    return
        parse_opts(&argc, &argv)  &&
        TEST_OK_(argc == 1  ||  argc == 2,
            "Wrong number of arguments.  Try `capture -h` for help.")  &&
        /* Note that we have to interrogate the archive parameters after parsing
         * the server settings, but before we parse the sample count or capture
         * mask, because these use the settings we read. */
        read_archive_parameters()  &&
        DO_PARSE("capture mask",
            parse_mask, argv[0], fa_entry_count, &capture_mask)  &&
        IF_(argc == 2,
            DO_PARSE("sample count", parse_samples, argv[1], &sample_count));
}


/* Performs final sanity checking on all parsed arguments. */
static bool validate_args(void)
{
    return
        TEST_OK_(continuous_capture || start_specified,
            "Must specify a start date or continuous capture")  &&
        TEST_OK_(!continuous_capture || !start_specified,
            "Cannot combine continuous and archive capture")  &&
        TEST_OK_(continuous_capture  ||  end_specified  ||  sample_count > 0,
            "Must specify sample count or end for historical data")  &&
        TEST_OK_(!continuous_capture  ||  !request_contiguous,
            "Gap checking not meaningful for subscription data")  &&
        TEST_OK_(sample_count == 0  ||  !end_specified,
            "Cannot specify both sample count and data end point")  &&
        TEST_OK_(!end_specified  ||  compare_ts(&start, &end) < 0,
            "End time isn't after start")  &&
        TEST_OK_(start_specified  ||  data_format == DATA_FA,
            "Decimated data must be historical")  &&
        TEST_OK_(!matlab_format  ||  sample_count <= UINT32_MAX,
            "Too many samples for matlab format capture")  &&
        TEST_OK_(matlab_format  ||  !save_id0,
            "Can only capture ID0 in matlab format")  &&
        TEST_OK_(request_contiguous  ||  !check_id0,
            "ID0 checking only meaningful with gap checking");
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Common data capture */

/* Timestamp capture. */
static struct extended_timestamp_header timestamp_header;
static struct extended_timestamp_id0 *timestamps_array = NULL;
static size_t timestamps_array_size = 0; // Current size of timestamps array
static size_t timestamps_count = 0;     // Current number of captured timestamps


/* Capture of data can be interrupted by Ctrl-C.  This is simply implemented by
 * resetting the running flag which is polled during capture. */
static volatile bool running = true;

static void interrupt_capture(int signum)
{
    running = false;
}

static bool initialise_signal(void)
{
    struct sigaction interrupt = {
        .sa_handler = interrupt_capture, .sa_flags = 0 };
    return
        TEST_IO(sigfillset(&interrupt.sa_mask))  &&
        TEST_IO(sigaction(SIGINT,  &interrupt, NULL))  &&
        TEST_IO(signal(SIGPIPE, SIG_IGN));
}


/* Formats data request options for archive data request.  See reader.c for
 * definitions of these options. */
static void format_read_options(char *options)
{
    if (true)               *options++ = 'N';   // Send sample count
    if (all_data)           *options++ = 'A';   // Send all available data
    if (matlab_format)      *options++ = 'T';   // Send timestamps
    if (matlab_format)      *options++ = 'E';   //  in extended format
    if (matlab_format  &&  save_id0)
                            *options++ = 'Z';   //  with id0 values
    if (request_contiguous) *options++ = 'C';   // Ensure no gaps in data
    if (request_contiguous  &&  check_id0)
                            *options++ = 'Z';   // Include ID0 in gap check
    *options = '\0';
}

/* Formats data request options for subscription data request.  See subscribe.c
 * for definitions of these options. */
static void format_subscribe_options(char *options)
{
    if (matlab_format)      *options++ = 'T';   // Timestamps
    if (matlab_format)      *options++ = 'E';   //  in extended format
    if (matlab_format  &&  save_id0)
                            *options++ = 'Z';   //  with id0 values
    *options = '\0';
}


/* Sends request for archived or live data to archiver. */
static bool request_data(FILE *stream)
{
    char raw_mask[RAW_MASK_BYTES];
    format_mask(&capture_mask, fa_entry_count, raw_mask);
    if (continuous_capture)
    {
        char options[64];
        format_subscribe_options(options);
        return TEST_OK(fprintf(stream, "S%s%s\n", raw_mask, options) > 0);
    }
    else
    {
        char format[16];
        switch (data_format)
        {
            case DATA_FA:   sprintf(format, "F");                   break;
            case DATA_D:    sprintf(format, "DF%u",  data_mask);    break;
            case DATA_DD:   sprintf(format, "DDF%u", data_mask);    break;
        }
        char end_str[64];
        if (end_specified)
            sprintf(end_str, "ES%ld.%09ld", end.tv_sec, end.tv_nsec);
        else
            sprintf(end_str, "N%"PRIu64, sample_count);
        char options[64];
        format_read_options(options);
        // Send R<source> M<mask> S<start> <end> <options>
        return TEST_OK(fprintf(stream, "R%sM%sS%ld.%09ld%s%s\n",
            format, raw_mask, start.tv_sec, start.tv_nsec,
            end_str, options) > 0);
    }
}


/* If the request was accepted the first byte of the response is a null
 * character, otherwise the entire response is an error message. */
static bool check_response(FILE *stream)
{
    char response[1024];
    size_t rx = fread(response, 1, 1, stream);
    if (rx != 1)
        return FAIL_("Unexpected server disconnect");
    else if (*response == '\0')
        return true;
    else
    {
        /* Pass entire error response from server to stderr. */
        if (read_response(stream, response + 1, sizeof(response) - 1))
            fprintf(stderr, "%s", response);
        return false;
    }
}


/* Show progress of capture on stderr. */
static void update_progress(uint64_t frames_written, size_t frame_size)
{
    const char *progress = "|/-\\";
    static uint64_t last_update = 0;
    uint64_t bytes_written = frame_size * frames_written;
    if (bytes_written >= last_update + PROGRESS_INTERVAL)
    {
        fprintf(stderr, "%c %9"PRIu64,
            progress[(bytes_written / PROGRESS_INTERVAL) % 4], frames_written);
        if (sample_count > 0)
            fprintf(stderr, " (%5.2f%%)",
                100.0 * (double) frames_written / (double) sample_count);
        fprintf(stderr, "\r");
        fflush(stderr);
        last_update = bytes_written;
    }
}

/* Erases residue of progress marker on command line. */
static void reset_progress(void)
{
    char spaces[40];
    memset(spaces, ' ', sizeof(spaces));
    fprintf(stderr, "%.*s\r", (int) sizeof(spaces), spaces);
}


/* Ensures there's enough room to record one more timestamp. */
static struct extended_timestamp_id0 *get_timestamp_block(void)
{
    if (timestamps_count >= timestamps_array_size)
    {
        if (timestamps_array == NULL)
            /* Initial size of 1024 blocks seems fair. */
            timestamps_array_size = 1024;
        else
            /* Resize array along an approximate Fibbonacci sequence. */
            timestamps_array_size =
                timestamps_array_size + timestamps_array_size / 2;
        timestamps_array = realloc(timestamps_array,
            timestamps_array_size * sizeof(struct extended_timestamp_id0));
    }
    return &timestamps_array[timestamps_count];
}


/* Reads the appropriate size of block from input. */
static bool read_timestamp_block(FILE *stream)
{
    struct extended_timestamp_id0 *block = get_timestamp_block();
    if (save_id0)
        return READ_ITEM(stream, *block);
    else
        return READ_ITEM(stream, *(struct extended_timestamp *) block);
}


/* This routine reads data from stream and writes out complete frames until
 * either the sample count is reached or the read is interrupted. */
static bool capture_data(FILE *stream, uint64_t *frames_written)
{
    /* Size of line of data. */
    size_t line_size =
        count_data_bits(data_mask) *
        count_mask_bits(&capture_mask, fa_entry_count) * FA_ENTRY_SIZE;

    /* If matlab_format is set we need to read timestamp frames interleaved
     * in the data stream.  These two variables keep track of this. */
    unsigned int timestamp_offset = timestamp_header.offset;
    size_t lines_to_timestamp = 0;

    /* Track state and number of frames written. */
    *frames_written = 0;
    bool ok = true;

    /* Read until write error, interruption, end of input, or requested number
     * of frames has been captured. */
    do {
        /* Read the extended timestamp if appropriate. */
        if (matlab_format  &&  lines_to_timestamp == 0)
        {
            if (!read_timestamp_block(stream))
                break;      // End of input

            timestamps_count += 1;
            lines_to_timestamp = timestamp_header.block_size - timestamp_offset;
            timestamp_offset = 0;
        }

        /* Figure out how many lines we can read in one chunk.  Can't read past
         * the next timestamp, can't read more than a buffer full, can't be more
         * than we're waiting for. */
        size_t lines_to_read = BUFFER_SIZE / line_size;
        if (matlab_format  &&  lines_to_read > lines_to_timestamp)
            lines_to_read = lines_to_timestamp;
        if (sample_count > 0  &&
                lines_to_read > sample_count - *frames_written)
            lines_to_read = (size_t) (sample_count - *frames_written);

        /* Read lines of data. */
        char buffer[BUFFER_SIZE];
        size_t lines_read = fread(buffer, line_size, lines_to_read, stream);
        if (lines_read == 0)
            break;          // End of input
        lines_to_timestamp -= lines_read;

        /* Ship out lines as read. */
        ok = TEST_OK(
            fwrite(buffer, line_size, lines_read, output_file) == lines_read);
        *frames_written += lines_read;

        if (show_progress)
            update_progress(*frames_written, line_size);
    } while (ok  &&  running  &&
        (sample_count == 0  ||  *frames_written < sample_count));

    if (show_progress)
        reset_progress();
    return ok;
}


/* Coordination of raw data capture. */
static bool capture_raw_data(FILE *stream)
{
    uint64_t frames_written;
    return
        capture_data(stream, &frames_written)  &&
        TEST_OK_(continuous_capture || frames_written == sample_count,
            "Only captured %"PRIu64" of %"PRIu64" frames",
            frames_written, sample_count);
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Matlab data capture */



/* Writes complete matlab header including size of captured data and auxilliary
 * data.  May be called twice if data size changes. */
static bool write_header(uint32_t frames_written)
{
    bool squeeze[4] = {
        false,                                      // X, Y
        data_format == DATA_FA || squeeze_matlab,   // Decimated subfield
        squeeze_matlab,                             // BPM ID
        false                                       // Sample number
    };
    uint32_t decimation = get_decimation();
    double frequency = sample_frequency / decimation;

    DECLARE_MATLAB_BUFFER(header, 4096);
    prepare_matlab_header(&header);

    /* Write out the decimation and sample frequency and timestamp. */
    place_matlab_value(&header, "decimation", miINT32, &decimation);
    place_matlab_value(&header, "f_s",        miDOUBLE, &frequency);

    /* Write out the index array tying data back to original BPM ids. */
    uint16_t mask_ids[fa_entry_count];
    unsigned int mask_length =
        compute_mask_ids(mask_ids, &capture_mask, fa_entry_count);
    place_matlab_vector(&header, "ids", miUINT16, mask_ids, mask_length);

    /* Finally write out the matrix mat_header for the fa data. */
    unsigned int field_count = count_data_bits(data_mask);
    unsigned int padding = place_matrix_header(&header, data_name,
        miINT32, squeeze,
        4, 2, field_count, mask_length, frames_written);
    ASSERT_OK(padding == 0);            // Minimum element size is 8 bytes

    return write_matlab_buffer(output_file, &header);
}


/* Support for converting and writing the blocks of footer data derived from
 * captured timestamp information. */
static bool buffered_convert_write(
    unsigned int padding, unsigned int frames_written, size_t element_size,
    void (*convert)(struct extended_timestamp_id0 *timestamps, void *result))
{
    struct extended_timestamp_id0 *timestamps = timestamps_array;
    size_t offset = timestamp_header.offset;
    size_t block_size = timestamp_header.block_size;

    bool ok = true;
    for (size_t written = 0; ok  &&  written < frames_written; )
    {
        /* Convert timestamps in blocks corresponding to original data. */
        char buffer[block_size * element_size];
        convert(timestamps, buffer);

        /* Write out as much of the current block as wanted. */
        size_t to_write = block_size - offset;
        if (to_write > frames_written - written)
            to_write = frames_written - written;
        ok = TEST_OK(fwrite(
            buffer + offset * element_size,
            element_size, to_write, output_file) == to_write);

        written += to_write;
        timestamps ++;
        offset = 0;
    }

    /* Write out any padding needed to ensure the data size is a multiple of 8
     * bytes. */
    if (ok  &&  padding > 0)
    {
        char buffer[padding];
        memset(buffer, 0, padding);
        ok = TEST_OK(fwrite(buffer, 1, padding, output_file) == padding);
    }
    return ok;
}


/* Timestamp to add to or subtract from server timestamps. */
static uint64_t timestamp_offset;

/* Computes a block of timestamps for a single data block. */
static void convert_timestamps(
    struct extended_timestamp_id0 *timestamps, void *buffer)
{
    unsigned int block_size = timestamp_header.block_size;
    double scaling = 1e-6 / SECS_PER_DAY;
    double increment = (scaling * timestamps->duration) / block_size;
    double timestamp =
        scaling * (double) (timestamps->timestamp + timestamp_offset);
    double delta = 0.0;     // Separate accumulator to improve precision

    double *result = buffer;
    for (unsigned int i = 0; i < block_size; i ++)
    {
        result[i] = timestamp + delta;
        delta += increment;
    }
}

static bool write_timestamps(unsigned int frames_written, time_t local_offset)
{
    /* Prepare timestamp conversion.  First we need to compute the starting
     * timestamp and day_zero and then from this the timestamp offset used in
     * the conversion above. */

    /* Matlab epoch in archive units, taking the local offset into account. */
    timestamp_offset = (uint64_t) 1000000 *
        ((uint64_t) local_offset + (uint64_t) SECS_PER_DAY * MATLAB_EPOCH);
    /* Timestamp of first point in captured data in archive epoch. */
    uint64_t start_ts =
        timestamps_array[0].timestamp +
        (uint64_t) timestamps_array[0].duration * timestamp_header.offset /
            timestamp_header.block_size;
    /* Can now compute timestamp and day */
    double timestamp =
        1e-6 / SECS_PER_DAY * (double) (start_ts + timestamp_offset);
    double day_zero = floor(timestamp);

    if (subtract_day_zero)
        timestamp_offset -= (uint64_t) (1e6 * SECS_PER_DAY * day_zero);

    /* Output the matlab values. */
    DECLARE_MATLAB_BUFFER(header, 512); // Just need space for vector heading
    place_matlab_value(&header, "timestamp", miDOUBLE, &timestamp);
    place_matlab_value(&header, "day", miDOUBLE, &day_zero);
    unsigned int padding = place_matrix_header(
        &header, "t", miDOUBLE, NULL, 2, 1, frames_written);

    return
        write_matlab_buffer(output_file, &header)  &&
        buffered_convert_write(
            padding, frames_written, sizeof(double), convert_timestamps);
}


static void convert_id0(struct extended_timestamp_id0 *timestamps, void *buffer)
{
    uint32_t *result = buffer;
    uint32_t id_zero = timestamps->id_zero;
    unsigned int decimation = get_decimation();
    for (unsigned int i = 0; i < timestamp_header.block_size; i ++)
    {
        result[i] = id_zero;
        id_zero += decimation;
    }
}

static bool write_id0(unsigned int frames_written)
{
    DECLARE_MATLAB_BUFFER(header, 512); // Just need space for vector heading
    unsigned int padding = place_matrix_header(
        &header, "id0", miINT32, NULL, 2, 1, frames_written);
    return
        write_matlab_buffer(output_file, &header)  &&
        buffered_convert_write(
            padding, frames_written, sizeof(uint32_t), convert_id0);
}


/* The matlab footer includes the timestamp information and optionally the id0
 * information, both derived from the timestamps blocks captured during data
 * transfer. */
static bool write_footer(unsigned int frames_written, time_t local_offset)
{
    return
        write_timestamps(frames_written, local_offset)  &&
        IF_(save_id0, write_id0(frames_written));
}


/* Coordination of matlab data capture. */
static bool capture_matlab_data(FILE *stream)
{
    uint64_t frames_written;
    time_t local_offset = offset_matlab_times ? local_time_offset() : 0;
    return
        TEST_OK(READ_ITEM(stream, timestamp_header))  &&
        TEST_OK_(timestamp_header.offset < timestamp_header.block_size,
            "Invalid response from server")  &&

        write_header((uint32_t) sample_count)  &&
        capture_data(stream, &frames_written)  &&
        write_footer((uint32_t) frames_written, local_offset)  &&

        IF_(frames_written != sample_count,
            /* For an incomplete capture, probably an interrupted continuous
             * capture, we need to rewrite the header with the correct
             * capture count. */
            TEST_IO_(fseek(output_file, 0, SEEK_SET),
                "Cannot update matlab file, file not seekable")  &&
            write_header((uint32_t) frames_written));
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Top level control */


/* Captures open data stream to configured output file. */
static bool capture_and_save(FILE *stream)
{
    return
        IF_(!continuous_capture,
            TEST_OK(READ_ITEM(stream, sample_count)))  &&
        IF_ELSE(matlab_format,
            capture_matlab_data(stream),
            capture_raw_data(stream));
}


/* Captures requested data from archiver and saves to file. */
int main(int argc, char **argv)
{
    char *server = getenv("FA_ARCHIVE_SERVER");
    if (server != NULL)
        server_name = server;

    output_file = stdout;
    FILE *stream;
    bool ok =
        parse_args(argc, argv)  &&
        validate_args()  &&

        connect_server(&stream)  &&
        IF_(output_filename != NULL,
            TEST_NULL_(
                output_file = fopen(output_filename, "w"),
                "Unable to open output file \"%s\"", output_filename))  &&
        request_data(stream)  &&
        check_response(stream)  &&

        initialise_signal()  &&
        capture_and_save(stream);
    return ok ? 0 : 1;
}
