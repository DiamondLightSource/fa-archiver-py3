=======
fa_load
=======

.. Written in reStructuredText
.. default-role:: literal

-----------------------------------------------
Matlab script for fetching data from FA archive
-----------------------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2012-07-30
:Manual section:    7
:Manual group:      Diamond Light Source

Synopsis
========
*data* = fa_load(*times*, *id-list* [, *type* [, *server* [, *show_bar*]])

Description
===========
Captures data for the FA ids in *id-list* over the range of time specified by
the two element array *times* and returns a data structure containing the
fetched data.  Used internally by fa_zoomer_.

Unless *type* is `C` the array *times* contains two elements, start and end
time, in Matlab format, namely in days in the Matlab epoch.  The *server* can be
specified, otherwise the default server programmed into fa-capture_\(1) is used.

The *type* parameter determines the possible shapes of data that can be
captured.  If `C` is specifed (for continuous data) then *times* must be a
single number specifying the number of points to capture.

The *server* parameter can be used to specify the archiver server used for data
capture overriding the hard-wired default, which is `SR`.

The *show_bar* parameter can be set to `false` to suppress the progress bar
normally shown during loading of F and D data.

The format of the structure returned is similar to that described in detail in
fa-capture_\(1).


Type Argument
=============

The *type* parameter specifies the precise format of the data captured and
defaults to `F`.  The last character can be `Z`, in which case "id0" data is
also captured.  The following options are available:

`F`
    Full resolution historical data over the time specified by *times*.  The
    returned `data` array is three dimensional of the form `data(xy, id, t)`.

`D`, `d`
    Decimated data over the time specified by *times*.  If `D` is specified then
    "double decimated" data is fetched, otherwise for `d` "single decimated"
    data is.  The returned `data` array is four dimensional of the form
    `data(xy, field, id, t)` where *field* ranges from 1 to 4 selecting mean,
    min, max, deviation respectively.

`C`
    Live full resolution data is captured for the number of samples specified by
    *times*.  The returned `data` array is the same format as for `F`.

`CD`
    If available from the server, live decimated data is captured for the number
    of samples specified by *times*, as for `C`, and similarly the format is as
    for `F`.

`.Z`
    If *type* ends in `Z` then the `id0` array is also stored.


Data Returned
=============

The value returned is a Matlab structure with the following fields:

`decimation`
    Decimation factor corresponding to requested data type.  Will be 1 for `F`
    and `C` data, as configured by server (typically 128 and 16384 respectively)
    for `d` and `D` data.

`f_s`
    Sample rate of captured data at selected decimation.

`ids`
    Array of FA ids, copy of *id-list*.

`data`
    The returned data, format depending on *type* as described above.

`timestamp`
    Timestamp (in Matlab format) of first point.

`day`
    Matlab number of day containing first sample

`t`
    Timestamp array for each sample point, stored as time of day offsets from
    day containing first sample: compute `t+day` for underlying full timestamp.

`id0`
    Only present if `Z` specified as part of *type*, contains FA timebase
    information for captured data.


See Also
========
fa_zoomer_\(7), fa-capture_\(1)

.. _fa_zoomer: fa_zoomer.html
.. _fa-capture: fa-capture.html
