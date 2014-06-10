/* Replays canned data for debugging.
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
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <memory.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "error.h"
#include "fa_sniffer.h"
#include "mask.h"
#include "matlab.h"
#include "sniffer.h"
#include "buffer.h"

#include "replay.h"


static unsigned int *column_index;  // Converts FA id to data column
static unsigned int fa_entry_count;

static unsigned int replay_row_count; // Number of rows available for replay
static char *replay_first_row;      // Pointer to first row of data
static unsigned int replay_index;   // Count of row currently being read
static void *replay_row;            // Pointer to row being read
static unsigned int replay_row_size; // Length of an individual row
static int32_t replay_id0_start;    // Reset id0 to this at start of cycle
static int32_t replay_id0;          // Current value of id0
static void (*convert_row)(struct fa_row *row); // Converts data to fa_entry
static bool interrupted = false;    // Used to implement interrupt functionality

static struct timespec next_sleep;  // Used for uniform sleep intervals


/* Advances target by requested number of nanoseconds (will silently go wrong
 * if duration >= 1s) and waits for it to come around. */
static void sleep_until(unsigned int duration)
{
    next_sleep.tv_nsec += (long) duration;
    if (next_sleep.tv_nsec >= 1000000000)
    {
        next_sleep.tv_nsec -= 1000000000;
        next_sleep.tv_sec += 1;
    }
    IGNORE(TEST_0(clock_nanosleep(
        CLOCK_MONOTONIC, TIMER_ABSTIME, &next_sleep, NULL)));
}

static bool read_replay_block(
    struct fa_row *rows, size_t size, uint64_t *timestamp)
{
    if (interrupted)
        return false;

    size_t row_count = size / fa_entry_count / FA_ENTRY_SIZE;
    for (size_t i = 0; i < row_count; i ++)
    {
        rows->row[0].x = replay_id0;
        rows->row[0].y = replay_id0;
        convert_row(rows);

        replay_index += 1;
        if (replay_index < replay_row_count)
        {
            replay_row += replay_row_size;
            replay_id0 += 1;
        }
        else
        {
            replay_row = replay_first_row;
            replay_index = 0;
            replay_id0 = replay_id0_start;
        }

        rows = (void *) rows + fa_entry_count * FA_ENTRY_SIZE;
    }

    // 100us = 100,000ns per row
    sleep_until((unsigned int) (100000 * row_count));
    *timestamp = get_timestamp();
    return true;
}


/* Conversion from matlab stored datatypes to the fa_entry values we need.  Alas
 * we have little control over how matlab stores its files, so really we need to
 * support a variety of conversions. */

#define DEFINE_CONVERT(type) \
    static void convert_xy_##type(struct fa_row *row) \
    { \
        struct fa_entry *entry = &row->row[1]; \
        for (unsigned int j = 1; j < fa_entry_count; j ++) \
        { \
            type *field = &((type *) replay_row)[2 * column_index[j]]; \
            entry->x = (int32_t) field[0]; \
            entry->y = (int32_t) field[1]; \
            entry ++; \
        } \
    }

DEFINE_CONVERT(int16_t)
DEFINE_CONVERT(int32_t)
DEFINE_CONVERT(double)

static const struct convert
{
    int data_type;
    void (*convert)(struct fa_row *row);
    size_t data_size;
} convert_type[] = {
    { miINT16,  convert_xy_int16_t, sizeof(int16_t) },
    { miINT32,  convert_xy_int32_t, sizeof(int32_t) },
    { miDOUBLE, convert_xy_double,  sizeof(double) },
};

static bool convert_datatype(int data_type, size_t *data_size)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(convert_type); i ++)
        if (convert_type[i].data_type == data_type)
        {
            convert_row = convert_type[i].convert;
            *data_size  = convert_type[i].data_size;
            return true;
        }
    return FAIL_("Can't handle data of type %d", data_type);
}


/* Prepares data array for replay.  The data has either two or three dimensions,
 * indexed as data[xy, fa_id, time]. */
static void prepare_data_array(
    const struct matlab_matrix *data, size_t data_size, unsigned int *columns)
{
    *columns = data->dim_count > 2 ? data->dims[1] : 1;

    replay_row_count = data->dim_count > 2 ? data->dims[2] : data->dims[1];
    replay_first_row = data->real.start;
    replay_index = 0;
    replay_row = replay_first_row;
    replay_row_size = *columns * 2 * (unsigned int) data_size;

    /* Create a default column index by just cycling through the available
     * columns. */
    column_index = calloc(fa_entry_count, sizeof(unsigned int));
    for (unsigned int i = 1; i < fa_entry_count; i ++)
        column_index[i] = i % *columns;

    /* By default replay id0 will start at zero. */
    replay_id0_start = 0;
    replay_id0 = replay_id0_start;
}

/* If an ids array has been given use this to ensure the correct FA ids are
 * replayed in the correct columns. */
static void prepare_index_array(
    const struct matlab_matrix *ids, unsigned int columns)
{
    /* Use ids to replace column entries. */
    uint8_t *id_array = (uint8_t *) ids->real.start;
    for (unsigned int i = 0; i < columns; i ++)
        if (id_array[i] < fa_entry_count)
            column_index[id_array[i]] = i;
}

static void prepare_id0(const struct matlab_matrix *id0)
{
    replay_id0_start = ((int32_t *) id0->real.start)[0];
    replay_id0 = replay_id0_start;
}


/* Validation of matrix.  Must be non empty, real and the right shape. */
static bool check_dimensions(
    const char *name, const struct matlab_matrix *matrix,
    unsigned int max_dims, unsigned int cols)
{
    return
        TEST_OK_(
            matrix->real.size > 0, "Empty array for %s", name)  &&
        TEST_OK_(
            2 <= matrix->dim_count  &&  matrix->dim_count <= max_dims,
            "Wrong number of dimensions for %s", name)  &&
        TEST_OK_(
            matrix->dims[0] == cols, "Wrong shape array for %s", name)  &&
        TEST_OK_(
            !matrix->complex_data, "Unexpected complex data for %s", name);
}

/* Searches matlab file for the arrays we need. */
static bool prepare_replay_data(struct region *region)
{
    bool found_data, found_ids, found_id0;
    struct matlab_matrix data, ids, id0;
    size_t data_size;
    unsigned int columns;
    bool ok =
        /* Prepare and validate the data area. */
        find_matrix_by_name(region, "data", &found_data, &data)  &&
        TEST_OK_(found_data, "No data element in replay file")  &&
        check_dimensions("data", &data, 3, 2)  &&
        convert_datatype(data.data_type, &data_size)  &&
        DO_(prepare_data_array(&data, data_size, &columns))  &&

        /* Prepare and validate the array of ids. */
        find_matrix_by_name(region, "ids",  &found_ids, &ids)  &&
        IF_(found_ids,
            check_dimensions("ids", &ids, 2, 1)  &&
            TEST_OK_(ids.dims[1] == columns, "Ids don't match data")  &&
            TEST_OK_(ids.data_type == miUINT8, "Bad datatype for ids")  &&
            DO_(prepare_index_array(&ids, columns)))  &&

        /* Prepare id0 if present. */
        find_matrix_by_name(region, "id0", &found_id0, &id0)  &&
        IF_(found_id0,
            check_dimensions("id0", &id0, 2, 1)  &&
            TEST_OK_(id0.data_type == miINT32, "Bad datatype for id0")  &&
            DO_(prepare_id0(&id0)));

    return ok;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Dummy sniffer using replay data. */

static bool reset_replay(void)
{
    interrupted = false;
    return true;
}

static bool read_replay_status(struct fa_status *status)
{
    return FAIL_("Sniffer status unavailable in replay mode");
}

static bool interrupt_replay(void)
{
    interrupted = true;
    return true;
}

static const struct sniffer_context sniffer_replay = {
    .reset = reset_replay,
    .read = read_replay_block,
    .status = read_replay_status,
    .interrupt = interrupt_replay,
};


const struct sniffer_context *initialise_replay(
    const char *replay_filename, unsigned int _fa_entry_count)
{
    fa_entry_count = _fa_entry_count;

    int file;
    struct region region;
    bool ok =
        TEST_IO_(file = open(replay_filename, O_RDONLY),
            "Unable to open replay file \"%s\"", replay_filename)  &&
        /* For simplicity, just map the entire file into memory! */
        map_matlab_file(file, &region)  &&
        prepare_replay_data(&region)  &&

        /* Finally prepare the replay sleep target.  This ensures we pace the
         * data correctly. */
        TEST_IO(clock_gettime(CLOCK_MONOTONIC, &next_sleep));
    return ok ? &sniffer_replay : NULL;
}
