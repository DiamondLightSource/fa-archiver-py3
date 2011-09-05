/* Interface to archive to disk writer.
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

/* First stage of disk writer initialisation: opens the archive file and loads
 * the header into memory.  Can be called before initialising buffers. */
bool initialise_disk_writer(const char *file_name, uint32_t *input_block_size);
/* Starts writing files to disk.  Must be called after initialising the buffer
 * layer. */
bool start_disk_writer(struct buffer *buffer);
/* Orderly shutdown of the disk writer. */
void terminate_disk_writer(void);

/* Methods for access to writer thread. */

/* Asks the writer thread to write out the given block.  If a previously
 * requested write is still in progress then this blocks until the write has
 * completed. */
void schedule_write(off64_t offset, void *block, size_t length);

/* Requests permission to perform a read, blocks while an outstanding write is
 * in progress. */
void request_read(void);
