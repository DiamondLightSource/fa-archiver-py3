from pkg_resources import require
require('cothread==1.17')
require('iocbuilder==3.3')

import os

from softioc import builder
from softioc import softioc
import numpy
import cothread

import falib

IOC_NAME = 'TS-DI-IOC-02'

FA_IDS = [1, 2, 185]
FA_NAMES = ['SR01C-DI-EBPM-01', 'SR01C-DI-EBPM-02', 'SR-RF-PM-01']

FA_SERVER ='localhost'


# Corresponds to 4 seconds per half sample.
HALF_SAMPLE_SIZE = 4096

FREQUENCIES = numpy.arange(1, 301)      # 1..300
SPEC_LEN = len(FREQUENCIES)


# Get the underlying sample frequency
# Actually, this ought to be an attribute of the subscription...
F_S = falib.get_sample_frequency() / falib.get_decimation()


class Results:
    def waveform_xy(self, suffix, **kargs):
        return [
            builder.Waveform(
                'SPEC:%s%s' % (xy, suffix), length = SPEC_LEN, **kargs)
            for xy in 'XY']

    def __init__(self, name):
        builder.SetDeviceName(name)
        self.power      = self.waveform_xy('AMPL')
        self.sum        = self.waveform_xy('SUM')
        self.mean_power = self.waveform_xy('MEANAMPL')
        self.mean_sum   = self.waveform_xy('MEANSUM')
        builder.UnsetDevice()

    def set_xy(self, xy, value):
        xy[0].set(value[:, 0])
        xy[1].set(value[:, 1])

    def update_value(self, sum, power):
        self.set_xy(self.sum, sum)
        self.set_xy(self.power, power)

    def update_mean(self, sum, power):
        self.set_xy(self.mean_sum, sum)
        self.set_xy(self.mean_power, power)


class Compute:
    def __init__(self, half_sample_size, id_count, frequencies):
        self.N2 = (2 * half_sample_size) ** 2

        # Compute offsets into cumsum where our target frequencies will be.
        fft_freqs = 0.5 * F_S * \
            numpy.arange(half_sample_size) / half_sample_size
        self.bins = numpy.concatenate(
            ([0], fft_freqs.searchsorted(frequencies)))
        self.fft_length = self.bins[-1] + 1

        # When returning the integrated power in each frequency bin scale the
        # result to a truthful power per Hertz
        self.bin_scale = numpy.diff(fft_freqs[self.bins])[:, None, None]
#         self.bin_scale = 1

        # We process the power spectrum in a sequence of windowed overlapping
        # ranges.  Each range consists of two half samples (so we have 50%
        # overlap between successive spectrum calculations) and the window is
        # half a cosine (over range -pi/2..+pi/2) -- this means that the power
        # contributions for each sample from successive ranges adds up to 1.
        pi2 = numpy.pi / 2
        self.window = numpy.cos(numpy.linspace(-pi2, pi2, 2 * half_sample_size))
        self.window = self.window[:, None, None]
        self.last = numpy.zeros((half_sample_size, id_count, 2))

    def compute(self, data):
        block = numpy.concatenate((self.last, data)) * self.window
        self.last = data

        ff = numpy.fft.fft(block, axis = 0)[:self.fft_length]
        ff[0] = 0
        p = numpy.abs(ff) ** 2
        # The scaling of the result is a little delicate.  We start from the
        # general observation that sum(|fft(x)|^2) = len(x)^2 * var(x), and as
        # we're summing over only half the fft in general we need a factor of 2
        # to correct this.  However in this case we also want to correct for the
        # effect of the window, which in effect reduces the overall power by a
        # further factor of 2, hence the scaling below.
        s = 4 * p.cumsum(axis = 0)[self.bins] / self.N2

        # Return both the integrated cumulative sum and the properly scaled
        # incremental power as the two are not completely comparable (due to
        # the subtle differences in bin widths).
        return s[1:], numpy.diff(s, axis = 0) / self.bin_scale


class Monitor:
    def __init__(self):
        self.spectrum = Compute(HALF_SAMPLE_SIZE, len(FA_IDS), FREQUENCIES)
        self.results = [Results(name) for name in FA_NAMES]

        # Control PVs
        builder.SetDeviceName('SR-DI-SPEC-01')
        self.target_pv = builder.longOut('TARGET', 1, 600, initial_value = 15)
        self.count_pv = builder.longIn('COUNT')
        builder.Waveform('FREQ', FREQUENCIES)
        builder.mbbIn('PVS', *FA_NAMES)
        builder.UnsetDevice()

    # Passes the updated spectrum for each FA ID through to the given action
    # routine for each entry in results (one per FA ID and spectrum entry).
    # At the same time we convert into um.
    def map_result(self, action, sum, power):
        sum   = 1e-3 * numpy.sqrt(sum)
        power = 1e-3 * numpy.sqrt(power)
        for n, result in enumerate(self.results):
            action(result, sum[:, n, :], power[:, n, :])

    def monitor(self):
        sub = falib.subscription(FA_IDS, server = FA_SERVER, decimated = True)
        count = 0
        total_sum = numpy.zeros((SPEC_LEN, len(FA_IDS), 2))
        total_power = numpy.zeros((SPEC_LEN, len(FA_IDS), 2))
        while True:
            sum, power = self.spectrum.compute(sub.read(HALF_SAMPLE_SIZE))
            self.map_result(Results.update_value, sum, power)

            total_sum += sum
            total_power += power

            count += 1
            self.count_pv.set(count)
            if count >= self.target_pv.get():
                self.map_result(Results.update_mean,
                    total_sum / count, total_power / count)
                count = 0
                total_sum[:] = 0
                total_power[:] = 0

    def run(self):
        import socket
        while True:
            try:
                self.monitor()
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

    def start(self):
        cothread.Spawn(self.run)


# Spectrum monitor and associated PVs.
monitor = Monitor()

# A couple of identification PVs
builder.SetDeviceName(IOC_NAME)
builder.stringIn('WHOAMI', VAL = 'Beam position spectrum analyser')
builder.stringIn('HOSTNAME', VAL = os.uname()[1])


# Finally fire up the IOC
builder.LoadDatabase()
softioc.iocInit()

monitor.start()
softioc.interactive_ioc(globals())
