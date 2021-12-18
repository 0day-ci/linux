// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright 2021 Innovative Advantage Inc.
 */

#include <asm/byteorder.h>
#include <linux/spi/spi.h>
#include <linux/kconfig.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "ocelot-mfd.h"

#define REG(reg, offset)	[reg] = offset

enum ocelot_mfd_gcb_regs {
	GCB_SOFT_RST,
	GCB_REG_MAX,
};

enum ocelot_mfd_gcb_regfields {
	GCB_SOFT_RST_CHIP_RST,
	GCB_REGFIELD_MAX,
};

static const u32 vsc7512_gcb_regmap[] = {
	REG(GCB_SOFT_RST,	0x0008),
};

static const struct reg_field vsc7512_mfd_gcb_regfields[GCB_REGFIELD_MAX] = {
	[GCB_SOFT_RST_CHIP_RST] = REG_FIELD(vsc7512_gcb_regmap[GCB_SOFT_RST], 0, 0),
};

struct ocelot_mfd_core {
	struct ocelot_mfd_config *config;
	struct regmap *gcb_regmap;
	struct regmap_field *gcb_regfields[GCB_REGFIELD_MAX];
};

static const struct resource vsc7512_gcb_resource = {
	.start	= 0x71070000,
	.end	= 0x7107022b,
	.name	= "devcpu_gcb",
};

static int ocelot_mfd_reset(struct ocelot_mfd_core *core)
{
	int ret;

	dev_info(core->config->dev, "resetting ocelot chip\n");

	ret = regmap_field_write(core->gcb_regfields[GCB_SOFT_RST_CHIP_RST], 1);
	if (ret)
		return ret;

	/*
	 * Note: This is adapted from the PCIe reset strategy. The manual doesn't
	 * suggest how to do a reset over SPI, and the register strategy isn't
	 * possible.
	 */
	msleep(100);

	ret = core->config->init_bus(core->config);
	if (ret)
		return ret;

	return 0;
}

void ocelot_mfd_get_resource_name(char *name, const struct resource *res,
				  int size)
{
	if (res->name)
		snprintf(name, size - 1, "ocelot_mfd-%s", res->name);
	else
		snprintf(name, size - 1, "ocelot_mfd@0x%08x", res->start);
}
EXPORT_SYMBOL(ocelot_mfd_get_resource_name);

static struct regmap *ocelot_mfd_regmap_init(struct ocelot_mfd_core *core,
					     const struct resource *res)
{
	struct device *dev = core->config->dev;
	struct regmap *regmap;
	char name[32];

	ocelot_mfd_get_resource_name(name, res, sizeof(name) - 1);

	regmap = dev_get_regmap(dev, name);

	if (!regmap)
		regmap = core->config->get_regmap(core->config, res, name);

	return regmap;
}

struct regmap *ocelot_mfd_get_regmap_from_resource(struct device *dev,
						   const struct resource *res)
{
	struct ocelot_mfd_core *core = dev_get_drvdata(dev);

	return ocelot_mfd_regmap_init(core, res);
}
EXPORT_SYMBOL(ocelot_mfd_get_regmap_from_resource);

static const struct resource vsc7512_miim1_resources[] = {
	{
		.start = 0x710700c0,
		.end = 0x710700e3,
		.name = "gcb_miim1",
		.flags = IORESOURCE_MEM,
	},
};

static const struct mfd_cell vsc7512_devs[] = {
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

int ocelot_mfd_init(struct ocelot_mfd_config *config)
{
	struct device *dev = config->dev;
	const struct reg_field *regfield;
	struct ocelot_mfd_core *core;
	int i, ret;

	core = devm_kzalloc(dev, sizeof(struct ocelot_mfd_config), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	dev_set_drvdata(dev, core);

	core->config = config;

	/* Create regmaps and regfields here */
	core->gcb_regmap = ocelot_mfd_regmap_init(core, &vsc7512_gcb_resource);
	if (!core->gcb_regmap)
		return -ENOMEM;

	for (i = 0; i < GCB_REGFIELD_MAX; i++) {
		regfield = &vsc7512_mfd_gcb_regfields[i];
		core->gcb_regfields[i] =
			devm_regmap_field_alloc(dev, core->gcb_regmap,
						*regfield);
		if (!core->gcb_regfields[i])
			return -ENOMEM;
	}

	/* Prepare the chip */
	ret = ocelot_mfd_reset(core);
	if (ret) {
		dev_err(dev, "ocelot mfd reset failed with code %d\n", ret);
		return ret;
	}

	ret = mfd_add_devices(dev, PLATFORM_DEVID_NONE, vsc7512_devs,
			      ARRAY_SIZE(vsc7512_devs), NULL, 0, NULL);

	dev_info(dev, "ocelot mfd core setup complete\n");

	return 0;
}
EXPORT_SYMBOL(ocelot_mfd_init);

int ocelot_mfd_remove(struct ocelot_mfd_config *config)
{
	mfd_remove_devices(config->dev);

	return 0;
}
EXPORT_SYMBOL(ocelot_mfd_remove);

MODULE_DESCRIPTION("Ocelot Chip MFD driver");
MODULE_AUTHOR("Colin Foster <colin.foster@in-advantage.com>");
MODULE_LICENSE("GPL v2");
