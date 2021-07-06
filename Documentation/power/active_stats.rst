.. SPDX-License-Identifier: GPL-2.0

=======================
Active Stats framework
=======================

1. Overview
-----------

The Active Stats framework provides useful statistical information on CPU
performance state time residency to other frameworks or governors. The
information contains a real time spent by the CPU when running at each
performance state (frequency), excluding idle time. This knowledge might be used
by other kernel subsystems to improve their mechanisms, e.g. used power
estimation. The statistics does not distinguish idle states and does not track
each idle state residency. It is done in this way because in most of the modern
platforms the kernel has no control over deeper CPU idle states.

The Active Stats consists of two components:
a) the Active Stats Tracking (AST) mechanism
b) Active Stats Monitor (ASM)

The diagram below presents the design::

       +---------------+  +---------------+
       |    CPUFreq    |  |    CPUIdle    |
       +---------------+  +---------------+
                 |                 |
                 |                 |
                 +-------+         |
                         |         |
                         |         |
       +--------------------------------------------------------+
       | Active Stats    |         |                            |
       | Framework       v         v                            |
       |                 +---------------------+                |
       |                 |    Active Stats     |                |
       |                 |     Tracking        |                |
       |                 +---------------------+                |
       |                           |                            |
       |                           |                            |
       |                 +---------------------+                |
       |                 |    Active Stats     |                |
       |                 |     Monitor         |                |
       |                 +---------------------+                |
       |                   ^         ^                          |
       |                   |         |                          |
       +--------------------------------------------------------+
                           |         |
                           |         |
                +----------+         |
                |                    |
        +------------------+   +--------------+
        | Thermal Governor |   |    Other     |
        +------------------+   +--------------+
                ^                    ^
                |                    |
        +------------------+   +--------------+
        | Thermal          |   |      ?       |
        +------------------+   +--------------+

The AST mechanism gathers and maintains statistics from CPUFreq and CPUIdle
frameworks. It is triggered in the CPU frequency switch paths. It accounts
the time spent at each frequency for each CPU. It supports per-CPU DVFS as
well as shared frequency domain. The idle time accounting is triggered every
time the CPU enters and exits idle state.

The ASM is a component which is used by other kernel subsystems (like thermal
governor) willing to know these statistical information. The client subsystem
uses its private ASM structure to keep track of the system statistics and
calculated local difference. Based on ASM data it is possible to calculate CPU
running time at each frequency, from last check. The time difference between
checks of ASM determines the period.


2. Core APIs
------------

2.1 Config options
^^^^^^^^^^^^^^^^^^

CONFIG_ACTIVE_STATS must be enabled to use the Active Stats framework.


2.2 Registration of the Active Stats Monitor
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Subsystems which want to use ASM to track CPU performance should register
into the Active Stats framework by calling the following API::

  struct active_stats_monitor *active_stats_cpu_setup_monitor(int cpu);

The allocated and configured ASM structure will be returned in success
otherwise a proper ERR_PTR() will be provided. See the
kernel/power/active_stats.c for further documenation of this function.


2.3 Accessing the Active Stats Monitor
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To get the latest statistics about CPU performance there is a need to call
the following API::

  int active_stats_cpu_update_monitor(struct active_stats_monitor *ast_mon);

In this call the Active Stats framework calculates and populates lastest
statistics for the given CPU. The statistics are then provided in the
'ast_mon->local' which is an array of 'struct active_stats_state'.
That structure contains array of time residency for each performance state
(in nanoseconds). Looping over the array 'ast_mon->local->residency' (which
has length provided in 'ast_mon->states_count') will provide detailed
information about time residency at each performance state (frequency) in
the given period present in 'ast_mon->local_period'. The local period value
might be bigger than the sum in the residency array, when the CPU was idle
or offline. More details about internals of the structures can be found in
include/linux/active_stats.h.
The 1st argument is the ASM allocated structure returned during setup (2.2).


2.4 Clean up of the Active Stats Monitor
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To clean up the ASM when it is not needed anymore there is a need to call
the following API::

  void active_stats_cpu_free_monitor(struct active_stats_monitor *ast_mon);

The associated structures will be freed.
