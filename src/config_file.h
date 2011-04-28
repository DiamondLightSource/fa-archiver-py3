/* Simple support for reading a fairly generic config file.
 *
 * The content of the config file is specified as an array of config_entry
 * structures, each defining how the corresponding config entry is to be
 * processed. */

typedef bool (*parser_t)(const char **string, void *result);

struct config_entry
{
    const char *name;
    void *result;
    parser_t parser;
    bool optional;
};

#define CONFIG(variable, parser, optional...) \
    { #variable, &variable, (parser_t) parser, ## optional }
#define OPTIONAL true

bool config_parse_file(
    const char *file_name, const struct config_entry *config_table,
    size_t config_size);

#define DECLARE_ARRAY_TYPE(type) \
    struct type##_array \
    { \
        int count; \
        type *data; \
    }

DECLARE_ARRAY_TYPE(int);
DECLARE_ARRAY_TYPE(double);
DECLARE_ARRAY_TYPE(void);

bool parse_int_array(const char **string, struct int_array *result);
bool parse_double_array(const char **string, struct double_array *result);
