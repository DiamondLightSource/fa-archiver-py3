/* Simple server for archive data.
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
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#include "error.h"
#include "fa_sniffer.h"
#include "archiver.h"
#include "mask.h"
#include "buffer.h"
#include "reader.h"
#include "parse.h"
#include "disk.h"
#include "transform.h"
#include "decimate.h"
#include "sniffer.h"
#include "locking.h"
#include "list.h"
#include "disk_writer.h"

#include "socket_server.h"


/* String used to report protocol version in response to CV command. */
#define PROTOCOL_VERSION    "1.1"


/* Block buffer for full resolution FA data. */
static struct buffer *fa_block_buffer;
/* Block buffer for decimated FA data. */
static struct buffer *decimated_buffer;

/* If set the debug commands are enabled.  These are normally only enabled for
 * debugging, as we don't want to allow external users to normally have this
 * control over the server. */
static bool debug_commands;



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Client list management. */


DECLARE_LOCKING(client_lock);
/* List of all connected clients, updated as clients connect and disconnect. */
static LIST_HEAD(client_list);

struct client_info
{
    struct list_head list;
    struct timespec ts;             // Time client connection completed
    char name[64];                  // Socket address of client
    char buf[256];                  // Command sent by client
};

/* Macro for walking lists of client_info structures. */
#define for_clients(cursor, clients) \
    list_for_each_entry(struct client_info, list, client, clients)

/* Adds newly connected client to list of connections. */
static struct client_info *add_client(void)
{
    struct client_info *client = calloc(1, sizeof(struct client_info));
    clock_gettime(CLOCK_REALTIME, &client->ts);
    LOCK(client_lock);
    list_add(&client->list, &client_list);
    UNLOCK(client_lock);
    return client;
}

/* Removes departing client from connection list. */
static void remove_client(struct client_info *client)
{
    LOCK(client_lock);
    list_del(&client->list);
    UNLOCK(client_lock);
    free(client);
}

/* Grabs a snapshot of the client list.  To avoid complications with
 * synchronisation the entire client list is copied wholesale. */
static void copy_clients(struct list_head *clients)
{
    LOCK(client_lock);
    for_clients(client, &client_list)
    {
        struct client_info *copy = malloc(sizeof(struct client_info));
        memcpy(copy, client, sizeof(struct client_info));
        list_add(&copy->list, clients);
    }
    UNLOCK(client_lock);
}

/* Releases a client list previously created by copy_clients(). */
static void delete_clients(struct list_head *clients)
{
    while (clients->next != clients)
    {
        struct client_info *client =
            container_of(clients->next, struct client_info, list);
        list_del(&client->list);
        free(client);
    }
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Socket server commands. */

static bool __attribute__((format(printf, 2, 3)))
    write_string(int sock, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char *buffer = NULL;
    bool ok =
        TEST_IO(vasprintf(&buffer, format, args))  &&
        TEST_write_(
            sock, buffer, strlen(buffer), "Unable to write response");
    free(buffer);
    return ok;
}


/* Logs error message and reports error to client. */
static bool report_error(
    int scon, const char *client_name, const char *error_message)
{
    log_message("Client %s sent error: %s", client_name, error_message);
    return write_string(scon, "%s\n", error_message);
}


/* Called if command not recognised. */
static bool process_error(int scon, const char *client_name, const char *buf)
{
    return report_error(scon, client_name, "Invalid command");
}


/* Sets socket receive timeout.  Used so we don't have threads hanging waiting
 * for users to complete sending their commands.  (Also so I can remember how to
 * do this!)  See socket(7) for documentatino of SO_RCVTIMEO option. */
static bool set_socket_timeout(int sock, int secs, int usecs)
{
    struct timeval timeout = { .tv_sec = secs, .tv_usec = usecs };
    return TEST_IO(setsockopt(
        sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
}

/* Using cork should be harmless and should increase write efficiency. */
static bool set_socket_cork(int sock, bool cork)
{
    int _cork = cork;       // In case sizeof(bool) isn't sizeof(int)
    return TEST_IO(setsockopt(sock, SOL_TCP, TCP_CORK, &_cork, sizeof(_cork)));
}


/* This macro calls action1 with error handling pushed, and if it succeeds
 * action2 is performed, otherwise the error message is sent to scon. */
#define CATCH_ERROR(scon, client_name, action1, action2) \
    ( { \
        push_error_handling(); \
        char *message = pop_error_handling(!(action1)); \
        bool __ok = IF_ELSE(message == NULL, \
            (action2), \
            report_error(scon, client_name, message)); \
        free(message); \
        __ok; \
    } )



/* These commands are only available if enabled on the command line.
 * The following commands are supported:
 *
 *  Q   Closes archive server
 *  H   Halts data capture (only sensible for debug use)
 *  R   Resumes halted data capture
 *  I   Interrupts data capture (by sending halt command to hardware)
 *  D   Disables writing to disk, leaves all other functions running
 *  E   Reenables writing to disk.
 *  S   Returns current data capture enable flags as a pair of numbers:
 *      capture_enable      0 => Data capture blocked by DH command
 *      disk_enable         0 => Writing to disk blocked by DD command
 */
static bool process_debug_command(
    int scon, const char *client_name, const char *buf)
{
    if (!debug_commands)
        return process_error(scon, client_name, buf);

    bool ok = true;
    for (buf ++; ok  &&  *buf != '\0'; buf ++)
    {
        switch (*buf)
        {
            case 'Q':
                log_message("Shutdown command received");
                ok = write_string(scon, "Shutdown\n");
                shutdown_archiver();
                break;
            case 'H':
                log_message("Temporary halt command received");
                ok = write_string(scon, "Halted\n");
                enable_buffer_write(fa_block_buffer, false);
                break;
            case 'R':
                log_message("Resume command received");
                enable_buffer_write(fa_block_buffer, true);
                break;
            case 'I':
                log_message("Interrupt command received");
                ok = CATCH_ERROR(scon, client_name, interrupt_sniffer(), true);
                break;
            case 'D':
                log_message("Disabling writing to disk");
                enable_disk_writer(false);
                break;
            case 'E':
                log_message("Enabling writing to disk");
                enable_disk_writer(true);
                break;
            case 'S':
                ok = write_string(scon, "%d %d\n",
                    buffer_write_enabled(fa_block_buffer),
                    disk_writer_enabled());
                break;

            default:
                ok = report_error(scon, client_name, "Unknown command");
                break;
        }
    }
    return ok;
}


static double get_mean_frame_rate(void)
{
    const struct disk_header *header = get_header();
    return (1e6 * header->major_sample_count) / header->last_duration;
}


/* Reports list of all currently connected clients. */
static bool report_clients(int scon)
{
    /* Walking the list of clients is a bit of a challenge: it's changing under
     * our feet, and we can't hold it locked as we walk it, as sending our
     * report can take time.  Instead for simplicitly we grab a copy of the list
     * under the lock. */
    LIST_HEAD(clients);
    copy_clients(&clients);

    bool ok = true;
    for_clients(client, &clients)
    {
        struct tm tm;
        ok =
            TEST_NULL(gmtime_r(&client->ts.tv_sec, &tm))  &&
            write_string(scon,
                "%4d-%02d-%02dT%02d:%02d:%02d.%03ldZ %s: %s\n",
                tm.tm_year + 1900, tm.tm_mon, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec,
                client->ts.tv_nsec / 1000000, client->name, client->buf);
        if (!ok)
            break;
    }

    delete_clients(&clients);
    return ok;
}


/* The C command prefix is followed by a sequence of one letter commands, and
 * each letter receives a one line response (except for the I command).  The
 * following commands are supported:
 *
 *  F   Returns current sample frequency
 *  d   Returns first decimation
 *  D   Returns second decimation
 *  T   Returns earliest available timestamp
 *  V   Returns protocol identification string
 *  M   Returns configured capture mask
 *  C   Returns live decimation factor if available
 *  S   Returns detailed sniffer status.  The numbers returned are:
 *          hardware link status        1 => ok, 2, 3 => link fault
 *          link partner                or 1023 if no connection
 *          last interrupt code         1 => running normally
 *          frame error count
 *          soft error count
 *          hard error count
 *          run state                   1 => Currently fetching data
 *          overrun                     1 => Halted due to buffer overrun
 *  I   Returns list of all conected clients, one client per line.
 */
static bool process_command(int scon, const char *client_name, const char *buf)
{
    const struct disk_header *header = get_header();
    bool ok = true;
    for (buf ++; ok  &&  *buf != '\0'; buf ++)
    {
        switch (*buf)
        {
            case 'F':
                ok = write_string(scon, "%f\n", get_mean_frame_rate());
                break;
            case 'd':
                ok = write_string(scon,
                     "%"PRIu32"\n", 1 << header->first_decimation_log2);
                break;
            case 'D':
                ok = write_string(scon,
                     "%"PRIu32"\n", 1 << header->second_decimation_log2);
                break;
            case 'T':
            {
                uint64_t start = get_earliest_timestamp();
                ok = write_string(scon, "%"PRIu64".%06"PRIu64"\n",
                    start / 1000000, start % 1000000);
                break;
            }
            case 'V':
                ok = write_string(scon, PROTOCOL_VERSION "\n");
                break;
            case 'M':
            {
                char string[RAW_MASK_BYTES + 1];
                format_raw_mask(&get_header()->archive_mask, string);
                ok = write_string(scon, "%s\n", string);
                break;
            }

            case 'C':
                ok = write_string(scon, "%d\n", get_decimation_factor());
                break;

            case 'S':
            {
                struct fa_status status;
                ok = CATCH_ERROR(scon, client_name,
                    get_sniffer_status(&status),
                    write_string(scon, "%u %u %u %u %u %u %u %u\n",
                        status.status, status.partner,
                        status.last_interrupt, status.frame_errors,
                        status.soft_errors, status.hard_errors,
                        status.running, status.overrun));
                break;
            }

            case 'I':
                ok = report_clients(scon);
                break;

            default:
                ok = report_error(scon, client_name, "Unknown command");
                break;
        }
    }
    return ok;
}


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
 * If both T and Z are specified then the timestamp is sent first before T0. */
static bool parse_subscription(
    const char **string, struct subscribe_parse *parse)
{
    return
        parse_char(string, 'S')  &&
        parse_mask(string, &parse->mask)  &&
        parse_options(string, parse);
}


bool report_socket_error(int scon, const char *client_name, bool ok)
{
    if (ok)
    {
        /* If all is well write a single null to let the caller know to expect a
         * normal response to follow. */
        pop_error_handling(false);
        char nul = '\0';
        return TEST_write(scon, &nul, 1);
    }
    else
    {
        /* If an error is encountered write the error message to the socket. */
        char *error_message = pop_error_handling(true);
        bool write_ok = report_error(scon, client_name, error_message);
        free(error_message);
        return write_ok;
    }
}


static bool send_extended_header(int scon, size_t block_size)
{
    struct extended_timestamp_header header = {
        .block_size = block_size,
        .offset = 0 };
    return TEST_write(scon, &header, sizeof(header));
}


static bool send_extended_timestamp(
    int scon, size_t block_size, uint64_t timestamp)
{
    /* This duration calculation is why we can't do decimated data with extended
     * timestamps -- just don't have the value to deliver!  Actually, it's not a
     * great match to the data anyway, but that's another problem... */
    const struct disk_header *header = get_header();
    uint32_t duration =
        header->last_duration / (header->major_sample_count / block_size);

    struct extended_timestamp extended_timestamp = {
        .timestamp = timestamp - duration,  // timestamp is after *last* point
        .duration = duration };
    return TEST_write_(
        scon, &extended_timestamp, sizeof(extended_timestamp),
        "Unable to write timestamp");
}


static bool send_subscription(
    int scon, struct reader_state *reader, uint64_t timestamp,
    struct subscribe_parse *parse, const void **block)
{
    /* The transmitted block optionally begins with the timestamp and T0 values,
     * in that order, if requested. */
    size_t block_size = reader_block_size(reader) / FA_FRAME_SIZE;
    bool ok =
        IF_(parse->send_timestamp == SEND_BASIC,
            TEST_write(scon, &timestamp, sizeof(uint64_t)))  &&
        IF_(parse->want_t0,
            TEST_write(scon, *block, sizeof(uint32_t)))  &&
        IF_(parse->uncork,
            set_socket_cork(scon, false))  &&
        IF_(parse->send_timestamp == SEND_EXTENDED,
            send_extended_header(scon, block_size));

    while (ok)
    {
        ok = FINALLY(
            IF_(parse->send_timestamp == SEND_EXTENDED,
                send_extended_timestamp(scon, block_size, timestamp))  &&
            write_frames(scon, &parse->mask, *block, block_size),

            // Always do this, even if write_frames fails.
            TEST_OK_(release_read_block(reader),
                "Write underrun to client"));
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
static bool process_subscribe(
    int scon, const char *client_name, const char *buf)
{
    push_error_handling();

    /* Parse the incoming request. */
    struct subscribe_parse parse;
    if (!DO_PARSE("subscription", parse_subscription, buf, &parse))
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
        ok = send_subscription(scon, reader, timestamp, &parse, &block);

    /* Clean up resources.  Rather important to get this right, as this can
     * happen many times. */
    if (block != NULL)
        release_read_block(reader);
    close_reader(reader);

    return ok;
}


static const struct command_table {
    char id;            // Identification character
    bool (*process)(int scon, const char *client_name, const char *buf);
} command_table[] = {
    { 'C', process_command },
    { 'R', process_read },
    { 'S', process_subscribe },
    { 'D', process_debug_command },
    { 0,   process_error }
};


/* Converts connected socket to a printable identification string. */
static void get_client_name(int scon, char *client_name)
{
    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    if (TEST_IO(getpeername(scon, (struct sockaddr *) &name, &namelen)))
    {
        /* Consider using inet_ntop() here.  However doesn't include the port
         * number, so probably not so interesting until IPv6 in use. */
        uint8_t *ip = (uint8_t *) &name.sin_addr.s_addr;
        sprintf(client_name, "%u.%u.%u.%u:%u",
            ip[0], ip[1], ip[2], ip[3], ntohs(name.sin_port));
    }
    else
        sprintf(client_name, "unknown");
}


/* Reads from the given socket until one of the following is encountered: a
 * newline (the preferred case), end of input, end of buffer or an error.  The
 * newline and anything following is discarded. */
static bool read_line(int sock, struct client_info *client)
{
    char *buf = client->buf;
    size_t buflen = sizeof(client->buf) - 1;    // Allow for '\0'
    ssize_t rx;
    while (
        TEST_OK_(buflen > 0, "Read buffer exhausted")  &&
        TEST_IO_(rx = read(sock, buf, buflen), "Socket read failed")  &&
        TEST_OK_(rx > 0, "End of file on input"))
    {
        char *newline = memchr(buf, '\n', rx);
        if (newline)
        {
            *newline = '\0';
            return true;
        }

        buflen -= rx;
        buf += rx;
    }
    /* On failure report what we managed to read before failing. */
    *buf = '\0';
    log_message("Client %s sent \"%s\"", client->name, client->buf);
    return false;
}


/* Command successfully read, dispatch it to the appropriate handler. */
static bool dispatch_command(
    int scon, const char *client_name, const char *buf)
{
    log_message("Client %s: \"%s\"", client_name, buf);
    const struct command_table *command = command_table;
    while (command->id  &&  command->id != buf[0])
        command += 1;
    return command->process(scon, client_name, buf);
}


static void *process_connection(void *context)
{
    int scon = (intptr_t) context;
    struct client_info *client = add_client();

    /* Retrieve client address so we can log all messages associated with this
     * client with the appropriate address. */
    get_client_name(scon, client->name);

    /* Read the command, required to be one line terminated by \n, and dispatch
     * to the appropriate handler.  Any errors are handled locally and are
     * reported below. */
    push_error_handling();
    bool ok = FINALLY(
        set_socket_cork(scon, true)  &&
        set_socket_timeout(scon, 1, 0)  &&      // 1 second rx timeout
        read_line(scon, client)  &&
        dispatch_command(scon, client->name, client->buf),

        // Always close the socket when done
        TEST_IO(close(scon)));

    /* Report any errors. */
    char *error_message = pop_error_handling(!ok);
    if (!ok)
        log_message("Client %s error: %s", client->name, error_message);
    free(error_message);
    remove_client(client);

    return NULL;
}


static void *run_server(void *context)
{
    int sock = (int)(intptr_t) context;
    /* Note that we need to create the spawned threads with DETACHED attribute,
     * otherwise we accumlate internal joinable state information and eventually
     * run out of resources. */
    pthread_attr_t attr;
    ASSERT_0(pthread_attr_init(&attr));
    ASSERT_0(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED));

    int scon;
    pthread_t thread;
    while (TEST_IO(scon = accept(sock, NULL, NULL)))
        IGNORE(TEST_0(pthread_create(&thread, &attr,
            process_connection, (void *)(intptr_t) scon)));
    return NULL;
}


static pthread_t server_thread;
static int server_socket;

bool initialise_server(
    struct buffer *fa_buffer, struct buffer *decimated, int port,
    bool extra, bool reuseaddr)
{
    fa_block_buffer = fa_buffer;
    decimated_buffer = decimated;
    debug_commands = extra;

    struct sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };
    int reuse = 1;
    return
        TEST_IO(server_socket = socket(AF_INET, SOCK_STREAM, 0))  &&
        IF_(reuseaddr,
            TEST_IO(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
                &reuse, sizeof(reuse))))  &&
        TEST_IO_(
            bind(server_socket, (struct sockaddr *) &sin, sizeof(sin)),
            "Unable to bind to server socket")  &&
        TEST_IO(listen(server_socket, 5))  &&
        DO_(log_message("Server listening on port %d", port));
}

bool start_server(void)
{
    return
        TEST_0(pthread_create(
            &server_thread, NULL, run_server,
            (void *) (intptr_t) server_socket));
}


void terminate_server(void)
{
    IGNORE(TEST_0(pthread_cancel(server_thread)));
    /* We probably need to kill all the client threads. */
}
