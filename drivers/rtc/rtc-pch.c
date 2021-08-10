// SPDX-License-Identifier: GPL-2.0+
/*
 * I2C read-only RTC driver for PCH with additional sysfs attribute for host
 * power control.
 *
 * Copyright (C) 2021 YADRO
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define PCH_REG_FORCE_OFF		0x00
#define PCH_REG_SC			0x09
#define PCH_REG_MN			0x0a
#define PCH_REG_HR			0x0b
#define PCH_REG_DW			0x0c
#define PCH_REG_DM			0x0d
#define PCH_REG_MO			0x0e
#define PCH_REG_YR			0x0f

#define NUM_TIME_REGS   (PCH_REG_YR - PCH_REG_SC + 1)

struct pch {
	struct rtc_device *rtc;
	struct regmap *regmap;
};

static int pch_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pch *pch = i2c_get_clientdata(client);
	unsigned char rtc_data[NUM_TIME_REGS] = {0};
	int rc;

	rc = regmap_bulk_read(pch->regmap, PCH_REG_SC, rtc_data, NUM_TIME_REGS);
	if (rc < 0) {
		dev_err(dev, "fail to read time reg(%d)\n", rc);
		return rc;
	}

	tm->tm_sec	= bcd2bin(rtc_data[0]);
	tm->tm_min	= bcd2bin(rtc_data[1]);
	tm->tm_hour	= bcd2bin(rtc_data[2]);
	tm->tm_wday	= rtc_data[3];
	tm->tm_mday	= bcd2bin(rtc_data[4]);
	tm->tm_mon	= bcd2bin(rtc_data[5]) - 1;
	tm->tm_year	= bcd2bin(rtc_data[6]) + 100;

	return 0;
}

static ssize_t force_off_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pch *pch = i2c_get_clientdata(client);
	unsigned long val;
	int rc;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val) {
		/* 0x02 host force off */
		rc = regmap_write(pch->regmap, PCH_REG_FORCE_OFF, 0x2);
		if (rc < 0) {
			dev_err(dev, "Fail to read time reg(%d)\n", rc);
			return rc;
		}
	}

	return 0;
}
static DEVICE_ATTR_WO(force_off);

static const struct rtc_class_ops pch_rtc_ops = {
	.read_time = pch_rtc_read_time,
};

static const struct regmap_config pch_rtc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.use_single_read = true,
};

static int pch_rtc_probe(struct i2c_client *client)
{
	struct pch *pch;
	int rc;

	pch = devm_kzalloc(&client->dev, sizeof(*pch), GFP_KERNEL);
	if (!pch)
		return -ENOMEM;

	pch->regmap = devm_regmap_init_i2c(client, &pch_rtc_regmap_config);
	if (IS_ERR(pch->regmap)) {
		dev_err(&client->dev, "regmap_init failed\n");
		return PTR_ERR(pch->regmap);
	}

	i2c_set_clientdata(client, pch);

	pch->rtc = devm_rtc_device_register(&client->dev, "pch-rtc",
					    &pch_rtc_ops, THIS_MODULE);
	if (IS_ERR(pch->rtc)) {
		dev_err(&client->dev, "rtc device register failed\n");
		return PTR_ERR(pch->rtc);
	}

	rc = sysfs_create_file(&client->dev.kobj, &dev_attr_force_off.attr);
	if (rc) {
		dev_err(&client->dev, "couldn't create sysfs attr : %i\n", rc);
		return rc;
	}

	return 0;
}

static int pch_rtc_remove(struct i2c_client *client)
{
	sysfs_remove_file(&client->dev.kobj, &dev_attr_force_off.attr);
	return 0;
}

static const struct of_device_id pch_rtc_of_match[] = {
	{ .compatible = "intel,pch-rtc", },
	{ }
};
MODULE_DEVICE_TABLE(of, pch_rtc_of_match);

static struct i2c_driver pch_rtc_driver = {
	.driver		= {
		.name	= "pch-rtc",
		.of_match_table = pch_rtc_of_match,
	},
	.probe_new	= pch_rtc_probe,
	.remove		= pch_rtc_remove,
};
module_i2c_driver(pch_rtc_driver);

MODULE_DESCRIPTION("RTC PCH driver");
MODULE_AUTHOR("Ivan Mikhaylov <i.mikhaylov@yadro.com>");
MODULE_LICENSE("GPL");
