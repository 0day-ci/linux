.. SPDX-License-Identifier: GPL-2.0

=================================
Block Device (blkdev) LED Trigger
=================================

Available when ``CONFIG_LEDS_TRIGGER_BLKDEV=y``.

See also:

* ``Documentation/ABI/testing/sysfs-class-led-trigger-blkdev``
* ``Documentation/ABI/testing/sysfs-block`` (``/sys/block/<disk>/leds``)

Overview
========

.. note::
	The examples below use ``<LED>`` to refer to the name of a
	system-specific LED.  If no suitable LED is available on a test
	system (in a virtual machine, for example), it is possible to
	use a userspace LED (``Documentation/leds/uleds.rst``).

Associate the LED with the ``blkdev`` LED trigger::

	# echo blkdev > /sys/class/leds/<LED>/trigger

	# cat /sys/class/leds/<LED>/trigger
	... kbd-ctrlrlock [blkdev] disk-activity ...

Note that several new device attributes are available.

* ``add_blkdev`` and ``delete_blkdev`` are used to associate block devices with
  this LED, and to remove associations.

* ``mode`` is used to control the type of device activity that will cause this
  LED to blink - read activity, write activity, or both.  (Note that any
  activity that changes the state of a device's non-volatile storage is
  considered to be a write.  This includes discard and cache flush requests.)

* ``blink_time`` is the duration (in milliseconds) of each blink of this LED.

* ``interval`` is the frequency (in milliseconds) with which devices are checked
  for activity.

* The ``block_devices`` directory will contain a symbolic link to every device
  that is associated with this LED.

Associate the LED with the block device::

	# echo sda > /sys/class/leds/<LED>/add_blkdev

	# ls /sys/class/leds/<LED>/block_devices
	sda

Reads and write activity on the device should cause the LED to blink.  The
duration of each blink (in milliseconds) can be adjusted by setting
``/sys/class/leds/<LED>/blink_on``, and the minimum delay between blinks can
be set via ``/sys/class/leds/<LED>/blink_off``.

Associate a second device with the LED::

	# echo sdb > /sys/class/leds/<LED>/add_blkdev

	# ls /sys/class/leds/<LED>/block_devices
	sda  sdb

When a block device is associated with one or more LEDs, the LEDs are linked
from the device's ``blkdev_leds`` directory::

	# ls /sys/block/sd{a,b}/blkdev_leds
	/sys/block/sda/blkdev_leds:
	<LED>

	/sys/block/sdb/blkdev_leds:
	<LED>

(The ``blkdev_leds`` directory only exists when the block device is associated
with at least one LED.)

The ``add_blkdev`` and ``delete_blkdev`` attributes both accept multiple,
whitespace separated, devices.  For example::

	# echo sda sdb > /sys/class/leds/<LED>/delete_blkdev

	# ls /sys/class/leds/<LED>/block_devices

``interval`` and ``blink_time``
===============================

* The ``interval`` attribute is a global setting.  Changing the value via
  ``/sys/class/leds/<LED>/interval`` will affect all LEDs associated with
  the ``blkdev`` LED trigger.

* All associated devices are checked for activity every ``interval``
  milliseconds, and a blink is triggered on appropriate LEDs.  The duration
  of an LED's blink is determined by its ``blink_time`` attribute (also in
  milliseconds).  Thus (assuming that activity of the relevant type has occurred
  on one of an LED's associated devices), the LED will be on for ``blink_time``
  milliseconds and off for ``interval - blink_time`` milliseconds.

* The LED subsystem ignores new blink requests for an LED that is currently in
  in the process of blinking, so setting a ``blink_time`` greater than or equal
  to ``interval`` will cause some blinks to be dropped.

* Because of processing times, scheduling latencies, etc., avoiding missed
  blinks actually requires a difference of at least a few milliseconds between
  the ``blink_time`` and ``interval``.  The required difference is likely to
  vary from system to system.  As a  reference, a Thecus N5550 NAS requires a
  difference of 7 milliseconds (``interval == 100``, ``blink_time == 93``).

* The default values (``interval == 100``, ``blink_time == 75``) cause the LED
  associated with a continuously active device to blink rapidly.  For a more
  "constantly on" effect, increase the ``blink_time`` (but not too much; see
  the previous bullet).

Other Notes
===========

* Many (possibly all) types of block devices work with this trigger, including:

  * SCSI (including SATA and USB) hard disk drives and SSDs
  * SCSI (including SATA and USB) optical drives
  * NVMe SSDs
  * SD cards
  * loopback block devices (``/dev/loop*``)
  * device mapper devices, such as LVM logical volumes
  * MD RAID devices
  * zRAM compressed RAM-disks

* The ``blkdev`` LED trigger supports many-to-many device/LED associations.
  A device can be associated with multiple LEDs, and an LED can be associated
  with multiple devices.
