// SPDX-License-Identifier: GPL-2.0-only
/*
 * Delta TN48M CPLD GPIO driver
 *
 * Copyright (C) 2021 Sartura Ltd.
 *
 * Author: Robert Marko <robert.marko@sartura.hr>
 */

#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

enum tn48m_gpio_type {
	TN48M_SFP_TX_DISABLE = 1,
	TN48M_SFP_PRESENT,
	TN48M_SFP_LOS,
};

static int tn48m_gpio_probe(struct platform_device *pdev)
{
	struct gpio_regmap_config config = {0};
	enum tn48m_gpio_type type;
	struct regmap *regmap;
	u32 base;
	int ret;

	if (!pdev->dev.parent)
		return -ENODEV;

	type = (uintptr_t)device_get_match_data(&pdev->dev);
	if (!type)
		return -ENODEV;

	ret = device_property_read_u32(&pdev->dev, "reg", &base);
	if (ret)
		return ret;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap)
		return -ENODEV;

	config.regmap = regmap;
	config.parent = &pdev->dev;
	config.ngpio = 4;

	switch (type) {
	case TN48M_SFP_TX_DISABLE:
		config.reg_set_base = base;
		break;
	case TN48M_SFP_PRESENT:
		config.reg_dat_base = base;
		break;
	case TN48M_SFP_LOS:
		config.reg_dat_base = base;
		break;
	default:
		dev_err(&pdev->dev, "unknown type %d\n", type);
		return -ENODEV;
	}

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(&pdev->dev, &config));
}

static const struct of_device_id tn48m_gpio_of_match[] = {
	{ .compatible = "delta,tn48m-gpio-sfp-tx-disable", .data = (void *)TN48M_SFP_TX_DISABLE },
	{ .compatible = "delta,tn48m-gpio-sfp-present", .data = (void *)TN48M_SFP_PRESENT },
	{ .compatible = "delta,tn48m-gpio-sfp-los", .data = (void *)TN48M_SFP_LOS },
	{ }
};
MODULE_DEVICE_TABLE(of, tn48m_gpio_of_match);

static struct platform_driver tn48m_gpio_driver = {
	.driver = {
		.name = "delta-tn48m-gpio",
		.of_match_table = tn48m_gpio_of_match,
	},
	.probe = tn48m_gpio_probe,
};
module_platform_driver(tn48m_gpio_driver);

MODULE_AUTHOR("Robert Marko <robert.marko@sartura.hr>");
MODULE_DESCRIPTION("Delta TN48M CPLD GPIO driver");
MODULE_LICENSE("GPL");
