.. SPDX-License-Identifier: GPL-2.0

=====================
IOAM6 Sysfs variables
=====================


/proc/sys/net/conf/<iface>/ioam6_* variables:
=============================================

ioam6_enabled - BOOL
	Accept or ignore IPv6 IOAM options for ingress on this interface.

	* 0 - disabled (default)
	* not 0 - enabled

ioam6_id - INTEGER
	Define the IOAM id of this interface.

	Default is 0.
