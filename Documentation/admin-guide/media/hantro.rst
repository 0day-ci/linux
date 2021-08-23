.. SPDX-License-Identifier: GPL-2.0

=================
The hantro driver
=================

Trace
~~~~~

You can trace the hardware decoding performances by using event tracing::

    # echo hantro_hevc_perf >> /sys/kernel/debug/tracing/set_event

That will keep a log of the number of hardware cycles spend per decoded macroblock
