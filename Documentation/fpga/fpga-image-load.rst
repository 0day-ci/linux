.. SPDX-License-Identifier: GPL-2.0

============================
FPGA Image Load Class Driver
============================

The FPGA Image Load class driver provides a common API for user-space
tools to manage image uploads to FPGA devices. Device drivers that
instantiate the FPGA Image Load class driver will interact with the
target device to transfer and authenticate the image data.

User API
========

open
----

An FPGA Image Load device is opened exclusively to control an image load.
Image loads are processed by a kernel worker thread. A user may choose
close the device while the upload continues.

ioctl
-----

FPGA_IMAGE_LOAD_WRITE:

Start an image load with the provided image buffer. This IOCTL returns
immediately after starting a kernel worker thread to process the image load
which could take as long a 40 minutes depending on the actual device being
updated. This is an exclusive operation; an attempt to start concurrent image
load for the same device will fail with EBUSY. An eventfd file descriptor
parameter is provided to this IOCTL, and it will be signalled at the
completion of the image load.
