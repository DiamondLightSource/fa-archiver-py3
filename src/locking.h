/* Common definitions for threads and locking.
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

struct locking {
    pthread_mutex_t mutex;
    pthread_cond_t signal;
};

#define DECLARE_LOCKING(lock) \
    static struct locking lock = { \
        .mutex = PTHREAD_MUTEX_INITIALIZER, \
        .signal = PTHREAD_COND_INITIALIZER \
    }

#define LOCK(locking) \
    do_lock(&locking); \
    pthread_cleanup_push((void(*)(void*)) do_unlock, &locking)
#define UNLOCK(locking) \
    pthread_cleanup_pop(true)

void initialise_locking(struct locking *locking);

void do_lock(struct locking *locking);
void do_unlock(struct locking *locking);
void psignal(struct locking *locking);
void pbroadcast(struct locking *locking);
void pwait(struct locking *locking);
bool pwait_timeout(struct locking *locking, int secs, long nsecs);
