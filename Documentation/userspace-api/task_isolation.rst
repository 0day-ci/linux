.. SPDX-License-Identifier: GPL-2.0

=============================
Task isolation prctl interface
=============================

Set thread isolation mode and parameters, which allows
informing the kernel that application is
executing latency sensitive code (where interruptions
are undesired).

Its composed of 4 prctl commands (passed as arg1 to
prctl):

PR_ISOL_SET:   set isolation parameters for the task

PR_ISOL_GET:   get isolation parameters for the task

PR_ISOL_ENTER: indicate that task should be considered
               isolated from this point on

PR_ISOL_EXIT: indicate that task should not be considered
              isolated from this point on

The isolation parameters and mode are not inherited by
children created by fork(2) and clone(2). The setting is
preserved across execve(2).

The meaning of isolated is specified as follows, when setting arg2 to
PR_ISOL_SET or PR_ISOL_GET, with the following arguments passed as arg3.

Isolation mode (PR_ISOL_MODE):
------------------------------

- PR_ISOL_MODE_NONE (arg4): no per-task isolation (default mode).
  PR_ISOL_EXIT sets mode to PR_ISOL_MODE_NONE.

- PR_ISOL_MODE_NORMAL (arg4): applications can perform system calls normally,
  and in case of interruption events, the notifications can be collected
  by BPF programs.
  In this mode, if system calls are performed, deferred actions initiated
  by the system call will be executed before return to userspace.

Other modes, which for example send signals upon interruptions events,
can be implemented.

Example
=======

The ``samples/task_isolation/`` directory contains a sample
application.

