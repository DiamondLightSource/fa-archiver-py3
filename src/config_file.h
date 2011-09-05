/* Simple support for reading a fairly generic config file.
 *
 * Copyright (c) 2011 Michael Abbott, Diamond Light Source Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Contact:
 *      Dr. Michael Abbott,
 *      Diamond Light Source Ltd,
 *      Diamond House,
 *      Chilton,
 *      Didcot,
 *      Oxfordshire,
 *      OX11 0DE
 *      michael.abbott@diamond.ac.uk
 */

/* The content of the config file is specified as an array of config_entry
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

#define DECLARE_ARRAY_TYPE(type, type_name) \
    struct type_name##_array \
    { \
        unsigned int count; \
        type *data; \
    }

DECLARE_ARRAY_TYPE(unsigned int, uint);
DECLARE_ARRAY_TYPE(double, double);
DECLARE_ARRAY_TYPE(void, void);

bool parse_uint_array(const char **string, struct uint_array *result);
bool parse_double_array(const char **string, struct double_array *result);
