#!/usr/bin/env dls-python2.6

from pkg_resources import require
require('cothread')

import optparse
import subprocess
import os
import sys
import fcntl
import math
import numpy

import cothread
from cothread import catools
import falib


# Offset applied to user programmed volume, in dB.
volume_offset = 50



class Sub:
    '''Manages subscription to archiver.  Received data is written to an event
    queue, and we take care not to let the length of the queue grow.'''

    def __init__(self, queue, bpm, server):
        self.queue = queue
        self.server = server

        self.running = True
        self.process = cothread.Spawn(self.__subscriber, bpm)

    def __subscriber(self, bpm):
        try:
            sub = falib.subscription([bpm], server = self.server)
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
        self.bpm = bpm
        self.server = server
        self.volume = volume
        self.channels = 'b'

        self.queue = cothread.EventQueue()
        self.sub = Sub(self.queue, self.bpm, self.server)

        cothread.Spawn(self.player)


    # Rescales audio to avoid clipping after volume scaling.
    def rescale(self, block):
        # First remove any DC component as this makes no sense for sound
        block -= block.mean(axis = 0)

        # Compute the available dynamic range
        range = numpy.amax(numpy.abs(block))
        vscale = 10 ** ((self.volume + volume_offset) / 20.)
        mscale = (2**31 - 1.) / range
        block *= min(vscale, mscale)

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
    def command_q(self, arg):
        '''q        Quit'''
        sys.exit(0)

    def command_b(self, arg):
        '''b<n>     Set FA id <n> for playback'''
        bpm = int(arg)
        assert 0 <= bpm <= 255
        self.bpm = bpm
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

    commands = 'qbvxya'


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
                    for command in self.commands:
                        print self.get_command(command).__doc__

parser = optparse.OptionParser(usage = '''\
fa-audio [options] server

Plays back FA data stream as audio through the PC speakers.''')
parser.add_option(
    '-v', dest = 'volume', default = 0, type = 'float',
    help = 'Set initial volume in dB, default is 0dB')
parser.add_option(
    '-b', dest = 'bpm_id', default = 1, type = 'int',
    help = 'Choose initial FA id for playback, default is 1')
options, args = parser.parse_args()

try:
    server, = args
except:
    parser.error("Expected FA server as only argument")

player = Player(options.bpm_id, server, options.volume)
player.shell()
