// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ampere Altra Family SMPro MFD - I2C
 *
 * Copyright (c) 2021, Ampere Computing LLC
 *
 * Author: Quan Nguyen <quan@os.amperecomputing..com>
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/mfd/core.h>
#include <linux/regmap.h>

/* Identification Registers */
#define MANUFACTURER_ID_REG     0x02
#define AMPERE_MANUFACTURER_ID  0xCD3A

static const struct regmap_config simple_word_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
};

static const struct mfd_cell smpro_devs[] = {
	MFD_CELL_NAME("smpro-hwmon"),
};

static int smpro_mfd_probe(struct i2c_client *i2c)
{
	const struct regmap_config *config;
	struct regmap *regmap;
	unsigned int val;
	int ret;

	config = device_get_match_data(&i2c->dev);
	if (!config)
		config = &simple_word_regmap_config;

	regmap = devm_regmap_init_i2c(i2c, config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Check for valid ID */
	ret = regmap_read(regmap, MANUFACTURER_ID_REG, &val);
	if (ret)
		return ret;

	if (val != AMPERE_MANUFACTURER_ID)
		return -ENODEV;

	return devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO,
				    smpro_devs, ARRAY_SIZE(smpro_devs), NULL, 0, NULL);
}

static const struct of_device_id smpro_mfd_of_match[] = {
	{ .compatible = "ampere,smpro", .data = &simple_word_regmap_config },
	{}
};
MODULE_DEVICE_TABLE(of, smpro_mfd_of_match);

static struct i2c_driver smpro_mfd_driver = {
	.probe_new = smpro_mfd_probe,
	.driver = {
		.name = "smpro-mfd-i2c",
		.of_match_table = smpro_mfd_of_match,
	},
};
module_i2c_driver(smpro_mfd_driver);

MODULE_AUTHOR("Quan Nguyen <quan@os.amperecomputing.com>");
MODULE_DESCRIPTION("SMPRO MFD - I2C driver");
MODULE_LICENSE("GPL v2");
