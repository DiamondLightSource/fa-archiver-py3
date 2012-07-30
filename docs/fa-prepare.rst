==========
fa-prepare
==========

.. Written in reStructuredText
.. default-role:: literal

-------------------------------------------
Prepares archive file for use as FA archive
-------------------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2012-07-30
:Manual section:    1
:Manual group:      Diamond Light Source

Synopsis
========
fa-prepare [*options*] *capture-mask* *archive-file*

fa-prepare -H [*H-options*] *archive-file*


Description
===========
Prepares or reinitialises an archive file for use with fa-archiver_\(1).  Can
alternatively be used with `-H` to inspect the archive file without modifying
it.

The capture-mask is specified using the same syntax described for the pv-list
for fa-capture_\(1), and defines which FA ids will be available for historical
readout from the archive.  In brief, the appropriate syntax is::

    pv-list = id [ "-" id ] [ "," pv-list ]

For best performance the archiver should be run with *archive-file* an umounted
block device with no file system installed, in which case the entire device is
used, but any file can be specified, in which case `-s` should be used to
specify the file size if the file does not already exist.

Note that the archiver can only operate on a single archive file.  A large
archive can be used by joining disks together using the logical volume manager.

For the remaining options the defaults are perfectly serviceable.


Options
=======

-s file-size
    Specify size of file.  The file will be resized to the given size with all
    disk blocks allocated.  Optional if the file already exists, should not be
    used when initialising a block device for use as an archive.

-N fa-count
    Specify number of FA ids to capture from sniffer.  This affects the maximum
    valid value of FA ids when communicating with the archiver.  The default
    value is 256.

-I block-size
    Specify input block size for reads from FA sniffer device.  The default
    value is 524288 bytes.

-M major-sample-count
    Specify number of samples in a single IO transfer to disk.  This should be
    comfortably larger than the product of the two decimation factors and must
    be a power of 2.  The default value 65536 allows for 6.5 seconds per block.

-d decimation
    Specify first decimation factor.  The default value is 64, must be a power
    of 2.

-D decimation
    Specify second decimation factor.  The default value is 256, must be a power
    of 2.  The total second decimation will be the product of the values
    specified with `-d` and `-D`.

-f frequency
    Specify nominal sample frequency.  The default is 10072.4Hz, the nominal
    update rate at Diamond.  There is normally no need to specify this value, as
    this frequency is tracked by the archiver, but if the communication
    controller update rate is substantially different from the default this can
    make settling to the correct frequency occur more quickly.

-n
    Print file header that would be generated but don't actually write anything.

-q
    Use faster but quiet mechanism for allocating file buffer.  Only relevant if
    used with `-s` option.


H Options
=========

If the `-H` option is given then the following options are available instead of
the standard options listed above.

-f
    Normally if the archive header fails validation nothing is printed.  This
    option will attempt to proceed anyway, with unpredictable results.

-d
    If this option is set then after printing the header (unless `-n` is
    specified) the database index is printed.  The remaining options control the
    printing of this index.

    A typical index line looks as follows::

        145: 1341064894.087784 / 6553599 / 145278818 => 6.553601 / 65536

    while the currently written index block is flagged thus::

        868: 1341063321.223783 / 6553601 / 144678010 <<<<<<<<<<<<<<<

    This shows, in order: index block number, timestamp in seconds and
    microseconds, duration of block in microseconds, communication controller
    timestamp ("T0"), all followed by timestamp delta and T0 delta to previous
    block.  The deltas are not meaningful for the current index block.

-s first-block
    Offset into index of first index block to print, otherwise printing will
    start from the beginning of the index.

-e last-block
    Offset into index of last index block to print, otherwise printing will
    proceed to the end of the index.

-n
    Setting this option will suppress printing of the header.  Combining `-n`
    without `-d` will produce no output.

-u
    Normally the archive is locked while printing the index to ensure a
    consistent display, but this option bypasses this lock so a live index can
    be printed.

-t
    When printing the index displays the timestamp in UTC as well as its raw
    value.


See Also
========
fa-archiver_\(1), fa-capture_\(1)

.. _fa-archiver:     fa-archiver.html
.. _fa-capture:      fa-capture.html
