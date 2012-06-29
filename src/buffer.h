/* Interface to central memory buffer.
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

/* At the heart of the archiver is a large memory buffer.  This is
 * continually filled from the FA archiver device all the time that the
 * communication controller network is running, and is emptied to disk
 * quickly enough that it should never overflow.
 *
 * There are a number of complications to this simple picture.
 *
 * Firstly, we support multiple readers of the buffer.  It is possible for
 * other applications to subscribe to the FA data stream, in which case they
 * will also be updated.  If a subscriber falls behind it is simply cut off!
 *
 * Secondly, we need some mechanism to cope with gaps in the FA data stream.
 * Whenever the communication controller network stops the feed of data into
 * the buffer is interrupted.  The presence of these gaps in the stream needs
 * to be recorded as they are written to disk.
 *
 * Finally, if writing to disk underruns it would be good to handle this
 * gracefully. */


/* Circular buffer with support for single writer and multiple independent
 * readers and optional support for timestamps. */
struct buffer;
/* A single reader connected to a buffer. */
struct reader_state;

/* All buffered times are represented in microseconds in the Unix epoch. */
uint64_t ts_to_microseconds(const struct timespec *ts);


/* Prepares central memory buffer. */
bool create_buffer(
    struct buffer **buffer, size_t block_size, size_t block_count);

/* Interrogates buffer block size. */
size_t buffer_block_size(struct buffer *buffer);
/* Similar helper routine when we only have a reader in our hands. */
size_t reader_block_size(struct reader_state *reader);

/* Reserves the next slot in the buffer for writing. An entire contiguous
 * block of block_size bytes is guaranteed to be returned, and
 * release_write_block() must be called when writing is complete. */
void *get_write_block(struct buffer *buffer);
/* Releases the previously reserved write block, returning false if the block
 * cannot be advanced -- in this case a gap is forced in the data stream.  Also
 * a gap can be requested, in both these cases the data is discarded. */
bool release_write_block(struct buffer *buffer, bool gap, uint64_t timestamp);


/* Creates a new reading connection to the buffer. */
struct reader_state *open_reader(struct buffer *buffer, bool reserved_reader);
/* Closes a previously opened reader connection. */
void close_reader(struct reader_state *reader);

/* Blocks until an entire block_size block is available to be read out,
 * returns pointer to data to be read.  If there is a gap in the available
 * data then NULL is returned, and release_write_block() should not be called
 * before calling get_read_block() again.
 *    If timestamp is not NULL then on a successful block read the timestamp of
 * the returned data is written to *timestamp. */
const void *get_read_block(struct reader_state *reader, uint64_t *timestamp);
/* Releases the write block.  If false is returned then the block was
 * overwritten while locked due to reader underrun; however, if the reader was
 * opened with reserved_reader set this is guaranteed not to happen.  Only
 * call if non-NULL value returned by get_read_block(). */
bool release_read_block(struct reader_state *reader);
/* Interrupts the reader, interruping any waits in release_read_block() and
 * forcing further calls to get_read_block() to immediately return NULL. */
void interrupt_reader(struct reader_state *reader);

/* Can be used to temporarily halt or resume buffered writing. */
void enable_buffer_write(struct buffer *buffer, bool enabled);
/* Returns state of buffer write enable flag. */
bool buffer_write_enabled(struct buffer *buffer);
