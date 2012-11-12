/* Header for data transposition and reduction functionality.
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


/* Processes a single input block by transposing and decimation.  If a major
 * block is filled then it is also written to disk. */
void process_block(const void *read_block, uint64_t timestamp);


/* Interlocked access. */

/* Returns timestamp corresponding to index block containing or nearest to the
 * given timestamp.  Can search for timestamps of 1 and -1 to get bounds of
 * archive. */
uint64_t __pure timestamp_to_index_ts(uint64_t timestamp);

/* Converts timestamp to block and offset into block together with number of
 * available samples.  Fails if timestamp is too early unless all_data set. */
bool timestamp_to_start(
    uint64_t timestamp, bool all_data, uint64_t *samples_available,
    unsigned int *block, unsigned int *offset);
/* Similar to timestamp_to_start, but used for end time, in particular won't
 * skip over gaps to find a timestamp.  Called with a start_block so that we can
 * verify that *block is no earlier than start_block. */
bool timestamp_to_end(
    uint64_t timestamp, bool all_data, unsigned int start_block,
    unsigned int *block, unsigned int *offset);

/* Searches a range of index blocks for a gap in the timestamp, returning true
 * iff a gap is found.  *start is updated to the index of the block directly
 * after the first gap and *blocks is decremented accordingly. */
bool find_gap(bool check_id0, unsigned int *start, unsigned int *blocks);
const struct data_index *__pure read_index(unsigned int ix);

/* Returns an unlocked pointer to the header: should only be used to access the
 * constant header fields. */
const struct disk_header *__pure get_header(void);


void initialise_transform(
    struct disk_header *header, struct data_index *data_index,
    struct decimated_data *dd_area);

// !!!!!!
// Not right.  Returns DD data area.
const struct decimated_data *__pure get_dd_area(void);
