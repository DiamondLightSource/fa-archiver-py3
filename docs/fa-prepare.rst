==========
fa-prepare
==========

.. Written in reStructuredText
.. default-role:: literal

-------------------------------------------
Prepares archive file for use as FA archive
-------------------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2011-05-27
:Manual section:    1
:Manual group:      Diamond Light Source

Synopsis
========
fa-prepare [*options*] *capture-mask* *archive-file*

fa-prepare -H *archive-file*


Description
===========
Prepares or reinitialises an archive file for use with fa-archiver_\(1).  Can
also be used with `-H` to inspect the archive file.

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
-H
    If `-H` is used then the archive header is printed, and no other option can
    be given.

-s file-size
    Specify size of file.  The file will be resized to the given size with all
    disk blocks allocated.  Optional if the file already exists, should not be
    used when initialising a block device for use as an archive.

-I block-size
    Specify input block size for reads from FA sniffer device.  The default
    value is 524288 bytes.

-O block-size
    Specify block size for IO transfers to disk.  This should match the disk's
    IO block size.  The default value is 524288.

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


See Also
========
fa-archiver_\(1), fa-capture_\(1)

.. _fa-archiver:     fa-archiver.html
.. _fa-capture:      fa-capture.html
