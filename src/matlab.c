/* Support for writing a matlab header on capture FA data.
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
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "error.h"
#include "fa_sniffer.h"
#include "mask.h"

#include "matlab.h"


/* Four byte header mark placed at offset 124 into header, consisting of the
 * version number 0x0100 in bytes 124:125 and the byte order mark MI in bytes
 * 126:127. */
#define MATLAB_HEADER_MARK   0x4d490100          // 0x0100, 'IM'



#define mxDOUBLE_CLASS  6
#define mxINT32_CLASS   12


/* Sizes of matlab formats, dynamically looked up. */
static struct { int format; size_t size; } format_sizes[] = {
    { miINT8,   sizeof(int8_t) },
    { miUINT8,  sizeof(uint8_t) },
    { miINT16,  sizeof(int16_t) },
    { miUINT16, sizeof(uint16_t) },
    { miINT32,  sizeof(int32_t) },
    { miUINT32, sizeof(uint32_t) },
    { miDOUBLE, sizeof(double) },
};

static size_t lookup_size(int format)
{
    for (size_t i = 0; i < ARRAY_SIZE(format_sizes); i ++)
        if (format_sizes[i].format == format)
            return format_sizes[i].size;
    ASSERT_FAIL();
}


unsigned int compute_mask_ids(
    uint16_t *array, struct filter_mask *mask, unsigned int max_bit_count)
{
    unsigned int count = 0;
    for (uint16_t bit = 0; bit < max_bit_count; bit ++)
        if (test_mask_bit(mask, bit))
        {
            *array++ = bit;
            count += 1;
        }
    return count;
}


static void *ensure_buffer(struct matlab_buffer *buffer, size_t size)
{
    ASSERT_OK(buffer->size + size <= buffer->max_size);
    void *result = buffer->buffer + buffer->size;
    buffer->size += size;
    return result;
}

static void write_buffer(struct matlab_buffer *buffer, void *data, size_t size)
{
    void *target = ensure_buffer(buffer, size);
    memcpy(target, data, size);
}

static uint32_t *ensure_buffer_uint32(struct matlab_buffer *buffer)
{
    return ensure_buffer(buffer, sizeof(int32_t));
}

static void write_buffer_uint32(struct matlab_buffer *buffer, uint32_t value)
{
    write_buffer(buffer, &value, sizeof(uint32_t));
}


bool write_matlab_buffer(FILE *output, struct matlab_buffer *buffer)
{
    return TEST_OK(fwrite(buffer->buffer, buffer->size, 1, output) == 1);
}


static void write_matlab_string(
    struct matlab_buffer *buffer, const char *string)
{
    size_t l = strlen(string);
    write_buffer_uint32(buffer, miINT8);
    write_buffer_uint32(buffer, (uint32_t) l);
    /* Need to pad string size to multiple of 8. */
    memcpy(ensure_buffer(buffer, (l + 7) & ~7U), string, l);
}


/* Returns the number of bytes of padding required after data_length bytes of
 * following data to ensure that the entire matrix is padded to 8 bytes. */
unsigned int place_matrix_header(
    struct matlab_buffer *buffer, const char *name, int data_type,
    bool *squeeze, unsigned int dimensions, ...)
{
    va_list dims;
    va_start(dims, dimensions);

    write_buffer_uint32(buffer, miMATRIX);
    /* Remember location where total length will be written. */
    uint32_t *l = ensure_buffer_uint32(buffer);

    // Matrix flags: consists of two uint32 words encoding the class.
    write_buffer_uint32(buffer, miUINT32);
    write_buffer_uint32(buffer, 8);
    write_buffer_uint32(buffer, mxDOUBLE_CLASS);
    write_buffer_uint32(buffer, 0);

    // Matrix dimensions: one int32 for each dimension
    write_buffer_uint32(buffer, miINT32);
    // Size of dimensions to be written here
    uint32_t *dim_size = ensure_buffer_uint32(buffer);
    uint32_t total_dims = 0;
    size_t data_length = lookup_size(data_type);
    for (unsigned int i = 0; i < dimensions; i ++)
    {
        unsigned int size = va_arg(dims, unsigned int);
        data_length *= size;
        if (size == 1  &&  squeeze != NULL  &&  squeeze[i])
            /* Squeeze this dimension out by ignoring it altogether. */
            ;
        else
        {
            write_buffer_uint32(buffer, size);
            total_dims += 1;
        }
    }
    *dim_size = total_dims * (uint32_t) sizeof(int32_t);
    if (total_dims & 1)
        write_buffer_uint32(buffer, 0);         // Padding if required

    // Element name
    write_matlab_string(buffer, name);

    // Data header: data follows directly after.
    write_buffer_uint32(buffer, (uint32_t) data_type);
    write_buffer_uint32(buffer, (uint32_t) data_length);

    /* Total size of matrix element goes from just after l to the end of the
     * data that's about to be written plus padding to multiple of 8 bytes. */
    unsigned int padding = (8 - data_length) & 7;
    *l = (uint32_t) (
        (size_t) (ensure_buffer(buffer, 0) - (void *) l) -
        sizeof(int32_t) + data_length + padding);

    va_end(dims);
    return padding;
}


void place_matlab_value(
    struct matlab_buffer *buffer, const char *name, int data_type, void *data)
{
    size_t data_size = lookup_size(data_type);
    unsigned int padding =
        place_matrix_header(buffer, name, data_type, NULL, 1, 1);
    memcpy(ensure_buffer(buffer, data_size + padding), data, data_size);
}


void place_matlab_vector(
    struct matlab_buffer *buffer, const char *name, int data_type,
    void *data, unsigned int vector_length)
{
    size_t data_length = lookup_size(data_type) * vector_length;
    unsigned int padding = place_matrix_header(
        buffer, name, data_type, NULL, 2, 1, vector_length);
    memcpy(ensure_buffer(buffer, data_length + padding), data, data_length);
}


void prepare_matlab_header(struct matlab_buffer *buffer)
{
    const char *description =
        "MATLAB 5.0 MAT-file generated from FA sniffer data";
    size_t l = strlen(description);

    char *mat_header = ensure_buffer(buffer, 124);
    memcpy(mat_header, description, l);
    memset(mat_header + l, ' ', 124 - l);
    write_buffer_uint32(buffer, MATLAB_HEADER_MARK);
}


unsigned int count_data_bits(unsigned int mask)
{
    return
        ((mask >> 0) & 1) + ((mask >> 1) & 1) +
        ((mask >> 2) & 1) + ((mask >> 3) & 1);
}


double matlab_timestamp(uint64_t timestamp, time_t local_offset)
{
    return MATLAB_EPOCH +
        (1e-6 * (double) timestamp + (double) local_offset) / SECS_PER_DAY;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Matlab file reading routines. */


bool nonempty_region(const struct region *region)
{
    return region->ptr < region->start + region->size;
}


bool map_matlab_file(int file, struct region *region)
{
    struct stat st;
    void *base;
    return
        TEST_IO(fstat(file, &st))  &&
        TEST_OK_(st.st_size > 128, "Matlab file too small")  &&
        TEST_IO(
            base = mmap(
                NULL, (size_t) st.st_size, PROT_READ, MAP_SHARED, file, 0))  &&
        TEST_OK_(
            *((uint32_t *)(base + 124)) == MATLAB_HEADER_MARK,
            "Invalid matlab header")  &&
        DO_(
            region->start = base;
            region->size = (size_t) st.st_size;
            region->ptr = base + 128);
}


bool read_data_element(struct region *region, struct region *result, int *type)
{
    size_t space = region->size - (size_t) (region->ptr - region->start);
    if (space < 8)
        return FAIL_("Region too small for any more data");
    else
    {
        uint32_t *elements = (uint32_t *) region->ptr;
        uint32_t tag = elements[0];
        if ((tag >> 16) != 0)
        {
            /* Special compact format with type and size compressed into one
             * word. */
            *type = tag & 0xffff;
            unsigned int size = tag >> 16;
            result->start = region->ptr + 4;
            result->ptr   = region->ptr + 4;
            result->size = size;
            region->ptr += 8;
            return TEST_OK_(size <= 4, "Unexpected large small data element");
        }
        else
        {
            /* Normal format. */
            *type = (int) tag;
            unsigned int size = elements[1];
            result->size = size;
            result->start = region->ptr + 8;
            result->ptr   = region->ptr + 8;
            if (tag != miCOMPRESSED)
                /* Except when processing a compressed tag round the size up to
                 * a multiple of 8 bytes for padding.  Strictly speaking this
                 * might not be sufficient if compressed and uncompressed data
                 * is mixed in the same file ... but in this case the padding
                 * rule is unclear anyway. */
                size = (size + 7) & ~7U;
            region->ptr += 8 + size;
            return TEST_OK_(
                size <= space - 8, "Data element larger than region");
        }
    }
}


static bool validate_matrix_dimensions(const struct matlab_matrix *matrix)
{
    size_t size = lookup_size(matrix->data_type);
    bool ok = true;
    for (unsigned int i = 0; ok  &&  i < matrix->dim_count; i ++)
    {
        ok = TEST_OK_(matrix->dims[i] > 0, "Negative dimension!");
        size *= matrix->dims[i];
    }
    return TEST_OK_(size == matrix->real.size, "Array size mismatch");
}


bool read_matlab_matrix(
    const struct region *region, struct matlab_matrix *matrix)
{
    struct region input = *region;
    struct region field;
    int type;
    matrix->imag = (struct region) { NULL, 0, {NULL} };
    return
        /* First we expect an miUINT32 of 8 bytes. */
        read_data_element(&input, &field, &type)  &&
        TEST_OK_(type == miUINT32  &&  field.size == 8,
            "Expected array flags")  &&
        DO_(
            matrix->complex_data = !!(field.ptr_char[1] & 0x08);
            matrix->logical_data = !!(field.ptr_char[1] & 0x02);
            matrix->data_class = field.ptr_char[0])  &&

        /* Next the dimensions array. */
        read_data_element(&input, &field, &type)  &&
        TEST_OK_(type == miINT32, "Expected dimensions array")  &&
        DO_(
            matrix->dim_count = (unsigned int) field.size / 4;
            matrix->dims = field.start)  &&

        /* The array name. */
        read_data_element(&input, &field, &type)  &&
        TEST_OK_(type == miINT8, "Expected array name")  &&
        DO_(
            matrix->name_length = field.size;
            matrix->name = field.start)  &&

        /* The real part of the data and optionally the imaginary part. */
        read_data_element(&input, &matrix->real, &matrix->data_type)  &&
        IF_(matrix->complex_data,
            read_data_element(&input, &matrix->imag, &type)  &&
            TEST_OK_(
                type == matrix->data_type  &&
                matrix->imag.size == matrix->real.size,
                "Imaginary and real parts should be the same type of data"))  &&

        validate_matrix_dimensions(matrix)  &&

        /* That's supposed to be the lot! */
        TEST_OK_(!nonempty_region(&input), "Unexpected extra data");
}


bool find_matrix_by_name(
    const struct region *region, const char *name,
    bool *found, struct matlab_matrix *matrix)
{
    struct region input = *region;
    struct region element;
    *found = false;
    bool ok = true;
    while (ok  &&  nonempty_region(&input))
    {
        int type;
        ok = read_data_element(&input, &element, &type);
        if (ok  &&  type == miMATRIX)
        {
            ok = read_matlab_matrix(&element, matrix);
            if (ok  &&
                strncmp(name, matrix->name, matrix->name_length) == 0)
            {
                *found = true;
                return true;
            }
        }
    }
    return ok;
}
