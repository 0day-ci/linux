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
#include <linux/regmap.h>
#include "sensehat.h"

static struct platform_device *
sensehat_client_dev_register(struct sensehat *sensehat, const char *name);

static struct regmap_config sensehat_config;

static int sensehat_probe(struct i2c_client *i2c,
			       const struct i2c_device_id *id)
{
	int ret;
	unsigned int reg;

	struct sensehat *sensehat = devm_kzalloc(&i2c->dev, sizeof(*sensehat), GFP_KERNEL);

	if (!sensehat)
		return -ENOMEM;

	i2c_set_clientdata(i2c, sensehat);
	sensehat->dev = &i2c->dev;
	sensehat->i2c_client = i2c;

	sensehat->regmap = devm_regmap_init_i2c(sensehat->i2c_client, &sensehat_config);

	if (IS_ERR(sensehat->regmap)) {
		dev_err(sensehat->dev, "Failed to initialize sensehat regmap");
		return PTR_ERR(sensehat->regmap);
	}


	ret = regmap_read(sensehat->regmap, SENSEHAT_WAI, &reg);
	if (ret < 0) {
		dev_err(sensehat->dev, "failed to read from device");
		return ret;
	}

	if (reg != SENSEHAT_ID) {
		dev_err(sensehat->dev, "expected device ID %i, got %i",
			SENSEHAT_ID, ret);
		return -EINVAL;
	}

	ret = regmap_read(sensehat->regmap, SENSEHAT_VER, &reg);
	if (ret < 0) {
		dev_err(sensehat->dev, "Unable to get sensehat firmware version");
		return ret;
	}

	dev_info(sensehat->dev,
		 "Raspberry Pi Sense HAT firmware version %i\n", reg);

	sensehat->joystick.pdev = sensehat_client_dev_register(sensehat,
							       "sensehat-joystick");

	if (IS_ERR(sensehat->joystick.pdev)) {
		dev_err(sensehat->dev, "failed to register sensehat-joystick");
		return PTR_ERR(sensehat->joystick.pdev);
	}

	sensehat->display.pdev = sensehat_client_dev_register(sensehat,
								  "sensehat-display");

	if (IS_ERR(sensehat->display.pdev)) {
		dev_err(sensehat->dev, "failed to register sensehat-display");
		return PTR_ERR(sensehat->display.pdev);
	}

	return 0;
}

static struct platform_device *
sensehat_client_dev_register(struct sensehat *sensehat, const char *name)
{
	long ret = -ENOMEM;
	struct platform_device *pdev = platform_device_alloc(name, -1);

	if (!pdev)
		goto alloc_fail;

	pdev->dev.parent = sensehat->dev;
	platform_set_drvdata(pdev, sensehat);

	ret = platform_device_add(pdev);
	if (ret)
		goto add_fail;

	ret = devm_add_action_or_reset(sensehat->dev,
		(void *)platform_device_unregister, pdev);
	if (ret)
		goto alloc_fail;

	return pdev;

add_fail:
	platform_device_put(pdev);
alloc_fail:
	return ERR_PTR(ret);
}

static bool sensehat_writeable_register(struct device *dev, unsigned int reg)
{
	return (reg >= SENSEHAT_DISPLAY &&
		reg < SENSEHAT_DISPLAY + sizeof(sensehat_fb_t))
		|| reg == SENSEHAT_EE_WP;
}
static bool sensehat_readable_register(struct device *dev, unsigned int reg)
{
	return (reg >= SENSEHAT_DISPLAY &&
		reg < SENSEHAT_DISPLAY + sizeof(sensehat_fb_t))
		|| reg == SENSEHAT_WAI || reg == SENSEHAT_VER
		|| reg == SENSEHAT_KEYS || reg == SENSEHAT_EE_WP;
}

static struct regmap_config sensehat_config = {
	.name = "sensehat",
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = sensehat_writeable_register,
	.readable_reg = sensehat_readable_register,
};

static const struct i2c_device_id sensehat_i2c_id[] = {
	{ "sensehat", 0 },
	{ "rpi-sense", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensehat_i2c_id);

static struct i2c_driver sensehat_driver = {
	.driver = {
		   .name = "sensehat",
	},
	.probe = sensehat_probe,
	.id_table = sensehat_i2c_id,
};

module_i2c_driver(sensehat_driver);

MODULE_DESCRIPTION("Raspberry Pi Sense HAT core driver");
MODULE_AUTHOR("Serge Schneider <serge@raspberrypi.org>");
MODULE_LICENSE("GPL");
