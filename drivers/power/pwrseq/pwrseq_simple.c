// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2014 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 *  Simple MMC power sequence management
 */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/property.h>
#include <linux/pwrseq/driver.h>

struct pwrseq_simple {
	bool clk_enabled;
	u32 post_power_on_delay_ms;
	u32 power_off_delay_us;
	struct clk *ext_clk;
	struct gpio_descs *reset_gpios;
};

static int pwrseq_simple_set_gpios_value(struct pwrseq_simple *pwrseq_simple,
					 int value)
{
	struct gpio_descs *reset_gpios = pwrseq_simple->reset_gpios;
	unsigned long *values;
	int nvalues;
	int ret;

	if (IS_ERR(reset_gpios))
		return PTR_ERR(reset_gpios);

	nvalues = reset_gpios->ndescs;

	values = bitmap_alloc(nvalues, GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	if (value)
		bitmap_fill(values, nvalues);
	else
		bitmap_zero(values, nvalues);

	ret = gpiod_set_array_value_cansleep(nvalues, reset_gpios->desc,
				       reset_gpios->info, values);
	kfree(values);

	return ret;
}

static int pwrseq_simple_pre_power_on(struct pwrseq *pwrseq)
{
	struct pwrseq_simple *pwrseq_simple = pwrseq_get_data(pwrseq);

	if (!IS_ERR(pwrseq_simple->ext_clk) && !pwrseq_simple->clk_enabled) {
		clk_prepare_enable(pwrseq_simple->ext_clk);
		pwrseq_simple->clk_enabled = true;
	}

	return pwrseq_simple_set_gpios_value(pwrseq_simple, 1);
}

static int pwrseq_simple_power_on(struct pwrseq *pwrseq)
{
	struct pwrseq_simple *pwrseq_simple = pwrseq_get_data(pwrseq);
	int ret;

	ret = pwrseq_simple_set_gpios_value(pwrseq_simple, 0);
	if (ret)
		return ret;

	if (pwrseq_simple->post_power_on_delay_ms)
		msleep(pwrseq_simple->post_power_on_delay_ms);

	return 0;
}

static void pwrseq_simple_power_off(struct pwrseq *pwrseq)
{
	struct pwrseq_simple *pwrseq_simple = pwrseq_get_data(pwrseq);

	pwrseq_simple_set_gpios_value(pwrseq_simple, 1);

	if (pwrseq_simple->power_off_delay_us)
		usleep_range(pwrseq_simple->power_off_delay_us,
			2 * pwrseq_simple->power_off_delay_us);

	if (!IS_ERR(pwrseq_simple->ext_clk) && pwrseq_simple->clk_enabled) {
		clk_disable_unprepare(pwrseq_simple->ext_clk);
		pwrseq_simple->clk_enabled = false;
	}
}

static const struct pwrseq_ops pwrseq_simple_ops = {
	.pre_power_on = pwrseq_simple_pre_power_on,
	.power_on = pwrseq_simple_power_on,
	.power_off = pwrseq_simple_power_off,
};

static const struct of_device_id pwrseq_simple_of_match[] = {
	{ .compatible = "mmc-pwrseq-simple",}, /* MMC-specific compatible */
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, pwrseq_simple_of_match);

static int pwrseq_simple_probe(struct platform_device *pdev)
{
	struct pwrseq_simple *pwrseq_simple;
	struct pwrseq *pwrseq;
	struct pwrseq_provider *provider;
	struct device *dev = &pdev->dev;

	pwrseq_simple = devm_kzalloc(dev, sizeof(*pwrseq_simple), GFP_KERNEL);
	if (!pwrseq_simple)
		return -ENOMEM;

	pwrseq_simple->ext_clk = devm_clk_get(dev, "ext_clock");
	if (IS_ERR(pwrseq_simple->ext_clk) && PTR_ERR(pwrseq_simple->ext_clk) != -ENOENT)
		return PTR_ERR(pwrseq_simple->ext_clk);

	pwrseq_simple->reset_gpios = devm_gpiod_get_array(dev, "reset",
							GPIOD_OUT_HIGH);
	if (IS_ERR(pwrseq_simple->reset_gpios) &&
	    PTR_ERR(pwrseq_simple->reset_gpios) != -ENOENT &&
	    PTR_ERR(pwrseq_simple->reset_gpios) != -ENOSYS) {
		return PTR_ERR(pwrseq_simple->reset_gpios);
	}

	device_property_read_u32(dev, "post-power-on-delay-ms",
				 &pwrseq_simple->post_power_on_delay_ms);
	device_property_read_u32(dev, "power-off-delay-us",
				 &pwrseq_simple->power_off_delay_us);

	pwrseq = devm_pwrseq_create(dev, &pwrseq_simple_ops, pwrseq_simple);
	if (IS_ERR(pwrseq))
		return PTR_ERR(pwrseq);

	provider = devm_of_pwrseq_provider_register(dev, of_pwrseq_xlate_single, pwrseq);

	return PTR_ERR_OR_ZERO(provider);
}

static struct platform_driver pwrseq_simple_driver = {
	.probe = pwrseq_simple_probe,
	.driver = {
		.name = "pwrseq_simple",
		.of_match_table = pwrseq_simple_of_match,
	},
};

module_platform_driver(pwrseq_simple_driver);
MODULE_LICENSE("GPL v2");
