.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver aquacomputer_farbwerk360
======================================

Supported devices:

* Aquacomputer Farbwerk 360 RGB controller

Author: Aleksa Savic

Description
-----------

This driver exposes hardware temperature sensors of the Aquacomputer Farbwerk 360
RGB controller, which communicates through a proprietary USB HID protocol.

Four temperature sensors are available. If a sensor is not connected, it will report
zeroes. Additionally, serial number and firmware version are exposed through debugfs.

Usage notes
-----------

Farbwerk 360 communicates via HID reports. The driver is loaded automatically by
the kernel and supports hotswapping.

Sysfs entries
-------------

=============== ==============================================
temp[1-4]_input Measured temperature (in millidegrees Celsius)
=============== ==============================================

Debugfs entries
---------------

================ ===============================================
serial_number    Serial number of the pump
firmware_version Version of installed firmware
================ ===============================================
