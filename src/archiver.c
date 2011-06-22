/* Archiver program for capturing data from FA sniffer and writing to disk.
 * Also makes the continuous data stream available over a dedicated socket. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <fenv.h>

#include "error.h"
#include "buffer.h"
#include "fa_sniffer.h"
#include "sniffer.h"
#include "mask.h"
#include "disk.h"
#include "disk_writer.h"
#include "socket_server.h"
#include "archiver.h"
#include "parse.h"
#include "reader.h"
#include "decimate.h"
#include "replay.h"


#define K               1024

/* Circular buffer between FA device and consumers.  The correct size here is a
 * little delicate... */
#define BUFFER_BLOCKS   64


/*****************************************************************************/
/*                               Option Parsing                              */
/*****************************************************************************/

/* If true then the archiver runs as a daemon with all messages written to
 * syslog. */
static bool daemon_mode = false;
static char *argv0;
/* Name of FA sniffer device. */
static const char *fa_sniffer_device = "/dev/fa_sniffer0";
/* Name of archive store, must be specified. */
static char *output_filename;
/* If set, the PID is written to this file, and the archiver can then be
 * interrupted with the command
 *      kill $(cat $pid_filename)   */
static char *pid_filename = NULL;
/* If set the sniffer thread will run at boosted priority. */
static bool boost_priority = false;
/* In memory buffer. */
static unsigned int buffer_blocks = BUFFER_BLOCKS;
/* Socket used for serving remote connections. */
static int server_socket = 8888;
/* Decimation configuration file. */
static const char *decimation_config = NULL;
/* Set if replay configured. */
static bool replay = false;
/* If set enables extra socket server commands for debug control. */
static bool extra_commands = false;
/* Enables logging of routine events and incoming commands. */
static bool verbose = true;
/* Enable SO_REUSEADDR option on listening socket. */
static bool reuseaddr = false;


static void usage(void)
{
    printf(
"Usage: %s [options] <archive-file>\n"
"Captures continuous FA streaming data to the specified <archive-file>.\n"
"\n"
"Options:\n"
"    -c:  Specify decimation configuration file.  If this is specified then\n"
"         streaming decimated data will be available for subscription.\n"
"    -d:  Specify device to use for FA sniffer (default /dev/fa_sniffer0)\n"
"    -r   Run sniffer thread at boosted priority.  Needs real time support\n"
"    -b:  Specify number of buffered input blocks (default %u)\n"
"    -q   Quiet operation, only log errors\n"
"    -t   Output timestamps with logs.  No effect when logging to syslog\n"
"    -D   Run as a daemon\n"
"    -p:  Write PID to specified file\n"
"    -s:  Specify server socket (default 8888)\n"
"    -F:  Run dummy sniffer with canned data.\n"
"    -X   Enable extra commands (debug only)\n"
"    -R   Set SO_REUSEADDR on listening socket, debug use only\n"
        , argv0, buffer_blocks);
}


static bool process_options(int *argc, char ***argv)
{
    argv0 = (*argv)[0];
    bool ok = true;
    while (ok)
    {
        switch (getopt(*argc, *argv, "+hc:d:rb:qtDp:s:F:XR"))
        {
            case 'h':   usage();                                    exit(0);
            case 'c':   decimation_config = optarg;                 break;
            case 'd':   fa_sniffer_device = optarg;                 break;
            case 'r':   boost_priority = true;                      break;
            case 'q':   verbose = false;                            break;
            case 't':   timestamp_logging(true);                    break;
            case 'D':   daemon_mode = true;                         break;
            case 'p':   pid_filename = optarg;                      break;
            case 'F':   fa_sniffer_device = optarg;
                        replay = true;                              break;
            case 'X':   extra_commands = true;                      break;
            case 'R':   reuseaddr = true;                           break;
            case 'b':
                ok = DO_PARSE("buffer blocks",
                    parse_uint, optarg, &buffer_blocks);
                break;
            case 's':
                ok = DO_PARSE("server socket",
                    parse_int, optarg, &server_socket);
                break;
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
    bool ok =
        process_options(&argc, &argv)  &&
        TEST_OK_(argc == 1, "Try `%s -h` for usage", argv0);

    output_filename = argv[0];
    verbose_logging(verbose);
    return ok;
}



/*****************************************************************************/
/*                            Startup and Control                            */
/*****************************************************************************/


static sem_t shutdown_semaphore;


void shutdown_archiver(void)
{
    if (daemon_mode)
        ASSERT_IO(sem_post(&shutdown_semaphore));
    else
        close(STDIN_FILENO);
}


static void at_exit(int signum)
{
    log_message("Caught signal %d", signum);
    shutdown_archiver();
}

static bool initialise_signals(void)
{
    struct sigaction do_shutdown = {
        .sa_handler = at_exit, .sa_flags = SA_RESTART };
    return
        TEST_IO(sem_init(&shutdown_semaphore, 0, 0))  &&

        TEST_IO(sigfillset(&do_shutdown.sa_mask))  &&
        /* Catch the usual interruption signals and use them to trigger an
         * orderly shutdown. */
        TEST_IO(sigaction(SIGHUP,  &do_shutdown, NULL))  &&
        TEST_IO(sigaction(SIGINT,  &do_shutdown, NULL))  &&
        TEST_IO(sigaction(SIGTERM, &do_shutdown, NULL))  &&
        /* When acting as a server we need to ignore SIGPIPE, of course. */
        TEST_IO(signal(SIGPIPE, SIG_IGN));
}


static bool maybe_daemonise(void)
{
    int pid_file = -1;
    char pid[32];
    return
        /* The logic here is a little odd: we want to check that we can write
         * the PID file before daemonising, to ensure that the caller gets the
         * error message if daemonising fails, but we need to write the PID file
         * afterwards to get the right PID. */
        IF_(pid_filename,
            TEST_IO_(pid_file = open(
                pid_filename, O_WRONLY | O_CREAT | O_EXCL, 0644),
                "PID file already exists: is archiver already running?"))  &&
        IF_(daemon_mode,
            /* Don't chdir to / so that we can unlink(pid_filename) at end. */
            TEST_IO(daemon(true, false))  &&
            DO_(start_logging("FA archiver")))  &&
        IF_(pid_filename,
            DO_(sprintf(pid, "%d", getpid()))  &&
            TEST_IO(write(pid_file, pid, strlen(pid)))  &&
            TEST_IO(close(pid_file)));
}


/* If running as a daemon wait for the shutdown semaphore, otherwise read lines
 * from stdin until it closes or we see an exit command. */
static void wait_for_exit(void)
{
    if (daemon_mode)
        /* Wait for a shutdown signal.  Ignore the signal, instead waiting for
         * the clean shutdown request. */
        while (sem_wait(&shutdown_semaphore) == -1  &&  TEST_OK(errno == EINTR))
            ; /* Repeat wait while we see EINTR. */
    else
    {
        char line[80];
        while (fgets(line, sizeof(line), stdin))
        {
            if (strcmp(line, "exit\n") == 0)
                break;
            printf("> ");
        }
    }
}


static void run_archiver(void)
{
    log_message("Started");
    wait_for_exit();

    log_message("Shutting down");
    terminate_server();
    terminate_sniffer();
    if (decimation_config)
        terminate_decimation();
    terminate_disk_writer();
    if (pid_filename)
        IGNORE(TEST_IO(unlink(pid_filename)));
    log_message("Shut Down");
}


int main(int argc, char **argv)
{
    uint32_t input_block_size;
    struct buffer *fa_block_buffer;
    struct buffer *decimated_buffer = NULL;
    bool ok =
        process_args(argc, argv)  &&
        initialise_disk_writer(output_filename, &input_block_size)  &&
        create_buffer(&fa_block_buffer, input_block_size, buffer_blocks)  &&
        IF_(decimation_config,
            initialise_decimation(
                decimation_config, fa_block_buffer, &decimated_buffer))  &&
        initialise_sniffer(fa_block_buffer, fa_sniffer_device, replay)  &&
        initialise_server(
            fa_block_buffer, decimated_buffer, server_socket,
            extra_commands, reuseaddr)  &&
        initialise_reader(output_filename)  &&

        maybe_daemonise()  &&
        initialise_signals()  &&

        /* All the thread initialisation must be done after daemonising, as of
         * course threads don't survive across the daemon() call!  Alas, this
         * means that many startup errors go into syslog rather than stderr. */
        start_disk_writer(fa_block_buffer)  &&
        start_sniffer(boost_priority)  &&
        IF_(decimation_config, start_decimation())  &&
        start_server()  &&

        DO_(run_archiver());

    return ok ? 0 : 1;
}
