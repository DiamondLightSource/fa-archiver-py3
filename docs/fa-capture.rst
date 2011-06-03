==========
fa-capture
==========

.. Written in reStructuredText
.. default-role:: literal

------------------------------------------
Captures data from the FA archiver to disk
------------------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2011-05-27
:Manual section:    1
:Manual group:      Diamond Light Source

Synopsis
========
fa-capture [-o output-file] [-C|start-time] [options] pv-list [samples]

Description
===========
Captures data from the FA archiver, either historical data from the archive, or
live data from the data stream.

The list of pvs is specified as a comma separated sequence of FA ids or ranges
of ids, where a range is written as two ids separated by a hyphen, ie:

    pv-list = id [ "-" id ] [ "," pv-list ]

For example, 1-172 specifies all arc BPMs at Diamond at the time of writing.

The number of samples to be captured must specified when reading historical data
(-b, -s or -t) unless a range of times has been specified with these options,
using `~`.  If samples is not specified with continuous capture (-C) capture
must be interrupted with ctrl-C.  The sample count can be followed by the letter
`s` to specify a capture duration in seconds.


Options
=======
If historical data is wanted one of the following must be specified:

-s start-date
    Specify start, as a date and time in ISO 8601 date time format (with
    fractional seconds allowed).  Use a trailing `Z` for UTC time.  The
    following syntax must be used here:

        *yyyy*-\ *MM*-\ *dd*\T\ *hh*:*mm*:*ss*\[.\ *us*][Z]

    where *yyyy*-\ *MM*-\ *dd* is the numerical date and *hh*:*mm*:*ss* is the
    time in hours, minutes and seconds.  The time in seconds can be followed by
    a decimal fraction to microsecond precision, and `Z` can be added to the
    end, otherwise local time on the client will be used to compute the start
    time.

-t start-time
    Specify start as a time of day today, or yesterday if `Y` added to the end,
    in format hh:mm:ss[Y], interpreted as a local time.

-b start-age
    Specify start as a time in the past as hh:mm:ss ago.

For each of these three flags a range of times separated by `~` can be specified
instead of giving a sample count.  In each of these cases the syntax and meaning
of the second time is exactly the same as the first.

Alternatively, continuous capture of live data can be specified:

-C
    Request continuous capture from live data stream

The following further options can be given:

-o output-file
    Save output to specified file, otherwise will be sent to stdout.

-f data-format
    Specify data format, can be `-fF` for full rate data (the default), `-fd`\
    [mask] for single decimated data, or `-fD`\ [mask] for double decimated
    data, where [mask] is an optional data mask, default value 15 (all fields).
    Decimated data is only available for archived data.

    The bits in the data mask correspond to decimated fields:

    :1:  mean
    :2:  min
    :4:  max
    :8:  standard deviation

    where each of these values is computed over the decimation interval

-a
    Capture all available data even if too much requested.  Otherwise
    capture fails if either the start or end time of the capture request falls
    outside the archive or in a gap in the arthive.

-R
    Save in raw format, otherwise the data is saved in matlab format.

-c
    Forbid any gaps in the captured sequence, contiguous data only.  Capture
    will fail if the data was interrupted.

-k
    Keep extra dimensions in matlab values

-n:
    Specify name of data array (default is "data")

-S:
    Specify archive server to read from.  The default name is compiled into
    fa-capture.

-p:
    Specify port to connect to on server (default is 8888).

-q
    Suppress display of progress of capture on stderr.

-z
    Check for gaps in ID0 data, otherwise any communication controller timebase
    skips are ignored.  If the *id0* field is wanted this must be specified.  If
    the hardware is unable to correctly retrieve the timebase information
    setting this information can report an excessive gap count.

-Z
    Use UTC timestamps for matlab timestamps, otherwise local time is used
    including any local daylight saving offset.


Data Format
===========
If -R is specified fa-capture saves the captured data in the format retrieved
from the server, see fa-archiver_\(1) for details.  Otherwise the data is saved
in matlab format with the following fields.

:decimation:
    Decimation factor, or 1 if full data captured.

:f_s:
    Nominal sample frequency as reported by archiver at the time of
    retrieval.  Note that this is not particularly accurate and is not tied
    to the time of capture.

:timestamp:
    Timestamp in matlab format of the first captured sample.

:ids:
    Array of requested FA ids captured in ascending numerical order.

:data:
    Data array.  See detailed description below.

Unless `-c` is specified the following fields are set:

:gapix:
    This is an array of gap indexes recording the offset into the *data* array
    of the start of each contiguous data block.  The first value in the array
    always has the value 0.

:gaptimes:
    Unless `-c` was specified the timestamps in matlab format of the start
    of each contiguous block is recorded in this array.  The first value in the
    array is always equal to *timestamp*.

If `-z` was specified then a further field is set:

:id0:
    If `-z` was used to retrieve id0 data then the communication controller
    timebase counter for the first sample is returned as this value.


Data Array
----------
The *data* array is a two, three or four dimensional array, depending on the
settings of the `-f` and `-k` options, with the following meanings:

    data(xy, [field,] [bpm-id,] timebase)

:xy:
    The xy dimension is always present with a range of 2, with *data(1,:)*
    containing X positions and *data(2,:)* containing Y positions.

:field:
    The field dimension is only present for decimated data, and is omitted if
    only one field of decimated data was captured and `-k` was not set.  This
    field ranges over the number of mask bits set in `-fD` or `-fd`.

:bpm-id:
    This dimension ranges over the list of captured FA ids, and the
    corresponding FA id can be looked up in *ids(bpm_id)*.  This dimension is
    omitted if `-k` is not set and only one FA id was requested.

:timebase:
    This ranges over the time of sample capture.


See Also
========
fa-archiver_\(1)

.. _fa-archiver: fa-archiver.html
