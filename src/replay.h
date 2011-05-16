/* Interface for canned data replay. */
bool initialise_replay(const char *replay_file);

bool read_replay_block(struct fa_row *block, size_t size);
