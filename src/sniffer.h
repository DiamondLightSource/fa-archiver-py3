/* Interface to sniffer capture routines. */

struct fa_entry { int32_t x, y; };

/* Size of a single FA frame in bytes: 256 entries, each consisting of two 4
 * byte integers. */
#define FA_ENTRY_SIZE   (sizeof(struct fa_entry))
#define FA_ENTRY_COUNT  256
#define FA_FRAME_SIZE   (FA_ENTRY_COUNT * FA_ENTRY_SIZE)

/* Type for an entire row representing a single FA frame.  The row is packaged
 * as a structure to avoid annoying C confusion between arrays and pointers with
 * the raw type. */
struct fa_row { struct fa_entry row[FA_ENTRY_COUNT]; };


struct buffer;
bool initialise_sniffer(
    struct buffer *buffer, const char *device_name, bool replay);
bool start_sniffer(bool boost_priority);

void terminate_sniffer(void);
