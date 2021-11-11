// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim MAX77714 Watchdog Driver
 *
 * Copyright (C) 2021 Luca Ceresoli
 * Author: Luca Ceresoli <luca@lucaceresoli.net>
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mfd/max77714.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>

struct max77714_wdt {
	struct device		*dev;
	struct regmap		*rmap;
	struct watchdog_device	wd_dev;
};

/* Timeout in seconds, indexed by TWD bits of CNFG_GLBL2 register */
static const unsigned int max77714_margin_value[] = { 2, 16, 64, 128 };

static int max77714_wdt_start(struct watchdog_device *wd_dev)
{
	struct max77714_wdt *wdt = watchdog_get_drvdata(wd_dev);

	return regmap_update_bits(wdt->rmap, MAX77714_CNFG_GLBL2,
				  MAX77714_WDTEN, MAX77714_WDTEN);
}

static int max77714_wdt_stop(struct watchdog_device *wd_dev)
{
	struct max77714_wdt *wdt = watchdog_get_drvdata(wd_dev);

	return regmap_update_bits(wdt->rmap, MAX77714_CNFG_GLBL2,
				  MAX77714_WDTEN, 0);
}

static int max77714_wdt_ping(struct watchdog_device *wd_dev)
{
	struct max77714_wdt *wdt = watchdog_get_drvdata(wd_dev);

	return regmap_update_bits(wdt->rmap, MAX77714_CNFG_GLBL3,
				  MAX77714_WDTC, 1);
}

static int max77714_wdt_set_timeout(struct watchdog_device *wd_dev,
				    unsigned int timeout)
{
	struct max77714_wdt *wdt = watchdog_get_drvdata(wd_dev);
	unsigned int new_timeout, new_twd;
	int err;

	for (new_twd = 0; new_twd < ARRAY_SIZE(max77714_margin_value) - 1; new_twd++)
		if (timeout <= max77714_margin_value[new_twd])
			break;

	/* new_wdt is not out of bounds here due to the "- 1" in the for loop */
	new_timeout = max77714_margin_value[new_twd];

	/*
	 * "If the value of TWD needs to be changed, clear the system
	 * watchdog timer first [...], then change the value of TWD."
	 * (MAX77714 datasheet)
	 */
	err = regmap_update_bits(wdt->rmap, MAX77714_CNFG_GLBL3,
				 MAX77714_WDTC, 1);
	if (err)
		return err;

	err = regmap_update_bits(wdt->rmap, MAX77714_CNFG_GLBL2,
				 MAX77714_TWD_MASK, new_twd);
	if (err)
		return err;

	wd_dev->timeout = new_timeout;

	dev_dbg(wdt->dev, "New timeout = %u s (WDT = 0x%x)", new_timeout, new_twd);

	return 0;
}

static const struct watchdog_info max77714_wdt_info = {
	.identity = "max77714-watchdog",
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops max77714_wdt_ops = {
	.start		= max77714_wdt_start,
	.stop		= max77714_wdt_stop,
	.ping		= max77714_wdt_ping,
	.set_timeout	= max77714_wdt_set_timeout,
};

static int max77714_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max77714_wdt *wdt;
	struct watchdog_device *wd_dev;
	unsigned int regval;
	int err;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->dev = dev;

	wd_dev = &wdt->wd_dev;
	wd_dev->info = &max77714_wdt_info;
	wd_dev->ops = &max77714_wdt_ops;
	wd_dev->min_timeout = 2;
	wd_dev->max_timeout = 128;

	platform_set_drvdata(pdev, wdt);
	watchdog_set_drvdata(wd_dev, wdt);

	wdt->rmap = dev_get_regmap(dev->parent, NULL);
	if (!wdt->rmap)
		return dev_err_probe(wdt->dev, -ENODEV, "Failed to get parent regmap\n");

	/* WD_RST_WK: if 1 wdog restarts; if 0 wdog shuts down */
	err = regmap_update_bits(wdt->rmap, MAX77714_CNFG2_ONOFF,
				 MAX77714_WD_RST_WK, MAX77714_WD_RST_WK);
	if (err)
		return dev_err_probe(wdt->dev, err, "Error updating CNFG2_ONOFF\n");

	err = regmap_read(wdt->rmap, MAX77714_CNFG_GLBL2, &regval);
	if (err)
		return dev_err_probe(wdt->dev, err, "Error reading CNFG_GLBL2\n");

	/* enable watchdog | enable auto-clear in sleep state */
	regval |= (MAX77714_WDTEN | MAX77714_WDTSLPC);

	err = regmap_write(wdt->rmap, MAX77714_CNFG_GLBL2, regval);
	if (err)
		return dev_err_probe(wdt->dev, err, "Error writing CNFG_GLBL2\n");

	wd_dev->timeout = max77714_margin_value[regval & MAX77714_TWD_MASK];

	dev_dbg(wdt->dev, "Timeout = %u s (WDT = 0x%x)",
		wd_dev->timeout, regval & MAX77714_TWD_MASK);

	set_bit(WDOG_HW_RUNNING, &wd_dev->status);

	watchdog_stop_on_unregister(wd_dev);

	err = devm_watchdog_register_device(dev, wd_dev);
	if (err)
		return dev_err_probe(dev, err, "Cannot register watchdog device\n");

	dev_info(dev, "registered as /dev/watchdog%d\n", wd_dev->id);

	return 0;
}

static const struct platform_device_id max77714_wdt_platform_id[] = {
	{ .name = "max77714-watchdog", },
	{ },
};
MODULE_DEVICE_TABLE(platform, max77714_wdt_platform_id);

static struct platform_driver max77714_wdt_driver = {
	.driver	= {
		.name	= "max77714-watchdog",
	},
	.probe	= max77714_wdt_probe,
	.id_table = max77714_wdt_platform_id,
};

module_platform_driver(max77714_wdt_driver);

MODULE_DESCRIPTION("MAX77714 watchdog timer driver");
MODULE_AUTHOR("Luca Ceresoli <luca@lucaceresoli.net>");
MODULE_LICENSE("GPL v2");
