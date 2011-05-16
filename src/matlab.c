/* Support for writing a matlab header on capture FA data. */

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

#include "sniffer.h"
#include "mask.h"
#include "error.h"

#include "matlab.h"


/* The delta (in days) between the Unix and matlab epochs, generated by running
 *  datenum(1970, 1, 1)
 * in matlab. */
#define MATLAB_EPOCH    719529
#define SECS_PER_DAY    (24 * 60 * 60)




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


int compute_mask_ids(uint8_t *array, struct filter_mask *mask)
{
    int count = 0;
    for (int bit = 0; bit < 256; bit ++)
        if (test_mask_bit(mask, bit))
        {
            *array++ = bit;
            count += 1;
        }
    return count;
}


static void write_matlab_string(int32_t **hh, const char *string)
{
    int32_t *h = *hh;
    int l = strlen(string);
    *h++ = miINT8;      *h++ = l;
    memcpy(h, string, l);
    *hh = h + 2 * ((l + 7) / 8);
}


/* Returns the number of bytes of padding required after data_length bytes of
 * following data to ensure that the entire matrix is padded to 8 bytes. */
int place_matrix_header(
    int32_t **hh, const char *name, int data_type,
    bool *squeeze, int data_length, int dimensions, ...)
{
    va_list dims;
    va_start(dims, dimensions);

    int32_t *h = *hh;
    *h++ = miMATRIX;
    int32_t *l = h++;   // total length will be written here.
    // Matrix flags: consists of two uint32 words encoding the class.
    *h++ = miUINT32;    *h++ = 8;
    *h++ = mxDOUBLE_CLASS;
    *h++ = 0;

    // Matrix dimensions: one int32 for each dimension
    *h++ = miINT32;
    int32_t *dim_size = h++;    // Size of dimensions to be written here
    int squeezed_dims = 0;
    for (int i = 0; i < dimensions; i ++)
    {
        int size = va_arg(dims, int32_t);
        if (size == 1  &&  squeeze != NULL  &&  squeeze[i])
            /* Squeeze this dimension out by ignoring it altogether. */
            ;
        else
        {
            *h++ = size;
            squeezed_dims += 1;
        }
    }
    *dim_size = squeezed_dims * sizeof(int32_t);
    h += squeezed_dims & 1;    // Padding if required

    // Element name
    write_matlab_string(&h, name);

    // Data header: data follows directly after.
    int padding = (8 - data_length) & 7;
    *h++ = data_type;   *h++ = data_length;
    *l = data_length + (h - l - 1) * sizeof(int32_t) + padding;

    *hh = h;
    return padding;
}


/* Advances pointer by length together with the precomputed padding. */
static void pad(int32_t **hh, int length, int padding)
{
    *hh = (int32_t *)((char *)*hh + length + padding);
}


void place_matlab_value(
    int32_t **hh, const char *name, int data_type, void *data)
{
    size_t data_size = lookup_size(data_type);
    int padding = place_matrix_header(
        hh, name, data_type, NULL, data_size, 1, 1);
    memcpy(*hh, data, data_size);
    pad(hh, data_size, padding);
}


void place_matlab_vector(
    int32_t **hh, const char *name, int data_type,
    void *data, int vector_length)
{
    int data_length = lookup_size(data_type) * vector_length;
    int padding = place_matrix_header(
        hh, name, data_type, NULL, data_length, 2, 1, vector_length);
    memcpy(*hh, data, data_length);
    pad(hh, data_length, padding);
}


void prepare_matlab_header(int32_t **hh, size_t buf_size)
{
    char *mat_header = (char *) *hh;
    memset(mat_header, 0, buf_size);

    /* The first 128 bytes are the description and format marks. */
    memset(mat_header, ' ', 124);
    sprintf(mat_header, "MATLAB 5.0 MAT-file generated from FA sniffer data");
    mat_header[strlen(mat_header)] = ' ';
    *(uint16_t *)&mat_header[124] = 0x0100;   // Version flag
    *(uint16_t *)&mat_header[126] = 0x4d49;   // 'IM' endian mark
    *hh = (int32_t *)&mat_header[128];
}


unsigned int count_data_bits(unsigned int mask)
{
    return
        ((mask >> 0) & 1) + ((mask >> 1) & 1) +
        ((mask >> 2) & 1) + ((mask >> 3) & 1);
}


double matlab_timestamp(uint64_t timestamp, time_t local_offset)
{
    return MATLAB_EPOCH + (1e-6 * timestamp + local_offset) / SECS_PER_DAY;
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
    char *base;
    return
        TEST_IO(fstat(file, &st))  &&
        TEST_OK_(st.st_size > 128, "Matlab file too small")  &&
        TEST_IO(
            base = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, file, 0))  &&
        TEST_OK_(
            *((uint32_t *)(base + 124)) == 0x4d490100,
            "Invalid matlab header")  &&
        DO_(
            region->start = base;
            region->size = st.st_size;
            region->ptr = base + 128);
}


bool read_data_element(struct region *region, struct region *result, int *type)
{
    size_t space = region->size - (region->ptr - region->start);
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
            *type = tag;
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
                size = (size + 7) & ~7;
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
    for (int i = 0; ok  &&  i < matrix->dim_count; i ++)
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
    matrix->imag = (struct region) { 0, 0, 0 };
    return
        /* First we expect an miUINT32 of 8 bytes. */
        read_data_element(&input, &field, &type)  &&
        TEST_OK_(type == miUINT32  &&  field.size == 8,
            "Expected array flags")  &&
        DO_(
            matrix->complex_data = !!(field.ptr[1] & 0x08);
            matrix->logical_data = !!(field.ptr[1] & 0x02);
            matrix->data_class = field.ptr[0])  &&

        /* Next the dimensions array. */
        read_data_element(&input, &field, &type)  &&
        TEST_OK_(type == miINT32, "Expected dimensions array")  &&
        DO_(
            matrix->dim_count = field.size / 4;
            matrix->dims = (int32_t *) field.start)  &&

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
