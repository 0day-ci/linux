// SPDX-License-Identifier: GPL-2.0+
/*
 * NXP i.MX8QXP ADC driver
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/sysfs.h>

#define ADC_DRIVER_NAME		"imx8qxp-adc"

static int imx8qxp_adc_probe(struct platform_device *pdev)
{
	return 0;
}

static int imx8qxp_adc_remove(struct platform_device *pdev)
{
	return 0;
}

static int imx8qxp_adc_runtime_suspend(struct device *dev)
{
	return 0;
}

static int imx8qxp_adc_runtime_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops imx8qxp_adc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(imx8qxp_adc_runtime_suspend, imx8qxp_adc_runtime_resume, NULL)
};

static const struct of_device_id imx8qxp_adc_match[] = {
	{ .compatible = "nxp,imx8qxp-adc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx8qxp_adc_match);

static struct platform_driver imx8qxp_adc_driver = {
	.probe		= imx8qxp_adc_probe,
	.remove		= imx8qxp_adc_remove,
	.driver		= {
		.name	= ADC_DRIVER_NAME,
		.of_match_table = imx8qxp_adc_match,
		.pm	= &imx8qxp_adc_pm_ops,
	},
};

module_platform_driver(imx8qxp_adc_driver);

MODULE_DESCRIPTION("i.MX8QuadXPlus ADC driver");
MODULE_LICENSE("GPL v2");
