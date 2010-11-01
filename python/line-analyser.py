# Spectrum analyser for a single frequency.  Converts a continuous beam position
# feed into an IQ orbital response at the selected line.

from pkg_resources import require
require('cothread==1.17')
require('iocbuilder==3.3')

import builder
import numpy
import cothread
from cothread import catools

import falib


SAMPLE_SIZE = 10000
F_S = 10072.4

builder.SetDeviceName('CS-DI-FAAN-01')


# ------------------------------------------------------------------------------
# Computation of BPM id, largely lifted from CS-DI-IOC-01/monitor.py

STRAIGHTS = [13]        # Expect straights 9 and 10 later on
STRAIGHT_ID = [174]     # Starting BPM id for each straight

def flatten(ll):
    '''Flattens a list of lists into a single list.'''
    return [x for l in ll for x in l]

def make_bpms(straight, cell):
    return flatten([
        [straight(c+1, n+1) for n in range(2) if c+1 in STRAIGHTS] +
        [cell(c+1, n+1) for n in range(7)]
        for c in range(24)])

BPM_ids = make_bpms(
    lambda c, n: c - 0.2 + 0.1*n,
    lambda c, n: c + 0.1*n)

builder.Waveform('BPMID', BPM_ids)


# List of BPM FA ids to be monitored: 168 arc BPMs plus the straights
FA_IDS = make_bpms(
    lambda c, n: STRAIGHT_ID[STRAIGHTS.index(c)] + n - 1,
    lambda c, n: 7 * (c - 1) + n)
# Compute the reverse permutation array to translate the monitored FA ids, which
# are returned in ascending numerical order, back into BPM index position.
PERMUTE = numpy.empty(len(FA_IDS), dtype = int)
PERMUTE[numpy.argsort(FA_IDS)] = numpy.arange(len(FA_IDS))


# ------------------------------------------------------------------------------

class cis:
    def __init__(self):
        self.set_freq(100.0)
        self.pv = builder.aOut('FREQ', 0, 1000,
            VAL = self.freq,
            on_update = self.set_freq)

    def set_freq(self, freq):
        print 'set_freq', freq
        self.freq = freq
        self.cis = self.phase(numpy.arange(SAMPLE_SIZE))
        self.cis *= 1e-3 / SAMPLE_SIZE      # Take mean and convert to microns
        mean.reset()

    def phase(self, n):
        '''Returns exp(2 pi n f / f_s), in other words, the phase advance at the
        selected frequency for sample n.'''
        freq_n = 2 * numpy.pi * self.freq / F_S
        return numpy.exp(1j * freq_n * n)


class waveform:
    def __waveform(self, axis, name):
        return builder.Waveform(axis + name, length = 168, datatype = float)

    def __init__(self, axis):
        self.wfi = self.__waveform(axis, 'I')
        self.wfq = self.__waveform(axis, 'Q')
        self.wfa = self.__waveform(axis, 'A')

    def update(self, value):
        # Note: need to copy the real and imag parts as numpy otherwise just
        # recycles the the underlying data which is too transient here.
        self.wfi.set(+numpy.real(value))
        self.wfq.set(+numpy.imag(value))
        self.wfa.set(numpy.abs(value))


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

    def reset(self):
        self.count = 0
        self.sum[:] = 0

    def set_target(self, target):
        print 'set_target', target
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
        sub = falib.subscription(FA_IDS, t0 = True)
        mean.reset()
        while True:
            r, t0 = sub.read_t0(SAMPLE_SIZE)
            # Remove mean before mixing to avoid avoid numerical problems
            r -= numpy.mean(r, axis=0)
            # Mix with the selected frequency and adjust for the phase advance
            # of the FA stream.  If this is an FA excitation this will keep us
            # in phase.  Finish by permuting result from archive order into BPM
            # display order.
            iq = numpy.tensordot(r, cis.cis, axes=(0, 0))
            iq *= cis.phase(t0)
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
        while True:
            try:
                self.subscription()
            except falib.connection.EOF, eof:
                print eof
            except:
                print 'Unexpected failure'
                import traceback
                traceback.print_exc()
            cothread.Sleep(1)



hide_disabled = builder.boolOut('HIDEDIS',
    'Show Disabled', 'Hide Disabled', RVAL = 1, PINI = 'YES')

# Monitors ENABLED waveform
enabled = enabled()
# Manages longer accumulations of data for narrow band monitoring
mean = mean()
# Converts frequency selection into a mixing waveform
cis = cis()
# Core processing
updater = updater()



# Finally fire up the IOC
builder.LoadDatabase()
from softioc import *
iocInit()
interactive_ioc(globals())
