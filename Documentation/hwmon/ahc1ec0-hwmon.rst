.. SPDX-License-Identifier: GPL-2.0

Kernel driver ahc1ec0-hwmon
=================================

Supported chips:

 * Advantech AHC1 Embedded Controller Chip for Advantech Devices

   Prefix: 'ahc1ec0-hwmon'

   Datasheet: Datasheet is not publicly available.

Author: Campion Kang <campion.kang@advantech.com.tw>


Description
-----------

This driver adds the temperature, voltage, current support for the Advantech
Devices with AHC1 Embedded Controller in Advantech IIoT Group.
The AHC1EC0 firmware is responsible for sensor data sampling and recording in
shared registers. The firmware is impleted by Advantech firmware team, it is
a common design suitable for different hardware pins of Advantech devices.
The host driver according to its hardware dynamic table and profile access its
registers and exposes them to users as hwmon interfaces.

The driver now is supports the AHC1EC0 for Advantech UNO, TPC series
devices.

Usage Notes
-----------

This driver will automatically probe and start via ahc1ec0 mfd driver
according to the attributes in ACPI table or device tree. More detail settings
you can refer the Documentation\devicetree\bindings\mfd\ahc1ec0.yaml.

The ahc1ec0 driver will not probe automatic. You will have to instantiate
devices explicitly. You can add it to /etc/modules.conf or insert module by
the following command:

	# insmod ahc1ec0


Sysfs attributes
----------------

The following attributes are supported:

- Advantech AHC1 Embedded Controller for Advantech UNO, TPC series:

======================= =======================================================
tempX_input             Temperature of the component (specified by tempX_label)
tempX_crit              Temperature critical setpoint of the component
temp1_label             "CPU Temp"
temp2_label             "System Temp"

inX_input               Measured voltage of the component (specified by
                        inX_label and may different with devices)
in0_label               "VBAT"
in1_label               "5VSB"
in2_label               "Vin"
in3_label               "VCore"
in4_label               "Vin1"
in5_label               "Vin2"
in6_label               "System Voltage"

curr1_input             Measured current of Vin
curr1_label             "Current"

======================= =======================================================

All the attributes are read-only.
