.. SPDX-License-Identifier: GPL-2.0

===============================
Task isolation prctl interface
===============================

Certain types of applications benefit from running uninterrupted by
background OS activities. Realtime systems and high-bandwidth networking
applications with user-space drivers can fall into the category.


To create a OS noise free environment for the application, this
interface allows userspace to inform the kernel the start and
end of the latency sensitive application section (with configurable
system behaviour for that section).

The prctl options are:


        - PR_ISOL_FEAT: Retrieve supported features.
        - PR_ISOL_GET: Retrieve task isolation parameters.
        - PR_ISOL_SET: Set task isolation parameters.
        - PR_ISOL_CTRL_GET: Retrieve task isolation state.
        - PR_ISOL_CTRL_SET: Set task isolation state (enable/disable task isolation).

The isolation parameters and state are not inherited by
children created by fork(2) and clone(2). The setting is
preserved across execve(2).

The sequence of steps to enable task isolation are:

1. Retrieve supported task isolation features (PR_ISOL_FEAT).

2. Configure task isolation features (PR_ISOL_SET/PR_ISOL_GET).

3. Activate or deactivate task isolation features
   (PR_ISOL_CTRL_GET/PR_ISOL_CTRL_SET).

This interface is based on ideas and code from the
task isolation patchset from Alex Belits:
https://lwn.net/Articles/816298/

--------------------
Feature description
--------------------

        - ``ISOL_F_QUIESCE``

        This feature allows quiescing select kernel activities on
        return from system calls.

---------------------
Interface description
---------------------

**PR_ISOL_FEAT**:

        Returns the supported features and feature
        capabilities, as a bitmask. Features and its capabilities
        are defined at include/uapi/linux/task_isolation.h::

                prctl(PR_ISOL_FEAT, feat, arg3, arg4, arg5);

        The 'feat' argument specifies whether to return
        supported features (if zero), or feature capabilities
        (if not zero). Possible non-zero values for 'feat' are:

        - ``ISOL_F_QUIESCE``:

                If arg3 is zero, returns a bitmask containing
                which kernel activities are supported for quiescing.

                If arg3 is ISOL_F_QUIESCE_DEFMASK, returns
                default_quiesce_mask, a system-wide configurable.
                See description of default_quiesce_mask below.

**PR_ISOL_GET**:

        Retrieve task isolation feature configuration.
        The general format is::

                prctl(PR_ISOL_GET, feat, arg3, arg4, arg5);

        Possible values for feat are:

        - ``ISOL_F_QUIESCE``:

                Returns a bitmask containing which kernel
                activities are enabled for quiescing.


**PR_ISOL_SET**:

        Configures task isolation features. The general format is::

                prctl(PR_ISOL_SET, feat, arg3, arg4, arg5);

        The 'feat' argument specifies which feature to configure.
        Possible values for feat are:

        - ``ISOL_F_QUIESCE``:

                The 'arg3' argument is a bitmask specifying which
                kernel activities to quiesce. Possible bit sets are:

                - ``ISOL_F_QUIESCE_VMSTATS``

                  VM statistics are maintained in per-CPU counters to
                  improve performance. When a CPU modifies a VM statistic,
                  this modification is kept in the per-CPU counter.
                  Certain activities require a global count, which
                  involves requesting each CPU to flush its local counters
                  to the global VM counters.

                  This flush is implemented via a workqueue item, which
                  might schedule a workqueue on isolated CPUs.

                  To avoid this interruption, task isolation can be
                  configured to, upon return from system calls, synchronize
                  the per-CPU counters to global counters, thus avoiding
                  the interruption.

                  To ensure the application returns to userspace
                  with no modified per-CPU counters, its necessary to
                  use mlockall() in addition to this isolcpus flag.

**PR_ISOL_CTRL_GET**:

        Retrieve task isolation control.

                prctl(PR_ISOL_CTRL_GET, 0, 0, 0, 0);

        Returns which isolation features are active.

**PR_ISOL_CTRL_SET**:

        Activates/deactivates task isolation control.

                prctl(PR_ISOL_CTRL_SET, mask, 0, 0, 0);

        The 'mask' argument specifies which features
        to activate (bit set) or deactivate (bit clear).

        For ISOL_F_QUIESCE, quiescing of background activities
        happens on return to userspace from the
        prctl(PR_ISOL_CTRL_SET) call, and on return from
        subsequent system calls.

        Quiescing can be adjusted (while active) by
        prctl(PR_ISOL_SET, ISOL_F_QUIESCE, ...).

--------------------
Default quiesce mask
--------------------

Applications can either explicitly specify individual
background activities that should be quiesced, or
obtain a system configurable value, which is to be
configured by the system admin/mgmt system.

/sys/kernel/task_isolation/available_quiesce lists, as
one string per line, the activities which the kernel
supports quiescing.

To configure the default quiesce mask, write a comma separated
list of strings (from available_quiesce) to
/sys/kernel/task_isolation/default_quiesce.

echo > /sys/kernel/task_isolation/default_quiesce disables
all quiescing via ISOL_F_QUIESCE_DEFMASK.

Using ISOL_F_QUIESCE_DEFMASK allows for the application to
take advantage of future quiescing capabilities without
modification (provided default_quiesce is configured
accordingly).

See PR_ISOL_FEAT subsection of "Interface description" section
for more details. samples/task_isolation/task_isolation.c
contains an example.

Examples
========

The ``samples/task_isolation/`` directory contains sample
applications.


