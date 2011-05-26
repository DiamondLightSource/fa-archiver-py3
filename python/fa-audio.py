#!/usr/bin/env dls-python2.6

print 'How about using `aplay` instead?'

# The OSS API appears to be nicely documented at
#   http://manuals.opensound.com/developer/
# however closer inspection seems to indicate a number of troubling
# discrepancies.  A number of problems arose in trying to optimise the code
# below:
#   1.  Non blocking doesn't seem to work very well, the result is ticking in
#       output for each block written.
#   2.  Attempting to discover anything about the state of the output buffer
#       (dev.obuffree() or dev.obufcount()) appears to reset the device
#       parameters.
# So, alas, we work in blocking mode.  This means that unless we spawn a thread
# for writing any interactive API will be unresponsive while writing.

import sys
from pkg_resources import require
# require('cothread')
sys.path.append('/home/mga83/epics/cothread')

import numpy
import ossaudiodev
import falib
import signal
import threading
import optparse

import cothread


parser = optparse.OptionParser(usage = '''
Usage: fa-audio [options]

Play audio of selected BPM.''')
parser.add_option(
    '-b', dest = 'bpm', default = 4, type = 'int',
    help = 'BPM to monitor')
parser.add_option(
    '-v', dest = 'volume', default = -10, type = 'float',
    help = 'Volume level (in dB)')
options, arglist = parser.parse_args()
if arglist:
    parser.error('Unexptected arguments')


rate = 10000
# vol = 10000
vol_db = options.volume
vol = 10 ** (vol_db / 20.)



# Maximum number of packets we hang onto.
MAX_QUEUE = 2
# Size of receive buffer in samples
BUF_LEN = 5000

class DataFeed:
    def __init__(self):
        self.queue = cothread.ThreadedEventQueue()
        self.subscriber = None

    def set_source(self, bpm_id):
        if self.__task:
            self.running = False
            self.__task.Wait()
        self.running = True
        self.__task = cothread.Spawn(self.__subscriber, bpm_id)

    def __subscriber(self, bpm_id):
        subscription = falib.subscription([bpm_id])
        while self.running:
            block = subscription.read(BUF_LEN)
            if len(self.queue) < MAX_QUEUE:
                self.queue.Signal(block)
            else:
                print >>sys.stderr, 'Dropping block'
#         # To ensure we don't get blocked finish off with one last signal.
#         self.queue.Signal(numpy.zeros((0, 1, 2)))
        subscription.close()

    def get_block(self):
        return self.queue.Wait()


class Playback:
    '''Sound playback task.'''

    def __init__(self, bpm_id, volume):
        self.bpm_id = bpm_id
        self.volume = volume

        self.dev = ossaudiodev.open('/dev/dsp', 'w')
        self.dev.setparameters(ossaudiodev.AFMT_S16_LE, 2, rate, True)

        self.done = cothread.ThreadedEventQueue()
        self.queue = cothread.ThreadedEventQueue()
        self.running = True

        # The subscriber
        cothread.Spawn(self.subscriber)
        signal.signal(signal.SIGINT, self.signal_handler)

        sound_thread = threading.Thread(target = self.sound_thread)
        sound_thread.daemon = True
        sound_thread.start()

    def subscriber(self):
        subscription = falib.subscription([self.bpm_id])
        while self.running:
            block = subscription.read(BUF_LEN)
            if len(self.queue) < MAX_QUEUE:
                self.queue.Signal(block)
            else:
                print >>sys.stderr, 'Dropping block'
        # To ensure we don't get blocked finish off with one last signal.
        self.queue.Signal(numpy.zeros((0, 1, 2)))
        subscription.close()

    def signal_handler(self, signal, frame):
        self.running = False
        self.dev.reset()

    def reshape(self, block):
        both = numpy.empty(len(block) * 2)
        both[1::2] = block[:, 0, 0]
        both[0::2] = block[:, 0, 1]
        return (self.volume * both).astype('h').tostring()

    def write_block(self, block):
        if self.running:
            self.dev.write(self.reshape(block))

    def sound_thread(self):
        # Grab two blocks to begin with to avoid that annoying initial
        # hesitation.
        blocks = [self.queue.Wait() for i in range(2)]
        for block in blocks:
            self.write_block(block)
        del blocks, block
        while self.running:
            self.write_block(self.queue.Wait())

        self.dev.close()
        self.done.Signal(None)

    def wait(self):
        self.done.Wait()


playback = Playback(options.bpm, vol)
playback.wait()
