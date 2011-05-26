/* Interface to sniffer capture routines. */

struct buffer;
bool initialise_sniffer(
    struct buffer *buffer, const char *device_name, bool replay);
bool start_sniffer(bool boost_priority);

void terminate_sniffer(void);
