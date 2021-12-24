.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver Ampere(R) Altra(R) SMpro miscellaneous
====================================================

Supported chips:

  * Ampere(R) Altra(R)

    Prefix: 'smpro'

    Reference: Altra SoC BMC Interface Specification

Author: Thu Nguyen <thu@os.amperecomputing.com>

Description
-----------

This driver support the monitoring and configuration of various miscellaneous
data provided by Ampere(R) Altra(R) SMpro processor.
At this time, these include:

  * Reading Boot Progress information
  * Configuring SoC Power Limit

Sysfs entries
-------------

1) Boot progress

SMpro misc driver creates the sysfs files ``boot_progress``.
The format of ``boot_progress`` file is as below::

<boot stage> <boot status> <boot progress>

Where:

* Boot stage::

    0: SMpro firmware booting.
    1: PMpro firmware booting.
    2: ATF BL1 firmware booting.
    3: DDR initialization.
    4: DDR training report status.
    5: ATF BL2 firmware booting.
    6: ATF BL31 firmware booting.
    7: ATF BL32 firmware booting.
    8: UEFI firmware booting.
    9: OS booting.

* Boot status::

    0: Not started.
    1: Started.
    2: Complete without error.
    3: Failure.

* boot progress: 32 bits boot progress code

The sysfs ``boot_progress`` only reports the boot state when the host is booting.
If the host is already booted, it returns latest state.

Example::

    #cat boot_progress
    0x01 0x02 0x808454A8

2) SoC Power Limit

SMpro misc driver creates the sysfs file ``soc_power_limit`` to get/set the SoC Power Limit.

Reading this sysfs return the current setting of SoC Power Limit (W) in decimal string.
Writing the desired value in decimal string to set the SoC Power Limit in Watt (W).
The range of SoC Power Limit is 90-500(W) and will be ignored if out of range.

Example::

    #cat soc_power_limit
    90
    #echo 95 > soc_power_limit
    #cat soc_power_limit
    95
