/* Filter mask routines.
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

/* Bit mask of BPM ids, array of bits. */
struct filter_mask
{
    uint8_t mask[MAX_FA_ENTRY_COUNT/8];
};

static inline void copy_mask(
    struct filter_mask *dest, const struct filter_mask *src)
{
    memcpy(dest, src, sizeof(struct filter_mask));
}

static inline void set_mask_bit(struct filter_mask *mask, unsigned int bit)
{
    mask->mask[bit >> 3] |= (uint8_t) (1 << (bit & 7));
}

static inline bool __pure test_mask_bit(
    const struct filter_mask *mask, unsigned int bit)
{
    return !!(mask->mask[bit >> 3] & (1 << (bit & 7)));
}

/* Returns number of bits set in mask. */
unsigned int __pure count_mask_bits(
    const struct filter_mask *mask, unsigned int fa_entry_count);


#define RAW_MASK_BYTES  (MAX_FA_ENTRY_COUNT / 4 + 2)

/* Formats string represetation of mask into buffer, which must be at least
 * RAW_MASK_BYTES bytes long.  Returns number of characters written. */
unsigned int format_raw_mask(
    const struct filter_mask *mask, unsigned int fa_entry_count, char *buffer);

/* Formats mask into buffer which must be at least RAW_MASK_BYTES bytes long
 * using either readable or raw format, depending on the optimum format.
 * Returns number of characters written. */
unsigned int format_mask(
    const struct filter_mask *mask, unsigned int fa_entry_count, char *buffer);

/* Formats mask in canonical form into the given string buffer.  If the buffer
 * overflows an error is reported and false returned. */
bool format_readable_mask(
    const struct filter_mask *mask, unsigned int fa_entry_count,
    char *string, size_t length);

/* Attempts to parse string as a mask specification, consisting of a sequence
 * of comma separated numbers or ranges, where a range is a pair of numbers
 * separated by -.  In other words:
 *
 *  mask = id [ "-" id ] [ "," mask ]
 *
 * Prints error message and returns false if parsing fails or mask is empty. */
bool parse_mask(
    const char **string, unsigned int fa_entry_count, struct filter_mask *mask);
