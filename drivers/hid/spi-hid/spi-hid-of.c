// SPDX-License-Identifier: GPL-2.0
/*
 * HID over SPI protocol, Open Firmware related code
 * spi-hid-of.c
 *
 * Copyright (c) 2021 Microsoft Corporation
 *
 * This code was forked out of the HID over SPI core code, which is partially
 * based on "HID over I2C protocol implementation:
 *
 * Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright (c) 2012 Ecole Nationale de l'Aviation Civile, France
 * Copyright (c) 2012 Red Hat, Inc
 *
 * which in turn is partially based on "USB HID support for Linux":
 *
 * Copyright (c) 1999 Andreas Gal
 * Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 * Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 * Copyright (c) 2007-2008 Oliver Neukum
 * Copyright (c) 2006-2010 Jiri Kosina
 */
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>

#include "spi-hid-core.h"

const struct of_device_id spi_hid_of_match[] = {
	{ .compatible = "hid-over-spi" },
	{},
};
MODULE_DEVICE_TABLE(of, spi_hid_of_match);

int spi_hid_of_populate_config(struct spi_hid_of_config *conf,
				struct device *dev)
{
	int ret;
	u32 val;

	ret = device_property_read_u32(dev, "input-report-header-address",
									&val);
	if (ret) {
		dev_err(dev, "Input report header address not provided\n");
		return -ENODEV;
	}
	conf->input_report_header_address = val;

	ret = device_property_read_u32(dev, "input-report-body-address", &val);
	if (ret) {
		dev_err(dev, "Input report body address not provided\n");
		return -ENODEV;
	}
	conf->input_report_body_address = val;

	ret = device_property_read_u32(dev, "output-report-address", &val);
	if (ret) {
		dev_err(dev, "Output report address not provided\n");
		return -ENODEV;
	}
	conf->output_report_address = val;

	ret = device_property_read_u32(dev, "read-opcode", &val);
	if (ret) {
		dev_err(dev, "Read opcode not provided\n");
		return -ENODEV;
	}
	conf->read_opcode = val;

	ret = device_property_read_u32(dev, "write-opcode", &val);
	if (ret) {
		dev_err(dev, "Write opcode not provided\n");
		return -ENODEV;
	}
	conf->write_opcode = val;

	ret = device_property_read_u32(dev, "post-power-on-delay-ms", &val);
	if (ret) {
		dev_err(dev, "Post-power-on delay not provided\n");
		return -ENODEV;
	}
	conf->post_power_on_delay_ms = val;

	ret = device_property_read_u32(dev, "minimal-reset-delay-ms", &val);
	if (ret) {
		dev_err(dev, "Minimal reset time not provided\n");
		return -ENODEV;
	}
	conf->minimal_reset_delay_ms = val;

	/* FIXME: not reading flags from DT, multi-SPI modes not supported */

	conf->supply = devm_regulator_get(dev, "vdd");
	if (IS_ERR(conf->supply)) {
		if (PTR_ERR(conf->supply) != -EPROBE_DEFER)
			dev_err(dev, "Failed to get regulator: %ld\n",
					PTR_ERR(conf->supply));
		return PTR_ERR(conf->supply);
	}

	conf->reset_gpio = devm_gpiod_get(dev, "reset-gpio", GPIOD_OUT_LOW);
	if (IS_ERR(conf->reset_gpio)) {
		dev_err(dev, "%s: error getting GPIO\n", __func__);
		return PTR_ERR(conf->reset_gpio);
	}

	return 0;
}

int spi_hid_of_power_down(struct spi_hid_of_config *conf)
{
	if (regulator_is_enabled(conf->supply) == 0)
		return 0;

	return regulator_disable(conf->supply);
}

int spi_hid_of_power_up(struct spi_hid_of_config *conf)
{
	int ret;

	if (regulator_is_enabled(conf->supply) > 0)
		return 0;

	ret = regulator_enable(conf->supply);

	usleep_range(1000 * conf->post_power_on_delay_ms,
			1000 * (conf->post_power_on_delay_ms + 1));

	return ret;
}

void spi_hid_of_assert_reset(struct spi_hid_of_config *conf)
{
	gpiod_set_value(conf->reset_gpio, 1);
}

void spi_hid_of_deassert_reset(struct spi_hid_of_config *conf)
{
	gpiod_set_value(conf->reset_gpio, 0);
}

void spi_hid_of_sleep_minimal_reset_delay(struct spi_hid_of_config *conf)
{
	usleep_range(1000 * conf->minimal_reset_delay_ms,
			1000 * (conf->minimal_reset_delay_ms + 1));
}