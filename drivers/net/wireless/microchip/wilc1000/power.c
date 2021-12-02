// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/version.h>

#include "netdev.h"

/**
 * wilc_of_parse_power_pins() - parse power sequence pins
 *
 * @wilc:	wilc data structure
 *
 * Returns:	 0 on success, negative error number on failures.
 */
int wilc_of_parse_power_pins(struct wilc *wilc)
{
	struct device_node *of = wilc->dev->of_node;
	struct wilc_power *power = &wilc->power;
	int ret;

	power->gpios.reset = of_get_named_gpio_flags(of, "reset-gpios", 0,
						     NULL);
	power->gpios.chip_en = of_get_named_gpio_flags(of, "chip_en-gpios", 0,
						       NULL);
	if (!gpio_is_valid(power->gpios.reset))
		return 0;	/* assume SDIO power sequence driver is used */

	if (gpio_is_valid(power->gpios.chip_en)) {
		ret = devm_gpio_request(wilc->dev, power->gpios.chip_en,
					"CHIP_EN");
		if (ret)
			return ret;
	}
	return devm_gpio_request(wilc->dev, power->gpios.reset, "RESET");
}
EXPORT_SYMBOL_GPL(wilc_of_parse_power_pins);

/**
 * wilc_wlan_power() - handle power on/off commands
 *
 * @wilc:	wilc data structure
 * @on:		requested power status
 *
 * Returns:	none
 */
void wilc_wlan_power(struct wilc *wilc, bool on)
{
	if (!gpio_is_valid(wilc->power.gpios.reset)) {
		/* In case SDIO power sequence driver is used to power this
		 * device then the powering sequence is handled by the bus
		 * via pm_runtime_* functions. */
		return;
	}

	if (on) {
		if (gpio_is_valid(wilc->power.gpios.chip_en)) {
			gpio_direction_output(wilc->power.gpios.chip_en, 1);
			mdelay(5);
		}
		gpio_direction_output(wilc->power.gpios.reset, 1);
	} else {
		gpio_direction_output(wilc->power.gpios.reset, 0);
		if (gpio_is_valid(wilc->power.gpios.chip_en))
			gpio_direction_output(wilc->power.gpios.chip_en, 0);
	}
}
EXPORT_SYMBOL_GPL(wilc_wlan_power);
