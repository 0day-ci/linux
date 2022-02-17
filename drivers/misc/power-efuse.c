// SPDX-License-Identifier: GPL-2.0
/*
 * This module provides a thin wrapper around a regulator device that exposes
 * status bits and on/off state via sysfs.
 *
 * Copyright (C) 2022 Zev Weiss <zev@bewilderbeest.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

struct efuse {
	struct regulator *reg;
	struct {
		unsigned int cache;
		unsigned long ttl;
		unsigned long fetch_time;
		struct mutex lock;
	} error_flags;
};

/* Ensure that the next error_flags access fetches them from the device */
static void efuse_invalidate_error_flags(struct efuse *efuse)
{
	mutex_lock(&efuse->error_flags.lock);
	efuse->error_flags.fetch_time = 0;
	mutex_unlock(&efuse->error_flags.lock);
}

static ssize_t efuse_show_operstate(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct efuse *efuse = dev_get_drvdata(dev);
	int status = regulator_is_enabled(efuse->reg);

	if (status < 0)
		return status;

	return sysfs_emit(buf, "%s\n", status ? "on" : "off");
}

static ssize_t efuse_set_operstate(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int status, wantstate;
	struct efuse *efuse = dev_get_drvdata(dev);
	struct regulator *reg = efuse->reg;

	if (sysfs_streq(buf, "on"))
		wantstate = 1;
	else if (sysfs_streq(buf, "off"))
		wantstate = 0;
	else
		return -EINVAL;

	status = regulator_is_enabled(reg);

	/*
	 * We need to ensure our enable/disable calls don't get imbalanced, so
	 * bail if we can't determine the current state.
	 */
	if (status < 0)
		return status;

	/* Return early if we're already in the desired state */
	if (!!status == wantstate)
		return count;

	if (wantstate)
		status = regulator_enable(reg);
	else
		status = regulator_disable(reg);

	/*
	 * Toggling operstate can reset latched status flags, so invalidate
	 * the cached value.
	 */
	efuse_invalidate_error_flags(efuse);

	if (!status && regulator_is_enabled(reg) != wantstate) {
		/*
		 * We could do
		 *
		 *   if (!wantstate)
		 *     regulator_force_disable(reg);
		 *
		 * here, but it's likely to leave it such that it can't then
		 * be re-enabled, so we'll just report the error and leave it
		 * as it is (and hopefully as long as our enable/disable calls
		 * remain balanced and nobody registers another consumer for
		 * the same supply we won't end up in this situation anyway).
		 */
		dev_err(dev, "regulator_%sable() didn't take effect\n", wantstate ? "en" : "dis");
		status = -EIO;
	}

	return status ? : count;
}

static int efuse_update_error_flags(struct efuse *efuse)
{
	int status = 0;
	unsigned long cache_expiry;

	mutex_lock(&efuse->error_flags.lock);

	cache_expiry = efuse->error_flags.fetch_time + efuse->error_flags.ttl;

	if (!efuse->error_flags.ttl || !efuse->error_flags.fetch_time ||
	    time_after(jiffies, cache_expiry)) {
		status = regulator_get_error_flags(efuse->reg, &efuse->error_flags.cache);
		if (!status)
			efuse->error_flags.fetch_time = jiffies;
	}

	mutex_unlock(&efuse->error_flags.lock);

	return status;
}

static DEVICE_ATTR(operstate, 0644, efuse_show_operstate, efuse_set_operstate);

#define EFUSE_ERROR_ATTR(name, bit)							    \
	static ssize_t efuse_show_##name(struct device *dev, struct device_attribute *attr, \
					 char *buf)                                         \
	{                                                                                   \
		struct efuse *efuse = dev_get_drvdata(dev);                                 \
		int status = efuse_update_error_flags(efuse);                               \
		if (status)                                                                 \
			return status;                                                      \
		return sysfs_emit(buf, "%d\n", !!(efuse->error_flags.cache & bit));         \
	}                                                                                   \
	static DEVICE_ATTR(name, 0444, efuse_show_##name, NULL)

EFUSE_ERROR_ATTR(under_voltage, REGULATOR_ERROR_UNDER_VOLTAGE);
EFUSE_ERROR_ATTR(over_current, REGULATOR_ERROR_OVER_CURRENT);
EFUSE_ERROR_ATTR(regulation_out, REGULATOR_ERROR_REGULATION_OUT);
EFUSE_ERROR_ATTR(fail, REGULATOR_ERROR_FAIL);
EFUSE_ERROR_ATTR(over_temp, REGULATOR_ERROR_OVER_TEMP);
EFUSE_ERROR_ATTR(under_voltage_warn, REGULATOR_ERROR_UNDER_VOLTAGE_WARN);
EFUSE_ERROR_ATTR(over_current_warn, REGULATOR_ERROR_OVER_CURRENT_WARN);
EFUSE_ERROR_ATTR(over_voltage_warn, REGULATOR_ERROR_OVER_VOLTAGE_WARN);
EFUSE_ERROR_ATTR(over_temp_warn, REGULATOR_ERROR_OVER_TEMP_WARN);

static struct attribute *attributes[] = {
	&dev_attr_operstate.attr,
	&dev_attr_under_voltage.attr,
	&dev_attr_over_current.attr,
	&dev_attr_regulation_out.attr,
	&dev_attr_fail.attr,
	&dev_attr_over_temp.attr,
	&dev_attr_under_voltage_warn.attr,
	&dev_attr_over_current_warn.attr,
	&dev_attr_over_voltage_warn.attr,
	&dev_attr_over_temp_warn.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = attributes,
};

static int efuse_probe(struct platform_device *pdev)
{
	int status;
	struct regulator *reg;
	struct efuse *efuse;
	u32 cache_ttl_ms;

	reg = devm_regulator_get(&pdev->dev, "vout");
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	status = regulator_enable(reg);
	if (status) {
		dev_err(&pdev->dev, "failed to enable regulator\n");
		return status;
	}

	efuse = devm_kzalloc(&pdev->dev, sizeof(*efuse), GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	efuse->reg = reg;
	mutex_init(&efuse->error_flags.lock);

	if (!of_property_read_u32(pdev->dev.of_node, "error-flags-cache-ttl-ms", &cache_ttl_ms))
		efuse->error_flags.ttl = msecs_to_jiffies(cache_ttl_ms);

	platform_set_drvdata(pdev, efuse);

	return sysfs_create_group(&pdev->dev.kobj, &attr_group);
}

static int efuse_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &attr_group);
	return 0;
}

static const struct of_device_id efuse_of_match_table[] = {
	{ .compatible = "power-efuse" },
	{ },
};

static struct platform_driver efuse_driver = {
	.driver = {
		.name = "power-efuse",
		.of_match_table = efuse_of_match_table,
	},
	.probe = efuse_probe,
	.remove = efuse_remove,
};
module_platform_driver(efuse_driver);

MODULE_AUTHOR("Zev Weiss <zev@bewilderbeest.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Power efuse driver");
