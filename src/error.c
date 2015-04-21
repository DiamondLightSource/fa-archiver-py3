/* Implementation of generic error handling support.
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
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <syslog.h>
#include <pthread.h>
#include <time.h>

#include "error.h"
#include "locking.h"


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Local error handling. */

struct error_stack {
    char *message;
    struct error_stack *last;
};

static __thread struct error_stack *error_stack = NULL;

void push_error_handling(void)
{
    struct error_stack *new_entry = malloc(sizeof(struct error_stack));
    new_entry->message = NULL;
    new_entry->last = error_stack;
    error_stack = new_entry;
}

char *pop_error_handling(bool return_message)
{
    struct error_stack *top = error_stack;
    error_stack = top->last;
    char *error_message = NULL;
    if (return_message)
        error_message = top->message;
    else if (top->message != NULL)
    {
        /* If the caller isn't claiming the error message this needs to be
         * logged. */
        log_error("Error message discarded: %s", top->message);
        free(top->message);
    }
    free(top);
    return error_message;
}


/* Takes ownership of message if the error stack is non-empty. */
static bool save_message(char *message)
{
    struct error_stack *top = error_stack;
    if (top)
    {
        if (top->message != NULL)
        {
            /* Repeated error messages can be a sign of a problem.  Keep the
             * first message, but log any extras. */
            log_error("Extra error message: %s", message);
            free(message);
        }
        else
            top->message = message;
        return true;
    }
    else
        return false;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Error handling and logging. */

/* Lock used to prevent interleaving of logs from multiple threads. */
DECLARE_LOCKING(log_lock);


/* Determines whether error messages go to stderr or syslog. */
static bool daemon_mode = false;
/* Determines whether to log non-error messages. */
static bool log_verbose = false;
/* Timestamp each log message. */
static bool log_timestamps = false;


void verbose_logging(bool verbose)
{
    log_verbose = verbose;
}

void timestamp_logging(bool timestamps)
{
    log_timestamps = timestamps;
}


void start_logging(const char *ident)
{
    openlog(ident, 0, LOG_DAEMON);
    daemon_mode = true;
}


static void print_timestamp(struct timespec *timestamp)
{
    /* Convert ns into microseconds, the extra ns detail is a bit much. */
    long usec = (timestamp->tv_nsec + 500) / 1000;
    if (usec >= 1000000)
    {
        usec -= 1000000;
        timestamp->tv_sec += 1;
    }

    /* Print the result in local time. */
    struct tm tm;
    localtime_r(&timestamp->tv_sec, &tm);

    fprintf(stderr, "%04d-%02d-%02d %02d:%02d:%02d.%06ld: ",
        1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, usec);
}


void vlog_message(int priority, const char *format, va_list args)
{
    /* Get the timestamp before entering the lock for more honest times. */
    struct timespec now;
    if (log_timestamps)
        clock_gettime(CLOCK_REALTIME, &now);

    LOCK(log_lock);
    if (daemon_mode)
        vsyslog(priority, format, args);
    else
    {
        if (log_timestamps)
            print_timestamp(&now);
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
    }
    UNLOCK(log_lock);
}

void log_message(const char *message, ...)
{
    if (log_verbose)
    {
        va_list args;
        va_start(args, message);
        vlog_message(LOG_INFO, message, args);
        va_end(args);
    }
}

void log_error(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    vlog_message(LOG_ERR, message, args);
    va_end(args);
}


static char *add_strerror(char *message, int last_errno)
{
    if (last_errno == 0)
        return message;
    else
    {
        /* This is very annoying: strerror() is not not necessarily thread
         * safe ... but not for any compelling reason, see:
         *  http://sources.redhat.com/ml/glibc-bugs/2005-11/msg00101.html
         * and the rather unhelpful reply:
         *  http://sources.redhat.com/ml/glibc-bugs/2005-11/msg00108.html
         *
         * On the other hand, the recommended routine strerror_r() is
         * inconsistently defined -- depending on the precise library and its
         * configuration, it returns either an int or a char*.  Oh dear.
         *
         * Ah well.  We go with the GNU definition, so here is a buffer to
         * maybe use for the message. */
        char StrError[64];
        char *result;
        IGNORE(asprintf(&result, "%s: (%d) %s", message, last_errno,
            strerror_r(last_errno, StrError, sizeof(StrError))));
        free(message);
        return result;
    }
}


void print_error(const char *format, ...)
{
    int last_errno = errno;
    va_list args;
    va_start(args, format);
    char *message;
    IGNORE(vasprintf(&message, format, args));
    va_end(args);

    message = add_strerror(message, last_errno);
    if (!save_message(message))
    {
        log_error("%s", message);
        free(message);
    }
}


void panic_error(const char *filename, int line)
{
    int last_errno = errno;
    char *message;
    IGNORE(asprintf(&message, "panic at %s, line %d", filename, line));
    message = add_strerror(message, last_errno);
    log_error("%s", message);
    free(message);

    fflush(stderr);
    _exit(255);
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Helper functions for reading and writing. */

#define ENSURE_ACTION(action, fd, buf, count) \
    size_t total = 0; \
    while (count > total) \
    { \
        ssize_t processed = action(fd, buf + total, count - total); \
        if (processed < 0) \
            return processed; \
        else if (processed == 0) \
            break; \
        total += (size_t) processed; \
    } \
    return (ssize_t) total

ssize_t ensure_write(int fd, const void *buf, size_t count)
{
    ENSURE_ACTION(write, fd, buf, count);
}

ssize_t ensure_read(int fd, void *buf, size_t count)
{
    ENSURE_ACTION(read, fd, buf, count);
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Utility function with no proper home. */


void dump_binary(FILE *out, const void *buffer, size_t length)
{
    const uint8_t *dump = buffer;

    for (size_t a = 0; a < length; a += 16)
    {
        fprintf(out, "%08zx: ", a);
        for (unsigned int i = 0; i < 16; i ++)
        {
            if (a + i < length)
                fprintf(out, " %02x", dump[a+i]);
            else
                fprintf(out, "   ");
            if (i % 16 == 7)
                fprintf(out, " ");
        }

        fprintf(out, "  ");
        for (unsigned int i = 0; i < 16; i ++)
        {
            uint8_t c = dump[a+i];
            if (a + i < length)
                fprintf(out, "%c", 32 <= c  &&  c < 127 ? c : '.');
            else
                fprintf(out, " ");
            if (i % 16 == 7)
                fprintf(out, " ");
        }
        fprintf(out, "\n");
    }
    if (length % 16 != 0)
        fprintf(out, "\n");
}

