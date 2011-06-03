=========
fa-viewer
=========

.. Written in reStructuredText
.. default-role:: literal

---------------------------------------
Provides graphical view of live FA data
---------------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2011-05-27
:Manual section:    1
:Manual group:      Diamond Light Source

Synopsis
========
fa-viewer [-S *server*] [-f] [*location*]

Description
===========
Invokes a graphical viewer for inspecting live FA data.  The source of data,
duration of displayed data and display mode (raw and various spectral views) can
all be selected interactively.  The data source and the names used for the
individual FA ids are determined by a configuration file which can be specified
on the command line.

By default decimated data is displayed if available from the archive server, and
the full resolution stream is available as an interactive option.

The graphical display supports interactive zooming and panning using the mouse.

Options
=======
-S server
    Can be used to override the server address in the location file.

-f
    Normally the location file is looked up in the python/conf directory, but if
    this flag is set it is interpreted as a path name.

Configuration File
==================
The configuration file read by fa-viewer is used to determine the location of
the archive server, the mapping from FA id to device name, and how to gather
these devices for presentation.  By default the file `python/conf/SR.conf` is
loaded from the fa-archiver directory, or if *location* is given without the
`-f` flag then the file `python/conf/`\ *location*\ `.conf` is loaded.

The file must contain the following definitions, written in Python:

FA_SERVER
    This is the network name of the FA archive server.

BPM_LIST
    This must be a path to a file containing a list of FA ids and device names,
    with one id per line (blank lines or lines beginning with `#` are ignored),
    and each line of the form

        *FA-id*     *device-name*

GROUPS
    This is a list of strings which will be used to populate the first drop-down
    list in the viewer, used to gather bpms.

PATTERNS
    This is a list of 3-tuples representing a regular expression match used to
    assign each device name to the appropriate group.  The first string is the
    pattern used to match the device name, the second and third represent a
    replacement used to compute the device name.  A tuple

        (*guard*, *match*, *replace*)

    is equivalent to the `sed` expression

        /*guard*/s/*match*/*replace*/
