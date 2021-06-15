.. SPDX-License-Identifier: GPL-2.0

====================================
File system Monitoring with fanotify
====================================

fanotify supports the FAN_FS_ERROR mark for file system-wide error
reporting.  It is meant to be used by file system health monitoring
daemons who listen on that interface and take actions (notify sysadmin,
start recovery) when a file system problem is detected by the kernel.

By design, A FAN_FS_ERROR notification exposes sufficient information for a
monitoring tool to know a problem in the file system has happened.  It
doesn't necessarily provide a user space application with semantics to
verify an IO operation was successfully executed.  That is outside of
scope of this feature. Instead, it is only meant as a framework for
early file system problem detection and reporting recovery tools.

When a file system operation fails, it is common for dozens of kernel
errors to cascade after the initial failure, hiding the original failure
log, which is usually the most useful debug data to troubleshoot the
problem.  For this reason, FAN_FS_ERROR only reports the first error that
occurred since the last notification, and it simply counts addition
errors.  This ensures that the most important piece of error information
is never lost.

At the time of this writing, the only file system that emits FAN_FS_ERROR
notifications is Ext4.

A user space example code is provided at ``samples/fanotify/fs-monitor.c``.

Notification structure
======================

A FAN_FS_ERROR Notification has the following format::

  [ Notification Metadata (Mandatory) ]
  [ Generic Error Record  (Mandatory) ]

Generic error record
--------------------

The generic error record provides enough information for a file system
agnostic tool to learn about a problem in the file system, without
providing any additional details about the problem.  This record is
identified by ``struct fanotify_event_info_header.info_type`` being set
to FAN_EVENT_INFO_TYPE_ERROR.

  struct fanotify_event_info_error {
	struct fanotify_event_info_header hdr;
	int error;
	__u32 error_count;
	__kernel_fsid_t fsid;
	__u64 ino;
	__u32 ino_generation;
  };

The `error` field identifies the type of error.  `fsid` identifies the
file system that originated the error, since multiple file systems might
be reporting to the same fanotify group.  The `inode` field is optional,
as it depends on whether the error is linked to an specific inode.
`error_count` count tracks the number of errors that occurred and were
suppressed to preserve the original error, since the last notification.
