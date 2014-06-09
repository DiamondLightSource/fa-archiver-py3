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
#include "disk.h"
#include "transform.h"
#include "decimate.h"
#include "sniffer.h"
#include "locking.h"
#include "list.h"
#include "disk_writer.h"
#include "subscribe.h"

#include "socket_server.h"


/* String used to report protocol version in response to CV command. */
#define PROTOCOL_VERSION    "1.1"


/* Block buffer for full resolution FA data. */
static struct buffer *fa_block_buffer;

/* If set the debug commands are enabled.  These are normally only enabled for
 * debugging, as we don't want to allow external users to normally have this
 * control over the server. */
static bool debug_commands;

/* Just for reporting with the CE command. */
static unsigned int events_fa_id;

/* Name annouced to clients for C? command. */
static const char *server_name;



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
    log_message("Client %s error sent: %s", client_name, error_message);
    return write_string(scon, "%s\n", error_message);
}


/* Called if command not recognised. */
static bool process_error(int scon, const char *client_name, const char *buf)
{
    return report_error(scon, client_name, "Invalid command");
}


/* Sets socket receive and transmit timeouts.  Used so we don't have threads
 * hanging waiting for users to complete sending their commands and to receive
 * their data.  See socket(7) for documentation of SO_RCVTIMEO option. */
static bool set_socket_timeout(int sock, int rx_secs, int tx_secs)
{
    struct timeval rx_timeout = { .tv_sec = rx_secs, .tv_usec = 0 };
    struct timeval tx_timeout = { .tv_sec = tx_secs, .tv_usec = 0 };
    return
        TEST_IO(setsockopt(
            sock, SOL_SOCKET, SO_RCVTIMEO, &rx_timeout, sizeof(rx_timeout)))  &&
        TEST_IO(setsockopt(
            sock, SOL_SOCKET, SO_SNDTIMEO, &tx_timeout, sizeof(tx_timeout)));
}

/* Using cork should be harmless and should increase write efficiency. */
bool set_socket_cork(int sock, bool cork)
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
                shutdown_archiver();
                ok = write_string(scon, "Shutdown\n");
                break;
            case 'H':
                log_message("Temporary halt command received");
                enable_buffer_write(fa_block_buffer, false);
                ok = write_string(scon, "Halted\n");
                break;
            case 'R':
                log_message("Resume command received");
                enable_buffer_write(fa_block_buffer, true);
                ok = write_string(scon, "Resumed\n");
                break;
            case 'I':
                log_message("Interrupt command received");
                ok = CATCH_ERROR(scon, client_name,
                    interrupt_sniffer(), write_string(scon, "Interrupted\n"));
                break;
            case 'D':
                log_message("Disabling writing to disk");
                enable_disk_writer(false);
                ok = write_string(scon, "Disabled\n");
                break;
            case 'E':
                log_message("Enabling writing to disk");
                enable_disk_writer(true);
                ok = write_string(scon, "Enabled\n");
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
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec,
                client->ts.tv_nsec / 1000000, client->name, client->buf);
        if (!ok)
            break;
    }

    delete_clients(&clients);
    return ok;
}


static bool write_index_timestamp(int scon, uint64_t timestamp)
{
    timestamp = timestamp_to_index_ts(timestamp);
    return write_string(scon, "%"PRIu64".%06"PRIu64"\n",
        timestamp / 1000000, timestamp % 1000000);
}


static bool write_mask(int scon)
{
    const struct disk_header *header = get_header();
    char string[RAW_MASK_BYTES + 1];
    format_raw_mask(&header->archive_mask, header->fa_entry_count, string);
    return write_string(scon, "%s\n", string);
}


static bool write_status(int scon, const char *client_name)
{
    struct fa_status status;
    return CATCH_ERROR(scon, client_name,
        get_sniffer_status(&status),
        write_string(scon, "%u %u %u %u %u %u %u %u\n",
            status.status, status.partner,
            status.last_interrupt, status.frame_errors,
            status.soft_errors, status.hard_errors,
            status.running, status.overrun));
}



/* The C command prefix is followed by a sequence of one letter commands, and
 * each letter receives a one line response (except for the I command).  The
 * following commands are supported:
 *
 *  F   Returns current sample frequency
 *  d   Returns first decimation
 *  D   Returns second decimation
 *  T   Returns earliest available timestamp
 *  U   Returns the latest available timestamp
 *  V   Returns protocol identification string
 *  M   Returns configured capture mask
 *  C   Returns live decimation factor if available
 *  K   Returns FA sample count
 *  S   Returns detailed sniffer status.  The numbers returned are:
 *          hardware link status        1 => ok, 2, 3 => link fault
 *          link partner                or 1023 if no connection
 *          last interrupt code         1 => running normally
 *          frame error count
 *          soft error count
 *          hard error count
 *          run state                   1 => Currently fetching data
 *          overrun                     1 => Halted due to buffer overrun
 *  E   Returns event mask FA id or -1 if not specied
 *  N   Returns server name configured on startup
 *  I   Returns list of all conected clients, one client per line.
 *  L   Returns list of FA ids and their descriptions
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
                ok = write_index_timestamp(scon, 1);
                break;
            case 'U':
                ok = write_index_timestamp(scon, (uint64_t)-1);
                break;
            case 'V':
                ok = write_string(scon, PROTOCOL_VERSION "\n");
                break;
            case 'M':
                ok = write_mask(scon);
                break;
            case 'C':
                ok = write_string(scon, "%u\n", get_decimation_factor());
                break;
            case 'S':
                ok = write_status(scon, client_name);
                break;
            case 'I':
                ok = report_clients(scon);
                break;
            case 'K':
                ok = write_string(scon, "%u\n", header->fa_entry_count);
                break;
            case 'E':
                ok = write_string(scon, "%d\n", events_fa_id);
                break;
            case 'N':
                ok = write_string(scon, "%s\n", server_name);
                break;
            case 'L':
                ok = write_fa_ids(scon, &header->archive_mask);
                break;
            default:
                ok = report_error(scon, client_name, "Unknown command");
                break;
        }
    }
    return ok;
}


/* Pops error message and writes it to client. */
static bool pop_client_error(int scon, const char *client_name)
{
    char *error_message = pop_error_handling(true);
    bool write_ok = report_error(scon, client_name, error_message);
    free(error_message);
    return write_ok;
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
        /* If an error is encountered write the error message to the socket. */
        return pop_client_error(scon, client_name);
}


typedef bool (*command_t)(int scon, const char *client_name, const char *buf);

static const struct command_table {
    char id;            // Identification character
    command_t process;
} command_table[] = {
    { 'C', process_command },
    { 'R', process_read },
    { 'S', process_subscribe },
    { 'D', process_debug_command },
    { 0,   process_error }
};


/* Looks up command character in command_table above. */
static command_t lookup_command(char ch)
{
    const struct command_table *command = command_table;
    while (command->id  &&  command->id != ch)
        command += 1;
    return command->process;
}


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
        char *newline = memchr(buf, '\n', (size_t) rx);
        if (newline)
        {
            *newline = '\0';
            return true;
        }

        buflen -= (size_t) rx;
        buf += rx;
    }

    /* On failure report what we managed to read before failing. */
    *buf = '\0';
    log_message("Client %s sent: \"%s\"", client->name, client->buf);
    return false;
}


/* Command successfully read, dispatch it to the appropriate handler. */
static void dispatch_command(
    int scon, const char *client_name, const char *buf)
{
    log_message("Client %s command: \"%s\"", client_name, buf);
    command_t command = lookup_command(buf[0]);
    bool ok = command(scon, client_name, buf);
    char *error_message = pop_error_handling(!ok);
    if (!ok)
        log_message("Client %s error: %s", client_name, error_message);
    free(error_message);
}


static void *process_connection(void *context)
{
    int scon = (int) (intptr_t) context;
    struct client_info *client = add_client();

    /* Retrieve client address so we can log all messages associated with this
     * client with the appropriate address. */
    get_client_name(scon, client->name);

    /* Read the command, required to be one line terminated by \n, and dispatch
     * to the appropriate handler.  Any errors are handled locally and are
     * reported below. */
    push_error_handling();
    bool ok =
        set_socket_cork(scon, true)  &&
        set_socket_timeout(scon, 1, 10)  &&
        read_line(scon, client);

    if (ok)
        /* Past this point only dispatch_command() can communicate with the
         * client, any further errors it needs to handle. */
        dispatch_command(scon, client->name, client->buf);
    else
        /* Any errors seen at this stage are reported back to the client. */
        pop_client_error(scon, client->name);

    /* Uncork the socket before closing to ensure any remaining data is sent.
     * It seems that if we close the socket with cork enabled and unread
     * incoming data then the tail end of the sent data stream can be lost. */
    set_socket_cork(scon, false);
    IGNORE(TEST_IO(close(scon)));

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
    struct buffer *fa_buffer, struct buffer *decimated,
    unsigned int _events_fa_id, const char *_server_name,
    const char *bind_address, int port, bool extra, bool reuseaddr)
{
    initialise_subscribe(fa_buffer, decimated);
    fa_block_buffer = fa_buffer;
    events_fa_id = _events_fa_id;
    server_name = _server_name;
    debug_commands = extra;

    struct sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };
    int reuse = 1;
    return
        IF_(bind_address,
            TEST_OK_(inet_aton(bind_address, &sin.sin_addr),
                "Malformed listening address"))  &&
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
    return TEST_0(pthread_create(
        &server_thread, NULL, run_server, (void *) (intptr_t) server_socket));
}


void terminate_server(void)
{
    IGNORE(TEST_0(pthread_cancel(server_thread)));
    /* We probably need to kill all the client threads. */
}
