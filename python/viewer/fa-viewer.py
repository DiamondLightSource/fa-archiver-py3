'''Viewer of live FA beam position data.'''

from pkg_resources import require
require('cothread')

import os, sys
import optparse
import numpy
from PyQt4 import Qwt5, QtGui, QtCore, uic

import cothread
import falib

import modes
import buffer

from modes import X_colour, Y_colour


#   FA Sniffer Viewer

# This is the implementation of the viewer as a Qt display application.

Display_modes = [
    modes.mode_raw, modes.mode_fft, modes.mode_fft_logf, modes.mode_integrated]

Timebase_list = [
    ('100ms', 1000),    ('250ms', 2500),    ('0.5s',  5000),
    ('1s',   10000),    ('2.5s', 25000),    ('5s',   50000),
    ('10s', 100000),    ('25s', 250000),    ('50s', 500000)]

# Start up with 1 second window
INITIAL_TIMEBASE = 3

SCROLL_THRESHOLD = 10000

# Start up in raw display mode
INITIAL_MODE = 0

# Default location used if no location specified on command line.
DEFAULT_LOCATION = 'SR'


class SpyMouse(QtCore.QObject):
    MouseMove = QtCore.pyqtSignal(QtCore.QPoint)

    def __init__(self, parent):
        QtCore.QObject.__init__(self, parent)
        parent.setMouseTracking(True)
        parent.installEventFilter(self)

    def eventFilter(self, object, event):
        if event.type() == QtCore.QEvent.MouseMove:
            self.MouseMove.emit(event.pos())
        return QtCore.QObject.eventFilter(self, object, event)


class Viewer:
    Plot_tooltip = \
        'Click and drag to zoom in, ' \
        'middle click to zoom out, right click and drag to pan.'

    '''application class'''
    def __init__(self, ui, server, port):
        self.ui = ui

        self.makeplot()

        self.monitor = buffer.monitor(
            server, port, self.on_data_update, self.on_connect, self.on_eof,
            500000, 10000)

        # Prepare the selections in the controls
        ui.timebase.addItems([l[0] for l in Timebase_list])
        ui.mode.addItems([l.mode_name for l in Display_modes])
        ui.channel_group.addItems([l[0] for l in BPM_list])
        ui.show_curves.addItems(['Show X&Y', 'Show X', 'Show Y'])

        ui.channel_id.setValidator(QtGui.QIntValidator(0, 255, ui))

        ui.position_xy = QtGui.QLabel('', ui.statusbar)
        ui.statusbar.addPermanentWidget(ui.position_xy)
        ui.status_message = QtGui.QLabel('', ui.statusbar)
        ui.statusbar.addWidget(ui.status_message)


        # For each possible display mode create the initial state used to manage
        # that display mode and set up the initial display mode.
        self.mode_list = [l(self) for l in Display_modes]
        self.mode = self.mode_list[INITIAL_MODE]
        self.mode.set_enable(True)
        self.ui.mode.setCurrentIndex(INITIAL_MODE)

        self.show_x = True
        self.show_y = True
        self.mode.show_xy(True, True)

        # Make the initial GUI connections
        ui.channel_group.currentIndexChanged.connect(self.set_group)
        ui.channel.currentIndexChanged.connect(self.set_channel)
        ui.channel_id.editingFinished.connect(self.set_channel_id)
        ui.timebase.currentIndexChanged.connect(self.set_timebase)
        ui.rescale.clicked.connect(self.rescale_graph)
        ui.mode.currentIndexChanged.connect(self.set_mode)
        ui.run.clicked.connect(self.toggle_running)
        ui.show_curves.currentIndexChanged.connect(self.show_curves)
        ui.full_data.clicked.connect(self.set_full_data)

        # Initial control settings: these all trigger GUI related actions.
        self.channel_ix = 0
        self.full_data = not decimation_factor
        ui.channel_group.setCurrentIndex(1)
        ui.timebase.setCurrentIndex(INITIAL_TIMEBASE)
        if not decimation_factor:
            ui.full_data.setVisible(False)

        # Go!
        self.monitor.start()
        self.ui.show()

    def makecurve(self, colour, dotted=False):
        c = Qwt5.QwtPlotCurve()
        pen = QtGui.QPen(colour)
        if dotted:
            pen.setStyle(QtCore.Qt.DotLine)
        c.setPen(pen)
        c.attach(self.plot)
        return c

    def makeplot(self):
        '''set up plotting'''
        # make any contents fill the empty frame
        self.ui.axes.setLayout(QtGui.QGridLayout(self.ui.axes))

        # Draw a plot in the frame.  We do this, rather than defining the
        # QwtPlot object in Qt designer because loadUi then fails!
        plot = Qwt5.QwtPlot(self.ui.axes)
        self.ui.axes.layout().addWidget(plot)

        self.plot = plot
        self.cx = self.makecurve(X_colour)
        self.cy = self.makecurve(Y_colour)

        # set background to black
        plot.setCanvasBackground(QtCore.Qt.black)

        # Enable zooming
        plot.setStatusTip(self.Plot_tooltip)
        zoom = Qwt5.QwtPlotZoomer(plot.canvas())
        zoom.setRubberBandPen(QtGui.QPen(QtCore.Qt.white))
        zoom.setTrackerPen(QtGui.QPen(QtCore.Qt.white))
        # This is a poorly documented trick to disable the use of the right
        # button for cancelling zoom, so we can use it for panning instead.  The
        # first argument of setMousePattern() selects the zooming action, and is
        # one of the following with the given default assignment:
        #
        #   Index   Button          Action
        #   0       Left Mouse      Start and stop rubber band selection
        #   1       Right Mouse     Restore to original unzoomed axes
        #   2       Middle Mouse    Zoom out one level
        #   3       Shift Left      ?
        #   4       Shift Right     ?
        #   5       Shift Middle    Zoom back in one level
        zoom.setMousePattern(1, QtCore.Qt.NoButton)
        self.zoom = zoom

        # Enable panning.  We reconfigure the active mouse to use the right
        # button so that panning and zooming can coexist.
        pan = Qwt5.QwtPlotPanner(plot.canvas())
        pan.setMouseButton(QtCore.Qt.RightButton)

        # Monitor mouse movements over the plot area so we can show the position
        # in coordinates.
        SpyMouse(plot.canvas()).MouseMove.connect(self.mouse_move)


    # --------------------------------------------------------------------------
    # GUI event handlers

    def set_group(self, ix):
        self.group_index = ix
        self.ui.channel_id.setVisible(ix == 0)
        self.ui.channel.setVisible(ix > 0)

        if ix == 0:
            self.ui.channel_id.setText(str(self.channel))
        else:
            self.ui.channel.blockSignals(True)
            self.ui.channel.clear()
            self.ui.channel.addItems([l[0] for l in BPM_list[ix][1]])
            self.ui.channel.setCurrentIndex(-1)
            self.ui.channel.blockSignals(False)

            self.ui.channel.setCurrentIndex(self.channel_ix)

    def set_channel(self, ix):
        self.channel_ix = ix
        bpm = BPM_list[self.group_index][1][ix]
        self.channel = bpm[1]
        self.bpm_name = 'BPM: %s (id %d)' % (bpm[0], self.channel)
        self.monitor.set_channel(
            id = self.channel, decimated = not self.full_data)

    def set_channel_id(self):
        channel = int(self.ui.channel_id.text())
        if channel != self.channel:
            self.channel = channel
            self.bpm_name = 'BPM id %d' % channel
            self.monitor.set_channel(id = channel)

    def set_full_data(self, full_data):
        self.full_data = full_data
        self.monitor.set_channel(decimated = not full_data)
        self.update_timebase()
        self.reset_mode()

    def rescale_graph(self):
        self.mode.rescale(self.monitor.read())
        self.plot.setAxisScale(
            Qwt5.QwtPlot.xBottom, self.mode.xmin, self.mode.xmax)
        self.plot.setAxisScale(
            Qwt5.QwtPlot.yLeft, self.mode.ymin, self.mode.ymax)
        self.zoom.setZoomBase()
        self.plot.replot()

    def set_timebase(self, ix):
        self.timebase = Timebase_list[ix][1]
        self.update_timebase()
        self.reset_mode()

    def update_timebase(self):
        timebase = self.timebase
        if self.full_data:
            factor = 1
        else:
            factor = decimation_factor
        self.monitor.resize(
            timebase / factor, min(timebase, SCROLL_THRESHOLD) / factor)

    def set_mode(self, ix):
        self.mode.set_enable(False)
        self.mode = self.mode_list[ix]
        self.mode.set_enable(True)
        self.reset_mode()

    def toggle_running(self, running):
        if running:
            self.monitor.start()
        else:
            self.monitor.stop()
            self.redraw()

    def show_curves(self, ix):
        self.show_x = ix in [0, 1]
        self.show_y = ix in [0, 2]
        self.cx.setVisible(self.show_x)
        self.cy.setVisible(self.show_y)
        self.mode.show_xy(self.show_x, self.show_y)
        self.plot.replot()

    def mouse_move(self, pos):
        x = self.plot.invTransform(Qwt5.QwtPlot.xBottom, pos.x())
        y = self.plot.invTransform(Qwt5.QwtPlot.yLeft, pos.y())
        self.ui.position_xy.setText(
            '%s: %.4g %s, %s: %.4g %s' % (
                self.mode.xshortname, x, self.mode.xunits,
                self.mode.yshortname, y, self.mode.yunits))


    # --------------------------------------------------------------------------
    # Data event handlers

    def on_data_update(self, value):
        self.mode.plot(value)
        if self.ui.autoscale.isChecked() and self.zoom.zoomRectIndex() == 0:
            self.mode.rescale(value)
            self.plot.setAxisScale(
                Qwt5.QwtPlot.yLeft, self.mode.ymin, self.mode.ymax)
            self.zoom.setZoomBase()
        self.plot.replot()

    def on_connect(self):
        self.ui.run.setChecked(True)
        self.ui.status_message.setText(self.bpm_name)

    def on_eof(self, message):
        self.ui.run.setChecked(False)
        self.ui.status_message.setText('FA server disconnected: %s' % message)


    # --------------------------------------------------------------------------
    # Handling

    def reset_mode(self):
        sample_count = self.timebase
        sample_frequency = F_S
        if not self.full_data:
            sample_count /= decimation_factor
            sample_frequency /= decimation_factor
        # Unify the above with self.set_timebase?
        self.mode.set_timebase(sample_count, sample_frequency)

        self.mode.show_xy(self.show_x, self.show_y)

        x = Qwt5.QwtPlot.xBottom
        y = Qwt5.QwtPlot.yLeft
        self.plot.setAxisTitle(
            x, '%s (%s)' % (self.mode.xname, self.mode.xunits))
        self.plot.setAxisTitle(
            y, '%s (%s)' % (self.mode.yname, self.mode.yunits))
        self.plot.setAxisScaleEngine(x, self.mode.xscale())
        self.plot.setAxisScaleEngine(y, self.mode.yscale())
        self.plot.setAxisMaxMinor(x, self.mode.xticks)
        self.plot.setAxisScale(x, self.mode.xmin, self.mode.xmax)
        self.plot.setAxisScale(y, self.mode.ymin, self.mode.ymax)
        self.zoom.setZoomBase()

        self.redraw()

    def redraw(self):
        self.on_data_update(self.monitor.read())


class KeyFilter(QtCore.QObject):
    # Implements ctrl-Q or the standard binding for fast exit.
    def eventFilter(self, watched, event):
        if event.type() == QtCore.QEvent.KeyPress:
            key = QtGui.QKeyEvent(event)
            # \x11 is CTRL-Q; I can't find any other way to force a match.
            if key.text() == '\x11' or key.matches(QtGui.QKeySequence.Quit):
                cothread.Quit()
                return True
        return False


parser = optparse.OptionParser(usage = '''\
fa-viewer [-f] [location]

Display live Fast Acquisition data from EBPM data stream.  The location can
be one of %s, or full path to location file if -f specified.
The default location is %s.''' % (
    ', '.join(falib.config.list_location_files()), DEFAULT_LOCATION))
parser.add_option(
    '-f', dest = 'full_path', default = False, action = 'store_true',
    help = 'Location is full path to location file')
parser.add_option(
    '-S', dest = 'server', default = None,
    help = 'Override server address in location file')
options, arglist = parser.parse_args()
if len(arglist) > 1:
    parser.error('Unexpected arguments')
if arglist:
    location = arglist[0]
else:
    location = DEFAULT_LOCATION

# Load the location file and compute the groups
falib.load_location_file(
    globals(), location, options.full_path, options.server)
BPM_list = falib.compute_bpm_groups(BPM_LIST, GROUPS, PATTERNS)


F_S = falib.get_sample_frequency(server = FA_SERVER, port = FA_PORT)
try:
    decimation_factor = falib.get_decimation(server = FA_SERVER, port = FA_PORT)
except:
    # Assume unable to receive decimation because not supported by server
    decimation_factor = 0


qapp = cothread.iqt()
key_filter = KeyFilter()
qapp.installEventFilter(key_filter)

# create and show form
ui_viewer = uic.loadUi(os.path.join(os.path.dirname(__file__), 'viewer.ui'))
# Bind code to form
s = Viewer(ui_viewer, FA_SERVER, FA_PORT)

cothread.WaitForQuit()
