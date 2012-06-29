/* Simple thread locking.
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
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/time.h>

#include "error.h"

#include "locking.h"

#define NSECS   1000000000      // 1e9


void initialise_locking(struct locking *locking)
{
    ASSERT_0(pthread_mutex_init(&locking->mutex, NULL));
    ASSERT_0(pthread_cond_init(&locking->signal, NULL));
}

void do_lock(struct locking *locking)
{
    ASSERT_0(pthread_mutex_lock(&locking->mutex));
}

void do_unlock(struct locking *locking)
{
    ASSERT_0(pthread_mutex_unlock(&locking->mutex));
}

void psignal(struct locking *locking)
{
    ASSERT_0(pthread_cond_signal(&locking->signal));
}

void pbroadcast(struct locking *locking)
{
    ASSERT_0(pthread_cond_broadcast(&locking->signal));
}

void pwait(struct locking *locking)
{
    ASSERT_0(pthread_cond_wait(&locking->signal, &locking->mutex));
}

bool pwait_timeout(struct locking *locking, int secs, long nsecs)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    struct timespec timeout = {
        .tv_sec = now.tv_sec + secs,
        .tv_nsec = 1000 * now.tv_usec + nsecs
    };
    if (timeout.tv_nsec >= NSECS)
    {
        timeout.tv_nsec -= NSECS;
        timeout.tv_sec += 1;
    }
    int rc = pthread_cond_timedwait(
        &locking->signal, &locking->mutex, &timeout);
    if (rc == ETIMEDOUT)
        return false;
    else
    {
        ASSERT_0(rc);
        return true;
    }
}
