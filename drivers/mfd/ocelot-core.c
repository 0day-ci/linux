// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * MFD core driver for the Ocelot chip family.
 *
 * The VSC7511, 7512, 7513, and 7514 can be controlled internally via an
 * on-chip MIPS processor, or externally via SPI, I2C, PCIe. This core driver is
 * intended to be the bus-agnostic glue between, for example, the SPI bus and
 * the MFD children.
 *
 * Copyright 2021 Innovative Advantage Inc.
 *
 * Author: Colin Foster <colin.foster@in-advantage.com>
 */

#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <asm/byteorder.h>

#include "ocelot.h"

#define GCB_SOFT_RST (0x0008)

#define SOFT_CHIP_RST (0x1)

static const struct resource vsc7512_gcb_resource = {
	.start	= 0x71070000,
	.end	= 0x7107022b,
	.name	= "devcpu_gcb",
};

static int ocelot_reset(struct ocelot_core *core)
{
	int ret;

	/*
	 * Reset the entire chip here to put it into a completely known state.
	 * Other drivers may want to reset their own subsystems. The register
	 * self-clears, so one write is all that is needed
	 */
	ret = regmap_write(core->gcb_regmap, GCB_SOFT_RST, SOFT_CHIP_RST);
	if (ret)
		return ret;

	msleep(100);

	/*
	 * A chip reset will clear the SPI configuration, so it needs to be done
	 * again before we can access any more registers
	 */
	ret = ocelot_spi_initialize(core);

	return ret;
}

static struct regmap *ocelot_devm_regmap_init(struct ocelot_core *core,
					      struct device *dev,
					      const struct resource *res)
{
	struct regmap *regmap;

	regmap = dev_get_regmap(dev, res->name);
	if (!regmap)
		regmap = ocelot_spi_devm_get_regmap(core, dev, res);

	return regmap;
}

struct regmap *ocelot_get_regmap_from_resource(struct device *dev,
					       const struct resource *res)
{
	struct ocelot_core *core = dev_get_drvdata(dev);

	return ocelot_devm_regmap_init(core, dev, res);
}
EXPORT_SYMBOL(ocelot_get_regmap_from_resource);

static const struct resource vsc7512_miim1_resources[] = {
	{
		.start = 0x710700c0,
		.end = 0x710700e3,
		.name = "gcb_miim1",
		.flags = IORESOURCE_MEM,
	},
};

static const struct resource vsc7512_pinctrl_resources[] = {
	{
		.start = 0x71070034,
		.end = 0x7107009f,
		.name = "gcb_gpio",
		.flags = IORESOURCE_MEM,
	},
};

static const struct resource vsc7512_sgpio_resources[] = {
	{
		.start = 0x710700f8,
		.end = 0x710701f7,
		.name = "gcb_sio",
		.flags = IORESOURCE_MEM,
	},
};

static const struct mfd_cell vsc7512_devs[] = {
	{
		.name = "pinctrl-ocelot",
		.of_compatible = "mscc,ocelot-pinctrl",
		.num_resources = ARRAY_SIZE(vsc7512_pinctrl_resources),
		.resources = vsc7512_pinctrl_resources,
	},
	{
		.name = "pinctrl-sgpio",
		.of_compatible = "mscc,ocelot-sgpio",
		.num_resources = ARRAY_SIZE(vsc7512_sgpio_resources),
		.resources = vsc7512_sgpio_resources,
	},
	{
		.name = "ocelot-miim1",
		.of_compatible = "mscc,ocelot-miim",
		.num_resources = ARRAY_SIZE(vsc7512_miim1_resources),
		.resources = vsc7512_miim1_resources,
	},
	{
		.name = "ocelot-ext-switch",
		.of_compatible = "mscc,vsc7512-ext-switch",
	},
};

int ocelot_core_init(struct ocelot_core *core)
{
	struct device *dev = core->dev;
	int ret;

	dev_set_drvdata(dev, core);

	core->gcb_regmap = ocelot_devm_regmap_init(core, dev,
						   &vsc7512_gcb_resource);
	if (!core->gcb_regmap)
		return -ENOMEM;

	/* Prepare the chip */
	ret = ocelot_reset(core);
	if (ret) {
		dev_err(dev, "ocelot mfd reset failed with code %d\n", ret);
		return ret;
	}

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, vsc7512_devs,
				   ARRAY_SIZE(vsc7512_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(dev, "error adding mfd devices\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_core_init);

int ocelot_remove(struct ocelot_core *core)
{
	return 0;
}
EXPORT_SYMBOL(ocelot_remove);

MODULE_DESCRIPTION("Ocelot Chip MFD driver");
MODULE_AUTHOR("Colin Foster <colin.foster@in-advantage.com>");
MODULE_LICENSE("GPL v2");
