// SPDX-License-Identifier: GPL-2.0
/*
 * I2C interface for Bosh BNO055 IMU.
 * This file implements I2C communication up to the register read/write
 * level.
 *
 * Copyright (C) 2021 Istituto Italiano di Tecnologia
 * Electronic Design Laboratory
 * Written by Andrea Merello <andrea.merello@iit.it>
 */

#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/module.h>

#include "bno055.h"

#define BNO055_I2C_XFER_BURST_BREAK_THRESHOLD 3 /* FIXME */

static int bno055_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct regmap *regmap =
		devm_regmap_init_i2c(client, &bno055_regmap_config);

	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Unable to init register map");
		return PTR_ERR(regmap);
	}

	return bno055_probe(&client->dev, regmap,
			    BNO055_I2C_XFER_BURST_BREAK_THRESHOLD);

	return 0;
}

static const struct i2c_device_id bno055_i2c_id[] = {
	{"bno055", 0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, bno055_i2c_id);

static struct i2c_driver bno055_driver = {
	.driver = {
		.name = "bno055-i2c",
	},
	.probe = bno055_i2c_probe,
	.id_table = bno055_i2c_id
};
module_i2c_driver(bno055_driver);

MODULE_AUTHOR("Andrea Merello");
MODULE_DESCRIPTION("Bosch BNO055 I2C interface");
MODULE_LICENSE("GPL v2");
