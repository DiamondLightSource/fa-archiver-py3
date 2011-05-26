/* Support for continuous CIC decimated data.  The CIC parameters and
 * compensation filter are read from the command line. */

bool initialise_decimation(
    const char *config_file, struct buffer *fa_buffer, struct buffer **buffer);
/* Starting decimation is separated from initialisation so that we can report
 * initialisation errors as soon as possible. */
bool start_decimation(void);

void terminate_decimation(void);

/* Returns decimation factor, or 0 if decimation not available. */
int get_decimation_factor(void);
