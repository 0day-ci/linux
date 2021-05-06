// SPDX-License-Identifier: GPL-2.0-only
/*
 * Watchdog Driver for Advantech AHC1EC0 Embedded Controller
 *
 * Copyright 2021, Advantech IIoT Group
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_data/ahc1ec0.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/watchdog.h>

struct ec_wdt_data {
	struct watchdog_device wdtdev;
	struct adv_ec_ddata *ddata;
	unsigned short timeout_in_ds; /* a decisecond */
};

#define EC_WDT_MIN_TIMEOUT	1   /* The watchdog devices minimum timeout value (in seconds). */
#define EC_WDT_MAX_TIMEOUT	600 /* The watchdog devices maximum timeout value (in seconds) */
#define EC_WDT_DEFAULT_TIMEOUT	45

static int set_delay(struct adv_ec_ddata *ddata, unsigned short delay_timeout_in_ms)
{
	if (ahc1ec_write_hw_ram(ddata, EC_RESET_DELAY_TIME_L,
				delay_timeout_in_ms & 0x00FF))
		return -EINVAL;

	if (ahc1ec_write_hw_ram(ddata, EC_RESET_DELAY_TIME_H,
				(delay_timeout_in_ms & 0xFF00) >> 8))
		return -EINVAL;

	return 0;
}

static int ec_wdt_start(struct watchdog_device *wdd)
{
	int ret;
	struct ec_wdt_data *ec_wdt_data = watchdog_get_drvdata(wdd);
	struct adv_ec_ddata *ddata;

	ddata = ec_wdt_data->ddata;

	/*
	 * Because an unit of ahc1ec0_wdt is 0.1 seconds, timeout 100 is 10 seconds
	 */
	ec_wdt_data->timeout_in_ds = wdd->timeout * 10;

	ret = set_delay(ddata, (ec_wdt_data->timeout_in_ds - 1));
	if (ret)
		goto exit;

	ahc1ec_write_hwram_command(ddata, EC_WDT_STOP);
	ret = ahc1ec_write_hwram_command(ddata, EC_WDT_START);
	if (ret)
		goto exit;

exit:
	return ret;
}

static int ec_wdt_stop(struct watchdog_device *wdd)
{
	struct ec_wdt_data *ec_wdt_data = watchdog_get_drvdata(wdd);
	struct adv_ec_ddata *ddata;

	ddata = ec_wdt_data->ddata;

	return ahc1ec_write_hwram_command(ddata, EC_WDT_STOP);
}

static int ec_wdt_ping(struct watchdog_device *wdd)
{
	int ret;
	struct ec_wdt_data *ec_wdt_data = watchdog_get_drvdata(wdd);
	struct adv_ec_ddata *ddata;

	ddata = ec_wdt_data->ddata;

	ret = ahc1ec_write_hwram_command(ddata, EC_WDT_RESET);
	if (ret)
		return -EINVAL;

	return 0;
}

static int ec_wdt_set_timeout(struct watchdog_device *wdd,
			      unsigned int timeout)
{
	wdd->timeout = timeout;

	if (watchdog_active(wdd))
		return ec_wdt_start(wdd);

	return 0;
}

static const struct watchdog_info ec_watchdog_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "AHC1EC0 Watchdog",
};

static const struct watchdog_ops ec_watchdog_ops = {
	.owner = THIS_MODULE,
	.start = ec_wdt_start,
	.stop = ec_wdt_stop,
	.ping = ec_wdt_ping,
	.set_timeout = ec_wdt_set_timeout,
};

static int adv_ec_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct adv_ec_ddata *ddata;
	struct ec_wdt_data *ec_wdt_data;
	struct watchdog_device *wdd;

	ddata = dev_get_drvdata(dev->parent);
	if (!ddata)
		return -EINVAL;

	ec_wdt_data = devm_kzalloc(dev, sizeof(*ec_wdt_data), GFP_KERNEL);
	if (!ec_wdt_data)
		return -ENOMEM;

	ec_wdt_data->ddata = ddata;
	wdd = &ec_wdt_data->wdtdev;

	watchdog_init_timeout(&ec_wdt_data->wdtdev, 0, dev);

	/* watchdog_set_nowayout(&ec_wdt_data->wdtdev, WATCHDOG_NOWAYOUT); */
	watchdog_set_drvdata(&ec_wdt_data->wdtdev, ec_wdt_data);
	platform_set_drvdata(pdev, ec_wdt_data);

	wdd->info = &ec_watchdog_info;
	wdd->ops = &ec_watchdog_ops;
	wdd->min_timeout = EC_WDT_MIN_TIMEOUT;
	wdd->max_timeout = EC_WDT_MAX_TIMEOUT;
	wdd->parent = dev;

	ec_wdt_data->wdtdev.timeout = EC_WDT_DEFAULT_TIMEOUT;

	watchdog_stop_on_reboot(wdd);
	watchdog_stop_on_unregister(wdd);

	ret = devm_watchdog_register_device(dev, wdd);
	if (ret == 0)
		dev_info(dev, "ahc1ec0 watchdog register success\n");

	return ret;
}

static struct platform_driver adv_wdt_drv = {
	.driver = {
		.name = "ahc1ec0-wdt",
	},
	.probe = adv_ec_wdt_probe,
};
module_platform_driver(adv_wdt_drv);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ahc1ec0-wdt");
MODULE_DESCRIPTION("Advantech Embedded Controller Watchdog Driver.");
MODULE_AUTHOR("Campion Kang <campion.kang@advantech.com.tw>");
MODULE_VERSION("1.0");
