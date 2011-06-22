===========
fa-archiver
===========

.. Written in reStructuredText
.. default-role:: literal

-----------------------------------------------------------------------
Captures, stores and redistributes data from the FA archive data stream
-----------------------------------------------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2011-05-27
:Manual section:    1
:Manual group:      Diamond Light Source

.. :Version:           blah


Synopsis
========
fa-archiver [*options*] *archive-file*


Description
===========
Runs the FA archiver process.  Data is captured from an FA sniffer card and
saved to the configured archive file, which must have previously been prepared
using fa-prepare_\(1).  Normally simply called thus:

    fa-archiver -t -c filters/decimate.config *archive-file*

A TCP socket server on the configured port, number 8888 by default, provides
access to both the live data stream and any part of the stored archive.  The
fa-capture_\(1) tool can be used to access this data, fa-viewer_\(1) can be used
to view the live data streams, or falib_\(3) can be used to write Python
applications using the data stream.

Live data is available for any FA id on the controller network, while historical
data is only available for those pvs configured to be saved by fa-prepare_\(1).

Live data can be delivered either at the full FA data rate, or if the `-c`
option is specified, as a decimated data stream.  Historical data is available
at full data rate or at two degrees of decimation.  Note that the decimated live
data stream is unrelated to the archived decimated data, in particular the
decimated live data is properly down filtered, whereas the historical decimated
data is binned.

The archiver should normally be halted by sending it a SIGINT signal.


Options
=======
-c config-file
    Specify decimation configuration file.  If this is specified then streaming
    decimated data will be available for subscription.  See `Filter
    Configuration File`_ below.

-d device
    Specify device to use for FA sniffer (default `/dev/fa_sniffer0`).

-r
    Run sniffer thread at boosted priority.  Needs real time support.  This can
    help to reduce the risk of dropping updates from the communication network.

-b blocks
    Specify number of buffered input blocks (default 64).  The input block size
    is configured by the `-I` option of fa-prepare_\(1), and so the default
    buffer is 32MB, or 1 1/2 seconds of raw data.

-q
    Specify quiet output.  Otherwise every connection request and a number of
    other routine events are logged.

-t
    Output timestamps with logs.  No effect when logging to syslog when running
    as a daemon.

-D
    Run as a daemon.  In this case all logging is sent to syslog, and the `-p`
    option should probably be used.

-p pid-file
    Write PID to specified file, will refuse to start if file already exists,
    and deletes file on successful termination.

-s socket
    Specify server socket (default 8888).

-F matfile
    Run dummy sniffer with canned data.  See `Canned Data Format`_ for details.

-X
    Enable extra commands (debug only), see `Debug Command (D)`_.

-R
    Set SO_REUSEADDR on the listening socket.  Only useful for debugging when
    repeatedly restarting the server.

The recommended options are -c and -t.

The rest of this man page can be ignored by most users.


Filter Configuration File
=========================
The archiver can be configured to generate filtered and decimated data stream
from the live data as well as the raw unfiltered stream.  This data stream can
only be read through the `S` subscription command, and is only available if a
filter configuration file has been specified by the -c option to the archiver.

The decimation stream is generated through two stages of filtering: first a CIC
filter is run, this should generate the majority of the decimation, and secondly
a compensation FIR filter can be used to straighten out the passband response
and possibly perform a second stage of decimation.  In the default filter,
`filters/decimate.config`, the CIC decimates by a factor of 5 and a 41 point FIR
is used for a futher decimation by 2.

The filter configuration file has a very simple syntax.  Blank lines and lines
starting with `#` are ignored, backslash can be used to split a long value over
multiple lines, and entries are of the form ::

    name = value [value]*

The following values can be specified:

decimation_factor
    The decimation factor for the CIC, must be greater than 1.

comb_orders
    A CIC filter consists of N stages of integration followed by data reduction
    by the decimation factor followed by N comb filters of the form
    1-z\ :sup:`-n`, where n is the order of the comb.  The value for
    `comb_orders` is a list of numbers recording the number of iterations of
    each comb for n = 1, 2, up to the length of the list.  The total number of
    integration stages is give by the sum of this list.

filter_decimation [optional]
    Decimation factor for the compensation filter, default is 1.

compensation_filter
    Coefficients of the compensation filter.  The filter will be rescaled on
    loading for a DC response of 1.  This filter is applied to the output of the
    CIC filter.

output_sample_count [optional]
    This is the minimum number of samples sent in each update to subscribed
    clients.  The default value is 100, which with an FA data rate of 10kHz and
    a decimation of 10 corresponds to an update rate of 10Hz.  Too small a value
    will make the socket server inefficient, two large a value can make clients
    unresponsive.

output_block_count [optional]
    This controls how many `output_sample_count` sized blocks are buffered.  The
    default is 50.  Two small a value can force clients to disconnect
    unnecessarily.


Socket Server
=============
The socket server listens on the configured TCP port (port 8888 by default) and
provides access to all the data available from the archiver.  This is the only
normal operational interface to the archiver.

The socket server provided by the archiver accepts commands in a very rigid and
stylised form.  The format of these commands will only be of interest to writers
of tools directly interfacing to the archiver, as the existing suite of tools
already provides the necessary functionality.

All commands are sent as an ASCII string terminated by a newline (\\n)
character.  For `S` and `R` commands the response to a successful command always
starts with a null byte followed by binary data in little endian order, and an
error is always reported by returning a newline terminated error message
instead.  For `C` and `D` commands each subcommand always generates a newline
terminated textual response.

Every valid command is in one of four classes with the command class determined
by the first character of the command.

C
    Configuration interrogation commands, used to interrogate parameters such as
    the current sample frequency, available decimations, etc.

S
    Subscription commands, used to request delivery of live data.

R
    Archival retrieval commands, used to fetch data from the archive.

D
    Debug commands, only available if -X was specified on the command line.


Configuration Command (C)
-------------------------
The rest of the configuration command line is interpreted as a sequence of
single character sub-commands, and to most commands the archiver returns a one
line text response to each command in turn before closing the connection.  The
following sub-commands are recognised:

F
    Returns the current estimate of the sample frequency as a floating point
    number in Hertz.  As FA frames are received the archiver estimates the
    underlying sample frequency.

d
    Returns the first decimation factor for stored decimated data.  This will be
    a power of 2.

D
    Returns the incremental second decimation factor for stored decimated data,
    also a power of 2.  As this is the decimation factor after first decimation,
    the final second decimation factor is determined as the product of the two
    numbers returned by the command `CdD`.

T
    Returns the timestamp, in seconds in the Unix UTC epoch, of the earliest
    available sample in the archive.  As the archive is structured as a rolling
    buffer this data is unlikely to remain available for more than a few
    seconds.

V
    Returns a protocol identification string.  Will probably always be 1.

M
    Returns a mask identifying the list of FA ids being archived.

C
    Returns the decimation factor for live data if decimated live data is
    available, returns 0 if no decimation stream available.  Live decimated data
    is available if -c was specified on the command line.

S
    Returns a number of registers reporting the detailed status of the sniffer
    hardware.  The following numbers are returned on one line:

    :link status:   Hardware link status, 1 means ok, other numbers are errors
    :link partner:  FA id of connected source, or 1023 if no link partner
    :last interrupt: Last interrupt code, 1 means running normally
    :frame errors:
        Count of received frame errors, where an incomplete communication
        controller frame was received or the frame CRC was invalid.
    :soft errors:
        Count of received soft errors, data corruption due to bit errors on the
        link.
    :hard errors:
        Count of received hard errors, error detected at a lower level in the
        data stream.
    :run state:     0 means halted, 1 means fetching data
    :overrun:       1 means halted due to driver buffer overflow

I
    Returns a list of all currently connected clients, one client per line.
    This command is an exception to the rule of one line per command, and so
    should not normally be followed by other commands.

    Each line returned has three fields showing the time the client connected,
    the IP address and socket number of the connection, and the command sent to
    the server by the client.

Unrecognised commands or any command generating an error cause a one line error
message, per command letter, to be returned instead of the response described
above.


Subscription Command (S)
------------------------
A subscription command is used to request a subset of the live data stream being
captured by the archiver, or a decimated version of that stream.  The response
to an `S` command is either a single null byte followed by the requested
subscription stream, or an error message terminated by a newline.

The syntax of a subscription request is::

    subscription = "S" filter-mask options
    filter-mask = "R" raw-mask | mask
    raw-mask = hex-digit{64}
    mask = id [ "-" id ] [ "," mask ]
    options = [ "T" ] [ "Z" ] [ "D" ]

In other words, a subscription request consists of a list of BPM ids to be
observed followed by options.  The list of ids can be specified either as a
comma separated list of numbers or ranges (with each number in the range 0 to
255 inclusive), or as a "raw mask" consisting of an array of 256 bits in hex
with the highest bits sent first.  Any options must be specified in precisely
the order shown.

Subscription data is returned in binary as a sequence of 32-bit words
transmitted in little endian order.  Data is sent as X,Y positions in sequence
for each subscribed BPM id in ascending numerical order for each time frame, and
data is transmitted continously until either the client closes the socket
connection or the server sees the data source disconnect.

For example, the subscription request ::

    S5,2

will generate the following sequence of updates (after the initial null byte
reporting success)::

    X(2,0) Y(2,0) X(5,0) Y(5,0) X(2,1) Y(2,1) X(5,1) Y(5,1) ...

where `X(n,t)` is the X position for BPM `n` at time `t`.  A new update (two
pairs of X,Y values) is transmitted every 100 microseconds.

The options have the following meanings.

T
    Transmit timestamp at start of data stream.  This is the timestamp of the
    first sample in the data stream in microseconds in the Unix epoch as an 64
    bit number in little endian order, and is sent after the initial null byte
    and before the rest of the stream.

Z
    Transmit T0 at start of data stream.  This is the FA turn counter of the
    first sample, if available from the data stream, sent as a 32 bit number in
    little endian order.

D
    Requests decimated data stream.  If the decimated data stream was enabled
    with `-c` then this will be returned instead of the full data stream.


Read Archive Command (R)
------------------------
The `R` command is used to retrieve data from the archive.  The detailed syntax
of a read request is defined by this syntax::

    read-request = "R" source "M" filter-mask start end options
    source = "F" | "D" [ "D" ] [ "F" data-mask ]
    data-mask = integer
    start = time-or-seconds
    end = "N" samples | "E" time-or-seconds
    time-or-seconds = "T" date-time | "S" seconds [ "." nanoseconds ] [ "Z" ]
    date-time = yyyy "-" mm "-" dd "T" hh ":" mm ":" ss [ "." ns ]
    samples = integer
    options = [ "N" ] [ "A" ] [ "T" ] [ "G" ] [ "C" ] [ "Z" ]

A read request specifies a source, one of `F`, `D` or `DD`, followed by a filter
mask (as specified for the `S` command), followed by a time range consisting of
a start time and either a sample count or an end time, optionally followed by a
number of options.  If the read command was successful a null byte is sent
followed by the requested data in the same format as described for the `S`
command, otherwise a newline terminated error message is returned.

For example, the command ::

    RFM1T2011-06-01T0:0:0ET2011-06-01T0:0:1

requests one second's worth of FA data for BPM number 1 starting at midnight 1st
June 2011.

Three sources of data can be requested:

F
    `F` is used to request full resolution archive data

D, DD
    Both `D` and `DD` are used to request decimated data, used for generating an
    overview of the available data.  By default `D` data is decimated by 64 and
    `DD` by a further 256 (for a total decimation of 16384), giving one point
    every 1.6 seconds.

    For decimated data four values are available for each data point, namely the
    mean, minimum, maximum and standard deviation of the underlying full
    resolution data for the decimation interval (eg, 1.6 seconds), and the `M`
    option can be used to select which of these values are returned by or-ing
    together the following values:

    :1:  Mean
    :2:  Minimum
    :4:  Maximum
    :8:  Standard Deviation

The start time can be specified either as a time in seconds in the Unix epoch,
or as a date and time string in a variant of ISO 8601 format, and the same
format can be used to specify the end time.  The precise format of datetime
string is `yyyy-mm-ddThh:mm:ss` possibly followed by a fractional time in
decimal fractions of a second and an optional `Z`, for example ::

    2011-05-31T11:32:11.5Z

specifies a precise time in UTC.  If the final `Z` is omitted the local timezone
on the archiver server is used to interpret the time.

The end time can be specified in the same format, or as a number of samples to
capture.  If either start or end time is not available in the archive the
default behaviour is to reject the request, but this can be modified by setting
the `A` option.

Data is transmitted in precisely the same format as specified for the `S`
command, except that for decimated data the extra fields are also transmitted.
For example, the request `RDF6M5,2...` (omitting times) generates the sequence
::

    DX(2,0,1) DY(2,0,1) DX(2,0,2) DY(2,0,2) DX(2,1,1) DY(2,1,1) DX(2,1,2) ...

where `DX(n,t,f)` is field `f` (numbered with 0 = mean, 1 = min, 2 = max, 3 =
standard deviation) for X for BPM `n` at time `t`.

The following options can be specified:

N
    Send sample count as part of data stream.  The number of samples between the
    start and end times being transmitted is sent as a 32 bit little endian
    integer.

A
    Send all data there is, even if samples is too large or starts too early.
    If this option is not set then both start and end time must be entirely
    within the archive, otherwise the request will fail.

T
    Send timestamp at head of dataset.  The timestamp of the first transmitted
    sample is sent as a 64 bit little endian integer counting microseconds in
    the Unix epoch.

G
    Send gap list at end of data capture.  After transmitting the complete
    dataset, if this option is specified then a gap list is transmitted.  First
    the gap count is sent, as a 32 bit integer, followed by start information
    for each block (so for a gap count of N a total of N+1 `gap_data` blocks are
    sent).  The format of a `struct gap_data` block is described in
    `src/reader.h`.

C
    Ensure no gaps in selected dataset, fail if any.  If this option is set then
    only contiguous data is returned from the archive.

Z
    Check for gaps generated by id0.  If this option is not set then
    discontinuities in the FA timebase are not treated as gaps.  This option
    should be omitted on systems with older firmware where the timebase
    information is not available to the FA sniffer hardware.


Debug Command (D)
-----------------
Debug commands are handled in the same way as `Configuration Command (C)`_.  The
following debug sub-commands are recognised:

Q
    Halts the archiver, same as sending SIGINT to the archiver.

H
    Halts data capture by internally blocking processing of received packets.
    Used to test the reaction of archiver clients subscribed to the live data
    feed.

R
    Resumes halted data capture.

I
    Interrupts data capture using HALT ioctl, see fa_sniffer_\(4).


Canned Data Format
==================
If `-F` is specified on the command line then no attempt will be made to open
the FA sniffer device, instead data will be replayed from the specified Matlab
file.  This file should contain the values described below and must be small
enough to be mapped into memory, so is limited to around 2GB on a 32-bit system.

The following array must be present:

:data:
    This is the array of data to be replayed.  The array should have two or
    three dimensions with an index range of 2 in the first dimension, and is
    interpreted as

        data(xy, [id,] timebase)

    If the *ids* array is present its length must match the range of the *id*
    dimension.

The following two arrays are optional:

:ids:
    If present this must be a 1 by *size(data,2)* dimensional array, and is used
    to assign data to FA ids on data replay.

:id0:
    If present this determines the communication controller counter value ("id
    0") for the first point of replayed data.


Files
=====
`/dev/fa_sniffer0`
    The sniffer device driver must be installed for the archiver to operate.

Archive file
    An archive file previously prepared with fa-prepare_\(1) must be specified
    for the archiver to operate.

Filter Configuration
    The decimation filter configuration is documented above in the `Filter
    Configuration File`_ section.


See Also
========
fa-prepare_\(1), fa_sniffer_\(8), fa-capture_\(1), fa-viewer_\(1), falib_\(3)

.. _fa-prepare: fa-prepare.html
.. _fa_sniffer: fa_sniffer.html
.. _fa-capture: fa-capture.html
.. _fa-viewer: fa-viewer.html
.. _falib: falib.html
