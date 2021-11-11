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
