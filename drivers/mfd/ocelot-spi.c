// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * SPI core driver for the Ocelot chip family.
 *
 * This driver will handle everything necessary to allow for communication over
 * SPI to the VSC7511, VSC7512, VSC7513 and VSC7514 chips. The main functions
 * are to prepare the chip's SPI interface for a specific bus speed, and a host
 * processor's endianness. This will create and distribute regmaps for any MFD
 * children.
 *
 * Copyright 2021 Innovative Advantage Inc.
 *
 * Author: Colin Foster <colin.foster@in-advantage.com>
 */

#include <linux/iopoll.h>
#include <linux/kconfig.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include <asm/byteorder.h>

#include "ocelot.h"

struct ocelot_spi {
	int spi_padding_bytes;
	struct spi_device *spi;
	struct ocelot_core core;
	struct regmap *cpuorg_regmap;
};

#define DEV_CPUORG_IF_CTRL	(0x0000)
#define DEV_CPUORG_IF_CFGSTAT	(0x0004)

static const struct resource vsc7512_dev_cpuorg_resource = {
	.start	= 0x71000000,
	.end	= 0x710002ff,
	.name	= "devcpu_org",
};

#define VSC7512_BYTE_ORDER_LE 0x00000000
#define VSC7512_BYTE_ORDER_BE 0x81818181
#define VSC7512_BIT_ORDER_MSB 0x00000000
#define VSC7512_BIT_ORDER_LSB 0x42424242

static struct ocelot_spi *core_to_ocelot_spi(struct ocelot_core *core)
{
	return container_of(core, struct ocelot_spi, core);
}

static int ocelot_spi_init_bus(struct ocelot_spi *ocelot_spi)
{
	struct spi_device *spi;
	struct device *dev;
	u32 val, check;
	int err;

	spi = ocelot_spi->spi;
	dev = &spi->dev;

#ifdef __LITTLE_ENDIAN
	val = VSC7512_BYTE_ORDER_LE;
#else
	val = VSC7512_BYTE_ORDER_BE;
#endif

	err = regmap_write(ocelot_spi->cpuorg_regmap, DEV_CPUORG_IF_CTRL, val);
	if (err)
		return err;

	val = ocelot_spi->spi_padding_bytes;
	err = regmap_write(ocelot_spi->cpuorg_regmap, DEV_CPUORG_IF_CFGSTAT,
			   val);
	if (err)
		return err;

	check = val | 0x02000000;

	err = regmap_read(ocelot_spi->cpuorg_regmap, DEV_CPUORG_IF_CFGSTAT,
			  &val);
	if (err)
		return err;

	if (check != val)
		return -ENODEV;

	return 0;
}

int ocelot_spi_initialize(struct ocelot_core *core)
{
	struct ocelot_spi *ocelot_spi = core_to_ocelot_spi(core);

	return ocelot_spi_init_bus(ocelot_spi);
}
EXPORT_SYMBOL(ocelot_spi_initialize);

static unsigned int ocelot_spi_translate_address(unsigned int reg)
{
	return cpu_to_be32((reg & 0xffffff) >> 2);
}

struct ocelot_spi_regmap_context {
	u32 base;
	struct ocelot_spi *ocelot_spi;
};

static int ocelot_spi_reg_read(void *context, unsigned int reg,
			       unsigned int *val)
{
	struct ocelot_spi_regmap_context *regmap_context = context;
	struct ocelot_spi *ocelot_spi = regmap_context->ocelot_spi;
	struct spi_transfer tx, padding, rx;
	struct spi_message msg;
	struct spi_device *spi;
	unsigned int addr;
	u8 *tx_buf;

	WARN_ON(!val);

	spi = ocelot_spi->spi;

	addr = ocelot_spi_translate_address(reg + regmap_context->base);
	tx_buf = (u8 *)&addr;

	spi_message_init(&msg);

	memset(&tx, 0, sizeof(struct spi_transfer));

	/* Ignore the first byte for the 24-bit address */
	tx.tx_buf = &tx_buf[1];
	tx.len = 3;

	spi_message_add_tail(&tx, &msg);

	if (ocelot_spi->spi_padding_bytes > 0) {
		u8 dummy_buf[16] = {0};

		memset(&padding, 0, sizeof(struct spi_transfer));

		/* Just toggle the clock for padding bytes */
		padding.len = ocelot_spi->spi_padding_bytes;
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
	struct ocelot_spi *ocelot_spi = regmap_context->ocelot_spi;
	struct spi_transfer tx[2] = {0};
	struct spi_message msg;
	struct spi_device *spi;
	unsigned int addr;
	u8 *tx_buf;

	spi = ocelot_spi->spi;

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

struct regmap *
ocelot_spi_devm_get_regmap(struct ocelot_core *core, struct device *dev,
			   const struct resource *res)
{
	struct ocelot_spi *ocelot_spi = core_to_ocelot_spi(core);
	struct ocelot_spi_regmap_context *context;
	struct regmap_config regmap_config;
	struct regmap *regmap;

	context = devm_kzalloc(dev, sizeof(*context), GFP_KERNEL);
	if (IS_ERR(context))
		return ERR_CAST(context);

	context->base = res->start;
	context->ocelot_spi = ocelot_spi;

	memcpy(&regmap_config, &ocelot_spi_regmap_config,
	       sizeof(ocelot_spi_regmap_config));

	regmap_config.name = res->name;
	regmap_config.max_register = res->end - res->start;

	regmap = devm_regmap_init(dev, NULL, context, &regmap_config);
	if (IS_ERR(regmap))
		return ERR_CAST(regmap);

	return regmap;
}

static int ocelot_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ocelot_spi *ocelot_spi;
	int err;

	ocelot_spi = devm_kzalloc(dev, sizeof(*ocelot_spi), GFP_KERNEL);

	if (!ocelot_spi)
		return -ENOMEM;

	if (spi->max_speed_hz <= 500000) {
		ocelot_spi->spi_padding_bytes = 0;
	} else {
		/*
		 * Calculation taken from the manual for IF_CFGSTAT:IF_CFG.
		 * Register access time is 1us, so we need to configure and send
		 * out enough padding bytes between the read request and data
		 * transmission that lasts at least 1 microsecond.
		 */
		ocelot_spi->spi_padding_bytes = 1 +
			(spi->max_speed_hz / 1000000 + 2) / 8;
	}

	ocelot_spi->spi = spi;

	spi->bits_per_word = 8;

	err = spi_setup(spi);
	if (err < 0) {
		dev_err(&spi->dev, "Error %d initializing SPI\n", err);
		return err;
	}

	ocelot_spi->cpuorg_regmap =
		ocelot_spi_devm_get_regmap(&ocelot_spi->core, dev,
					   &vsc7512_dev_cpuorg_resource);
	if (!ocelot_spi->cpuorg_regmap)
		return -ENOMEM;

	ocelot_spi->core.dev = dev;

	/*
	 * The chip must be set up for SPI before it gets initialized and reset.
	 * This must be done before calling init, and after a chip reset is
	 * performed.
	 */
	err = ocelot_spi_init_bus(ocelot_spi);
	if (err) {
		dev_err(dev, "Error %d initializing Ocelot SPI bus\n", err);
		return err;
	}

	err = ocelot_core_init(&ocelot_spi->core);
	if (err < 0) {
		dev_err(dev, "Error %d initializing Ocelot MFD\n", err);
		return err;
	}

	return 0;
}

static int ocelot_spi_remove(struct spi_device *spi)
{
	return 0;
}

const struct of_device_id ocelot_spi_of_match[] = {
	{ .compatible = "mscc,vsc7512_mfd_spi" },
	{ },
};
MODULE_DEVICE_TABLE(of, ocelot_spi_of_match);

static struct spi_driver ocelot_spi_driver = {
	.driver = {
		.name = "ocelot_mfd_spi",
		.of_match_table = of_match_ptr(ocelot_spi_of_match),
	},
	.probe = ocelot_spi_probe,
	.remove = ocelot_spi_remove,
};
module_spi_driver(ocelot_spi_driver);

MODULE_DESCRIPTION("Ocelot Chip MFD SPI driver");
MODULE_AUTHOR("Colin Foster <colin.foster@in-advantage.com>");
MODULE_LICENSE("Dual MIT/GPL");
