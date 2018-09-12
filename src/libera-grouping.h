/* Definitions specific to Libera Grouping.
 *
 * This supports Fast Orbit data by transmission over UDP.
 *
 * Unfortunately there seems to be some confusion in the documentation for this
 * format, and in particular the precise layout of the Libera grouping packet
 * appears to depend on the precise version of the implementation.
 *
 * Each BPM data update is sent as a 16-bit frame within the overall UDP packet.
 * The first 12 bytes are fixed, but the layout of the remaining 4 bytes depends
 * on the Grouping generation.
 *
 * In the first implementation of Libera Grouping for which support was
 * implemented here, the last four bytes consist of 2 bytes of counter followed
 * by 2 status and control bytes.  This is largely as documented in
 *  "Libera Grouping+ Specifications", Version 1.02 (2017),
 * where the low 16 bits of the last four bytes (as a little endian word) are
 * documented as the packet number.
 *
 * Unfortunately this appears to differ from the implementation of Grouping+ as
 * seen on the output of Libera Brilliance+ systems with grouping, where the
 * last four bytes are the packet number.  Thus this file contains the
 * appropriate definitions, which are parameterised by the LIBERA_GROUPING
 * #define which must be 0 for the original definition, or 1 for the newer
 * implementation. */

/* In the original Libera Grouping definition the unit id field was only 6 bits
 * long, but as the adjacent bits were reserved and always set to zero, it seems
 * harmless to use 8 bits for both forms. */
struct libera_status {
    uint16_t lock_status : 1;       // 0     MC PLL status (1 if locked)
    uint16_t _unused_1 : 1;         // 1
    uint16_t libera_id : 8;         // 9:2   Libera ID (8 bits)
    uint16_t _unused_2 : 1;         // 10
    uint16_t valid : 1;             // 11    1 for valid data
    uint16_t _unused_3 : 2;         // 13:12
    uint16_t overflow : 1;          // 14    1 for ADC overflow
    uint16_t interlock : 1;         // 15    1 for interlock active
};

/* We can make some simplifying assumptions: both the transmitter and the
 * receiver are little endian machines, so the payload can be mapped directly to
 * the underlying datatypes. */
struct libera_payload {
    int32_t sum;
    int32_t x;
    int32_t y;
#if LIBERA_GROUPING == 0
    uint16_t counter;
    struct libera_status status;
#elif LIBERA_GROUPING == 1
    struct libera_status status;
    uint16_t counter;
#else
    #error "Invalid value for LIBERA_GROUPING, must be 0 or 1"
#endif
} __attribute__((packed));


/* We can receive up to 256 separate updates in a single datagram. */
#define LIBERAS_PER_DATAGRAM    256
#define LIBERAS_ID_MASK         0xFF

/* Each transmission is 16 bytes. */
#define LIBERA_BLOCK_SIZE       (sizeof(struct libera_payload))
