/* Common parsing support.
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

/* All of these parse routines take two arguments and return a boolean value:
 *
 *  bool parse_<object>(const char **string, typeof(<object>) *result);
 *
 * After a successful parse *string points after the parsed part of the string,
 * otherwise it points to the point where an error was detected. */

bool parse_int(const char **string, int *result);
bool parse_uint(const char **string, unsigned int *result);
bool parse_uint32(const char **string, uint32_t *result);
bool parse_uint64(const char **string, uint64_t *result);
bool parse_double(const char **string, double *result);

/* Integer possibly followed by K or M. */
bool parse_size32(const char **string, uint32_t *result);
/* Integer possibly followed by K, M, G or T. */
bool parse_size64(const char **string, uint64_t *result);

/* Parses date and time in ISO format with an optional trailing nanoseconds
 * part, ie:
 *      yyyy-mm-ddThh:mm:ss[.nnnnnnnnnn][Z] . */
struct timespec;
bool parse_datetime(const char **string, struct timespec *ts);
/* Parses time of day in ISO 8601 format with optional nanoseconds:
 *      hh:mm:ss[.nnnnnnnnnn] . */
bool parse_time(const char **string, struct timespec *ts);
/* Parses timestamp in format: secs[.nnn] */
bool parse_seconds(const char **string, struct timespec *ts);
/* Parses fractional part of number: [.nnn] */
bool parse_nanoseconds(const char **string, long *nsec);

/* Only succeeds if **string=='\0', ie end of string. */
bool parse_eos(const char **string);

/* Checks for presence of ch, consumes it if present.  No error is generated if
 * ch is not found, unlike the parse functions. */
bool read_char(const char **string, char ch);
/* Like read_char(), but generates an error if ch is not found. */
bool parse_char(const char **string, char ch);

/* Parses over whitespace, fails if not found. */
bool parse_whitespace(const char **string);
/* Skips over optional whitespace, returns true if whitespace seen. */
bool skip_whitespace(const char **string);

/* Wraps parsing of a complete string and generation of a suitable error
 * message. */
#define DO_PARSE(message, parse, string, result...) \
    ( { \
      const char *string_in__ = (string); \
      const char *__string__ = string_in__; \
      push_error_handling(); \
      bool __parse_ok = (parse)(&__string__, ##result); \
      report_parse_error((message), __parse_ok, string_in__, __string__); \
    } )

/* This must be called with push_error_handling() in force, and will call
 * pop_error_handling() before returning.  Designed to be wrapped by the
 * DO_PARSE macro above. */
bool report_parse_error(
    const char *message, bool ok, const char *start, const char *end);
