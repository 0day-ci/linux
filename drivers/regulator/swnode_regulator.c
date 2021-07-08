// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/property.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include "internal.h"

static struct fwnode_handle *
regulator_swnode_get_init_node(struct fwnode_handle *fwnode,
			       const struct regulator_desc *desc)
{
	const struct software_node *parent, *child;

	parent = to_software_node(fwnode->secondary);

	if (desc->regulators_node)
		child = software_node_find_by_name(parent,
						   desc->regulators_node);
	else
		child = software_node_find_by_name(parent, desc->name);

	return software_node_fwnode(child);
}

static int swnode_get_regulator_constraints(struct fwnode_handle *swnode,
					    struct regulator_init_data *init_data)
{
	struct regulation_constraints *constraints = &init_data->constraints;
	u32 pval;
	int ret;

	ret = fwnode_property_read_string(swnode, "regulator-name", &constraints->name);
	if (ret)
		return ret;

	if (!fwnode_property_read_u32(swnode, "regulator-min-microvolt", &pval))
		constraints->min_uV = pval;

	if (!fwnode_property_read_u32(swnode, "regulator-max-microvolt", &pval))
		constraints->max_uV = pval;

	/* Voltage change possible? */
	if (constraints->min_uV != constraints->max_uV)
		constraints->valid_ops_mask |= REGULATOR_CHANGE_VOLTAGE;

	/* Do we have a voltage range, if so try to apply it? */
	if (constraints->min_uV && constraints->max_uV)
		constraints->apply_uV = true;

	constraints->boot_on = fwnode_property_read_bool(swnode, "regulator-boot-on");
	constraints->always_on = fwnode_property_read_bool(swnode, "regulator-always-on");
	if (!constraints->always_on) /* status change should be possible. */
		constraints->valid_ops_mask |= REGULATOR_CHANGE_STATUS;

	return 0;
}

struct regulator_init_data *
regulator_swnode_get_init_data(struct device *dev,
			       const struct regulator_desc *desc,
			       struct regulator_config *config,
			       struct fwnode_handle **regnode)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct regulator_init_data *init_data;
	struct fwnode_handle *regulator;
	int ret;

	if (!fwnode || !is_software_node(fwnode->secondary))
		return NULL;

	regulator = regulator_swnode_get_init_node(fwnode, desc);
	if (!regulator)
		return NULL;

	init_data = devm_kzalloc(dev, sizeof(*init_data), GFP_KERNEL);
	if (!init_data)
		return ERR_PTR(-ENOMEM);

	ret = swnode_get_regulator_constraints(regulator, init_data);
	if (ret)
		return ERR_PTR(ret);

	*regnode = regulator;

	return init_data;
}

struct regulator_dev *
swnode_find_regulator_by_node(struct fwnode_handle *swnode)
{
	struct device *dev;

	dev = class_find_device_by_fwnode(&regulator_class, swnode);

	return dev ? dev_to_rdev(dev) : NULL;
}

struct fwnode_handle *swnode_get_regulator_node(struct device *dev, const char *supply)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	char *prop_name;

	prop_name = devm_kasprintf(dev, GFP_KERNEL, "%s-supply", supply);
	if (!prop_name)
		return ERR_PTR(-ENOMEM);

	return fwnode_find_reference(fwnode->secondary, prop_name, 0);
}
