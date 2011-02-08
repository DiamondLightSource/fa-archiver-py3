# Spectrum analyser for a single frequency.  Converts a continuous beam position
# feed into an IQ orbital response at the selected line.

from pkg_resources import require
require('cothread==1.17')
require('iocbuilder==3.3')

import sys

import builder
import numpy
import cothread
from cothread import catools

import falib

if len(sys.argv) > 1:
    if sys.argv[1] == 'BR':
        BOOSTER = True
        SERVER = '172.23.234.70'
    elif sys.argv[1] == 'SR':
        BOOSTER = False
        SERVER = 'fa-archiver.cs.diamond.ac.uk'
    else:
        print 'Unrecognised ring specification'
        sys.exit(1)
else:
    print 'Specify BR or SR'
    sys.exit(1)


SAMPLE_SIZE = 10000
# Nominal sample frequency used to compute exitation waveform.
F_S = 10072

if BOOSTER:
    builder.SetDeviceName('BR-DI-FAAN-01')
else:
    builder.SetDeviceName('SR-DI-FAAN-01')


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

if BOOSTER:
    BPM_ids = range(1, 23)
    FA_IDS = BPM_ids
else:
    BPM_ids = make_bpms(
        lambda c, n: c - 0.2 + 0.1*n,
        lambda c, n: c + 0.1*n)
    # List of BPM FA ids to be monitored: 168 arc BPMs plus the straights
    FA_IDS = make_bpms(
        lambda c, n: STRAIGHT_ID[STRAIGHTS.index(c)] + n - 1,
        lambda c, n: 7 * (c - 1) + n)

builder.Waveform('BPMID', BPM_ids)


# Compute the reverse permutation array to translate the monitored FA ids, which
# are returned in ascending numerical order, back into BPM index position.
PERMUTE = numpy.empty(len(FA_IDS), dtype = int)
PERMUTE[numpy.argsort(FA_IDS)] = numpy.arange(len(FA_IDS))


# ------------------------------------------------------------------------------

class cis:
    def __init__(self):
        self.f_s = F_S
        self.freq = 100.0
        self.reset()

        builder.aOut('FREQ', 0, 1000,
            VAL  = self.freq,   PREC = 2,
            on_update = self.set_freq)
        builder.aOut('F_S',
            VAL  = self.f_s,    PREC = 2,
            on_update = self.set_f_s)

    def reset(self):
        # To be called when frequency has changed
        self.cis = self.phase(numpy.arange(SAMPLE_SIZE))
        self.cis *= 1e-3 / SAMPLE_SIZE      # Take mean and convert to microns
        mean.reset()

    def set_freq(self, freq):
        self.freq = freq
        self.reset()

    def set_f_s(self, f_s):
        self.f_s = f_s
        self.reset()

    def phase(self, n):
        '''Returns exp(2 pi n f / f_s), in other words, the phase advance at the
        selected frequency for sample n.'''
        freq_n = 2 * numpy.pi * self.freq / self.f_s
        return numpy.exp(1j * freq_n * n)


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
        A = numpy.abs(value)

        coh = numpy.sum(value * value) / numpy.sum(A * A)
        phase = numpy.angle(coh) / 2
        value *= numpy.exp(-1j * phase)

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
        sub = falib.subscription(FA_IDS, t0 = True, server=SERVER)
        mean.reset()
        while True:
            r, t0 = sub.read_t0(SAMPLE_SIZE)
            # Remove mean before mixing to avoid avoid numerical problems
            if cis.freq > 10:
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



# Finally fire up the IOC
builder.LoadDatabase()
from softioc import *
iocInit()
interactive_ioc(globals())
