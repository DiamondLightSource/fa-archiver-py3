===========
fa-spectrum
===========

.. Written in reStructuredText
.. default-role:: literal

----------------------------------------------------
Soft IOC for computing and monitoring power spectrum
----------------------------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2011-05-27
:Manual section:    1
:Manual group:      Diamond Light Source

Synopsis
========
fa-spectrum [*options*] *location* *id-list*

Description
===========
Computes full spectrum analysis for the given PVs.  Runs as a Python soft IOC,
subscribes to the decimated archiver data stream, and computes detailed spectral
analysis of the FA ids listed in *id-list*.

The *location* is specified precisely as for fa-viewer_\(1).

Options
=======
-f
    Location is full path to file, default is to look in `python/conf`
    directory.

-S server
    Override server address in location file.

-I ioc_name
    Name of this IOC, used for a couple of status PVs.

-D device_name
    Device name for control PVs.

-s sample_size
    Number of decimated samples per update.  Should be power of 2, default is
    4096 or around 4s per update with default configuration.

-t target_count
    Number of updates per mean spectrum.  Can be changed via PV.

-F frequencies
    Python numpy expression evaluating to a range of frequencies.  The default
    is `arange(1,301)`, but any monotonically increasing range from 1 to a
    sensible upper bound can be specified.

-Q threshold_spec
    A beam current PV together with a threshold current can be specified, in
    which case the integrated amplitude will not be integrated or updated while
    the beam current is below this threshold.

    The *threshold_spec* must be of the form

        *threshold*,\ *pv*

    where *threshold* is the initial threshold current and *pv* is an EPICS PV
    to monitor.

Generated PVs
=============
A number of PVs are published by this IOC.

The following 2 PVs are published for administration with `$(ioc-name)` set to
the *ioc_name* parameter passed to the `-I` option:

$(ioc-name):HOSTNAME
    Host name of the machine running the IOC.

$(ioc-name):WHOAMI
    Brief description of the functionality of this IOC.

The following PVs provide overall control and are published with `$(control)`
set to the *device_name* parameter passed to the `-D` option:

$(control):TARGET
    Controls how many samples are accumulated before a cumulative spectrum
    update is generated, initialised to the parameter passed to the `-t` option.

$(control):COUNT
    Counts up to `$(control):TARGET` as samples are accumulated.

$(control):FREQ
    A waveform of the frequencies being analysed, representing the top edge of
    each frequency bin, in Hz.

$(control):THRESHOLD
    If `-Q` was used to enable threshold detection this PV can be used to adjust
    the current threshold.

$(control):STATUS
    If `-Q` was used to enable threshold detection this indicates whether
    spectrum integration is running or paused.


For each FA id listed on the command line in the given *id-list* the following
PVs are generated with `$(device)` set to the corresponding device name as
looked up in the `BPM_LIST` field in the configuration file specified by
*location*, see fa-viewer_\(1).

$(device):XAMPL, $(device):YAMPL
    Power density spectrum computed for the most recent sample and binned
    according to the frequency bins defined by the `-F` option and recorded in
    the `$(control):FREQ` PV, for both X and Y axes.  The units of this waveform
    are in micrometres per sqrt(Hz), assuming FA data transmitted in nanometres.

$(device):XSUM, $(device):YSUM
    Cumulative power spectra for X and Y for the most recent sample.  Note that
    because the power density curve is normalised to the bin size and internally
    compensated for slightly varying bin size, this waveform is not an exact
    integral of the corresponding `AMPL` waveform.

$(device):XMEANAMPL, $(device):YMEANAMPL
    Mean power density spectrum accumulated over `$(control):TARGET` samples.

$(device):XMEANSUM, $(device):YMEANSUM
    Mean cumulative power spectra accumulated over `$(control):TARGET` samples.


See Also
========
fa-viewer_\(1)

.. _fa-viewer: fa-viewer.html
