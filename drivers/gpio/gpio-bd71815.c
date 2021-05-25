// SPDX-License-Identifier: GPL-2.0
/*
 * Support to GPOs on ROHM BD71815
 * Copyright 2021 ROHM Semiconductors.
 * Author: Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>
 *
 * Copyright 2014 Embest Technology Co. Ltd. Inc.
 * Author: yanglsh@embest-tech.com
 */

#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
/* For the BD71815 register definitions */
#include <linux/mfd/rohm-bd71815.h>

static int bd71815_gpio_set_config(const struct gpio_regmap_config *gr_config,
				   unsigned int offset, unsigned long config)
{
	struct regmap *regmap = gr_config->regmap;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(regmap,
					  BD71815_REG_GPO,
					  BD71815_GPIO_DRIVE_MASK << offset,
					  BD71815_GPIO_OPEN_DRAIN << offset);
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(regmap,
					  BD71815_REG_GPO,
					  BD71815_GPIO_DRIVE_MASK << offset,
					  BD71815_GPIO_CMOS << offset);
	default:
		dev_err(gr_config->parent, "Unsupported config (0x%lx)\n",
			config);
		break;
	}
	return -ENOTSUPP;
}

#define BD71815_TWO_GPIOS	GENMASK(1, 0)
#define BD71815_ONE_GPIO	BIT(0)

/*
 * Sigh. The BD71815 and BD71817 were originally designed to support two GPO
 * pins. At some point it was noticed the second GPO pin which is the E5 pin
 * located at the center of IC is hard to use on PCB (due to the location). It
 * was decided to not promote this second GPO and the pin is marked as GND in
 * the datasheet. The functionality is still there though! I guess driving a GPO
 * connected to the ground is a bad idea. Thus we do not support it by default.
 * OTOH - the original driver written by colleagues at Embest did support
 * controlling this second GPO. It is thus possible this is used in some of the
 * products.
 *
 * This driver does not by default support configuring this second GPO
 * but allows using it by providing the DT property
 * "rohm,enable-hidden-gpo".
 */
static int bd71815_init_valid_mask(const struct gpio_regmap_config *c,
				   unsigned long *valid_mask,
				   unsigned int ngpios)
{

	if (ngpios != 2)
		return 0;

	/* The property should be in MFD DT node */
	if (c->parent && fwnode_property_present(c->fwnode,
						 "rohm,enable-hidden-gpo"))
		*valid_mask = BD71815_TWO_GPIOS;
	else
		*valid_mask = BD71815_ONE_GPIO;

	return 0;
}

/* Template for regmap gpio config */
static const struct gpio_regmap_config gpio_cfg_template = {
	.label			= "bd71815",
	.reg_set_base		= BD71815_REG_GPO,
	.ngpio			= 2,
};

static const struct gpio_regmap_ops ops = {
	.set_config		= bd71815_gpio_set_config,
	.init_valid_mask	= bd71815_init_valid_mask,
};

static int gpo_bd71815_probe(struct platform_device *pdev)
{
	struct gpio_regmap_config cfg;
	struct device *parent, *dev;

	/*
	 * Bind devm lifetime to this platform device => use dev for devm.
	 * also the prints should originate from this device.
	 */
	dev = &pdev->dev;
	/* The device-tree and regmap come from MFD => use parent for that */
	parent = dev->parent;

	cfg = gpio_cfg_template;
	cfg.parent = parent;
	cfg.regmap = dev_get_regmap(parent, NULL);
	cfg.fwnode = dev_fwnode(dev);

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(dev, &cfg, &ops));
}

static struct platform_driver gpo_bd71815_driver = {
	.driver = {
		.name	= "bd71815-gpo",
	},
	.probe		= gpo_bd71815_probe,
};
module_platform_driver(gpo_bd71815_driver);

MODULE_ALIAS("platform:bd71815-gpo");
MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_AUTHOR("Peter Yang <yanglsh@embest-tech.com>");
MODULE_DESCRIPTION("GPO interface for BD71815");
MODULE_LICENSE("GPL");
