==========
fa_sniffer
==========

.. Written in reStructuredText
.. default-role:: literal

----------------------------------
Linux Device Driver for FA sniffer
----------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2011-05-27
:Manual section:    4
:Manual group:      Diamond Light Source


Synopsis
========

/dev/fa_sniffer0

#include "fa_sniffer.h"


Description
===========

The fa_sniffer device driver provides a character device interface to the
Diamond FA sniffer PCI express card.  This card captures fast acquisition frames
to memory at a data rate of one frame every 100 microseconds, where a data frame
contains X and Y position data for 256 FA nodes.

Frames are buffered in memory and made available through the character device
`/dev/fa_sniffer0`, and a careful client of this device can capture a continuous
stream of data without dropping a single frame.  A limited ioctl interface is
provided for restarting a halted stream and for interrogating the status of the
sniffer device, and similar status information is available through sysfs nodes.

Note that the driver will work with multiple sniffer cards, which will appear as
`/dev/fa_sniffer1`, etc, but the numbering of devices is likely to be
unpredictable!


Module Parameters
=================

Two module parameters can be specified when loading the fa_sniffer device.

`fa_buffer_count`
    A circular queue of fixed sized blocks is used to buffer the interface from
    the sniffer device to the client application.  The default value for this
    parameter is 5, and the driver cannot work if this is set to a value smaller
    than 3.  Too small a value increased the risk of the data stream being
    broken.

`fa_block_shift`
    This is used to program the size of each block in the circular buffer.  This
    number is the logarithm base 2 of the block size, and the default is 19,
    corresponding to a block size of 512KB.


Character Device
================

When the device `/dev/fa_sniffer0` is opened for reading the FA sniffer card is
immediately configured to start capturing frames into a circular buffer.  If the
client application reads the data in a timely enough way a continuous, complete
and uninterrupted data stream is available.

The data stream can be interrupt if the client lags too far behind and the
buffer overflows, or if there is an interrupt in the communication network and
the FA sniffer card times out.  In either case reading will return 0 bytes,
indicating end of file.  In this case the stream can be restarted with the
RESTART ioctl, or by closing and reopening the device, and the reason for
interrupt can be read using the GET_STATUS ioctl.

The data stream is delivered as a sequence of 2048 byte frames, one frame for
each communication controller update, at an update rate of 10072 Hz at Diamond.
Each frame consists of a sequence of X,Y pairs for FA ids in the range 0 to 255,
except that id 0 is used by the sniffer to write the communication controller
timestamp.

Only one file handle to the sniffer device can be open at any time.


Ioctl Interface
---------------

For fa_sniffer ioctls include the file `fa_sniffer.h` from the device driver
sources.

The following ioctls are available for this device:

`FASNIF_IOCTL_GET_VERSION`
    Returns an ioctl version number for library sanity checking.  Should return
    `FASNIF_IOCTL_VERSION`.

`FASNIF_IOCTL_RESTART`
    Restarts the data stream if interrupted.  If the sniffer is receiving
    packets from the communication network then calling `read()` after this
    should succeed.

`FASNIF_IOCTL_HALT`
    Debug use only, interrupts the data stream by halting the sniffer.  Can be
    followed by a RESTART call.

`FASNIF_IOCTL_GET_STATUS`
    Called with a `struct fa_status` pointer as the third argument, returns the
    following information about the sniffer hardware:

    :status:   Hardware link status, 1 means ok, other numbers are errors
    :partner:  FA id of connected source, or 1023 if no link partner
    :last_interrupt: Last interrupt code, 1 means running normally
    :frame_errors:
        Count of received frame errors, where an incomplete communication
        controller frame was received or the frame CRC was invalid.
    :soft_errors:
        Count of received soft errors, data corruption due to bit errors on the
        link.
    :hard_errors:
        Count of received hard errors, error detected at a lower level in the
        data stream.
    :running:   0 means halted, 1 means fetching data
    :overrun:   1 means halted due to driver buffer overflow


Sysfs Interface
===============

The fa_sniffer is a standard PCI express device, and so appears in `/sys` in the
usual places, namely::

    /sys/class/fa_sniffer
    /sys/module/fa_sniffer
    /sys/bus/pci/drivers/fa_sniffer

as well as some further bus specific locations.  The following sysfs nodes are
added by this driver under `/sys/class/fa_sniffer/fa_sniffer0/device`.  Note
that this is essentially the same information as provided by the GET_STATUS
ioctl.

    :firmware:          FPGA version number
    :last_interrupt:    Last interrupt reason code
    :link_status:       Link status (1 for established link)
    :link_partner:      Link partner or 1023 if no partner detected
    :frame_errors:      Count of total frame errors since hardware reset
    :soft_errors:       Count of total soft errors
    :hard_errors:       Count of total hard errors


Files
=====
/dev/fa_sniffer0
