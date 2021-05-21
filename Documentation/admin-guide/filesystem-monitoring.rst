.. SPDX-License-Identifier: GPL-2.0

====================================
File system Monitoring with fanotify
====================================

fanotify supports the FAN_ERROR mark for file system-wide error
reporting.  It is meant to be used by file system health monitoring
daemons who listen on that interface and take actions (notify sysadmin,
start recovery) when a file system problem is detected by the kernel.

By design, A FAN_ERROR notification exposes sufficient information for a
monitoring tool to know a problem in the file system has happened.  It
doesn't necessarily provide a user space application with semantics to
verify an IO operation was successfully executed.  That is outside of
scope of this feature. Instead, it is only meant as a framework for
early file system problem detection and reporting recovery tools.

At the time of this writing, the only file system that emits this
FAN_ERROR notifications is ext4.

A user space example code is provided at ``samples/fanotify/fs-monitor.c``.

Usage
=====

Notification structure
======================

A FAN_ERROR Notification has the following format::

  [ Notification Metadata (Mandatory) ]
  [ Generic Error Record  (Mandatory) ]

With the exception of the notification metadata and the generic
information, all information records are optional.  Each record type is
identified by its unique ``struct fanotify_event_info_header.info_type``.

Generic error Location
----------------------

The Generic error record provides enough information for a file system
agnostic tool to learn about a problem in the file system, without
requiring any details about the problem.::

  struct fanotify_event_info_error {
	struct fanotify_event_info_header hdr;
	int error;
	__kernel_fsid_t fsid;
	unsigned long inode;
	__u32 error_count;
  };
