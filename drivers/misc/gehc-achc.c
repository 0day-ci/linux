// SPDX-License-Identifier: GPL-2.0-only
/*
 * datasheet: https://www.nxp.com/docs/en/data-sheet/K20P144M120SF3.pdf
 *
 * Copyright (C) 2018-2021 Collabora
 * Copyright (C) 2018-2021 GE Healthcare
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include "nxp-ezport.h"

#define ACHC_MAX_FREQ_HZ 300000

struct achc_data {
	struct spi_device *main;
	struct spi_device *ezport;
	struct gpio_desc *reset;

	struct mutex device_lock; /* avoid concurrent device access */
};

static ssize_t update_firmware_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct achc_data *achc = dev_get_drvdata(dev);
	int ret;

	if (count != 1 || buf[0] != '1')
		return -EINVAL;

	mutex_lock(&achc->device_lock);
	ret = ezport_flash(achc->ezport, achc->reset, "achc.bin");
	mutex_unlock(&achc->device_lock);

	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(update_firmware);

static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct achc_data *achc = dev_get_drvdata(dev);

	if (count != 1 || buf[0] != '1')
		return -EINVAL;

	mutex_lock(&achc->device_lock);
	ezport_reset(achc->reset);
	mutex_unlock(&achc->device_lock);

	return count;
}
static DEVICE_ATTR_WO(reset);

static struct attribute *gehc_achc_attrs[] = {
	&dev_attr_update_firmware.attr,
	&dev_attr_reset.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gehc_achc);

static void unregister_ezport(void *data)
{
	struct spi_device *ezport = data;

	spi_unregister_device(ezport);
}

static int gehc_achc_probe(struct spi_device *spi)
{
	struct achc_data *achc;
	int ezport_reg, ret;

	spi->max_speed_hz = ACHC_MAX_FREQ_HZ;
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;

	achc = devm_kzalloc(&spi->dev, sizeof(*achc), GFP_KERNEL);
	if (!achc)
		return -ENOMEM;
	spi_set_drvdata(spi, achc);
	achc->main = spi;

	mutex_init(&achc->device_lock);

	ret = of_property_read_u32_index(spi->dev.of_node, "reg", 1, &ezport_reg);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "missing second reg entry!\n");

	achc->ezport = spi_new_ancillary_device(spi, ezport_reg);
	if (IS_ERR(achc->ezport))
		return PTR_ERR(achc->ezport);

	ret = devm_add_action_or_reset(&spi->dev, unregister_ezport, achc->ezport);
	if (ret)
		return ret;

	achc->reset = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(achc->reset))
		return dev_err_probe(&spi->dev, PTR_ERR(achc->reset), "Could not get reset gpio\n");

	return 0;
}

static const struct spi_device_id gehc_achc_id[] = {
	{ "ge,achc", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, gehc_achc_id);

static const struct of_device_id gehc_achc_of_match[] = {
	{ .compatible = "ge,achc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gehc_achc_of_match);

static struct spi_driver gehc_achc_spi_driver = {
	.driver = {
		.name	= "gehc-achc",
		.of_match_table = gehc_achc_of_match,
		.dev_groups = gehc_achc_groups,
	},
	.probe		= gehc_achc_probe,
	.id_table	= gehc_achc_id,
};
module_spi_driver(gehc_achc_spi_driver);

MODULE_DESCRIPTION("GEHC ACHC driver");
MODULE_AUTHOR("Sebastian Reichel <sebastian.reichel@collabora.com>");
MODULE_LICENSE("GPL");
