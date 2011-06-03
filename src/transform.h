/* Header for data transposition and reduction functionality. */


/* Processes a single input block by transposing and decimation.  If a major
 * block is filled then it is also written to disk. */
void process_block(const void *read_block, uint64_t timestamp);


/* Interlocked access. */

/* Returns earliest timestamp in archive. */
uint64_t get_earliest_timestamp(void);
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
const struct data_index * read_index(unsigned int ix);

/* Returns an unlocked pointer to the header: should only be used to access the
 * constant header fields. */
const struct disk_header *get_header(void);


void initialise_transform(
    struct disk_header *header, struct data_index *data_index,
    struct decimated_data *dd_area);

// !!!!!!
// Not right.  Returns DD data area.
const struct decimated_data * get_dd_area(void);
