// SPDX-License-Identifier: GPL-2.0
/*
 * nct6775-i2c - I2C driver for the hardware monitoring functionality of
 *	         Nuvoton NCT677x Super-I/O chips
 *
 * Copyright (C) 2021 Zev Weiss <zev@bewilderbeest.net>
 *
 * This driver interacts with the chip via it's "back door" i2c interface, as
 * is often exposed to a BMC.  Because the host may still be operating the
 * chip via the ("front door") LPC interface, this driver cannot assume that
 * it actually has full control of the chip, and in particular must avoid
 * making any changes that could confuse the host's LPC usage of it.  It thus
 * operates in a strictly read-only fashion, with the only exception being the
 * bank-select register (which seems, thankfully, to be replicated for the i2c
 * interface so it doesn't affect the LPC interface).
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include "nct6775.h"

static u16 nct6775_i2c_read_value(struct nct6775_data *data, u16 reg)
{
	int ret;
	u32 tmp, val;
	u8 bank = reg >> 8;
	struct regmap *regmap = data->driver_data;
	struct device *dev = regmap_get_device(regmap);

	if (bank != data->bank) {
		ret = regmap_write(regmap, NCT6775_REG_BANK, bank);
		if (ret) {
			dev_warn_ratelimited(dev, "bank %02x select regmap_write() failed: %d\n",
					     bank, ret);
			return 0;
		}
		data->bank = bank;
	}

	ret = regmap_read(regmap, reg & 0xff, &val);
	if (ret) {
		dev_warn_ratelimited(dev, "regmap_read(%03x) failed: %d\n", reg, ret);
		return 0;
	}

	if (nct6775_reg_is_word_sized(data, reg)) {
		ret = regmap_read(regmap, (reg & 0xff) + 1, &tmp);
		if (ret) {
			dev_warn_ratelimited(dev, "second-byte regmap_read(%03x) failed: %d\n",
					     reg, ret);
			return 0;
		}
		val = (val << 8) | tmp;
	}

	return val;
}

/*
 * The write operation is a dummy so as not to disturb anything being done
 * with the chip via LPC.
 */
static int nct6775_i2c_write_value(struct nct6775_data *data, u16 reg, u16 value)
{
	struct device *dev = regmap_get_device(data->driver_data);

	dev_dbg(dev, "skipping attempted write: %02x -> %03x\n", value, reg);

	/*
	 * This is a lie, but writing anything but the bank-select register is
	 * something this driver shouldn't be doing.  (Though at least at time
	 * of writing no callers actually check this return value anyway.)
	 */
	return 0;
}

static const struct regmap_config nct6775_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct of_device_id __maybe_unused nct6775_i2c_of_match[] = {
	{ .compatible = "nuvoton,nct6106", .data = (void *)nct6106, },
	{ .compatible = "nuvoton,nct6116", .data = (void *)nct6116, },
	{ .compatible = "nuvoton,nct6775", .data = (void *)nct6775, },
	{ .compatible = "nuvoton,nct6776", .data = (void *)nct6776, },
	{ .compatible = "nuvoton,nct6779", .data = (void *)nct6779, },
	{ .compatible = "nuvoton,nct6791", .data = (void *)nct6791, },
	{ .compatible = "nuvoton,nct6792", .data = (void *)nct6792, },
	{ .compatible = "nuvoton,nct6793", .data = (void *)nct6793, },
	{ .compatible = "nuvoton,nct6795", .data = (void *)nct6795, },
	{ .compatible = "nuvoton,nct6796", .data = (void *)nct6796, },
	{ .compatible = "nuvoton,nct6797", .data = (void *)nct6797, },
	{ .compatible = "nuvoton,nct6798", .data = (void *)nct6798, },
	{ },
};
MODULE_DEVICE_TABLE(of, nct6775_i2c_of_match);

static const struct i2c_device_id nct6775_i2c_id[] = {
	{ "nct6106", nct6106 },
	{ "nct6116", nct6116 },
	{ "nct6775", nct6775 },
	{ "nct6776", nct6776 },
	{ "nct6779", nct6779 },
	{ "nct6791", nct6791 },
	{ "nct6792", nct6792 },
	{ "nct6793", nct6793 },
	{ "nct6795", nct6795 },
	{ "nct6796", nct6796 },
	{ "nct6797", nct6797 },
	{ "nct6798", nct6798 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nct6775_i2c_id);

static int nct6775_i2c_probe_init(struct nct6775_data *data)
{
	/*
	 * The i2c interface doesn't provide access to the control registers
	 * needed to determine the presence of other fans, but fans 1 and 2
	 * are (in principle) always there.
	 *
	 * In practice this is perhaps a little silly, because the system
	 * using this driver is mostly likely a BMC, and hence probably has
	 * totally separate fan tachs & pwms of its own that are actually
	 * controlling/monitoring the fans -- these are thus unlikely to be
	 * doing anything actually useful.
	 */
	data->has_fan = 0x03;
	data->has_fan_min = 0x03;
	data->has_pwm = 0x03;
	return 0;
}

static int nct6775_i2c_probe(struct i2c_client *client)
{
	struct nct6775_data *data;
	const struct of_device_id *of_id;
	const struct i2c_device_id *i2c_id;
	struct regmap *regmap;
	struct device *dev = &client->dev;

	of_id = of_match_device(nct6775_i2c_of_match, dev);
	i2c_id = i2c_match_id(nct6775_i2c_id, client);

	if (of_id && (unsigned long)of_id->data != i2c_id->driver_data)
		dev_notice(dev, "Device mismatch: %s in device tree, %s detected\n",
			   of_id->name, i2c_id->name);

	regmap = devm_regmap_init_i2c(client, &nct6775_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to init regmap\n");
		return PTR_ERR(regmap);
	}

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->kind = i2c_id->driver_data;
	data->read_value = nct6775_i2c_read_value;
	data->write_value = nct6775_i2c_write_value;

	data->read_only = true;
	data->driver_data = regmap;
	data->driver_init = nct6775_i2c_probe_init;

	return nct6775_probe(dev, data);
}

static struct i2c_driver nct6775_i2c_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "nct6775-i2c",
		.of_match_table = of_match_ptr(nct6775_i2c_of_match),
	},
	.probe_new = nct6775_i2c_probe,
	.id_table = nct6775_i2c_id,
};

module_i2c_driver(nct6775_i2c_driver);

MODULE_AUTHOR("Zev Weiss <zev@bewilderbeest.net>");
MODULE_DESCRIPTION("I2C driver for NCT6775F and compatible chips");
MODULE_LICENSE("GPL");
