/* Implements reading from disk.
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

/* scon is the connected socket and buf is the command read from the user: rx
 * bytes have already been received and the buffer is buf_size bytes long.
 * The first character in the buffer is R. */
bool process_read(int scon, const char *client_name, const char *buf);

bool initialise_reader(const char *archive);


/* Timestamp header when sending extended data. */
struct extended_timestamp_header {
    uint32_t block_size;        // Number of samples in each major block
    uint32_t offset;            // Offset into block of first sample sent
} __attribute__((packed));

/* Timestamp sent at head of each block. */
struct extended_timestamp {
    uint64_t timestamp;         // Start of block in microseconds
    uint32_t duration;          // Duration of block in microseconds
} __attribute__((packed));
struct extended_timestamp_id0 {
    uint64_t timestamp;         // Start of block in microseconds
    uint32_t duration;          // Duration of block in microseconds
    uint32_t id_zero;           // CC cycle number
} __attribute__((packed));
