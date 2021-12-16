// SPDX-License-Identifier: GPL-2.0+
/* Realtek MDIO interface driver
 *
 *
 * ASICs we intend to support with this driver:
 *
 * RTL8366   - The original version, apparently
 * RTL8369   - Similar enough to have the same datsheet as RTL8366
 * RTL8366RB - Probably reads out "RTL8366 revision B", has a quite
 *             different register layout from the other two
 * RTL8366S  - Is this "RTL8366 super"?
 * RTL8367   - Has an OpenWRT driver as well
 * RTL8368S  - Seems to be an alternative name for RTL8366RB
 * RTL8370   - Also uses SMI
 *
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 * Copyright (C) 2010 Antti Seppälä <a.seppala@gmail.com>
 * Copyright (C) 2010 Roman Yeryomin <roman@advem.lv>
 * Copyright (C) 2011 Colin Leitner <colin.leitner@googlemail.com>
 * Copyright (C) 2009-2010 Gabor Juhos <juhosg@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/bitops.h>
#include <linux/if_bridge.h>

#include "realtek.h"

/* Read/write via mdiobus */
#define MDC_MDIO_CTRL0_REG              31
#define MDC_MDIO_START_REG              29
#define MDC_MDIO_CTRL1_REG              21
#define MDC_MDIO_ADDRESS_REG            23
#define MDC_MDIO_DATA_WRITE_REG         24
#define MDC_MDIO_DATA_READ_REG          25

#define MDC_MDIO_START_OP               0xFFFF
#define MDC_MDIO_ADDR_OP                0x000E
#define MDC_MDIO_READ_OP                0x0001
#define MDC_MDIO_WRITE_OP               0x0003
#define MDC_REALTEK_DEFAULT_PHY_ADDR    0x0

int realtek_mdio_read_reg(struct realtek_priv *priv, u32 addr, u32 *data)
{
        u32 phy_id = priv->phy_id;
	struct mii_bus *bus = priv->bus;

        BUG_ON(in_interrupt());

        mutex_lock(&bus->mdio_lock);
        /* Write Start command to register 29 */
        bus->write(bus, phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

        /* Write address control code to register 31 */
        bus->write(bus, phy_id, MDC_MDIO_CTRL0_REG, MDC_MDIO_ADDR_OP);

        /* Write Start command to register 29 */
        bus->write(bus, phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

        /* Write address to register 23 */
        bus->write(bus, phy_id, MDC_MDIO_ADDRESS_REG, addr);

        /* Write Start command to register 29 */
        bus->write(bus, phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

        /* Write read control code to register 21 */
        bus->write(bus, phy_id, MDC_MDIO_CTRL1_REG, MDC_MDIO_READ_OP);

        /* Write Start command to register 29 */
        bus->write(bus, phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

        /* Read data from register 25 */
        *data = bus->read(bus, phy_id, MDC_MDIO_DATA_READ_REG);

        mutex_unlock(&bus->mdio_lock);

        return 0;
}

static int realtek_mdio_write_reg(struct realtek_priv *priv, u32 addr, u32 data)
{
        u32 phy_id = priv->phy_id;
        struct mii_bus *bus = priv->bus;

        BUG_ON(in_interrupt());

        mutex_lock(&bus->mdio_lock);

        /* Write Start command to register 29 */
        bus->write(bus, phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

        /* Write address control code to register 31 */
        bus->write(bus, phy_id, MDC_MDIO_CTRL0_REG, MDC_MDIO_ADDR_OP);

        /* Write Start command to register 29 */
        bus->write(bus, phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

        /* Write address to register 23 */
        bus->write(bus, phy_id, MDC_MDIO_ADDRESS_REG, addr);

        /* Write Start command to register 29 */
        bus->write(bus, phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

        /* Write data to register 24 */
        bus->write(bus, phy_id, MDC_MDIO_DATA_WRITE_REG, data);

        /* Write Start command to register 29 */
        bus->write(bus, phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

        /* Write data control code to register 21 */
        bus->write(bus, phy_id, MDC_MDIO_CTRL1_REG, MDC_MDIO_WRITE_OP);

        mutex_unlock(&bus->mdio_lock);
        return 0;
}


/* Regmap accessors */

static int realtek_mdio_write(void *ctx, u32 reg, u32 val)
{
	struct realtek_priv *priv = ctx;

	return realtek_mdio_write_reg(priv, reg, val);
}

static int realtek_mdio_read(void *ctx, u32 reg, u32 *val)
{
	struct realtek_priv *priv = ctx;

	return realtek_mdio_read_reg(priv, reg, val);
}

static const struct regmap_config realtek_mdio_regmap_config = {
	.reg_bits = 10, /* A4..A0 R4..R0 */
	.val_bits = 16,
	.reg_stride = 1,
	/* PHY regs are at 0x8000 */
	.max_register = 0xffff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.reg_read = realtek_mdio_read,
	.reg_write = realtek_mdio_write,
	.cache_type = REGCACHE_NONE,
};

static int realtek_mdio_probe(struct mdio_device *mdiodev)
{
	struct realtek_priv *priv;
	struct device *dev = &mdiodev->dev;
	const struct realtek_variant *var;
	int ret;
	struct device_node *np;

	var = of_device_get_match_data(dev);
	priv = devm_kzalloc(&mdiodev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->phy_id = mdiodev->addr;

	// Start by setting up the register mapping
	priv->map = devm_regmap_init(dev, NULL, priv, &realtek_mdio_regmap_config);

	priv->bus = mdiodev->bus;
	priv->dev = &mdiodev->dev;
	priv->chip_data = (void *)priv + sizeof(*priv);

	priv->clk_delay = var->clk_delay;
	priv->cmd_read = var->cmd_read;
	priv->cmd_write = var->cmd_write;
	priv->ops = var->ops;

	if (IS_ERR(priv->map))
		dev_warn(dev, "regmap initialization failed");

	priv->write_reg_noack=realtek_mdio_write_reg;

	np = dev->of_node;

	dev_set_drvdata(dev, priv);
	spin_lock_init(&priv->lock);

	/* TODO: if power is software controlled, set up any regulators here */

	/* FIXME: maybe skip if no gpio but reset after the switch was detected */
	/* Assert then deassert RESET */
	/*
	priv->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset)) {
		dev_err(dev, "failed to get RESET GPIO\n");
		return PTR_ERR(priv->reset);
	}
	msleep(REALTEK_SMI_HW_STOP_DELAY);
	gpiod_set_value(priv->reset, 0);
	msleep(REALTEK_SMI_HW_START_DELAY);
	dev_info(dev, "deasserted RESET\n");
	*/

	priv->leds_disabled = of_property_read_bool(np, "realtek,disable-leds");

	ret = priv->ops->detect(priv);
	if (ret) {
		dev_err(dev, "unable to detect switch\n");
		return ret;
	}

	priv->ds = devm_kzalloc(dev, sizeof(*priv->ds), GFP_KERNEL);
	if (!priv->ds)
		return -ENOMEM;

	priv->ds->dev = dev;
	priv->ds->num_ports = priv->num_ports;
	priv->ds->priv = priv;
	priv->ds->ops = var->ds_ops;

	ret = dsa_register_switch(priv->ds);
	if (ret) {
		dev_err(priv->dev, "unable to register switch ret = %d\n", ret);
		return ret;
	}

	return 0;
}

static void realtek_mdio_remove(struct mdio_device *mdiodev)
{
	struct realtek_priv *priv = dev_get_drvdata(&mdiodev->dev);

	if (!priv)
		return;

	dsa_unregister_switch(priv->ds);
	//gpiod_set_value(smi->reset, 1);
	dev_set_drvdata(&mdiodev->dev, NULL);
}

static void realtek_mdio_shutdown(struct mdio_device *mdiodev)
{
	struct realtek_priv *priv = dev_get_drvdata(&mdiodev->dev);

	if (!priv)
		return;

        dsa_switch_shutdown(priv->ds);

        dev_set_drvdata(&mdiodev->dev, NULL);
}

static const struct of_device_id realtek_mdio_of_match[] = {
#if IS_ENABLED(CONFIG_NET_DSA_REALTEK_RTL8366RB)
	{ .compatible = "realtek,rtl8366rb", .data = &rtl8366rb_variant, },
#endif
	/* FIXME: add support for RTL8366S and more */
	{ .compatible = "realtek,rtl8366s", .data = NULL, },
#if IS_ENABLED(CONFIG_NET_DSA_REALTEK_RTL8367C)
	{ .compatible = "realtek,rtl8365mb", .data = &rtl8367c_variant, },
	{ .compatible = "realtek,rtl8367s", .data = &rtl8367c_variant, },
#endif
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, realtek_mdio_of_match);

static struct mdio_driver realtek_mdio_driver = {
	.mdiodrv.driver = {
		.name = "realtek-mdio",
		.of_match_table = of_match_ptr(realtek_mdio_of_match),
	},
	.probe  = realtek_mdio_probe,
	.remove = realtek_mdio_remove,
	.shutdown = realtek_mdio_shutdown,
};
mdio_module_driver(realtek_mdio_driver);

MODULE_LICENSE("GPL");
