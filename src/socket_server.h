/* Interface to archive server.
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

bool initialise_server(
    struct buffer *fa_buffer, struct buffer *decimated,
    unsigned int events_fa_id, const char *server_name,
    const char *bind_address, int port, bool extra, bool reuseaddr);
bool start_server(void);
void terminate_server(void);

/* Reports error status on the connected socket and calls pop_error_handling().
 * If there is no error to report then a single null byte is written to the
 * socket to signal a valid status. */
bool report_socket_error(int scon, const char *client_name, bool ok);
/* Controls buffering of socket. */
bool set_socket_cork(int sock, bool cork);
