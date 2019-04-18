#!/usr/bin/env dls-python

# Audio player for playing FA position data through PC speakers

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

if __name__ == '__main__':
    from pkg_resources import require
    require('cothread')
    require('numpy')

import optparse
import subprocess
import os
import sys
import fcntl
import math
import numpy

import cothread
import falib


# Offset applied to user programmed volume, in dB.
volume_offset = 50

DEFAULT_LOCATION = 'SR'


class Sub:
    '''Manages subscription to archiver.  Received data is written to an event
    queue, and we take care not to let the length of the queue grow.'''

    def __init__(self, queue, bpm, server):
        self.queue = queue
        self.server = server

        self.running = True
        self.process = cothread.Spawn(self.__subscriber, bpm)
        self.bpm = bpm

    def __subscriber(self, bpm):
        try:
            sub = server.subscription([bpm])
        except Exception, error:
            print 'Error', error, 'connecting to archiver'
            os._exit(0)

        while self.running:
            block = sub.read(1000)
            if len(self.queue) <= 1:
                # Drop extra blocks to ensure we don't run too far ahead of the
                # player, which is deliberately running about 0.7% slower.
                self.queue.Signal(block)
        sub.close()

    def close(self):
        self.running = False
        self.process.Wait()


class Player:
    def __init__(self, bpm, server, volume):
        self.server = server
        self.volume = volume
        self.channels = 'b'

        cothread.Spawn(self.player)

        self.queue = cothread.EventQueue()
        self.sub = None

        # Hang onto input volume level history for last 5 seconds.
        self.mvolume = 100 * numpy.ones(50)
        self.set_bpm(bpm)

    def set_bpm(self, bpm):
        if self.sub:
            self.sub.close()
        self.sub = Sub(self.queue, bpm, self.server)
        # Reset volume history when changing FA id
        self.mvolume[:] = 100


    # Rescales audio to avoid clipping after volume scaling.
    def rescale(self, block):
        # First remove any DC component as this makes no sense for sound
        block = numpy.float32(block)
        block -= block.mean(axis = 0)

        # Compute the available dynamic range
        range = numpy.amax(numpy.abs(block))
        vscale = 10 ** ((self.volume + volume_offset) / 20.)
        mscale = (2**31 - 1.) / range
        block *= min(vscale, mscale)

        # Convert mscale into a measure of raw volume
        mvol = 20 * numpy.log10(mscale) - volume_offset
        self.mvolume = numpy.roll(self.mvolume, 1)
        self.mvolume[0] = mvol

        return block


    def player(self):
        aplay = subprocess.Popen(
            ['aplay', '-c2', '-fS32_LE', '-r10000'], stdin = subprocess.PIPE)
        fcntl.fcntl(aplay.stdin, fcntl.F_SETFL, os.O_NONBLOCK)

        to_write = ''
        while True:
            block = self.queue.Wait()[:,0,:]
            if self.channels == 'l':
                block[:, 1] = block[:, 0]
            elif self.channels == 'r':
                block[:, 0] = block[:, 1]

            block = self.rescale(block)

            to_write = to_write + block.astype('l').tostring()
            while len(to_write) > 4096:
                assert cothread.poll_list([(aplay.stdin, cothread.POLLOUT)])
                written = os.write(aplay.stdin.fileno(), to_write)
                to_write = to_write[written:]


    # Command definitions
    def command_h(self, arg):
        '''h        Show this list of possible commands'''
        for command in self.commands:
            print self.get_command(command).__doc__

    def command_q(self, arg):
        '''q        Quit'''
        sys.exit(0)

    def command_b(self, arg):
        '''b<n>     Set FA id <n> for playback'''
        bpm = int(arg)
        assert 0 <= bpm <= 255
        self.set_bpm(bpm)

    def command_v(self, arg):
        '''v<n>     Set volume to <n>dB.  Default volume is 0dB'''
        self.volume = float(arg)

    def command_x(self, arg):
        '''x        Only play X channel'''
        self.channels = 'l'
    def command_y(self, arg):
        '''y        Only play Y channel'''
        self.channels = 'r'
    def command_a(self, arg):
        '''a        Play X channel in left speaker, Y channel in right'''
        self.channels = 'b'

    def command_i(self, arg):
        '''i        Show information about current settings'''
        print 'BPM', self.sub.bpm
        print 'Volume', self.volume, 'dB'
        print 'Channel', self.channels
        print 'Max volume: %.1f dB' % numpy.min(self.mvolume)

    commands = 'hqbvxyai'


    def get_command(self, command):
        return getattr(self, 'command_%s' % command)

    def shell(self):
        '''Runs a simple command shell.'''
        while True:
            try:
                input = raw_input('> ').lstrip()
            except:
                sys.exit(0)

            if input:
                command, arg = input[0], input[1:]
                if command in self.commands:
                    try:
                        self.get_command(command)(arg)
                    except Exception, error:
                        print 'Error', error, 'in command'
                else:
                    print 'Unknown command.  Possible commands are:'
                    self.command_h(arg)

parser = optparse.OptionParser(usage = '''\
fa-audio [options] [location]

Plays back FA data stream as audio through the PC speakers.''')
parser.add_option(
    '-v', dest = 'volume', default = 0, type = 'float',
    help = 'Set initial volume in dB, default is 0dB')
parser.add_option(
    '-b', dest = 'bpm_id', default = 1, type = 'int',
    help = 'Choose initial FA id for playback, default is 1')
parser.add_option(
    '-f', dest = 'full_path', default = False, action = 'store_true',
    help = 'Location is full path to location file')
parser.add_option(
    '-S', dest = 'server', default = None,
    help = 'Override server address in location file')
options, args = parser.parse_args()

if len(args) > 1:
    parser.error('Unexpected arguments')
elif args:
    location = args[0]
else:
    location = DEFAULT_LOCATION

falib.load_location_file(
    globals(), location, options.full_path, options.server)


server = falib.Server(server = FA_SERVER, port = FA_PORT)
player = Player(options.bpm_id, server, options.volume)

def main():
    player.shell()

if __name__ == '__main__':
    main()
