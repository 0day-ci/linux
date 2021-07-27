.. SPDX-License-Identifier: GPL-2.0

========================================
FPGA Security Manager Class Driver
========================================

The FPGA Security Manager class driver provides a common
API for user-space tools to manage updates for secure FPGA
devices. Device drivers that instantiate the Security
Manager class driver will interact with a HW secure update
engine in order to transfer new FPGA and BMC images to FLASH so
that they will be automatically loaded when the FPGA card reboots.

A significant difference between the FPGA Manager and the FPGA
Security Manager is that the FPGA Manager does a live update (Partial
Reconfiguration) to a device, whereas the FPGA Security Manager
updates the FLASH images for the Static Region and the BMC so that
they will be loaded the next time the FPGA card boots. Security is
enforced by hardware and firmware. The security manager interacts
with the firmware to initiate an update, pass in the necessary data,
and collect status on the update.

In addition to managing secure updates of the FPGA and BMC images,
the FPGA Security Manager update process may also be used to
program root entry hashes and cancellation keys for the FPGA static
region, the FPGA partial reconfiguration region, and the BMC.

Secure updates make use of the request_firmware framework, which
requires that image files are accessible under /lib/firmware. A request
for a secure update returns immediately, while the update itself
proceeds in the context of a kernel worker thread. Sysfs files provide
a means for monitoring the progress of a secure update and for
retrieving error information in the event of a failure.

Sysfs Attributes
================

The API includes a sysfs entry *name* to export the name of the parent
driver. It also includes an *update* sub-directory that can be used to
instantiate and monitor a secure update.

See `<../ABI/testing/sysfs-class-fpga-sec-mgr>`__ for a full
description of the sysfs attributes for the FPGA Security
Manager.
