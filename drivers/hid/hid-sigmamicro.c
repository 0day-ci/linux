// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HID driver for SiGma Micro based keyboards
 *
 * Copyright (c) 2016 Kinglong Mee
 * Copyright (c) 2021 Desmond Lim
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

static __u8 *sm_report_fixup(struct hid_device *hdev, __u8 *rdesc,
			     unsigned int *rsize)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_SIGMA_MICRO_KEYBOARD2:
		if (*rsize == 167 && rdesc[98] == 0x81 && rdesc[99] == 0x00) {
			hid_info(hdev, "Fixing up SiGma Micro report descriptor\n");
			rdesc[99] = 0x02;
		}
		break;
	}
	return rdesc;
}

static const struct hid_device_id sm_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SIGMA_MICRO, USB_DEVICE_ID_SIGMA_MICRO_KEYBOARD2) },
	{ }
};
MODULE_DEVICE_TABLE(hid, sm_devices);

static struct hid_driver sm_driver = {
	.name = "sigmamicro",
	.id_table = sm_devices,
	.report_fixup = sm_report_fixup,
};
module_hid_driver(sm_driver);

MODULE_AUTHOR("Kinglong Mee <kinglongmee@gmail.com>");
MODULE_AUTHOR("Desmond Lim <peckishrine@gmail.com>");
MODULE_DESCRIPTION("SiGma Micro HID driver");
MODULE_LICENSE("GPL");
