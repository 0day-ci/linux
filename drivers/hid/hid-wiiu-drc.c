// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HID driver for Nintendo Wii U gamepad, connected via console-internal DRH
 *
 * Copyright (C) 2021 Emmanuel Gil Peyrot <linkmauve@linkmauve.fr>
 * Copyright (C) 2019 Ash Logan <ash@heyquark.com>
 * Copyright (C) 2013 Mema Hacking
 *
 * Based on the excellent work at http://libdrc.org/docs/re/sc-input.html and
 * https://bitbucket.org/memahaxx/libdrc/src/master/src/input-receiver.cpp .
 * libdrc code is licensed under BSD 2-Clause.
 * Driver based on hid-udraw-ps3.c.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

#define DEVICE_NAME	"Nintendo Wii U gamepad"

/* Button and stick constants */
#define VOLUME_MIN	0
#define VOLUME_MAX	255
#define NUM_STICK_AXES	4
#define STICK_MIN	900
#define STICK_MAX	3200

#define BUTTON_SYNC	BIT(0)
#define BUTTON_HOME	BIT(1)
#define BUTTON_MINUS	BIT(2)
#define BUTTON_PLUS	BIT(3)
#define BUTTON_R	BIT(4)
#define BUTTON_L	BIT(5)
#define BUTTON_ZR	BIT(6)
#define BUTTON_ZL	BIT(7)
#define BUTTON_DOWN	BIT(8)
#define BUTTON_UP	BIT(9)
#define BUTTON_RIGHT	BIT(10)
#define BUTTON_LEFT	BIT(11)
#define BUTTON_Y	BIT(12)
#define BUTTON_X	BIT(13)
#define BUTTON_B	BIT(14)
#define BUTTON_A	BIT(15)

#define BUTTON_TV	BIT(21)
#define BUTTON_R3	BIT(22)
#define BUTTON_L3	BIT(23)

#define BUTTON_POWER	BIT(25)

/* Touch constants */
/* Resolution in pixels */
#define RES_X		854
#define RES_Y		480
/* Display/touch size in mm */
#define WIDTH		138
#define HEIGHT		79
#define NUM_TOUCH_POINTS 10
#define MAX_TOUCH_RES	(1 << 12)
#define TOUCH_BORDER_X	100
#define TOUCH_BORDER_Y	200

/*
 * The device is setup with multiple input devices:
 * - A joypad with the buttons and sticks.
 * - The touch area which works as a touchscreen.
 */

struct drc {
	struct input_dev *joy_input_dev;
	struct input_dev *touch_input_dev;
	struct hid_device *hdev;
};

static int drc_raw_event(struct hid_device *hdev, struct hid_report *report,
	 u8 *data, int len)
{
	struct drc *drc = hid_get_drvdata(hdev);
	int i, x, y, pressure, base;
	u32 buttons;

	if (len != 128)
		return 0;

	buttons = (data[4] << 24) | (data[80] << 16) | (data[2] << 8) | data[3];
	/* joypad */
	input_report_key(drc->joy_input_dev, BTN_DPAD_RIGHT, buttons & BUTTON_RIGHT);
	input_report_key(drc->joy_input_dev, BTN_DPAD_DOWN, buttons & BUTTON_DOWN);
	input_report_key(drc->joy_input_dev, BTN_DPAD_LEFT, buttons & BUTTON_LEFT);
	input_report_key(drc->joy_input_dev, BTN_DPAD_UP, buttons & BUTTON_UP);

	input_report_key(drc->joy_input_dev, BTN_EAST, buttons & BUTTON_A);
	input_report_key(drc->joy_input_dev, BTN_SOUTH, buttons & BUTTON_B);
	input_report_key(drc->joy_input_dev, BTN_NORTH, buttons & BUTTON_X);
	input_report_key(drc->joy_input_dev, BTN_WEST, buttons & BUTTON_Y);

	input_report_key(drc->joy_input_dev, BTN_TL, buttons & BUTTON_L);
	input_report_key(drc->joy_input_dev, BTN_TL2, buttons & BUTTON_ZL);
	input_report_key(drc->joy_input_dev, BTN_TR, buttons & BUTTON_R);
	input_report_key(drc->joy_input_dev, BTN_TR2, buttons & BUTTON_ZR);

	input_report_key(drc->joy_input_dev, BTN_Z, buttons & BUTTON_TV);
	input_report_key(drc->joy_input_dev, BTN_THUMBL, buttons & BUTTON_L3);
	input_report_key(drc->joy_input_dev, BTN_THUMBR, buttons & BUTTON_R3);

	input_report_key(drc->joy_input_dev, BTN_SELECT, buttons & BUTTON_MINUS);
	input_report_key(drc->joy_input_dev, BTN_START, buttons & BUTTON_PLUS);
	input_report_key(drc->joy_input_dev, BTN_MODE, buttons & BUTTON_HOME);

	input_report_key(drc->joy_input_dev, BTN_DEAD, buttons & BUTTON_POWER);

	for (i = 0; i < NUM_STICK_AXES; i++) {
		s16 val = (data[7 + 2*i] << 8) | data[6 + 2*i];
		/* clamp */
		if (val < STICK_MIN)
			val = STICK_MIN;
		if (val > STICK_MAX)
			val = STICK_MAX;

		switch (i) {
		case 0:
			input_report_abs(drc->joy_input_dev, ABS_X, val);
			break;
		case 1:
			input_report_abs(drc->joy_input_dev, ABS_Y, val);
			break;
		case 2:
			input_report_abs(drc->joy_input_dev, ABS_RX, val);
			break;
		case 3:
			input_report_abs(drc->joy_input_dev, ABS_RY, val);
			break;
		default:
			break;
		}
	}

	input_report_abs(drc->joy_input_dev, ABS_VOLUME, data[14]);

	input_sync(drc->joy_input_dev);

	/* touch */
	/* Average touch points for improved accuracy. */
	x = y = 0;
	for (i = 0; i < NUM_TOUCH_POINTS; i++) {
		base = 36 + 4 * i;

		x += ((data[base + 1] & 0xF) << 8) | data[base];
		y += ((data[base + 3] & 0xF) << 8) | data[base + 2];
	}
	x /= NUM_TOUCH_POINTS;
	y /= NUM_TOUCH_POINTS;

	/* Pressure reporting isn’t properly understood, so we don’t report it yet. */
	pressure = 0;
	pressure |= ((data[37] >> 4) & 7) << 0;
	pressure |= ((data[39] >> 4) & 7) << 3;
	pressure |= ((data[41] >> 4) & 7) << 6;
	pressure |= ((data[43] >> 4) & 7) << 9;

	if (pressure != 0) {
		input_report_key(drc->touch_input_dev, BTN_TOUCH, 1);
		input_report_key(drc->touch_input_dev, BTN_TOOL_FINGER, 1);

		input_report_abs(drc->touch_input_dev, ABS_X, x);
		input_report_abs(drc->touch_input_dev, ABS_Y, MAX_TOUCH_RES - y);
	} else {
		input_report_key(drc->touch_input_dev, BTN_TOUCH, 0);
		input_report_key(drc->touch_input_dev, BTN_TOOL_FINGER, 0);
	}
	input_sync(drc->touch_input_dev);

	/* let hidraw and hiddev handle the report */
	return 0;
}

static int drc_open(struct input_dev *dev)
{
	struct drc *drc = input_get_drvdata(dev);

	return hid_hw_open(drc->hdev);
}

static void drc_close(struct input_dev *dev)
{
	struct drc *drc = input_get_drvdata(dev);

	hid_hw_close(drc->hdev);
}

static struct input_dev *allocate_and_setup(struct hid_device *hdev,
		const char *name)
{
	struct input_dev *input_dev;

	input_dev = devm_input_allocate_device(&hdev->dev);
	if (!input_dev)
		return NULL;

	input_dev->name = name;
	input_dev->phys = hdev->phys;
	input_dev->dev.parent = &hdev->dev;
	input_dev->open = drc_open;
	input_dev->close = drc_close;
	input_dev->uniq = hdev->uniq;
	input_dev->id.bustype = hdev->bus;
	input_dev->id.vendor  = hdev->vendor;
	input_dev->id.product = hdev->product;
	input_dev->id.version = hdev->version;
	input_set_drvdata(input_dev, hid_get_drvdata(hdev));

	return input_dev;
}

static bool drc_setup_touch(struct drc *drc,
		struct hid_device *hdev)
{
	struct input_dev *input_dev;

	input_dev = allocate_and_setup(hdev, DEVICE_NAME " Touch");
	if (!input_dev)
		return false;

	input_dev->evbit[0] = BIT(EV_ABS) | BIT(EV_KEY);

	input_set_abs_params(input_dev, ABS_X, TOUCH_BORDER_X, MAX_TOUCH_RES - TOUCH_BORDER_X, 20, 0);
	input_abs_set_res(input_dev, ABS_X, RES_X / WIDTH);
	input_set_abs_params(input_dev, ABS_Y, TOUCH_BORDER_Y, MAX_TOUCH_RES - TOUCH_BORDER_Y, 20, 0);
	input_abs_set_res(input_dev, ABS_Y, RES_Y / HEIGHT);

	set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, input_dev->keybit);

	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	drc->touch_input_dev = input_dev;

	return true;
}

static bool drc_setup_joypad(struct drc *drc,
		struct hid_device *hdev)
{
	struct input_dev *input_dev;

	input_dev = allocate_and_setup(hdev, DEVICE_NAME " Joypad");
	if (!input_dev)
		return false;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

	set_bit(BTN_DPAD_RIGHT, input_dev->keybit);
	set_bit(BTN_DPAD_DOWN, input_dev->keybit);
	set_bit(BTN_DPAD_LEFT, input_dev->keybit);
	set_bit(BTN_DPAD_UP, input_dev->keybit);
	set_bit(BTN_EAST, input_dev->keybit);
	set_bit(BTN_SOUTH, input_dev->keybit);
	set_bit(BTN_NORTH, input_dev->keybit);
	set_bit(BTN_WEST, input_dev->keybit);
	set_bit(BTN_TL, input_dev->keybit);
	set_bit(BTN_TL2, input_dev->keybit);
	set_bit(BTN_TR, input_dev->keybit);
	set_bit(BTN_TR2, input_dev->keybit);
	set_bit(BTN_THUMBL, input_dev->keybit);
	set_bit(BTN_THUMBR, input_dev->keybit);
	set_bit(BTN_SELECT, input_dev->keybit);
	set_bit(BTN_START, input_dev->keybit);
	set_bit(BTN_MODE, input_dev->keybit);

	/* These two buttons are actually TV control and Power. */
	set_bit(BTN_Z, input_dev->keybit);
	set_bit(BTN_DEAD, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, STICK_MIN, STICK_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, STICK_MIN, STICK_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_RX, STICK_MIN, STICK_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_RY, STICK_MIN, STICK_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_VOLUME, VOLUME_MIN, VOLUME_MAX, 0, 0);

	drc->joy_input_dev = input_dev;

	return true;
}

static int drc_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct drc *drc;
	int ret;

	drc = devm_kzalloc(&hdev->dev, sizeof(struct drc), GFP_KERNEL);
	if (!drc)
		return -ENOMEM;

	drc->hdev = hdev;

	hid_set_drvdata(hdev, drc);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	if (!drc_setup_joypad(drc, hdev) ||
	    !drc_setup_touch(drc, hdev)) {
		hid_err(hdev, "could not allocate interfaces\n");
		return -ENOMEM;
	}

	ret = input_register_device(drc->joy_input_dev) ||
		input_register_device(drc->touch_input_dev);
	if (ret) {
		hid_err(hdev, "failed to register interfaces\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW | HID_CONNECT_DRIVER);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	return 0;
}

static const struct hid_device_id drc_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_NINTENDO, USB_DEVICE_ID_NINTENDO_WIIU_DRH) },
	{ }
};
MODULE_DEVICE_TABLE(hid, drc_devices);

static struct hid_driver drc_driver = {
	.name = "hid-wiiu-drc",
	.id_table = drc_devices,
	.raw_event = drc_raw_event,
	.probe = drc_probe,
};
module_hid_driver(drc_driver);

MODULE_AUTHOR("Ash Logan <ash@heyquark.com>");
MODULE_DESCRIPTION("Nintendo Wii U gamepad driver");
MODULE_LICENSE("GPL");
