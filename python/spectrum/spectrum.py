from pkg_resources import require
require('cothread==1.17')
require('iocbuilder==3.3')

import os
import optparse

from softioc import builder
from softioc import softioc
import numpy
import cothread

import falib


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
    def __init__(self, threshold = 0, qualifier_pv = None):
        self.spectrum = Compute(HALF_SAMPLE_SIZE, len(FA_IDS), FREQUENCIES)
        self.results = [Results(name) for name in FA_NAMES]
        self.running = True

        # Control PVs
        builder.SetDeviceName(DEVICE_NAME)
        self.target_pv = builder.longOut(
            'TARGET', 1, 600, initial_value = TARGET_COUNT)
        self.count_pv = builder.longIn('COUNT')
        builder.Waveform('FREQ', FREQUENCIES)
        self.threshold_pv = builder.aOut(
            'THRESHOLD', EGU = 'mA', PREC = 2,
            initial_value = float(threshold))
        self.status_pv = builder.boolOut(
            'STATUS', 'Paused', 'Running', initial_value = True)
        builder.UnsetDevice()

        if qualifier_pv:
            from cothread import catools
            catools.camonitor(qualifier_pv, self.__on_update)

    # Passes the updated spectrum for each FA ID through to the given action
    # routine for each entry in results (one per FA ID and spectrum entry).
    # At the same time we convert into um.
    def map_result(self, action, sum, power):
        sum   = 1e-3 * numpy.sqrt(sum)
        power = 1e-3 * numpy.sqrt(power)
        for n, result in enumerate(self.results):
            action(result, sum[:, n, :], power[:, n, :])

    def monitor(self):
        sub = server.subscription(FA_IDS, decimated = True)
        count = 0
        total_sum = numpy.zeros((SPEC_LEN, len(FA_IDS), 2))
        total_power = numpy.zeros((SPEC_LEN, len(FA_IDS), 2))
        while True:
            sample = sub.read(HALF_SAMPLE_SIZE)
            sum, power = self.spectrum.compute(sample)
            self.map_result(Results.update_value, sum, power)

            if self.running:
                count += 1
                total_sum += sum
                total_power += power

                if count >= self.target_pv.get():
                    self.map_result(Results.update_mean,
                        total_sum / count, total_power / count)
                    count = 0
                    total_sum[:] = 0
                    total_power[:] = 0
            else:
                count = 0
                total_sum[:] = 0
                total_power[:] = 0
            self.count_pv.set(count)

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

    def __on_update(self, value):
        running = value > self.threshold_pv.get()
        if running != self.running:
            self.running = running
            self.status_pv.set(running)


# -----------------------------------------------------------------------------
# Command line argument processing.

def eval_expr(expr):
    '''Evaluates expr in a context with numpy in the local dictionary.'''
    numpy_dict = dict((n, getattr(numpy, n)) for n in dir(numpy))
    return eval(expr, {}, numpy_dict)

def call_eval(option, opt, value, parser):
    parser.values.frequencies = eval_expr(value)


parser = optparse.OptionParser(usage = '''\
fa-spectrum [options] location id-list

Computes full spectrum analysis for the given PVs.''')
parser.add_option(
    '-f', dest = 'full_path', default = False, action = 'store_true',
    help = 'location is full path to file')
parser.add_option(
    '-S', dest = 'server', default = None,
    help = 'Override server address in location file')
parser.add_option(
    '-I', dest = 'ioc_name', default = 'TS-DI-IOC-02',
    help = 'Name of this IOC')
parser.add_option(
    '-D', dest = 'device_name', default = None,
    help = 'Device name for control PVs')
parser.add_option(
    '-s', dest = 'sample_size', default = 4096, type = 'int',
    help = 'Number of samples (at 1kHz) per update.  Should be power of 2, '
        'default is 4096 or around 4s per update')
parser.add_option(
    '-t', dest = 'target_count', default = 15, type = 'int',
    help = 'Number of updates per mean spectrum.  Can be changed via PV.')
parser.add_option(
    '-F', dest = 'frequencies', default = eval_expr('arange(1,301)'),
    action = 'callback', callback = call_eval, type = 'string',
    help = 'numpy expression evaluating to a range of frequencies.  '
        'The default is \'arange(1,301)\', but any monotonically increasing '
        'range from 1 to a sensible upper bound can be specified.')
parser.add_option(
    '-Q', dest = 'qualifier', default = None,
    help = 'Threshold current and qualifier PV for generating updates')
options, args = parser.parse_args()
try:
    location, id_list = args
except:
    parser.error('Arguments should be location and id list')

# The device name is computed from the location if appropriate.
if options.device_name:
    DEVICE_NAME = options.device_name
else:
    if options.full_path:
        parser.error('Must specify device name with location path')
    DEVICE_NAME = '%s-DI-SPEC-01' % location


# Load the configured location file.
falib.load_location_file(
    globals(), location, options.full_path, options.server)
server = falib.Server(FA_SERVER, FA_PORT)


# BPMs are specified as a comma separated list of FA ids, and we map these to
# the corresponding BPM names using the configured BPM list.
FA_IDS = map(int, id_list.split(','))
bpm_list = dict(falib.load_bpm_list(BPM_LIST))
FA_NAMES = [bpm_list[bpm] for bpm in FA_IDS]

# Extract remaining options for processing.
IOC_NAME = options.ioc_name
HALF_SAMPLE_SIZE = options.sample_size
TARGET_COUNT = options.target_count
FREQUENCIES = options.frequencies

SPEC_LEN = len(FREQUENCIES)

# Get the underlying sample frequency
F_S = server.sample_frequency / server.decimation


# Spectrum monitor and associated PVs.
if options.qualifier:
    monitor = Monitor(*options.qualifier.split(',', 1))
else:
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
