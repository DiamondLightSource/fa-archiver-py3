/* Interface to sniffer capture routines.
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

/* Abstraction of sniffer device interface so we can implement debug or
 * alternate versions of the sniffer. */
struct sniffer_context
{
    bool (*reset)(void);
    bool (*read)(struct fa_row *block, size_t block_size);
    bool (*status)(struct fa_status *status);
    bool (*interrupt)(void);
};

const struct sniffer_context *initialise_sniffer_device(
    const char *device_name);

struct buffer;
void configure_sniffer(
    struct buffer *buffer, const struct sniffer_context *sniffer_context);
bool start_sniffer(bool boost_priority);


bool get_sniffer_status(struct fa_status *status);
bool interrupt_sniffer(void);

void terminate_sniffer(void);
