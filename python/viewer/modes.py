# ------------------------------------------------------------------------------
#   Mode Specific Functionality

# Four display modes are supported: raw data, FFT of data with linear and with
# logarithmic frequency axis, and integrated displacement (derived from the
# FFT).  These modes and their user support functionality are implemented by the
# classes below, one for each display mode.

import numpy
from PyQt4 import Qwt5, QtGui, QtCore


# Actually, these really belong in fa-viewer.py, but the practicalities of doing
# this are not worth the trouble.
X_colour = QtGui.QColor(64, 64, 255)    # QtCore.Qt.blue is too dark
Y_colour = QtCore.Qt.red


# Unicode characters
char_times  = u'\u00D7'             # Multiplication sign
char_mu     = u'\u03BC'             # Greek mu
char_sqrt   = u'\u221A'             # Square root sign
char_cdot   = u'\u22C5'             # Centre dot
char_squared = u'\u00B2'            # Superscript 2

micrometre  = char_mu + 'm'


class mode_common:
    yshortname = 'Y'

    def __init__(self, parent):
        self.parent = parent
        self.__tray = QtGui.QWidget(parent.ui)
        self.__tray_layout = QtGui.QHBoxLayout()
        self.__tray.setLayout(self.__tray_layout)
        parent.ui.bottom_row.addWidget(self.__tray)
        self.__tray_layout.setContentsMargins(0, 0, 0, 0)

        self.__tray.setVisible(False)

    def set_enable(self, enabled):
        self.__tray.setVisible(enabled)

    def addWidget(self, widget):
        self.__tray_layout.addWidget(widget)

    def show_xy(self, show_x, show_y):
        self.show_x = show_x
        self.show_y = show_y

    def plot(self, value):
        v = self.compute(value)
        self.parent.cx.setData(self.xaxis, v[:, 0])
        self.parent.cy.setData(self.xaxis, v[:, 1])

    def compute(self, value):
        return value

    def get_minmax(self, value):
        value = self.compute(value)
        ix = (self.show_x, self.show_y)
        ix = numpy.nonzero(ix)[0]           # Ugly numpy clever indexing failure
        return numpy.nanmin(value[:, ix]), numpy.nanmax(value[:, ix])

    def linear_rescale(self, value):
        low, high = self.get_minmax(value)
        margin = max(1e-3, 0.2 * (high - low))
        self.ymin = low - margin
        self.ymax = high + margin

    def log_rescale(self, value):
        self.ymin, self.ymax = self.get_minmax(value)

    rescale = log_rescale           # Most common default


class decimation:
    '''Common code for decimation selection.'''

    # Note that this code assumes that filter selectes a prefix of item_list
    def __init__(self, mode, parent, item_list, filter, on_update):
        self.parent = parent
        self.item_list = item_list
        self.filter = filter
        self.on_update = on_update

        mode.addWidget(QtGui.QLabel('Decimation', parent.ui))

        self.selector = QtGui.QComboBox(parent.ui)
        # To get the initial size right, start by adding all items
        self.selector.addItems(['%d:1' % n for n in item_list])
        self.selector.setSizeAdjustPolicy(QtGui.QComboBox.AdjustToContents)
        self.selector.currentIndexChanged.connect(self.set_decimation)
        mode.addWidget(self.selector)
        self.decimation = self.item_list[0]

    def set_decimation(self, ix):
        self.decimation = self.item_list[ix]
        self.on_update(self.decimation)
        self.parent.redraw()

    def update(self):
        self.selector.blockSignals(True)
        self.selector.clear()
        valid_items = filter(self.filter, self.item_list)
        if not valid_items:
            valid_items = self.item_list[:1]
        self.selector.addItems(['%d:1' % n for n in valid_items])

        if self.decimation not in valid_items:
            self.decimation = valid_items[-1]
        current_index = valid_items.index(self.decimation)
        self.selector.setCurrentIndex(current_index)
        self.selector.blockSignals(False)

        self.set_decimation(current_index)

    def resetIndex(self):
        self.selector.setCurrentIndex(0)


class mode_raw(mode_common):
    mode_name = 'Raw Signal'
    xname = 'Time'
    yname = 'Position'
    xshortname = 't'
    yunits = micrometre
    xscale = Qwt5.QwtLinearScaleEngine
    yscale = Qwt5.QwtLinearScaleEngine
    xticks = 5
    xmin = 0
    ymin = -10
    ymax = 10

    rescale = mode_common.linear_rescale
    Decimations = [1, 100, 1000]

    def __init__(self, parent):
        mode_common.__init__(self, parent)

        self.qt_diff = QtGui.QCheckBox('Diff', parent.ui)
        self.diff = False
        self.qt_diff.stateChanged.connect(self.set_diff)
        self.addWidget(self.qt_diff)

        self.selector = decimation(
            self, parent, self.Decimations,
            lambda d: 50*d < self.sample_count, self.set_decimation)
        self.decimation = self.selector.decimation

        self.maxx = parent.makecurve(X_colour, True)
        self.maxy = parent.makecurve(Y_colour, True)
        self.minx = parent.makecurve(X_colour, True)
        self.miny = parent.makecurve(Y_colour, True)
        self.show_x = True
        self.show_y = True
        self.set_visible(False)

    def set_diff(self, diff):
        self.diff = diff != 0
        if self.diff:
            self.selector.resetIndex()

    def set_visible(self, enabled=True):
        self.maxx.setVisible(enabled and self.decimation > 1 and self.show_x)
        self.maxy.setVisible(enabled and self.decimation > 1 and self.show_y)
        self.minx.setVisible(enabled and self.decimation > 1 and self.show_x)
        self.miny.setVisible(enabled and self.decimation > 1 and self.show_y)

    def set_enable(self, enabled):
        mode_common.set_enable(self, enabled)
        self.set_visible(enabled)

    def set_timebase(self, sample_count, sample_frequency):
        self.sample_count = sample_count
        duration = sample_count / sample_frequency
        if duration <= 1:
            self.xunits = 'ms'
            self.scale = 1e3
        else:
            self.xunits = 's'
            self.scale = 1.0
        self.xmax = self.scale * duration

        self.selector.update()

    def set_decimation(self, decimation):
        self.decimation = decimation
        sample_count = self.sample_count / decimation
        self.xaxis = self.xmax * numpy.arange(sample_count) / sample_count
        self.set_visible()
        if self.decimation != 1:
            self.qt_diff.setChecked(False)

    def compute(self, value):
        if self.diff:
            return numpy.diff(value, axis=0)
        else:
            return value

    def plot(self, value):
        value = self.compute(value)
        if self.decimation == 1:
            mean = value
        else:
            points = len(value) // self.decimation
            value = value[:points * self.decimation].reshape(
                (points, self.decimation, 2))
            mean = numpy.mean(value, axis=1)
            min = numpy.min(value, axis=1)
            max = numpy.max(value, axis=1)

            self.maxx.setData(self.xaxis, max[:, 0])
            self.maxy.setData(self.xaxis, max[:, 1])
            self.minx.setData(self.xaxis, min[:, 0])
            self.miny.setData(self.xaxis, min[:, 1])

        self.parent.cx.setData(self.xaxis, mean[:, 0])
        self.parent.cy.setData(self.xaxis, mean[:, 1])

    def show_xy(self, show_x, show_y):
        mode_common.show_xy(self, show_x, show_y)
        self.set_visible()


def scaled_abs_fft(value, sample_frequency, windowed=False, axis=0):
    '''Returns the fft of value (along axis 0) scaled so that values are in
    units per sqrt(Hz).  The magnitude of the first half of the spectrum is
    returned.'''
    if windowed:
        # The Hann window is good enough.  In some cases the Hamming window
        # looks a bit better, but then I'd need a choice of windows.  Not really
        # the point here, so just go for the simplest...
        window = 1 + numpy.cos(
            numpy.linspace(-numpy.pi, numpy.pi, value.shape[axis]))
        value = value * window[:, None]
    fft = numpy.fft.fft(value, axis=axis)

    # This trickery below is simply implementing fft[:N//2] where the slicing is
    # along the specified axis rather than axis 0.  It does seem a bit
    # complicated...
    N = value.shape[axis]
    slice = [numpy.s_[:] for s in fft.shape]
    slice[axis] = numpy.s_[:N//2]
    fft = fft[slice]

    # Finally scale the result into units per sqrt(Hz)
    return numpy.abs(fft) * numpy.sqrt(2.0 / (sample_frequency * N))

def fft_timebase(sample_count, sample_frequency, scale=1.0):
    '''Returns a waveform suitable for an FFT timebase with the given number of
    points.'''
    return scale * sample_frequency * \
        numpy.arange(sample_count // 2) / sample_count


class mode_fft(mode_common):
    mode_name = 'FFT'
    xname = 'Frequency'
    yname = 'Amplitude'
    xshortname = 'f'
    xunits = 'Hz'
    xscale = Qwt5.QwtLinearScaleEngine
    yscale = Qwt5.QwtLog10ScaleEngine
    xticks = 5
    xmin = 0
    ymin_normal = 1e-4
    ymax = 1

    Decimations = [1, 10, 100]

    def __init__(self, parent):
        mode_common.__init__(self, parent)

        self.windowed = QtGui.QCheckBox('Windowed', parent.ui)
        self.addWidget(self.windowed)

        squared = QtGui.QCheckBox(
            '%s%s/Hz' % (micrometre, char_squared), parent.ui)
        squared.stateChanged.connect(self.set_squared)
        self.addWidget(squared)

        self.selector = decimation(
            self, parent, self.Decimations,
            lambda d: 1000 * d <= self.sample_count, self.set_decimation)

        self.set_squared_state(False)
        self.decimation = self.selector.decimation

    def set_timebase(self, sample_count, sample_frequency):
        self.sample_count = sample_count
        self.sample_frequency = sample_frequency
        self.xmax = sample_frequency / 2
        self.selector.update()

    def set_decimation(self, decimation):
        self.decimation = decimation
        self.xaxis = fft_timebase(
            self.sample_count // self.decimation, self.sample_frequency)

    def set_squared_state(self, show_squared):
        self.show_squared = show_squared
        if show_squared:
            self.yunits = '%s%s/Hz' % (micrometre, char_squared)
            self.ymin = self.ymin_normal ** 2
        else:
            self.yunits = '%s/%sHz' % (micrometre, char_sqrt)
            self.ymin = self.ymin_normal

    def set_squared(self, squared):
        self.set_squared_state(squared != 0)
        self.parent.reset_mode()

    def compute(self, value):
        windowed = self.windowed.isChecked()
        if self.decimation == 1:
            result = scaled_abs_fft(
                value, self.sample_frequency, windowed = windowed)
        else:
            # Compute a decimated fft by segmenting the waveform (by reshaping),
            # computing the fft of each segment, and computing the mean power of
            # all the resulting transforms.
            N = len(value)
            points = len(value) // self.decimation
            value = value[:points * self.decimation].reshape(
                (self.decimation, points, 2))
            fft = scaled_abs_fft(
                value, self.sample_frequency, windowed = windowed, axis=1)
            result = numpy.sqrt(numpy.mean(fft**2, axis=0))
        if self.show_squared:
            return result ** 2
        else:
            return result


def compute_gaps(l, N):
    '''This computes a series of logarithmically spaced indexes into an array
    of length l.  N is a hint for the number of indexes, but the result may
    be somewhat shorter.'''
    gaps = numpy.int_(numpy.logspace(0, numpy.log10(l), N))
    counts = numpy.diff(gaps)
    return counts[counts > 0]

def condense(value, counts):
    '''The given waveform is condensed in logarithmic intervals so that the same
    number of points are generated in each decade.  The accumulation and number
    of accumulated points are returned as separate waveforms.'''

    # The result is the same shape as the value in all axes except the first.
    shape = list(value.shape)
    shape[0] = len(counts)
    sums = numpy.empty(shape)

    left = 0
    for i, step in enumerate(counts):
        sums[i] = numpy.sum(value[left:left + step], axis=0)
        left += step
    return sums


FFT_LOGF_POINTS = 5000

class mode_fft_logf(mode_common):
    mode_name = 'FFT (log f)'
    xname = 'Frequency'
    xshortname = 'f'
    xunits = 'Hz'
    xscale = Qwt5.QwtLog10ScaleEngine
    yscale = Qwt5.QwtLog10ScaleEngine
    xticks = 10

    Filters = [1, 10, 100]

    def set_timebase(self, sample_count, sample_frequency):
        self.sample_frequency = sample_frequency
        self.xmax = sample_frequency / 2
        self.counts = compute_gaps(sample_count // 2 - 1, FFT_LOGF_POINTS)
        self.xaxis = sample_frequency * numpy.cumsum(self.counts) / sample_count
        self.xmin = self.xaxis[0]
        self.reset = True

    def compute(self, value):
        windowed = self.windowed.isChecked()
        fft = scaled_abs_fft(
            value, self.sample_frequency, windowed = windowed)[1:]
        fft_logf = numpy.sqrt(
            condense(fft**2, self.counts) / self.counts[:,None])
        if self.scalef:
            fft_logf *= self.xaxis[:, None]

        if self.filter == 1:
            return fft_logf
        elif self.reset:
            self.reset = False
            self.history = fft_logf**2
            return fft_logf
        else:
            self.history = \
                self.filter * fft_logf**2 + (1 - self.filter) * self.history
            return numpy.sqrt(self.history)

    def __init__(self, parent):
        mode_common.__init__(self, parent)

        self.windowed = QtGui.QCheckBox('Windowed', parent.ui)
        self.addWidget(self.windowed)

        check_scalef = QtGui.QCheckBox('scale by f', parent.ui)
        self.addWidget(check_scalef)
        check_scalef.stateChanged.connect(self.set_scalef_state)

        self.addWidget(QtGui.QLabel('Filter', parent.ui))

        selector = QtGui.QComboBox(parent.ui)
        selector.addItems(['%ds' % f for f in self.Filters])
        self.addWidget(selector)
        selector.currentIndexChanged.connect(self.set_filter)

        self.filter = 1
        self.reset = True
        self.set_scalef(False)

    def set_filter(self, ix):
        self.filter = 1.0 / self.Filters[ix]
        self.reset = True

    def set_scalef(self, scalef):
        self.scalef = scalef
        if self.scalef:
            self.yname = 'Amplitude %s freq' % char_times
            self.yunits = '%s%s%sHz' % (micrometre, char_cdot, char_sqrt)
            self.yshortname = 'f%sY' % char_cdot
            self.ymin = 1e-3
            self.ymax = 100
        else:
            self.yname = 'Amplitude'
            self.yunits = '%s/%sHz' % (micrometre, char_sqrt)
            self.yshortname = 'Y'
            self.ymin = 1e-4
            self.ymax = 1

    def set_scalef_state(self, scalef):
        self.set_scalef(scalef != 0)
        self.parent.reset_mode()


class mode_integrated(mode_common):
    mode_name = 'Integrated'
    xname = 'Frequency'
    yname = 'Cumulative amplitude'
    xshortname = 'f'
    xunits = 'Hz'
    yunits = micrometre
    xscale = Qwt5.QwtLog10ScaleEngine
    yscale = Qwt5.QwtLog10ScaleEngine
    xticks = 10
    ymin = 1e-3
    ymax = 10

    def set_timebase(self, sample_count, sample_frequency):
        self.sample_frequency = sample_frequency
        self.xmax = sample_frequency / 2
        self.counts = compute_gaps(sample_count // 2 - 1, FFT_LOGF_POINTS)[1:]
        self.xaxis = sample_frequency * (
            numpy.cumsum(self.counts) + 1) / sample_count
        self.xmin = self.xaxis[0]

    def compute(self, value):
        N = len(value)
        fft2 = condense(
            scaled_abs_fft(value, self.sample_frequency)[2:]**2, self.counts)
        if self.reversed:
            cumsum = numpy.cumsum(fft2[::-1], axis=0)[::-1]
        else:
            cumsum = numpy.cumsum(fft2, axis=0)
        return numpy.sqrt(self.sample_frequency / N * cumsum)

    def __init__(self, parent):
        mode_common.__init__(self, parent)

        reversed = QtGui.QCheckBox('Reversed', parent.ui)
        self.addWidget(reversed)
        reversed.stateChanged.connect(self.set_reversed)
        self.reversed = False

        yselect = QtGui.QCheckBox('Linear', parent.ui)
        self.addWidget(yselect)
        yselect.stateChanged.connect(self.set_yscale)

        button = QtGui.QPushButton('Background', parent.ui)
        self.addWidget(button)
        button.clicked.connect(self.set_background)

        self.cxb = parent.makecurve(X_colour, True)
        self.cyb = parent.makecurve(Y_colour,  True)
        self.show_x = True
        self.show_y = True

    def set_enable(self, enabled):
        mode_common.set_enable(self, enabled)
        self.cxb.setVisible(enabled and self.show_x)
        self.cyb.setVisible(enabled and self.show_y)

    def set_background(self):
        v = self.compute(self.parent.monitor.read())
        self.cxb.setData(self.xaxis, v[:, 0])
        self.cyb.setData(self.xaxis, v[:, 1])
        self.parent.plot.replot()

    def set_yscale(self, linear):
        if linear:
            self.yscale = Qwt5.QwtLinearScaleEngine
        else:
            self.yscale = Qwt5.QwtLog10ScaleEngine
        self.parent.reset_mode()

    def set_reversed(self, reversed):
        self.reversed = reversed
        self.parent.plot.replot()

    def show_xy(self, show_x, show_y):
        mode_common.show_xy(self, show_x, show_y)
        self.cxb.setVisible(show_x)
        self.cyb.setVisible(show_y)
