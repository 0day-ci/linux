// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HID driver for Nintendo Wii U gamepad (DRC), connected via console-internal DRH
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
#include <linux/input.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include "hid-ids.h"
#include "hid-nintendo.h"

#define DEVICE_NAME	"Nintendo Wii U gamepad (DRC)"

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

/* Accelerometer, gyroscope and magnetometer constants */
#define ACCEL_MIN	-(1 << 15)
#define ACCEL_MAX	((1 << 15) - 1)
#define GYRO_MIN	-(1 << 23)
#define GYRO_MAX	((1 << 23) - 1)
#define MAGNET_MIN	-(1 << 15)
#define MAGNET_MAX	((1 << 15) - 1)

/*
 * The device is setup with multiple input devices:
 * - A joypad with the buttons and sticks.
 * - The touch area which works as a touchscreen.
 * - An accelerometer + gyroscope + magnetometer device.
 */

struct drc {
	enum nintendo_driver driver;
	struct hid_device *hdev;
	struct input_dev *joy_input_dev;
	struct input_dev *touch_input_dev;
	struct input_dev *accel_input_dev;
};

/*
 * The format of this report has been reversed by the libdrc project, the
 * documentation can be found here:
 * https://libdrc.org/docs/re/sc-input.html
 *
 * We receive this report from USB, but it is actually formed on the DRC, the
 * DRH only retransmits it over USB.
 */
int wiiu_hid_event(struct hid_device *hdev, struct hid_report *report,
		   u8 *data, int len)
{
	struct drc *drc = hid_get_drvdata(hdev);
	int i, x, y, z, pressure, base;
	u32 buttons;

	if (len != 128)
		return -EINVAL;

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

		val = clamp(val, (s16)STICK_MIN, (s16)STICK_MAX);

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
	/*
	 * Average touch points for improved accuracy.  Sadly these are always
	 * reported extremely close from each other…  Even when the user
	 * pressed two (or more) different points, all ten values will be
	 * approximately in the middle of the pressure points.
	 */
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

	/* accelerometer */
	x = (data[16] << 8) | data[15];
	y = (data[18] << 8) | data[17];
	z = (data[20] << 8) | data[19];
	input_report_abs(drc->accel_input_dev, ABS_X, (int16_t)x);
	input_report_abs(drc->accel_input_dev, ABS_Y, (int16_t)y);
	input_report_abs(drc->accel_input_dev, ABS_Z, (int16_t)z);

	/* gyroscope */
	x = (data[23] << 24) | (data[22] << 16) | (data[21] << 8);
	y = (data[26] << 24) | (data[25] << 16) | (data[24] << 8);
	z = (data[29] << 24) | (data[28] << 16) | (data[27] << 8);
	input_report_abs(drc->accel_input_dev, ABS_RX, x >> 8);
	input_report_abs(drc->accel_input_dev, ABS_RY, y >> 8);
	input_report_abs(drc->accel_input_dev, ABS_RZ, z >> 8);

	/* magnetometer */
	x = (data[31] << 8) | data[30];
	y = (data[33] << 8) | data[32];
	z = (data[35] << 8) | data[34];
	input_report_abs(drc->accel_input_dev, ABS_THROTTLE, (int16_t)x);
	input_report_abs(drc->accel_input_dev, ABS_RUDDER, (int16_t)y);
	input_report_abs(drc->accel_input_dev, ABS_WHEEL, (int16_t)z);
	input_sync(drc->accel_input_dev);

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

static bool drc_setup_joypad(struct drc *drc,
			     struct hid_device *hdev)
{
	struct input_dev *input_dev;

	input_dev = allocate_and_setup(hdev, DEVICE_NAME " buttons and sticks");
	if (!input_dev)
		return false;

	drc->joy_input_dev = input_dev;

	input_set_capability(input_dev, EV_KEY, BTN_DPAD_RIGHT);
	input_set_capability(input_dev, EV_KEY, BTN_DPAD_DOWN);
	input_set_capability(input_dev, EV_KEY, BTN_DPAD_LEFT);
	input_set_capability(input_dev, EV_KEY, BTN_DPAD_UP);
	input_set_capability(input_dev, EV_KEY, BTN_EAST);
	input_set_capability(input_dev, EV_KEY, BTN_SOUTH);
	input_set_capability(input_dev, EV_KEY, BTN_NORTH);
	input_set_capability(input_dev, EV_KEY, BTN_WEST);
	input_set_capability(input_dev, EV_KEY, BTN_TL);
	input_set_capability(input_dev, EV_KEY, BTN_TL2);
	input_set_capability(input_dev, EV_KEY, BTN_TR);
	input_set_capability(input_dev, EV_KEY, BTN_TR2);
	input_set_capability(input_dev, EV_KEY, BTN_THUMBL);
	input_set_capability(input_dev, EV_KEY, BTN_THUMBR);
	input_set_capability(input_dev, EV_KEY, BTN_SELECT);
	input_set_capability(input_dev, EV_KEY, BTN_START);
	input_set_capability(input_dev, EV_KEY, BTN_MODE);

	/*
	 * These two buttons are actually TV Control and Power.
	 *
	 * TV Control draws a line at the bottom of the DRC’s screen saying to
	 * go into System Settings (on the original proprietary OS), while
	 * Power will shutdown the DRC when held for four seconds, but those
	 * two are still normal buttons otherwise.
	 */
	input_set_capability(input_dev, EV_KEY, BTN_Z);
	input_set_capability(input_dev, EV_KEY, BTN_DEAD);

	input_set_abs_params(input_dev, ABS_X, STICK_MIN, STICK_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, STICK_MIN, STICK_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_RX, STICK_MIN, STICK_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_RY, STICK_MIN, STICK_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_VOLUME, VOLUME_MIN, VOLUME_MAX, 0, 0);

	return true;
}

static bool drc_setup_touch(struct drc *drc,
			    struct hid_device *hdev)
{
	struct input_dev *input_dev;

	input_dev = allocate_and_setup(hdev, DEVICE_NAME " touchscreen");
	if (!input_dev)
		return false;

	drc->touch_input_dev = input_dev;

	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	input_set_abs_params(input_dev, ABS_X, TOUCH_BORDER_X, MAX_TOUCH_RES - TOUCH_BORDER_X, 20, 0);
	input_abs_set_res(input_dev, ABS_X, RES_X / WIDTH);
	input_set_abs_params(input_dev, ABS_Y, TOUCH_BORDER_Y, MAX_TOUCH_RES - TOUCH_BORDER_Y, 20, 0);
	input_abs_set_res(input_dev, ABS_Y, RES_Y / HEIGHT);

	input_set_capability(input_dev, EV_KEY, BTN_TOUCH);
	input_set_capability(input_dev, EV_KEY, BTN_TOOL_FINGER);

	return true;
}

static bool drc_setup_accel(struct drc *drc,
			    struct hid_device *hdev)
{
	struct input_dev *input_dev;

	input_dev = allocate_and_setup(hdev, DEVICE_NAME " accelerometer, gyroscope and magnetometer");
	if (!input_dev)
		return false;

	drc->accel_input_dev = input_dev;

	set_bit(INPUT_PROP_ACCELEROMETER, input_dev->propbit);

	/* 1G accel is reported as about -7900 */
	input_set_abs_params(input_dev, ABS_X, ACCEL_MIN, ACCEL_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, ACCEL_MIN, ACCEL_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Z, ACCEL_MIN, ACCEL_MAX, 0, 0);

	/* gyroscope */
	input_set_abs_params(input_dev, ABS_RX, GYRO_MIN, GYRO_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_RY, GYRO_MIN, GYRO_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_RZ, GYRO_MIN, GYRO_MAX, 0, 0);

	/* magnetometer */
	/* TODO: Figure out which ABS_* would make more sense to expose, or
	 * maybe go for the iio subsystem?
	 */
	input_set_abs_params(input_dev, ABS_THROTTLE, MAGNET_MIN, MAGNET_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_RUDDER, MAGNET_MIN, MAGNET_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_WHEEL, MAGNET_MIN, MAGNET_MAX, 0, 0);

	return true;
}

int wiiu_hid_probe(struct hid_device *hdev,
		   const struct hid_device_id *id)
{
	struct drc *drc;
	int ret;

	drc = devm_kzalloc(&hdev->dev, sizeof(struct drc), GFP_KERNEL);
	if (!drc)
		return -ENOMEM;

	drc->driver = NINTENDO_WIIU;
	drc->hdev = hdev;

	hid_set_drvdata(hdev, drc);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	if (!drc_setup_joypad(drc, hdev) ||
	    !drc_setup_touch(drc, hdev) ||
	    !drc_setup_accel(drc, hdev)) {
		hid_err(hdev, "could not allocate interfaces\n");
		return -ENOMEM;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW | HID_CONNECT_DRIVER);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	ret = input_register_device(drc->joy_input_dev) ||
	      input_register_device(drc->touch_input_dev) ||
	      input_register_device(drc->accel_input_dev);
	if (ret) {
		hid_err(hdev, "failed to register interfaces\n");
		return ret;
	}

	return 0;
}
