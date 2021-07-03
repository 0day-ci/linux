// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Jabra USB HID Driver
 *
 *  Copyright (c) 2017 Niels Skou Olsen <nolsen@jabra.com>
 */

/*
 */

#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

#define HID_UP_VENDOR_DEFINED_MIN	0xff000000
#define HID_UP_VENDOR_DEFINED_MAX	0xffff0000

static int jabra_input_mapping(struct hid_device *hdev,
			       struct hid_input *hi,
			       struct hid_field *field,
			       struct hid_usage *usage,
			       unsigned long **bit, int *max)
{
	int is_vendor_defined =
		((usage->hid & HID_USAGE_PAGE) >= HID_UP_VENDOR_DEFINED_MIN &&
		 (usage->hid & HID_USAGE_PAGE) <= HID_UP_VENDOR_DEFINED_MAX);

	dbg_hid("hid=0x%08x appl=0x%08x coll_idx=0x%02x usage_idx=0x%02x: %s\n",
		usage->hid,
		field->application,
		usage->collection_index,
		usage->usage_index,
		is_vendor_defined ? "ignored" : "defaulted");

	/* Ignore vendor defined usages, default map standard usages */
	return is_vendor_defined ? -1 : 0;
}

static int jabra_event(struct hid_device *hdev, struct hid_field *field,
		       struct hid_usage *usage, __s32 value)
{
	struct hid_field *mute_led_field;
	int offset;

	/* Usages are filtered in jabra_usages. */

	if (!value) /* Handle key presses only. */
		return 0;

	offset = hidinput_find_field(hdev, EV_LED, LED_MUTE, &mute_led_field);
	if (offset == -1)
		return 0; /* No mute LED, proceed. */

	/*
	 * The device changes the LED state automatically on the mute key press,
	 * however, it still expects the host to change the LED state. If there
	 * is a mismatch (i.e. the host didn't change the LED state), the next
	 * mute key press won't generate an event. To avoid missing every second
	 * mute key press, change the LED state here.
	 */
	input_event(mute_led_field->hidinput->input, EV_LED, LED_MUTE,
		    !mute_led_field->value[offset]);

	return 0;
}

static const struct hid_device_id jabra_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_JABRA, HID_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, jabra_devices);

static const struct hid_usage_id jabra_usages[] = {
	{ 0x000b002f, EV_KEY, HID_ANY_ID }, /* Mic mute */
	{ HID_TERMINATOR, HID_TERMINATOR, HID_TERMINATOR }
};

static struct hid_driver jabra_driver = {
	.name = "jabra",
	.id_table = jabra_devices,
	.usage_table = jabra_usages,
	.input_mapping = jabra_input_mapping,
	.event = jabra_event,
};
module_hid_driver(jabra_driver);

MODULE_AUTHOR("Niels Skou Olsen <nolsen@jabra.com>");
MODULE_DESCRIPTION("Jabra USB HID Driver");
MODULE_LICENSE("GPL");
