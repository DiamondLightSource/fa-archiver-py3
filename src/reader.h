/* Implements reading from disk. */

/* scon is the connected socket and buf is the command read from the user: rx
 * bytes have already been received and the buffer is buf_size bytes long.
 * The first character in the buffer is R. */
bool process_read(int scon, const char *buf);

bool initialise_reader(const char *archive);


/* Format of gap data transmitted over the wire. */
struct gap_data {
    uint32_t data_index;
    uint32_t id_zero;
    uint64_t timestamp;
};
