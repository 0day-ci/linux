// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright 2021 Innovative Advantage Inc.
 */

#include <asm/byteorder.h>
#include <linux/spi/spi.h>
#include <linux/iopoll.h>
#include <linux/kconfig.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include "ocelot-mfd.h"

#define REG(reg, offset)	[reg] = offset

struct ocelot_spi {
	int spi_padding_bytes;
	struct spi_device *spi;
	struct ocelot_mfd_config config;
	struct regmap *cpuorg_regmap;
	const u32 *map;
};

enum ocelot_dev_cpuorg_regs {
	DEV_CPUORG_IF_CTRL,
	DEV_CPUORG_IF_CFGSTAT,
	DEV_CPUORG_REG_MAX,
};

static const u32 vsc7512_dev_cpuorg_regmap[] = {
	REG(DEV_CPUORG_IF_CTRL,		0x0000),
	REG(DEV_CPUORG_IF_CFGSTAT,	0x0004),
};

static const struct resource vsc7512_dev_cpuorg_resource = {
	.start	= 0x71000000,
	.end	= 0x710002ff,
	.name	= "devcpu_org",
};

#define VSC7512_BYTE_ORDER_LE 0x00000000
#define VSC7512_BYTE_ORDER_BE 0x81818181
#define VSC7512_BIT_ORDER_MSB 0x00000000
#define VSC7512_BIT_ORDER_LSB 0x42424242

static struct ocelot_spi *
config_to_ocelot_spi(struct ocelot_mfd_config *config)
{
	return container_of(config, struct ocelot_spi, config);
}

static int ocelot_spi_init_bus(struct ocelot_spi *ocelot_spi)
{
	struct spi_device *spi;
	struct device *dev;
	u32 val, check;
	int err;

	spi = ocelot_spi->spi;
	dev = &spi->dev;

	dev_info(dev, "initializing SPI interface for chip\n");

	val = 0;

#ifdef __LITTLE_ENDIAN
	val |= VSC7512_BYTE_ORDER_LE;
#else
	val |= VSC7512_BYTE_ORDER_BE;
#endif

	err = regmap_write(ocelot_spi->cpuorg_regmap,
			   ocelot_spi->map[DEV_CPUORG_IF_CTRL], val);
	if (err)
		return err;

	val = ocelot_spi->spi_padding_bytes;
	err = regmap_write(ocelot_spi->cpuorg_regmap,
			   ocelot_spi->map[DEV_CPUORG_IF_CFGSTAT], val);
	if (err)
		return err;

	check = val | 0x02000000;

	err = regmap_read(ocelot_spi->cpuorg_regmap,
			  ocelot_spi->map[DEV_CPUORG_IF_CFGSTAT], &val);
	if (err)
		return err;

	if (check != val) {
		dev_err(dev, "Error configuring SPI bus. V: 0x%08x != 0x%08x\n",
			val, check);
		return -ENODEV;
	}

	return 0;
}

static int ocelot_spi_init_bus_from_config(struct ocelot_mfd_config *config)
{
	struct ocelot_spi *ocelot_spi = config_to_ocelot_spi(config);

	return ocelot_spi_init_bus(ocelot_spi);
}

static unsigned int ocelot_spi_translate_address(unsigned int reg)
{
	return cpu_to_be32((reg & 0xffffff) >> 2);
}

struct ocelot_spi_regmap_context {
	struct spi_device *spi;
	u32 base;
	int padding_bytes;
};

static int ocelot_spi_reg_read(void *context, unsigned int reg,
			       unsigned int *val)
{
	struct ocelot_spi_regmap_context *regmap_context = context;
	struct spi_transfer tx, padding, rx;
	struct ocelot_spi *ocelot_spi;
	struct spi_message msg;
	struct spi_device *spi;
	unsigned int addr;
	u8 *tx_buf;

	WARN_ON(!val);

	spi = regmap_context->spi;

	ocelot_spi = spi_get_drvdata(spi);

	addr = ocelot_spi_translate_address(reg + regmap_context->base);
	tx_buf = (u8 *)&addr;

	spi_message_init(&msg);

	memset(&tx, 0, sizeof(struct spi_transfer));

	/* Ignore the first byte for the 24-bit address */
	tx.tx_buf = &tx_buf[1];
	tx.len = 3;

	spi_message_add_tail(&tx, &msg);

	if (regmap_context->padding_bytes > 0) {
		u8 dummy_buf[16] = {0};

		memset(&padding, 0, sizeof(struct spi_transfer));

		/* Just toggle the clock for padding bytes */
		padding.len = regmap_context->padding_bytes;
		padding.tx_buf = dummy_buf;
		padding.dummy_data = 1;

		spi_message_add_tail(&padding, &msg);
	}

	memset(&rx, 0, sizeof(struct spi_transfer));
	rx.rx_buf = val;
	rx.len = 4;

	spi_message_add_tail(&rx, &msg);

	return spi_sync(spi, &msg);
}

static int ocelot_spi_reg_write(void *context, unsigned int reg,
				unsigned int val)
{
	struct ocelot_spi_regmap_context *regmap_context = context;
	struct spi_transfer tx[2] = {0};
	struct spi_message msg;
	struct spi_device *spi;
	unsigned int addr;
	u8 *tx_buf;

	spi = regmap_context->spi;

	addr = ocelot_spi_translate_address(reg + regmap_context->base);
	tx_buf = (u8 *)&addr;

	spi_message_init(&msg);

	/* Ignore the first byte for the 24-bit address and set the write bit */
	tx_buf[1] |= BIT(7);
	tx[0].tx_buf = &tx_buf[1];
	tx[0].len = 3;

	spi_message_add_tail(&tx[0], &msg);

	memset(&tx[1], 0, sizeof(struct spi_transfer));
	tx[1].tx_buf = &val;
	tx[1].len = 4;

	spi_message_add_tail(&tx[1], &msg);

	return spi_sync(spi, &msg);
}

static const struct regmap_config ocelot_spi_regmap_config = {
	.reg_bits = 24,
	.reg_stride = 4,
	.val_bits = 32,

	.reg_read = ocelot_spi_reg_read,
	.reg_write = ocelot_spi_reg_write,

	.max_register = 0xffffffff,
	.use_single_write = true,
	.use_single_read = true,
	.can_multi_write = false,

	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};

static struct regmap *
ocelot_spi_get_regmap(struct ocelot_mfd_config *config,
		      const struct resource *res, const char *name)
{
	struct ocelot_spi *ocelot_spi = config_to_ocelot_spi(config);
	struct ocelot_spi_regmap_context *context;
	struct regmap_config regmap_config;
	struct regmap *regmap;
	struct device *dev;


	dev = &ocelot_spi->spi->dev;

	/* Don't re-allocate another regmap if we have one */
	regmap = dev_get_regmap(dev, name);
	if (regmap)
		return regmap;

	context = devm_kzalloc(dev, sizeof(struct ocelot_spi_regmap_context),
			       GFP_KERNEL);

	if (IS_ERR(context))
		return ERR_CAST(context);

	context->base = res->start;
	context->spi = ocelot_spi->spi;
	context->padding_bytes = ocelot_spi->spi_padding_bytes;

	memcpy(&regmap_config, &ocelot_spi_regmap_config,
	       sizeof(ocelot_spi_regmap_config));

	regmap_config.name = name;
	regmap_config.max_register = res->end - res->start;

	regmap = devm_regmap_init(dev, NULL, context, &regmap_config);
	if (IS_ERR(regmap))
		return ERR_CAST(regmap);

	return regmap;
}

static int ocelot_spi_probe(struct spi_device *spi)
{
	struct ocelot_spi *ocelot_spi;
	struct device *dev;
	char name[32];
	int err;

	dev = &spi->dev;

	ocelot_spi = devm_kzalloc(dev, sizeof(struct ocelot_spi),
				      GFP_KERNEL);

	if (!ocelot_spi)
		return -ENOMEM;

	if (spi->max_speed_hz <= 500000) {
		ocelot_spi->spi_padding_bytes = 0;
	} else {
		/*
		 * Calculation taken from the manual for IF_CFGSTAT:IF_CFG. Err
		 * on the side of more padding bytes, as having too few can be
		 * difficult to detect at runtime.
		 */
		ocelot_spi->spi_padding_bytes = 1 +
			(spi->max_speed_hz / 1000000 + 2) / 8;
	}

	ocelot_spi->spi = spi;
	ocelot_spi->map = vsc7512_dev_cpuorg_regmap;

	spi->bits_per_word = 8;

	err = spi_setup(spi);
	if (err < 0) {
		dev_err(&spi->dev, "Error %d initializing SPI\n", err);
		return err;
	}

	dev_info(dev, "configured SPI bus for speed %d, rx padding bytes %d\n",
			spi->max_speed_hz, ocelot_spi->spi_padding_bytes);

	/* Ensure we have devcpu_org regmap before we call ocelot_mfd_init */
	ocelot_mfd_get_resource_name(name, &vsc7512_dev_cpuorg_resource,
				     sizeof(name) - 1);

	/*
	 * Since we created dev, we know there isn't a regmap, so create one
	 * here directly.
	 */
	ocelot_spi->cpuorg_regmap =
		ocelot_spi_get_regmap(&ocelot_spi->config,
				      &vsc7512_dev_cpuorg_resource, name);
	if (!ocelot_spi->cpuorg_regmap)
		return -ENOMEM;

	ocelot_spi->config.init_bus = ocelot_spi_init_bus_from_config;
	ocelot_spi->config.get_regmap = ocelot_spi_get_regmap;
	ocelot_spi->config.dev = dev;

	spi_set_drvdata(spi, ocelot_spi);

	/*
	 * The chip must be set up for SPI before it gets initialized and reset.
	 * Do this once here before calling mfd_init
	 */
	err = ocelot_spi_init_bus(ocelot_spi);
	if (err) {
		dev_err(dev, "Error %d initializing Ocelot SPI bus\n", err);
		return err;
	}

	err = ocelot_mfd_init(&ocelot_spi->config);
	if (err < 0) {
		dev_err(dev, "Error %d initializing Ocelot MFD\n", err);
		return err;
	}

	dev_info(&spi->dev, "ocelot spi mfd probed\n");

	return 0;
}

static int ocelot_spi_remove(struct spi_device *spi)
{
	struct ocelot_spi *ocelot_spi;

	ocelot_spi = spi_get_drvdata(spi);
	devm_kfree(&spi->dev, ocelot_spi);
	return 0;
}

const struct of_device_id ocelot_mfd_of_match[] = {
	{ .compatible = "mscc,vsc7514_mfd_spi" },
	{ .compatible = "mscc,vsc7513_mfd_spi" },
	{ .compatible = "mscc,vsc7512_mfd_spi" },
	{ .compatible = "mscc,vsc7511_mfd_spi" },
	{ },
};
MODULE_DEVICE_TABLE(of, ocelot_mfd_of_match);

static struct spi_driver ocelot_mfd_spi_driver = {
	.driver = {
		.name = "ocelot_mfd_spi",
		.of_match_table = of_match_ptr(ocelot_mfd_of_match),
	},
	.probe = ocelot_spi_probe,
	.remove = ocelot_spi_remove,
};
module_spi_driver(ocelot_mfd_spi_driver);

MODULE_DESCRIPTION("Ocelot Chip MFD SPI driver");
MODULE_AUTHOR("Colin Foster <colin.foster@in-advantage.com>");
MODULE_LICENSE("Dual MIT/GPL");
