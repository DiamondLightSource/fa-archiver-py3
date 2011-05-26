#!/usr/bin/env dls-python2.6

import subprocess
import os
import fcntl
import math

from pkg_resources import require
require('cothread')

import cothread
from cothread import catools
import falib


volume = +100
bpm = 1
channels = 'b'
min_current = 1



class Sub:
    def __init__(self, bpm):
        self.running = True
        self.process = cothread.Spawn(self.subscriber, bpm)

    def subscriber(self, bpm):
        sub = falib.subscription([bpm])
        while self.running:
            block = sub.read(1000)
            if len(queue) <= 1:
                # Drop extra blocks to ensure we don't run too far ahead of the
                # player, which is deliberately running about 0.7% slower.
                queue.Signal(block)
            else:
                print 'dropped packet'
        sub.close()

    def close(self):
        self.running = False
        self.process.Wait()


current = 0
def update_current(value):
    global current
    current = 100
catools.camonitor('SR21C-DI-DCCT-01:SIGNAL', update_current)


queue = cothread.EventQueue()
sub = Sub(bpm)
cothread.Sleep(0.2)

@cothread.Spawn
def player():
    aplay = subprocess.Popen(
        ['aplay', '-c2', '-fS32_LE', '-r10000'], stdin = subprocess.PIPE)
    fcntl.fcntl(aplay.stdin, fcntl.F_SETFL, os.O_NONBLOCK)

    to_write = ''
    while True:
        block = queue.Wait()[:,0,:]
        if current < min_current:
            block[:] = 0
        elif channels == 'l':
            block[:, 1] = block[:, 0]
        elif channels == 'r':
            block[:, 0] = block[:, 1]

        block -= block.mean(axis=0)
        block *= 10 ** (volume / 20.)
        to_write = to_write + block.astype('l').tostring()
        while len(to_write) > 4096:
            assert cothread.poll_list([(aplay.stdin, cothread.POLLOUT)])
            written = os.write(aplay.stdin.fileno(), to_write)
            to_write = to_write[written:]


running = True
while running:
    command = raw_input('> ').lstrip()
    if command == '' or command[0] == 'q':
        running = False
    elif command[0] == 'b':
        try:
            bpm = int(command[1:])
            assert 0 <= bpm <= 255
        except:
            print 'Expected BPM number as command'
        else:
            sub.close()
            sub = Sub(bpm)
    elif command[0] == 'v':
        try:
            volume = float(command[1:])
        except:
            print 'Expected volume level'
    elif command[0] == 'c':
        channels = command[1]
    else:
        print 'Unrecognised command'
