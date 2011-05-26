#!/usr/bin/env dls-python2.6

from pkg_resources import require
require('cothread')
require('matplotlib')

import sys
import cothread
import falib
import numpy
from cothread import catools
cothread.iqt()

from PyQt4 import QtCore, QtGui, Qwt5

X_colour = QtGui.QColor(64, 64, 255)    # QtCore.Qt.blue is too dark
Y_colour = QtCore.Qt.red

def make_curve(colour):
    c = Qwt5.QwtPlotCurve()
    p = QtGui.QPen(colour)
    c.setPen(p)
    c.attach(plot)
    return c

minmax = (-1000, 1000)

plot = Qwt5.QwtPlot()
ca = make_curve(QtCore.Qt.black)
cx = make_curve(X_colour)
cy = make_curve(Y_colour)
plot.setAxisScale(Qwt5.QwtPlot.xBottom, 0, 167)
# plot.setAxisScale(Qwt5.QwtPlot.yLeft, *minmax)
plot.show()


def update_minmax(graph):
    global minmax
    mm = (1.1 * min(graph), 1.1 * max(graph))
    if mm[0] < minmax[0] or minmax[1] < mm[1]:
        minmax = (min(mm[0], minmax[0]), max(mm[1], minmax[1]))
        plot.setAxisScale(Qwt5.QwtPlot.yLeft, *minmax)


a = numpy.arange(170)
good = numpy.concatenate((a[0:12*7], a[12*7+2:]))
enabled = catools.caget('SR-DI-EBPM-01:ENABLED')[good] == 0

f_s = falib.get_sample_frequency()


def update_cis(freq):
    freq_n = 2 * numpy.pi * freq / f_s
    rf = 1j * freq_n * numpy.arange(sample_size)
    global cis
    cis = numpy.exp(rf) / sample_size

def update_mains(mains):
    update_cis(2 * mains)

if len(sys.argv) > 2:
    sample_size = int(sys.argv[2])
else:
    sample_size = 10000

if len(sys.argv) > 1 and sys.argv[1] != 'm':
    update_cis(float(sys.argv[1]))
else:
    update_mains(50.0)
    catools.camonitor('LI-TI-MTGEN-01:AC-FREQ', update_mains)


@cothread.Spawn
def show_response():
    sub = falib.subscription(range(1,169))

    x_axis = numpy.arange(168)[enabled]
    while True:
        r = sub.read(sample_size)

#         iq = numpy.sum(r * cis[:, None, None], 0)
        iq = numpy.tensordot(r, cis, axes=(0, 0))
        iq = iq[enabled]
        ph = numpy.exp(- 1j * numpy.angle(iq[3,0]))
        iq *= ph

        ca.setData(x_axis, numpy.abs(iq[:, 0]))
        cx.setData(x_axis, numpy.real(iq[:, 0]))
        cy.setData(x_axis, numpy.imag(iq[:, 0]))
#         update_minmax(numpy.abs(iq[:, 0]))
        plot.replot()

# We sit here (rather than in the show_response loop) so that we'll exit when
# the plot window is closed.
cothread.WaitForQuit()
