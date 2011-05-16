/* Filter mask routines. */

/* Bit mask of BPM ids, array of 256 bits. */
struct filter_mask
{
    uint32_t mask[FA_ENTRY_COUNT/32];
};

static inline void copy_mask(
    struct filter_mask *dest, const struct filter_mask *src)
{
    memcpy(dest, src, sizeof(struct filter_mask));
}

static inline void set_mask_bit(struct filter_mask *mask, unsigned int bit)
{
    mask->mask[bit >> 5] |= 1 << (bit & 0x1f);
}

static inline bool test_mask_bit(
    const struct filter_mask *mask, unsigned int bit)
{
    return !!(mask->mask[bit >> 5] & (1 << (bit & 0x1f)));
}

/* Returns number of bits set in mask. */
unsigned int count_mask_bits(const struct filter_mask *mask);


#define RAW_MASK_BYTES  (FA_ENTRY_COUNT / 4)

/* Formats string represetation of mask into buffer, which must be at least
 * RAW_MASK_BYTES+1 bytes long.  Returns RAW_MASK_BYTES, number of characters
 * written. */
int format_raw_mask(const struct filter_mask *mask, char *buffer);

/* Attempts to parse string as a mask specification, consisting of a sequence
 * of comma separated numbers or ranges, where a range is a pair of numbers
 * separated by -.  In other words:
 *
 *  mask = id [ "-" id ] [ "," mask ]
 *
 * Prints error message and returns false if parsing fails. */
bool parse_mask(const char **string, struct filter_mask *mask);

/* Formats mask in canonical form into the given string buffer.  If the buffer
 * overflows an error is reported and false returned. */
bool format_mask(struct filter_mask *mask, char *string, size_t length);

/* Copies a single FA frame taking the mask into account, returns the number
 * of bytes copied into the target buffer (will be 8*count_mask_bits(mask)).
 * 'from' should point to a completely populated frame, 'to' will contain X,Y
 * pairs in ascending numerical order for bits set in mask. */
int copy_frame(void *to, const void *from, const struct filter_mask *mask);

/* Writes the selected number of masked frames to the given file, returning
 * false if writing fails. */
bool write_frames(
    int file, const struct filter_mask *mask, const void *frame, int count);
