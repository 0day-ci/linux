.. SPDX-License-Identifier: GPL-2.0

============================
FPGA Image Load Class Driver
============================

The FPGA Image Load class driver provides a common API for user-space
tools to manage image uploads to FPGA devices. Device drivers that
instantiate the FPGA Image Load class driver will interact with the
target device to transfer and authenticate the image data.
