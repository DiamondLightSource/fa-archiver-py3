========
fa-audio
========

.. Written in reStructuredText
.. default-role:: literal

-----------------------------------------
Plays FA data as audio to the PC speakers
-----------------------------------------

:Author:            Michael Abbott, Diamond Light Source Ltd
:Date:              2011-05-27
:Manual section:    1
:Manual group:      Diamond Light Source

Synopsis
========
fa-audio [*options*] *server*

Description
===========
Plays FA data through the PC speakers using the aplay(1) tool.  The DNS name of
the archive server is given as the command line argument.  A simple command
interface is available with the following commands:

q
    Quit

b *fa-id*
    Select given *fa-id* for playback

v *volume*
    Set volume level to specified dB level.  The nominal volume level of 0dB
    corresponds to a level that is not excessive on BPMs with no signal input,
    position dominated by BPM nose.

    Note that the actual volume level is adjusted, block by block, to avoid
    clipping.

x
    Selects playback of X data in both speaker channels.

y
    Selects playback of Y data in both speaker channels.

a
    Selects playback of X data in left speaker channel, Y data in right speaker
    channel.

Options
=======
The following options can be specified on the command line:

-v volume
    Set the initial volume level, the default is 0dB.

-b fa-id
    Set the initial FA id for playback, the default is number 1.

See Also
========
aplay(1)
