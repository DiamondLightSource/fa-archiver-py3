/* Replays canned data for debugging. */

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
#include "sniffer.h"
#include "mask.h"
#include "matlab.h"

#include "replay.h"


static int column_index[FA_ENTRY_COUNT];    // Converts FA id to data column

static int replay_row_count;        // Total number of rows available for replay
static char *replay_first_row;      // Pointer to first row of data
static int replay_index;            // Count of row currently being read
static char *replay_row;            // Pointer to row being read
static int replay_row_size;         // Length of an individual row
static void (*convert_row)(struct fa_row *row); // Converts data to fa_entry

static struct timespec next_sleep;  // Used for uniform sleep intervals


/* Advances target by requested number of nanoseconds (will silently go wrong
 * if duration >= 1s) and waits for it to come around. */
static void sleep_until(int duration)
{
    next_sleep.tv_nsec += duration;
    if (next_sleep.tv_nsec >= 1000000000)
    {
        next_sleep.tv_nsec -= 1000000000;
        next_sleep.tv_sec += 1;
    }
    TEST_0(clock_nanosleep(
        CLOCK_MONOTONIC, TIMER_ABSTIME, &next_sleep, NULL));
}

bool read_replay_block(struct fa_row *rows, size_t size)
{
    int row_count = size / sizeof(struct fa_row);
    for (int i = 0; i < row_count; i ++)
    {
        convert_row(&rows[i]);

        replay_index += 1;
        if (replay_index < replay_row_count)
            replay_row += replay_row_size;
        else
        {
            replay_row = replay_first_row;
            replay_index = 0;
        }
    }

    sleep_until(100000 * row_count);    // 100us = 100,000ns per row
    return true;
}


/* Conversion from matlab stored datatypes to the fa_entry values we need.  Alas
 * we have little control over how matlab stores its files, so really we need to
 * support a variety of conversions. */

#define DEFINE_CONVERT(type) \
    static void convert_xy_##type(struct fa_row *row) \
    { \
        struct fa_entry *entry = row->row; \
        for (int j = 0; j < FA_ENTRY_COUNT; j ++) \
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
    return FAIL_("Can't handle data of type %d\n", data_type);
}


/* Prepares data array for replay.  The data has either two or three dimensions,
 * indexed as data[xy, fa_id, time]. */
static void prepare_data_array(
    const struct matlab_matrix *data, size_t data_size, int *columns)
{
    *columns = data->dim_count > 2 ? data->dims[1] : 1;

    replay_row_count = data->dim_count > 2 ? data->dims[2] : data->dims[1];
    replay_first_row = data->real.start;
    replay_index = 0;
    replay_row = replay_first_row;
    replay_row_size = *columns * 2 * data_size;
}

/* Prepares the index array used for replay.  By default the given data just
 * cycles through the available columns, but if an ids array was present it is
 * used to ensure that those ids are correctly replayed. */
static void prepare_index_array(
    bool found_ids, const struct matlab_matrix *ids, int columns)
{
    /* First just cycle through the available columns. */
    for (int i = 0; i < FA_ENTRY_COUNT; i ++)
        column_index[i] = i % columns;

    /* Use any ids to replace column entries. */
    if (found_ids)
    {
        uint8_t *id_array = (uint8_t *) ids->real.start;
        for (int i = 0; i < columns; i ++)
            column_index[id_array[i]] = i;
    }
}


/* Searches matlab file for the arrays we need. */
static bool prepare_replay_data(struct region *region)
{
    bool found_data, found_ids;
    struct matlab_matrix data, ids;
    size_t data_size;
    int columns;
    bool ok =
        /* Prepare and validate the data area. */
        find_matrix_by_name(region, "data", &found_data, &data)  &&
        TEST_OK_(found_data, "No data element in replay file")  &&
        TEST_OK_(data.real.size > 0, "Empty data matrix")  &&
        convert_datatype(data.data_type, &data_size)  &&
        TEST_OK_(
            2 <= data.dim_count  &&  data.dim_count <= 3  &&
            data.dims[0] == 2, "Wrong dimensions for data array")  &&
        TEST_OK_(!data.complex_data, "Unexpected complex data")  &&
        DO_(prepare_data_array(&data, data_size, &columns))  &&

        /* Prepare and validate the array of ids. */
        find_matrix_by_name(region, "ids",  &found_ids,  &ids)  &&
        IF_(found_ids,
            TEST_OK_(
                ids.dim_count == 2  &&  ids.dims[0] == 1,
                "Invalid shape for ids array")  &&
            TEST_OK_(ids.dims[1] == columns, "Ids don't match data")  &&
            TEST_OK_(ids.data_type == miUINT8, "Bad datatype for ids"))  &&
        DO_(prepare_index_array(found_ids, &ids, columns));

    return ok;
}


bool initialise_replay(const char *replay_filename)
{
    int file;
    struct region region;
    return
        TEST_IO_(file = open(replay_filename, O_RDONLY),
            "Unable to open replay file \"%s\"\n", replay_filename)  &&
        /* For simplicity, just map the entire file into memory! */
        map_matlab_file(file, &region)  &&
        prepare_replay_data(&region)  &&

        /* Finally prepare the replay sleep target.  This ensures we pace the
         * data correctly. */
        TEST_IO(clock_gettime(CLOCK_MONOTONIC, &next_sleep));
}
