// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015, Samsung Electronics Co., Ltd.
 *
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * Simple eMMC hardware reset provider
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/reboot.h>
#include <linux/pwrseq/driver.h>

struct pwrseq_emmc {
	struct notifier_block reset_nb;
	struct gpio_desc *reset_gpio;
};

static void pwrseq_ereset(struct pwrseq *pwrseq)
{
	struct pwrseq_emmc *pwrseq_emmc = pwrseq_get_data(pwrseq);

	gpiod_set_value_cansleep(pwrseq_emmc->reset_gpio, 1);
	udelay(1);
	gpiod_set_value_cansleep(pwrseq_emmc->reset_gpio, 0);
	udelay(200);
}

static int pwrseq_ereset_nb(struct notifier_block *this,
				    unsigned long mode, void *cmd)
{
	struct pwrseq_emmc *pwrseq_emmc = container_of(this,
					struct pwrseq_emmc, reset_nb);
	gpiod_set_value(pwrseq_emmc->reset_gpio, 1);
	udelay(1);
	gpiod_set_value(pwrseq_emmc->reset_gpio, 0);
	udelay(200);

	return NOTIFY_DONE;
}

static const struct pwrseq_ops pwrseq_eops = {
	.reset = pwrseq_ereset,
};

static int pwrseq_eprobe(struct platform_device *pdev)
{
	struct pwrseq_emmc *pwrseq_emmc;
	struct pwrseq *pwrseq;
	struct pwrseq_provider *provider;
	struct device *dev = &pdev->dev;

	pwrseq_emmc = devm_kzalloc(dev, sizeof(*pwrseq_emmc), GFP_KERNEL);
	if (!pwrseq_emmc)
		return -ENOMEM;

	pwrseq_emmc->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(pwrseq_emmc->reset_gpio))
		return PTR_ERR(pwrseq_emmc->reset_gpio);

	if (!gpiod_cansleep(pwrseq_emmc->reset_gpio)) {
		/*
		 * register reset handler to ensure emmc reset also from
		 * emergency_reboot(), priority 255 is the highest priority
		 * so it will be executed before any system reboot handler.
		 */
		pwrseq_emmc->reset_nb.notifier_call = pwrseq_ereset_nb;
		pwrseq_emmc->reset_nb.priority = 255;
		register_restart_handler(&pwrseq_emmc->reset_nb);
	} else {
		dev_notice(dev, "EMMC reset pin tied to a sleepy GPIO driver; reset on emergency-reboot disabled\n");
	}

	platform_set_drvdata(pdev, pwrseq_emmc);

	pwrseq = devm_pwrseq_create(dev, &pwrseq_eops, pwrseq_emmc);
	if (IS_ERR(pwrseq))
		return PTR_ERR(pwrseq);

	provider = devm_of_pwrseq_provider_register(dev, of_pwrseq_xlate_single, pwrseq);

	return PTR_ERR_OR_ZERO(provider);
}

static int pwrseq_eremove(struct platform_device *pdev)
{
	struct pwrseq_emmc *pwrseq_emmc = platform_get_drvdata(pdev);

	unregister_restart_handler(&pwrseq_emmc->reset_nb);

	return 0;
}

static const struct of_device_id pwrseq_eof_match[] = {
	{ .compatible = "mmc-pwrseq-emmc",},
	{/* sentinel */},
};

MODULE_DEVICE_TABLE(of, pwrseq_eof_match);

static struct platform_driver pwrseq_edriver = {
	.probe = pwrseq_eprobe,
	.remove = pwrseq_eremove,
	.driver = {
		.name = "pwrseq_emmc",
		.of_match_table = pwrseq_eof_match,
	},
};

module_platform_driver(pwrseq_edriver);
MODULE_LICENSE("GPL v2");
