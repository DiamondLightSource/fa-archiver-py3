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


/* Need quite a long line size to handle line joining for array reads. */
#define LINE_SIZE 4096

#define NAME_LENGTH 40


/* Supporting parse routines for arrays. */

/* Generic routine for parsing an array of values. */
static bool parse_array(
    const char **string, parser_t parse_type,
    struct void_array *result, size_t type_size)
{
    /* Start with a sensible upper bound for the number of values and allocate
     * enough for this; we'll then resize back down when we're done.  There
     * can't be more than one element every two characters as separators are
     * mandatory in this parser. */
    int initial_count = (strlen(*string) + 1) / 2;
    char *data = malloc(type_size * initial_count);
    int count = 0;
    bool ok = true;
    bool whitespace = true;     // Did we see whitespace before this?
    while (ok  &&  **string != 0)
    {
        ok =
            TEST_OK(count < initial_count)  &&
            TEST_OK_(whitespace, "Whitespace separator expected")  &&
            parse_type(string, data + type_size * count);
        whitespace = skip_whitespace(string);
        count += 1;
    }
    if (ok)
    {
        result->count = count;
        result->data = realloc(data, type_size * count);
    }
    else
        free(data);
    return ok;
}


bool parse_uint_array(const char **string, struct uint_array *result)
{
    return parse_array(
        string, (parser_t) parse_uint,
        (struct void_array *) result, sizeof(unsigned int));
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
    skip_whitespace(&string);
    if (*string == '\0'  ||  *string == '#')
        /* Empty line or comment, can just ignore. */
        return true;

    /* We'll report all errors on this line against file name and line. */
    push_error_handling();

    /* A valid definition is
     *
     *  name<opt-whitespace>=<opt-whitespace><parse><opt-whitespace>
     *
     * The optional whitespace which our parser doesn't support makes the parse
     * a lot more long winded than it otherwise ought to be. */
    char name[NAME_LENGTH];
    int ix;
    bool ok =
        parse_name(&string, name, NAME_LENGTH)  &&
        DO_(skip_whitespace(&string))  &&
        parse_char(&string, '=')  &&
        DO_(skip_whitespace(&string))  &&
        lookup_name(name, config_table, config_size, &ix)  &&
        config_table[ix].parser(&string, config_table[ix].result)  &&
        DO_(skip_whitespace(&string))  &&
        parse_eos(&string);

    /* Report parse error. */
    char *error = pop_error_handling(!ok);
    if (!ok)
        print_error("Error parsing %s, line %d, offset %zd: %s",
            file_name, line_number, string - line_buffer, error);
    free(error);

    return ok &&
        /* Perform post parse validation. */
        TEST_OK_(!seen[ix],
            "Parameter %s repeated on line %d", name, line_number)  &&
        DO_(seen[ix] = true);
}


/* Wraps the slightly annoying behaviour of fgets.  Returns error status and eof
 * separately, returns length of line read, and removes trailing newline
 * character.  Also returns an error if the buffer is filled. */
static bool read_one_line(
    FILE *input, char *line_buffer, size_t line_length,
    int line_number, size_t *length_read, bool *eof)
{
    errno = 0;
    *eof = fgets(line_buffer, line_length, input) == NULL;
    if (*eof)
    {
        *length_read = 0;
        line_buffer[0] = '\0';
        return TEST_OK_(errno == 0,
            "Error reading file on line %d", line_number);
    }
    else
    {
        *length_read = strlen(line_buffer);
        ASSERT_OK(*length_read > 0);
        if (line_buffer[*length_read - 1] == '\n')
        {
            *length_read -= 1;
            line_buffer[*length_read] = '\0';
            return true;
        }
        else
            return TEST_OK_(*length_read + 1 < line_length,
                "Read buffer overflow on line %d", line_number);
    }
}


/* Reads a single line after joining lines with trailing \ characters.  Fails if
 * line buffer overflows or fgets fails, sets *eof on end of file. */
static bool read_line(
    FILE *input, char *line_buffer, size_t line_length,
    int *line_number, bool *eof)
{
    bool ok = true;
    bool want_line = true;
    while (ok  &&  !*eof  &&  want_line)
    {
        size_t length_read = 0;
        *line_number += 1;
        ok = read_one_line(
            input, line_buffer, line_length, *line_number, &length_read, eof);
        want_line = ok  &&  !*eof  &&
            length_read > 0  &&  line_buffer[length_read - 1] == '\\';
        if (want_line)
        {
            line_buffer += length_read - 1;
            line_length -= length_read - 1;
            ok = TEST_OK_(line_length > 2,
                "Run out of read buffer on line %d", *line_number);
        }
    }
    return ok;
}


bool config_parse_file(
    const char *file_name,
    const struct config_entry *config_table, size_t config_size)
{
    FILE *input = fopen(file_name, "r");
    if (!TEST_NULL_(input, "Unable to open config file \"%s\"", file_name))
        return false;

    /* Array of seen flags for each configuration entry, used to ensure that
     * every needed configuration setting is set. */
    bool seen[config_size];
    memset(seen, 0, sizeof(seen));

    /* Process each line in the file. */
    bool ok = true;
    bool eof = false;
    int line_number = 0;
    while (ok  &&  !eof)
    {
        char line_buffer[LINE_SIZE];
        ok =
            read_line(
                input, line_buffer, sizeof(line_buffer), &line_number, &eof)  &&
            do_parse_line(
                file_name, line_number, line_buffer,
                config_table, config_size, seen);
    }
    fclose(input);

    /* Check that all required entries were present. */
    errno = 0;      // Can linger over into error reporting
    for (size_t i = 0; ok  &&  i < config_size; i ++)
        ok = TEST_OK_(seen[i]  ||  config_table[i].optional,
            "No value specified for parameter: %s", config_table[i].name);

    return ok;
}
