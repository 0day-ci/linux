.. SPDX-License-Identifier: GPL-2.0

============
LED Triggers
============

Available when ``CONFIG_BLK_LED_TRIGGERS=y``.

sysfs interface
===============

Create a new block device LED trigger::

	# echo foo > /sys/class/block/led_trigger_new

The name must be unique among all LED triggers (not just block device LED
triggers).

Create two more::

	# echo bar baz > /sys/class/block/led_trigger_new

List the triggers::

	# cat /sys/class/block/led_trigger_list
	baz: 0
	bar: 0
	foo: 0

(The number after each trigger is its reference count.)

Associate a trigger with a block device::

	# cat /sys/class/block/sda/led_trigger
	(none)

	# echo foo > /sys/class/block/sda/led_trigger
	# cat /sys/class/block/sda/led_trigger
	foo

Note that ``foo``'s reference count has increased, and it cannot be deleted::

	# cat /sys/class/block/led_trigger_list
	baz: 0
	bar: 0
	foo: 1

	# echo foo > /sys/class/block/led_trigger_del
	-bash: echo: write error: Device or resource busy

	# dmesg | tail -n 1
	[23176.475424] blockdev LED trigger foo still in use

Associate the ``foo`` trigger with an LED::

	# cat /sys/class/leds/input1::scrolllock/trigger
	none usb-gadget usb-host rc-feedback [kbd-scrolllock] kbd-numlock
	kbd-capslock kbd-kanalock kbd-shiftlock kbd-altgrlock kbd-ctrllock
	kbd-altlock kbd-shiftllock kbd-shiftrlock kbd-ctrlllock kbd-ctrlrlock
	disk-activity disk-read disk-write ide-disk mtd nand-disk panic
	audio-mute audio-micmute rfkill-any rfkill-none foo bar baz

	# echo foo > /sys/class/leds/input1::scrolllock/trigger

	# cat /sys/class/leds/input1::scrolllock/trigger
	none usb-gadget usb-host rc-feedback [kbd-scrolllock] kbd-numlock
	kbd-capslock kbd-kanalock kbd-shiftlock kbd-altgrlock kbd-ctrllock
	kbd-altlock kbd-shiftllock kbd-shiftrlock kbd-ctrlllock kbd-ctrlrlock
	disk-activity disk-read disk-write ide-disk mtd nand-disk panic
	audio-mute audio-micmute rfkill-any rfkill-none [foo] bar baz

Reads and writes to ``sda`` should now cause the scroll lock LED on your
keyboard to blink (assuming that it has one).

Multiple devices can be associated with a trigger::

	# echo foo > /sys/class/block/sdb/led_trigger

	# cat /sys/class/block/led_trigger_list
	baz: 0
	bar: 0
	foo: 2

Activity on either ``sda`` or ``sdb`` should now be shown by your scroll lock
LED.

Clear ``sda``'s LED trigger::

	# echo > /sys/class/block/sda/led_trigger

	# cat /sys/class/block/sda/led_trigger
	(none)

	# cat /sys/class/block/led_trigger_list
	baz: 0
	bar: 0
	foo: 1

And ``sdb``'s trigger::

	# echo > /sys/class/block/sdb/led_trigger

Delete the triggers::

	# echo foo bar baz > /sys/class/block/led_trigger_del

	# cat /sys/class/block/led_trigger_list

	# cat /sys/class/leds/input1::scrolllock/trigger
	none usb-gadget usb-host rc-feedback [kbd-scrolllock] kbd-numlock
	kbd-capslock kbd-kanalock kbd-shiftlock kbd-altgrlock kbd-ctrllock
	kbd-altlock kbd-shiftllock kbd-shiftrlock kbd-ctrlllock kbd-ctrlrlock
	disk-activity disk-read disk-write ide-disk mtd nand-disk panic
	audio-mute audio-micmute rfkill-any rfkill-none

Also see **Userspace LEDs** (``Documentation/leds/uleds.rst``).

Kernel API
==========

``#include <linux/blk-ledtrig.h>``

.. kernel-doc:: block/blk-ledtrig.c
   :export:
