// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/hid.h>

/* Every HID drvdata supported by this driver MUST start with this
 * enum, so that dispatch can work properly. */
enum nintendo_driver {
	NINTENDO_SWITCH,
};

int switch_hid_event(struct hid_device *hdev,
		     struct hid_report *report, u8 *raw_data, int size);
int switch_hid_probe(struct hid_device *hdev,
		     const struct hid_device_id *id);
void switch_hid_remove(struct hid_device *hdev);
