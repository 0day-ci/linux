// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Connected Cars A/S
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "fxls8962af.h"

static const struct regmap_config fxls8962af_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int fxls8962af_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap *regmap;
	const char *name = NULL;

	regmap = devm_regmap_init_spi(spi, &fxls8962af_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	if (id)
		name = id->name;

	return fxls8962af_core_probe(&spi->dev, regmap, spi->irq, name);
}

static int fxls8962af_remove(struct spi_device *spi)
{
	return fxls8962af_core_remove(&spi->dev);
}

static const struct of_device_id fxls8962af_spi_of_match[] = {
	{.compatible = "nxp,fxls8962af",},
	{.compatible = "nxp,fxls8964af",},
	{},
};
MODULE_DEVICE_TABLE(of, fxls8962af_spi_of_match);

static const struct spi_device_id fxls8962af_spi_id_table[] = {
	{"fxls8962af", fxls8962af},
	{"fxls8964af", fxls8964af},
	{},
};
MODULE_DEVICE_TABLE(spi, fxls8962af_spi_id_table);

static struct spi_driver fxls8962af_driver = {
	.driver = {
		   .name = "fxls8962af_spi",
		   .pm = &fxls8962af_pm_ops,
		   .of_match_table = fxls8962af_spi_of_match,
		   },
	.probe = fxls8962af_probe,
	.remove = fxls8962af_remove,
	.id_table = fxls8962af_spi_id_table,
};

module_spi_driver(fxls8962af_driver);

MODULE_AUTHOR("Sean Nyekjaer <sean@geanix.com>");
MODULE_DESCRIPTION("NXP FXLS8962AF/FXLS8964AF accelerometer driver");
MODULE_LICENSE("GPL v2");
