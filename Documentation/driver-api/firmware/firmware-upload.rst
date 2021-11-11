.. SPDX-License-Identifier: GPL-2.0

=========================
Firmware Upload Framework
=========================

Some devices load firmware from on-board FLASH when the card initializes.
These cards do not require the request_firmware framework to load the
firmware when the card boots, but they to require a utility to allow
users to update the FLASH contents.

The Firmware Upload framework provides a common API for user-space tools
to manage firmware uploads to devices. Device drivers that instantiate the
Firmware Upload class driver will interact with the target device to
transfer and authenticate the firmware data. Uploads are performed in the
context of a kernel worker thread in order to facilitate progress
indicators during lengthy uploads.

User API
========

open
----

An fw_upload device is opened exclusively to control a firmware upload.
The device must remain open throughout the duration of the firmware upload.
An attempt to close the device while an upload is in progress will block
until the firmware upload is complete.

ioctl
-----

FW_UPLOAD_WRITE:

The FW_UPLOAD_WRITE IOCTL passes in the address of a data buffer and starts
the firmware upload. This IOCTL returns immediately after assigning the work
to a kernel worker thread. This is an exclusive operation; an attempt to
start concurrent firmware uploads for the same device will fail with EBUSY.
An eventfd file descriptor parameter is also passed to this IOCTL. It will
be signalled at the completion of the firmware upload.
