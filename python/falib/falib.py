# Simple FA capture library for reading data from the FA sniffer.

# Copyright (c) 2011 Michael Abbott, Diamond Light Source Ltd.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# Contact:
#      Dr. Michael Abbott,
#      Diamond Light Source Ltd,
#      Diamond House,
#      Chilton,
#      Didcot,
#      Oxfordshire,
#      OX11 0DE
#      michael.abbott@diamond.ac.uk

DEFAULT_SERVER = 'fa-archiver.diamond.ac.uk'
DEFAULT_PORT = 8888

import socket
import struct
import numpy
import cothread


__all__ = [
    'connection', 'subscription', 'get_sample_frequency', 'get_decimation',
    'Server']


def format_mask(mask):
    '''Returns number of bits set in mask and a mask request suitable for
    sending to the server.  The parameter mask should be a list of FA ids.'''

    # Normalise the mask by removing duplicates and sorting into order.
    mask = sorted(list(set(mask)))
    count = len(mask)

    # Format mask pattern
    ranges = []
    first = mask[0]
    last = mask[0]
    for id in mask[1:] + [None]:
        if id != last + 1:
            # New id breaks range, complete the range and write it out.
            if last == first:
                ranges.append('%d' % last)
            else:
                ranges.append('%d-%d' % (first, last))
            first = id
        last = id

    return count, ','.join(ranges)


class connection:
    class EOF(Exception):
        pass
    class Error(Exception):
        pass

    def __init__(self, server = DEFAULT_SERVER, port = DEFAULT_PORT):
        self.sock = socket.create_connection((server, port))
        self.sock.setblocking(0)
        self.buf = []

    def close(self):
        self.sock.close()

    def recv(self, block_size = 65536, timeout = 1):
        if not cothread.poll_list(
                [(self.sock.fileno(), cothread.POLLIN)], timeout):
            raise socket.timeout('Receive timeout')
        chunk = self.sock.recv(block_size)
        if not chunk:
            raise self.EOF('Connection closed by server')
        return chunk

    def read_block(self, length):
        result = numpy.empty(length, dtype = numpy.int8)
        rx = 0
        buf = self.buf
        while True:
            l = len(buf)
            if l:
                if rx + l <= length:
                    result.data[rx:rx+l] = buf
                    buf = []
                else:
                    result.data[rx:] = buf[:length - rx]
                    buf = buf[length - rx:]
                rx += l
                if rx >= length:
                    break
            buf = self.recv()
        self.buf = buf
        return result


class subscription(connection):
    '''s = subscription(bpm_list, decimated, server, port)

    Creates a stream connection to the given FA archiver server (or the default
    server if not specified) returning continuous data for the selected bpms.
    The s.read() method must be called frequently enough to ensure that the
    connection to the server doesn't overflow.
    '''

    def __init__(self, mask, decimated=False, uncork=False, **kargs):
        connection.__init__(self, **kargs)
        self.count, format = format_mask(mask)
        self.decimated = decimated

        flags = ''
        if uncork: flags = flags + 'U'
        if decimated: flags = flags + 'D'
        self.sock.send('S%s%s\n' % (format, flags))
        c = self.recv(1)
        if c != chr(0):
            raise self.Error((c + self.recv())[:-1])    # Discard trailing \n

    def read(self, samples):
        '''Returns a waveform of samples indexed by sample count, bpm count
        (from the original subscription) and channel, thus:
            wf = s.read(N)
        wf[n, b, x] = sample n of BPM b on channel x, where x=0 for horizontal
        position and x=1 for vertical position.'''
        raw = self.read_block(8 * samples * self.count)
        array = numpy.frombuffer(raw, dtype = numpy.int32)
        return array.reshape((samples, self.count, 2))


def server_command(command, **kargs):
    server = connection(**kargs)
    server.sock.send(command)
    result = server.recv()
    server.close()
    return result


def get_sample_frequency(**kargs):
    return float(server_command('CF\n', **kargs))

def get_decimation(**kargs):
    return int(server_command('CC\n', **kargs))


class Server:
    '''A simple helper class to gather together the information required to
    identify the requested server and act as a proxy for the useful commands in
    this module.'''

    def __init__(self, server = DEFAULT_SERVER, port = DEFAULT_PORT):
        self.server = server
        self.port = port

        response = self.server_command('CFCK\n').split('\n')
        self.sample_frequency = float(response[0])
        self.decimation = int(response[1])
        try:
            self.fa_id_count = int(response[2])
        except ValueError:
            self.fa_id_count = 256      # If server responds with error message

    def server_command(self, command):
        return server_command(command, server = self.server, port = self.port)

    def subscription(self, mask, **kargs):
        return subscription(
            mask, server = self.server, port = self.port, **kargs)
