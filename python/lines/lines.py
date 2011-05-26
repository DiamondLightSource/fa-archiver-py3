# Spectrum analyser for a single frequency.  Converts a continuous beam position
# feed into an IQ orbital response at the selected line.

from pkg_resources import require
require('cothread==1.17')
require('iocbuilder==3.3')

import sys
import os
import re
import optparse

from softioc import builder
from softioc import softioc
import numpy
import cothread
from cothread import catools

import falib


# ------------------------------------------------------------------------------

class cis:
    def __init__(self):
        self.f_s = F_S
        self.freq = 100.0
        self.freq = 260.0
        self.delay = INITIAL_DELAY
        self.reset()

        builder.aOut('FREQ', 0, 1000,
            VAL  = self.freq,   EGU  = 'Hz',    PREC = 2,
            on_update = self.set_freq)
        builder.aOut('F_S',
            VAL  = self.f_s,    EGU  = 'Hz',    PREC = 2,
            on_update = self.set_f_s)
        builder.aOut('DELAY', 0, 10000,
            EGU = 'us', VAL = self.delay, on_update = self.set_delay)

    def reset(self):
        # To be called when frequency has changed
        self.freq_n = 2j * numpy.pi * self.freq / self.f_s
        self.cis = self.phase(numpy.arange(SAMPLE_SIZE) * decimation)
        self.cis *= 1e-3 / SAMPLE_SIZE      # Take mean and convert to microns
        mean.reset()

    def set_freq(self, freq):
        self.freq = freq
        self.reset()

    def set_f_s(self, f_s):
        self.f_s = f_s
        self.reset()

    def set_delay(self, delay):
        self.delay = delay

    def phase(self, n):
        '''Returns exp(2 pi n f / f_s), in other words, the phase advance at the
        selected frequency for sample n.  Note that sample n is in terms of the
        underlying sample rate, not the decimated rate.'''
        return numpy.exp(self.freq_n * n)

    def compensate(self, iq, t0):
        # Compensates input waveform for given t0 and the programmed delay.
        delta = self.delay * 1e-6 * self.f_s
#         print 'delta', delta
        # The extra 1j compensates for an extra phase offset introduced by the
        # fact that the excitation is  cos(2 * pi * f * t0 / 10072)
        iq *= self.phase(t0 + delta) * -1j


class waveform:
    def __waveform(self, axis, name, scale=1):
        return builder.Waveform(
            axis + name, length = scale * len(BPM_ids), datatype = float)

    def __init__(self, axis):
        self.axis = axis
        self.wfi = self.__waveform(axis, 'I')
        self.wfq = self.__waveform(axis, 'Q')
        self.wfa = self.__waveform(axis, 'A')
        self.wfiq = self.__waveform(axis, 'IQ', 2)

    def update(self, value):
        I = numpy.real(value)
        Q = numpy.imag(value)
        A = numpy.abs(value)

        # Note: need to copy the real and imag parts as numpy otherwise just
        # recycles the the underlying data which is too transient here.
        self.wfi.set(+I)
        self.wfq.set(+Q)
        self.wfa.set(A)
        self.wfiq.set(numpy.concatenate((I, Q)))


class enabled:
    '''Monitors the ENABLED waveform from the concentrator.'''
    def __init__(self):
        pv = 'SR-DI-EBPM-01:ENABLED'
        self.__update(catools.caget(pv))
        catools.camonitor(pv, self.__update)

    def __update(self, enabled):
        self.enabled = enabled == 0


class mean:
    def __init__(self):
        self.meanx = waveform('MX')
        self.meany = waveform('MY')
        self.sum = numpy.empty((len(FA_IDS), 2), dtype = numpy.complex)
        self.set_target(10)
        self.reset()

        builder.longOut('TARGET', 1, 600,
            VAL = self.target, on_update = self.set_target)
        self.count_pv = builder.longIn('COUNT', VAL = 0)
        builder.Action('RESET', on_update = lambda _: self.reset())

    def reset(self):
        self.count = 0
        self.sum[:] = 0

    def set_target(self, target):
        self.target = target

    def update(self, value):
        self.sum += value
        self.count += 1
        self.count_pv.set(self.count)

        # Only generate an update when we reach our target
        if self.count >= self.target:
            mean = self.sum / self.count
            if hide_disabled.get():
                mean[~enabled.enabled] = 0
            self.meanx.update(mean[:, 0])
            self.meany.update(mean[:, 1])

            self.reset()


class updater:
    def __init__(self):
        self.wfx = waveform('X')
        self.wfy = waveform('Y')
        cothread.Spawn(self.run)

    def subscription(self):
        sub = falib.subscription(
            (0,) + FA_IDS, server=FA_SERVER, decimated=DECIMATED)
        mean.reset()
        while True:
            r = sub.read(SAMPLE_SIZE)
            t0, r = numpy.uint32(r[0, 0, 0]), r[:, 1:]

            # Remove mean before mixing to avoid avoid numerical problems
            if cis.freq > 10:
                r -= numpy.mean(r, axis=0)
            # Mix with the selected frequency and adjust for the phase advance
            # of the FA stream.  If this is an FA excitation this will keep us
            # in phase.  Finish by permuting result from archive order into BPM
            # display order.
            iq = numpy.tensordot(r, cis.cis, axes=(0, 0))
            cis.compensate(iq, t0)
            iq = iq[PERMUTE]

            # This is the fully digested data ready for any other processing
            mean.update(iq)

            # Finally display the data we have, zeroing disabled BPMs if
            # required.
            if hide_disabled.get():
                iq[~enabled.enabled] = 0
            self.wfx.update(iq[:, 0])
            self.wfy.update(iq[:, 1])


    def run(self):
        import socket
        while True:
            try:
                self.subscription()
            except falib.connection.EOF, eof:
                print eof
            except falib.connection.Error, error:
                print error
            except socket.timeout:
                print 'Server timed out'
            except:
                print 'Unexpected failure'
                import traceback
                traceback.print_exc()
            cothread.Sleep(1)



# ------------------------------------------------------------------------------
# Command line argument processing.

parser = optparse.OptionParser(usage = '''\
fa-lines [options] location

Computes full waveform beam response for one or more frequencies.''')
parser.add_option(
    '-f', dest = 'full_path', default = False, action = 'store_true',
    help = 'location is full path to file')
parser.add_option(
    '-S', dest = 'server', default = None,
    help = 'Override server address in location file')
parser.add_option(
    '-I', dest = 'ioc_name', default = 'TS-DI-IOC-01',
    help = 'Name of this IOC')
parser.add_option(
    '-D', dest = 'device_name', default = None,
    help = 'Device name for control PVs')
parser.add_option(
    '-r', dest = 'raw_data', default = False, action = 'store_true',
    help = 'Use full spectrum data stream rather than decimated data')
parser.add_option(
    '-F', dest = 'frequency', default = 10072, type = 'float',
    help = 'Nominal underlying sample frequency.  Must match excitation, '
        'default is 10072Hz')
parser.add_option(
    '-s', dest = 'sample_size', default = 10000, type = 'int',
    help = 'Number of underlying FA rate samples per update')
parser.add_option(
    '-d', dest = 'delay', default = 200, type = 'float',
    help = 'Initial value for delay pv')
options, args = parser.parse_args()
try:
    location, = args
except:
    parser.error('Argument should be location')

# The device name is computed from the location if appropriate.
if options.device_name:
    DEVICE_NAME = options.device_name
else:
    if options.full_path:
        parser.error('Must specify device name with location path')
    DEVICE_NAME = '%s-DI-FAAN-01' % location

# Load the configured location file.
falib.load_location_file(
    globals(), location, options.full_path, options.server)

# Extract remaining options for processing.
IOC_NAME = options.ioc_name
DECIMATED = not options.raw_data
if DECIMATED:
    decimation = falib.get_decimation(server = FA_SERVER)
else:
    decimation = 1
F_S = options.frequency
SAMPLE_SIZE = options.sample_size // decimation
INITIAL_DELAY = options.delay

# Compute list of FA ids and the corresponding BPM ids.
FA_IDS, bpm_names = zip(*[(id, bpm)
    for id, bpm in falib.load_bpm_list(BPM_LIST)
    if id in BPM_ID_RANGE])
MAKE_ID_PATTERN = re.compile(MAKE_ID_PATTERN)
BPM_ids = [
    MAKE_ID_FN(*MAKE_ID_PATTERN.match(bpm).groups())
    for bpm in bpm_names]

# Compute the reverse permutation array to translate the monitored FA ids, which
# are returned in ascending numerical order, back into BPM index position.
PERMUTE = numpy.empty(len(FA_IDS), dtype = int)
PERMUTE[numpy.argsort(FA_IDS)] = numpy.arange(len(FA_IDS))
# The PERMUTE array should be redundant now.


# ------------------------------------------------------------------------------
# Record creation and IOC initialisation

builder.SetDeviceName('%s-DI-FAAN-01' % location)

builder.Waveform('BPMID', BPM_ids)

hide_disabled = builder.boolOut('HIDEDIS',
    'Show Disabled', 'Hide Disabled', RVAL = 0, PINI = 'YES')

# Monitors ENABLED waveform
enabled = enabled()
# Manages longer accumulations of data for narrow band monitoring
mean = mean()
# Converts frequency selection into a mixing waveform
cis = cis()
# Core processing
updater = updater()


# A couple of identification PVs
builder.SetDeviceName(IOC_NAME)
builder.stringIn('WHOAMI', VAL = 'Whole orbit frequency response analyser')
builder.stringIn('HOSTNAME', VAL = os.uname()[1])

# Finally fire up the IOC
builder.LoadDatabase()
softioc.iocInit()
softioc.interactive_ioc()
