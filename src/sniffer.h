/* Interface to sniffer capture routines. */

struct buffer;
bool initialise_sniffer(
    struct buffer *buffer, const char *device_name, bool replay);
bool start_sniffer(bool boost_priority);

bool get_sniffer_status(struct fa_status *status);
bool interrupt_sniffer(void);

void terminate_sniffer(void);
