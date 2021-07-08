/* SPDX-License-Identifier: GPL-2.0 */
/* Author: Dan Scally <djrscally@gmail.com> */
#ifndef __LINUX_SWNODE_REG_H
#define __LINUX_SWNODE_REG_H

#if defined(CONFIG_REGULATOR_SWNODE)
struct regulator_init_data *regulator_swnode_get_init_data(struct device *dev,
							   const struct regulator_desc *desc,
							   struct regulator_config *config,
							   struct fwnode_handle **regnode);
struct regulator_dev *swnode_find_regulator_by_node(struct fwnode_handle *swnode);
struct fwnode_handle *swnode_get_regulator_node(struct device *dev, const char *supply);
#else
struct regulator_init_data *regulator_swnode_get_init_data(struct device *dev,
							   const struct regulator_desc *desc,
							   struct regulator_config *config)
{
	return NULL;
}

struct regulator_dev *swnode_find_regulator_by_node(struct fwnode_handle *swnode)
{
	return NULL;
}

struct fwnode_handle *swnode_get_regulator_node(struct device *dev, const char *supply)
{
	return NULL;
}

#endif

#endif
