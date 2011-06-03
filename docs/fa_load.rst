=======
fa_load
=======

.. Written in reStructuredText
.. default-role:: literal

-----------------------------------------------
Matlab script for fetching data from FA archive
-----------------------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2011-05-27
:Manual section:    7
:Manual group:      Diamond Light Source

Synopsis
========
*data* = fa_load(*times*, *id-list* [, *type* [, *server*]])

Description
===========
Captures data for the FA ids in *id-list* over the range of time specified by
the two element array *times* and returns a data structure containing the
fetched data.  Used internally by fa_zoomer_.

The array *times* contains two elements, start and end time, in Matlab format,
namely in days in the Matlab epoch.  The *server* can be specified, otherwise
the default server programmed into fa-capture_\(1) is used.

The *type* parameter is passed through to fa-capture_\(1) as the parameter to
its `-f` option, and has default `F`, so possible values for *type* are `F` for
full data, `d` for single decimated data, and `D` for double decimated data.

The format of the structure returned is described in detail in fa-capture_\(1),
but is augmented by fa_load with an estimated timebase `t` using Matlab
timestamps.

See Also
========
fa_zoomer_\(7), fa-capture_\(1)

.. _fa_zoomer: fa_zoomer.html
.. _fa-capture: fa-capture.html
