// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/regmap.h>

static int regmap_miim_read(void *context, unsigned int reg, unsigned int *val)
{
	struct mdio_device *mdio_dev = context;
	int ret;

	ret = mdiobus_read(mdio_dev->bus, mdio_dev->addr, reg);
	*val = ret & 0xffff;

	return ret < 0 ? ret : 0;
}

static int regmap_miim_write(void *context, unsigned int reg, unsigned int val)
{
	struct mdio_device *mdio_dev = context;

	return mdiobus_write(mdio_dev->bus, mdio_dev->addr, reg, val);
}

static const struct regmap_bus regmap_miim_bus = {
	.reg_write = regmap_miim_write,
	.reg_read = regmap_miim_read,
};

struct regmap *__regmap_init_miim(struct mdio_device *mdio_dev,
	const struct regmap_config *config, struct lock_class_key *lock_key,
	const char *lock_name)
{
	if (config->reg_bits != 5 || config->val_bits != 16)
		return ERR_PTR(-ENOTSUPP);

	return __regmap_init(&mdio_dev->dev, &regmap_miim_bus, mdio_dev, config,
		lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_miim);

struct regmap *__devm_regmap_init_miim(struct mdio_device *mdio_dev,
	const struct regmap_config *config, struct lock_class_key *lock_key,
	const char *lock_name)
{
	if (config->reg_bits != 5 || config->val_bits != 16)
		return ERR_PTR(-ENOTSUPP);

	return __devm_regmap_init(&mdio_dev->dev, &regmap_miim_bus, mdio_dev,
		config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_miim);

MODULE_AUTHOR("Sander Vanheule <sander@svanheule.net>");
MODULE_DESCRIPTION("Regmap MIIM Module");
MODULE_LICENSE("GPL v2");

