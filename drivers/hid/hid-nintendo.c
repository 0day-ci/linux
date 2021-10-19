// SPDX-License-Identifier: GPL-2.0+
/*
 * HID driver for Nintendo controllers
 *
 * Copyright (c) 2021 Emmanuel Gil Peyrot <linkmauve@linkmauve.fr>
 */

#include "hid-ids.h"
#include "hid-nintendo.h"
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>

static int nintendo_hid_event(struct hid_device *hdev,
			      struct hid_report *report, u8 *raw_data, int size)
{
	enum nintendo_driver *driver = hid_get_drvdata(hdev);

#ifdef CONFIG_HID_NINTENDO_SWITCH
	if (*driver == NINTENDO_SWITCH)
		return switch_hid_event(hdev, report, raw_data, size);
#endif
	unreachable();
}

static int nintendo_hid_probe(struct hid_device *hdev,
			      const struct hid_device_id *id)
{
	int ret;

#ifdef CONFIG_HID_NINTENDO_SWITCH
	ret = switch_hid_probe(hdev, id);
#endif

	return ret;
}

static void nintendo_hid_remove(struct hid_device *hdev)
{
#ifdef CONFIG_HID_NINTENDO_SWITCH
	enum nintendo_driver *driver = hid_get_drvdata(hdev);

	if (*driver == NINTENDO_SWITCH)
		switch_hid_remove(hdev);
#endif
}

static const struct hid_device_id nintendo_hid_devices[] = {
#ifdef CONFIG_HID_NINTENDO_SWITCH
	{ HID_USB_DEVICE(USB_VENDOR_ID_NINTENDO,
			 USB_DEVICE_ID_NINTENDO_PROCON) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
			 USB_DEVICE_ID_NINTENDO_PROCON) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NINTENDO,
			 USB_DEVICE_ID_NINTENDO_CHRGGRIP) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
			 USB_DEVICE_ID_NINTENDO_JOYCONL) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
			 USB_DEVICE_ID_NINTENDO_JOYCONR) },
#endif

	{ }
};
MODULE_DEVICE_TABLE(hid, nintendo_hid_devices);

static struct hid_driver nintendo_hid_driver = {
	.name		= "nintendo",
	.id_table	= nintendo_hid_devices,
	.probe		= nintendo_hid_probe,
	.remove		= nintendo_hid_remove,
	.raw_event	= nintendo_hid_event,
};
module_hid_driver(nintendo_hid_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emmanuel Gil Peyrot <linkmauve@linkmauve.fr>");
MODULE_DESCRIPTION("Driver for Nintendo controllers");

