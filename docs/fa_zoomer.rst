=========
fa_zoomer
=========

.. Written in reStructuredText
.. default-role:: literal

------------------------------------------------------
Matlab script for interactive inspection of FA archive
------------------------------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2011-05-27
:Manual section:    7
:Manual group:      Diamond Light Source

Synopsis
========
fa_zoomer [*server*]

Description
===========
Invokes an interactive Matlab window with two graphs and a number of GUI
controls.  By default connects to a server hard-wired into the source, but an
alternative server name can be specified as an argument.

Starts with the last 24 hours of archive data, or as much as available,
displayed.  Initially FA id 4 is selected.  X data is shown in the top graph and
Y data in the bottom graph, and normally second decimated data (1/16384
decimation) is show with min and max limits plotted.

To use the zoomer first use the interactive zoom facility (invoked via the
magnifying glass icon) to select the region of interest and then press on the
Zoom button to load the zoomed data from the archive.  The decimation factor of
the loaded data is automatically adjusted to avoid trying to fetch too much data
from the archiver.

All the data loaded into the zoomer window is stored in a global Matlab variable
called `data`, so this can be accessed by typing the Matlab command ::

    global data

and accessing the fields of this structure.  The format of this structure is
described in fa_load_\(7).

Controls
========
The following controls are available:

List of FA ids
    This editable box contains a list of the FA ids which will be fetched from
    the archiver and displayed in the zoomer graphs.  The syntax here is a
    normal Matlab expression.

Back
    Pressing this will return the zoom state to the previously selected zoom.
    History is maintained from the point `fa_zoomer` is invoked.

Full
    This displays the entire archive period, and works by asking the archiver to
    return all available data.

24h
    This displays the last 24 hours of archive data.

Zoom
    Pressing this after selecting an area in either of the graphs will cause the
    selected data to be fetched from the archive and displayed.

Spectrogram
    If undecimated data is on display a spectrogram is shown.

Status message
    This status window shows "busy" while fetching from the archive, otherwise
    shows the following information:

        [*id-count*] *samples*/*decimation*

    This is a summary of the status of the captured data.  *id-count* is the
    number of FA ids fetched and on display, *samples* is the total number of
    samples, and *decimation* is the current decimation.

Maximum number of samples
    This editable number, initialised to 1,000,000, is used to regulate how much
    data is fetched from the archive by controlling the choice of decimation.
    Obviously forcing this number too large can cause Matlab to run out of
    memory and long delays while fetching data.

Zoomed
    If this is selected then the vertical scale is automatically forced to +-100
    microns -- this is useful for normal stored beam with position feedback.

Show std
    When decimated data is on display this selects the display of the standard
    deviation value rather than min and max ranges.

See Also
========
fa_load_\(7)

.. _fa_load:    fa_load.html
