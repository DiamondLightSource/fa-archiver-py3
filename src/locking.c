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

#include "error.h"

#include "locking.h"


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
    ASSERT_0(pthread_cond_broadcast(&locking->signal));
}

void pwait(struct locking *locking)
{
    ASSERT_0(pthread_cond_wait(&locking->signal, &locking->mutex));
}
