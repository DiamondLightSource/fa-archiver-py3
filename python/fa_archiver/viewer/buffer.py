#   Data Acquisition
#
# A stream of data for one selected BPM is acquired by connecting to the FA
# sniffer server.  This is maintained in a "circular" buffer containing the last
# 50 seconds worth of data (500,000 points) and delivered on demand to the
# display layer.

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

import cothread
import numpy


class buffer:
    '''Circular buffer.'''
    # Super lazy implementation: we always just copy the data to the bottom!

    def __init__(self, buffer_size):
        self.buffer = numpy.zeros((buffer_size, 2))
        self.buffer_size = buffer_size

    def write(self, block):
        blen = len(block)
        self.buffer[:-blen] = self.buffer[blen:]
        self.buffer[-blen:] = block

    def size(self):
        return self.data_size

    def read(self, size):
        return self.buffer[-size:]

    def reset(self):
        self.buffer[:] = 0


class monitor:
    def __init__(self,
            server, on_event, on_connect, on_eof, buffer_size, read_size):
        self.server = server
        self.on_event = on_event
        self.on_connect = on_connect
        self.on_eof = on_eof
        self.buffer = buffer(buffer_size)
        self.update_size = read_size
        self.notify_size = read_size
        self.data_ready = 0
        self.running = False
        self.decimated = False
        self.id = 0

    def start(self):
        assert not self.running, 'Strange: we are already running'
        try:
            self.subscription = self.server.subscription(
                [self.id], decimated = self.decimated, uncork = self.decimated)
        except Exception, message:
            import traceback
            traceback.print_exc()
            self.on_eof('Unable to connect to server: %s' % message)
        else:
            self.running = True
            self.buffer.reset()
            self.task = cothread.Spawn(self.__monitor)

    def stop(self):
        if self.running:
            self.running = False
            self.task.Wait()

    def set_channel(self, id=None, decimated=None):
        running = self.running
        self.stop()
        if id is not None:
            self.id = id
        if decimated is not None:
            self.decimated = decimated
        if running:
            self.start()

    def resize(self, notify_size, update_size):
        '''The notify_size is the data size delivered in each update, while
        the update_size determines how frequently an update is delivered.'''
        self.notify_size = notify_size
        self.update_size = update_size
        self.data_ready = 0

    def __monitor(self):
        stop_reason = 'Stopped'
        self.on_connect()
        while self.running:
            try:
                block = self.subscription.read(self.update_size)[:,0,:]
            except Exception, exception:
                stop_reason = str(exception)
                self.running = False
            else:
                self.buffer.write(block)
                self.data_ready += self.update_size
                self.on_event(self.read())
                self.data_ready -= self.update_size
        self.subscription.close()
        self.on_eof(stop_reason)

    def read(self):
        '''Can be called at any time to read the most recent buffer.'''
        return 1e-3 * self.buffer.read(self.notify_size)
