/* Matlab support interface. */

/* The matlab format symbol definitions we use.  Note that matlab is very buggy
 * when it comes to interpreting these formats, and only the following format
 * types are known to work in matlab arrays: miUINT8, miINT32, miDOUBLE.  In
 * particular miUINT32 definitely doesn't work properly! */
#define miINT8          1
#define miUINT8         2
#define miINT16         3
#define miUINT16        4
#define miINT32         5
#define miUINT32        6
#define miDOUBLE        9

#define miMATRIX        14
#define miCOMPRESSED    15


/* Functions for writing matlab files. */
void prepare_matlab_header(int32_t **hh, size_t buf_size);
int place_matrix_header(
    int32_t **hh, const char *name, int data_type,
    bool *squeeze, int data_length, int dimensions, ...);
void place_matlab_value(
    int32_t **hh, const char *name, int data_type, void *data);
void place_matlab_vector(
    int32_t **hh, const char *name, int data_type,
    void *data, int vector_length);


/* Functions for reading matlab files. */

/* Structures used to represent an in-memory matlab file as we process it.  Also
 * used for matlab array fields which have a similar element sub-structure. */
struct region
{
    void *start;            // Start of memory region
    size_t size;            // Size of memory region
    union {
        void *ptr;              // Current pointer into memory region
        char *ptr_char;
    };
};

/* Maps matlab file into memory, returning the mapped memory region. */
bool map_matlab_file(int file, struct region *region);
/* Checks if the given region has been consumed. */
bool nonempty_region(const struct region *region);
/* Reads a matlab data element from a region. */
bool read_data_element(struct region *region, struct region *result, int *type);

/* This structure contains all the information about an miMATRIX element. */
struct matlab_matrix
{
    bool complex_data;      // Set if imag is populated with imaginary data
    bool logical_data;      // Set if the data is boolean
    int data_type, data_class;  // Type of stored data and target class
    int dim_count;          // Number of dimensions
    int32_t *dims;          // Array of dimensions
    const char *name;       // Name of this data array (not null terminated)
    size_t name_length;     // Length of name
    struct region real;     // Real data
    struct region imag;     // Imaginary data if present
};

/* When called on a data element of type miMATRIX populates the matlab_matrix
 * structure accordingly. */
bool read_matlab_matrix(
    const struct region *region, struct matlab_matrix *matrix);
/* Searches for an miMATRIX element with the given name. */
bool find_matrix_by_name(
    const struct region *region, const char *name,
    bool *found, struct matlab_matrix *result);


unsigned int count_data_bits(unsigned int mask);
int compute_mask_ids(uint8_t *array, struct filter_mask *mask);

/* Converts a timestamp in FA sniffer format (microseconds in Unix epoch) to a
 * timestamp in matlab format (double days in Matlab epoch).  As matlab times
 * are normally in local time the local time offset is also passed. */
double matlab_timestamp(uint64_t timestamp, time_t local_offset);
