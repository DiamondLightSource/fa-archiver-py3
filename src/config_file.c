/* Configuration file parsing. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include "error.h"
#include "parse.h"

#include "config_file.h"


/* Absurdly long line size. */
#define LINE_SIZE 1024

#define NAME_LENGTH 40


/* Supporting parse routines for arrays. */

/* Generic routine for parsing an array of values. */
static bool parse_array(
    const char **string, parser_t parse_type,
    struct void_array *result, size_t type_size)
{
    /* We take our arrays to be self sizing.  This means we need some way to
     * determine the size.  It's going to be easiest to simply parse the data
     * twice. */
    const char *array_string = *string;
    char dummy[type_size];
    int count = 0;
    while (parse_whitespace(string)  &&  **string != '\0')
    {
        if (!parse_type(string, dummy))
            return false;
        count += 1;
    }

    /* We assume that nothing can go wrong on the second pass! */
    result->count = count;
    result->data = malloc(type_size * count);
    for (int i = 0; i < count; i++)
    {
        parse_whitespace(&array_string);
        parse_type(&array_string, (char *) result->data + i * type_size);
    }
    return true;
}


bool parse_int_array(const char **string, struct int_array *result)
{
    return parse_array(
        string, (parser_t) parse_int,
        (struct void_array *) result, sizeof(int));
}

bool parse_double_array(const char **string, struct double_array *result)
{
    return parse_array(
        string, (parser_t) parse_double,
        (struct void_array *) result, sizeof(double));
}



static bool parse_name(const char **string, char *name, size_t length)
{
    bool ok = TEST_OK_(isalpha(**string), "Not a valid name");
    while (ok  &&  (isalnum(**string)  ||  **string == '_'))
    {
        *name++ = *(*string)++;
        length -= 1;
        ok = TEST_OK_(length > 0, "Name too long");
    }
    if (ok)
        *name = '\0';
    return ok;
}


static bool lookup_name(
    const char *name,
    const struct config_entry *config_table, size_t config_size, int *ix)
{
    for (size_t i = 0; i < config_size; i ++)
        if (strcmp(name, config_table[i].name) == 0)
        {
            *ix = i;
            return true;
        }
    return FAIL_("Identifier %s not known", name);
}


static bool do_parse_line(
    const char *file_name, int line_number, const char *line_buffer,
    const struct config_entry *config_table, size_t config_size, bool *seen)
{
    const char *string = line_buffer;
    parse_whitespace(&string);
    if (*string == '\0'  ||  *string == '#')
        /* Empty line or comment, can just ignore. */
        return true;

    /* We'll report all errors on this line against file name and line. */
    push_error_handling();

    /* A valid name is simply
     *  name = <parse>
     * but the details of the parse end up being a little involved.  One
     * annoyance is we have to deliberately skip whitespace. */
    char name[NAME_LENGTH];
    int ix;
    bool ok =
        parse_name(&string, name, NAME_LENGTH)  &&
        parse_whitespace(&string)  &&
        parse_char(&string, '=')  &&
        lookup_name(name, config_table, config_size, &ix)  &&
        TEST_OK_(!seen[ix], "Parameter %s repeated", name)  &&
        config_table[ix].parser(&string, config_table[ix].result)  &&
        parse_whitespace(&string)  &&
        parse_eos(&string)  &&
        DO_(seen[ix] = true);

    /* Report any error. */
    char *error = pop_error_handling(true);
    if (!ok)
        print_error("Error parsing %s, line %d: %s at offset %d",
            file_name, line_number, error, string - line_buffer);
    free(error);
    return ok;
}


bool config_parse_file(
    const char *file_name,
    const struct config_entry *config_table, size_t config_size)
{
    FILE *input = fopen(file_name, "r");
    if (!TEST_NULL_(input, "Unable to open config file \"%s\"", file_name))
        return false;

    /* Array of seen flags for each configuration entry. */
    bool seen[config_size];
    memset(seen, 0, sizeof(seen));

    /* Process each line in the file. */
    bool ok = true;
    int line = 1;
    char line_buffer[LINE_SIZE];
    while (ok  &&  fgets(line_buffer, sizeof(line_buffer), input))
    {
        int length = strlen(line_buffer);
        ok =
            TEST_OK(length > 0)  &&
            TEST_OK_(line_buffer[length - 1] == '\n',
                "Line %d possibly truncated", line)  &&
            DO_(line_buffer[length - 1] = '\n')  &&
            do_parse_line(
                file_name, line, line_buffer,
                config_table, config_size, seen);
        line += 1;
    }

    /* Check that all required entries were present. */
    errno = 0;
    for (size_t i = 0; ok  &&  i < config_size; i ++)
        ok = TEST_OK_(seen[i]  ||  config_table[i].optional,
            "No value specified for parameter: %s", config_table[i].name);

    fclose(input);
    return ok;
}
