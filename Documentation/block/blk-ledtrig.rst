.. SPDX-License-Identifier: GPL-2.0

=================================
Block Device (blkdev) LED Trigger
=================================

Available when ``CONFIG_BLK_LED_TRIGGERS=y``.

See also:

* ``Documentation/ABI/testing/sysfs-class-led-trigger-blkdev``
* ``Documentation/ABI/testing/sysfs-block`` (``/sys/block/<disk>/led``)

.. note::
	The examples below use ``<LED>`` to refer to the name of a
	system-specific LED.  If no suitable LED is available on a test
	system (in a virtual machine, for example), it is possible to
	use a userspace LED (``Documentation/leds/uleds.rst``).

Associate the LED with the ``blkdev`` LED trigger::

	# echo blkdev > /sys/class/leds/<LED>/trigger

	# cat /sys/class/leds/<LED>/trigger
	... kbd-ctrlrlock [blkdev] disk-activity ...

Note that the ``blink_on`` and ``blink_off`` attributes have been added to the
LED, along with the ``block_devices`` subdirectory.

The LED is now available for association with block devices::

	# cat /sys/block/sda/led
	[none] <LED>

Associate the LED with the block device::

	# echo <LED> > /sys/block/sda/led

	# cat /sys/block/sda/led
	none [<LED>]

Reads and write activity on the device should cause the LED to blink.  The
duration of each blink (in milliseconds) can be adjusted by setting
``/sys/class/leds/<LED>/blink_on``, and the minimum delay between blinks can
be set via ``/sys/class/leds/<LED>/blink_off``.

Associate a second device with the LED::

	# echo <LED> > /sys/block/sdb/led

	# cat /sys/block/sdb/led
	none [<LED>]

Note that both block devices are linked from the LED's ``block_devices``
subdirectory::

	# ls /sys/class/leds/<LED>/block_devices
	sda sdb

Other notes:

* Many types of block devices work with this trigger, including:

  * SCSI (including SATA and USB) hard disk drives and SSDs
  * SCSI (including SATA and USB) optical drives
  * SD cards
  * loopback block devices (``/dev/loop*``)

* NVMe SSDs and most virtual block devices can be associated with LEDs, but
  they will produce little or no LED activity.

* Multiple LEDs can be associated with the ``blkdev`` trigger; different block
  devices can be associated with different LEDs.

* This trigger causes associated LED(s) to blink (per the LED's ``blink_on``
  and ``blink_off`` attributes) when a request is sent to an associated
  block device's low-level driver.  It does not track the duration (or
  result) of requests further.  Thus, it provides an approximate visual
  indication of device activity, not an exact measurement.
