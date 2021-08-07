// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Raspberry Pi Sense HAT core driver
 * http://raspberrypi.org
 *
 * Copyright (C) 2015 Raspberry Pi
 * Copyright (C) 2021 Charles Mirabile, Mwesigwa Guma, Joel Savitz
 *
 * Original Author: Serge Schneider
 * Revised for upstream Linux by: Charles Mirabile, Mwesigwa Guma, Joel Savitz
 *
 * This driver is based on wm8350 implementation and was refactored to use the
 * misc device subsystem rather than the deprecated framebuffer subsystem.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/rpisense.h>

#define RPISENSE_DISPLAY		0x00
#define RPISENSE_WAI			0xF0
#define RPISENSE_VER			0xF1
#define RPISENSE_KEYS			0xF2
#define RPISENSE_EE_WP			0xF3

#define RPISENSE_ID			's'

static struct platform_device *
rpisense_client_dev_register(struct rpisense *rpisense, const char *name);

static int rpisense_probe(struct i2c_client *i2c,
			       const struct i2c_device_id *id)
{
	int ret;

	struct rpisense *rpisense = devm_kzalloc(&i2c->dev, sizeof(*rpisense), GFP_KERNEL);

	if (rpisense == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rpisense);
	rpisense->dev = &i2c->dev;
	rpisense->i2c_client = i2c;


	ret = i2c_smbus_read_byte_data(rpisense->i2c_client, RPISENSE_WAI);
	if (ret < 0)
		return ret;

	if (ret != RPISENSE_ID)
		return -EINVAL;

	ret = i2c_smbus_read_byte_data(rpisense->i2c_client, RPISENSE_VER);
	if (ret < 0)
		return ret;

	dev_info(rpisense->dev,
		 "Raspberry Pi Sense HAT firmware version %i\n", ret);

	rpisense->joystick.pdev = rpisense_client_dev_register(rpisense,
							       "rpi-sense-js");

	if (IS_ERR(rpisense->joystick.pdev)) {
		dev_err(rpisense->dev, "failed to register rpisense-js");
		return PTR_ERR(rpisense->joystick.pdev);
	}

	rpisense->display.pdev = rpisense_client_dev_register(rpisense,
								  "rpi-sense-fb");

	if (IS_ERR(rpisense->display.pdev)) {
		dev_err(rpisense->dev, "failed to register rpisense-fb");
		return PTR_ERR(rpisense->display.pdev);
	}

	return 0;
}

static struct platform_device *
rpisense_client_dev_register(struct rpisense *rpisense, const char *name)
{
	long ret = -ENOMEM;
	struct platform_device *pdev = platform_device_alloc(name, -1);

	if (pdev == NULL)
		goto alloc_fail;

	pdev->dev.parent = rpisense->dev;
	platform_set_drvdata(pdev, rpisense);

	ret = platform_device_add(pdev);
	if (ret != 0)
		goto add_fail;

	ret = devm_add_action_or_reset(rpisense->dev,
		(void *)platform_device_unregister, pdev);
	if (ret != 0)
		goto alloc_fail;

	return pdev;

add_fail:
	platform_device_put(pdev);
alloc_fail:
	return ERR_PTR(ret);
}

int rpisense_get_joystick_state(struct rpisense *rpisense)
{
	int ret = i2c_smbus_read_byte_data(rpisense->i2c_client, RPISENSE_KEYS);

	return ret < 0 ? ret : ret & 0x1f;
}
EXPORT_SYMBOL_GPL(rpisense_get_joystick_state);

int rpisense_update_display(struct rpisense *rpisense)
{
	int i, j, ret;
	struct rpisense_display *display = &rpisense->display;
	struct {u8 reg, pixel_data[8][3][8]; } msg;

	msg.reg = RPISENSE_DISPLAY;
	for (i = 0; i < 8; ++i) {
		for (j = 0; j < 8; ++j) {
			msg.pixel_data[i][0][j] = display->gamma[display->vmem[i][j].r];
			msg.pixel_data[i][1][j] = display->gamma[display->vmem[i][j].g];
			msg.pixel_data[i][2][j] = display->gamma[display->vmem[i][j].b];
		}
	}

	ret = i2c_master_send(rpisense->i2c_client, (u8 *)&msg, sizeof(msg));
	if (ret < 0)
		dev_err(rpisense->dev, "Update to 8x8 LED matrix display failed");
	return ret;
}
EXPORT_SYMBOL_GPL(rpisense_update_display);

static const struct i2c_device_id rpisense_i2c_id[] = {
	{ "rpi-sense", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rpisense_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id rpisense_core_id[] = {
	{ .compatible = "rpi,rpi-sense" },
	{ },
};
MODULE_DEVICE_TABLE(of, rpisense_core_id);
#endif


static struct i2c_driver rpisense_driver = {
	.driver = {
		   .name = "rpi-sense",
	},
	.probe = rpisense_probe,
	.id_table = rpisense_i2c_id,
};

module_i2c_driver(rpisense_driver);

MODULE_DESCRIPTION("Raspberry Pi Sense HAT core driver");
MODULE_AUTHOR("Serge Schneider <serge@raspberrypi.org>");
MODULE_LICENSE("GPL");
