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
Computes full spectrum analysis for the given PVs.

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
