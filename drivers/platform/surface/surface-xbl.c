// SPDX-License-Identifier: GPL-2.0-only
/*
 * surface-xbl.c - Surface E(x)tensible (B)oot(l)oader
 *
 * Copyright (C) 2021 Microsoft Corporation
 * Author: Jarrett Schultz <jaschultz@microsoft.com>
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/kstrtox.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define SURFACE_XBL_MAX_VERSION_LEN	16
#define SURFACE_XBL_BOARD_ID		0
#define SURFACE_XBL_BATTERY_PRESENT	1
#define SURFACE_XBL_HW_INIT_RETRIES	2
#define SURFACE_XBL_IS_CUSTOMER_MODE	3
#define SURFACE_XBL_IS_ACT_MODE		4
#define SURFACE_XBL_PMIC_RESET_REASON	5
#define SURFACE_XBL_TOUCH_FW_VERSION	6
#define SURFACE_XBL_OCP_ERROR_LOCATION	((SURFACE_XBL_TOUCH_FW_VERSION) +\
					(SURFACE_XBL_MAX_VERSION_LEN))

struct surface_xbl {
	struct device	*dev;
	void __iomem	*regs;

	u8		board_id;
	u8		battery_present;
	u8		hw_init_retries;
	u8		is_customer_mode;
	u8		is_act_mode;
	u8		pmic_reset_reason;
	char		touch_fw_version[SURFACE_XBL_MAX_VERSION_LEN];
	u16		ocp_error_location;
} __packed;

static ssize_t
board_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct surface_xbl	*sxbl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", sxbl->board_id);
}
static DEVICE_ATTR_RO(board_id);

static ssize_t
battery_present_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct surface_xbl	*sxbl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", sxbl->battery_present);
}
static DEVICE_ATTR_RO(battery_present);

static ssize_t
hw_init_retries_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct surface_xbl	*sxbl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", sxbl->hw_init_retries);
}
static DEVICE_ATTR_RO(hw_init_retries);

static ssize_t
is_customer_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct surface_xbl	*sxbl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", sxbl->is_customer_mode);
}
static DEVICE_ATTR_RO(is_customer_mode);

static ssize_t
is_act_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct surface_xbl	*sxbl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", sxbl->is_act_mode);
}
static DEVICE_ATTR_RO(is_act_mode);

static ssize_t
pmic_reset_reason_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct surface_xbl	*sxbl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", sxbl->pmic_reset_reason);
}
static DEVICE_ATTR_RO(pmic_reset_reason);

static ssize_t
touch_fw_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct surface_xbl	*sxbl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%s\n", sxbl->touch_fw_version);
}
static DEVICE_ATTR_RO(touch_fw_version);

static ssize_t
ocp_error_location_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct surface_xbl	*sxbl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", sxbl->ocp_error_location);
}
static DEVICE_ATTR_RO(ocp_error_location);

static struct attribute *inputs_attrs[] = {
	&dev_attr_board_id.attr,
	&dev_attr_battery_present.attr,
	&dev_attr_hw_init_retries.attr,
	&dev_attr_is_customer_mode.attr,
	&dev_attr_is_act_mode.attr,
	&dev_attr_pmic_reset_reason.attr,
	&dev_attr_touch_fw_version.attr,
	&dev_attr_ocp_error_location.attr,
	NULL,
};

static const struct attribute_group inputs_attr_group = {
	.attrs = inputs_attrs,
};

static u8 surface_xbl_readb(void __iomem *base, u32 offset)
{
	return readb(base + offset);
}

static u16 surface_xbl_readw(void __iomem *base, u32 offset)
{
	return readw(base + offset);
}

static int surface_xbl_probe(struct platform_device *pdev)
{
	struct surface_xbl	*sxbl;
	struct device		*dev;
	void __iomem		*regs;
	int					index;
	int					retval;

	dev = &pdev->dev;
	sxbl = devm_kzalloc(dev, sizeof(*sxbl), GFP_KERNEL);
	if (!sxbl)
		return -ENOMEM;

	sxbl->dev = dev;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	sxbl->regs = regs;

	platform_set_drvdata(pdev, sxbl);

	sxbl->board_id = surface_xbl_readb(sxbl->regs,
					   SURFACE_XBL_BOARD_ID);
	sxbl->battery_present = surface_xbl_readb(sxbl->regs,
						  SURFACE_XBL_BATTERY_PRESENT);
	sxbl->hw_init_retries = surface_xbl_readb(sxbl->regs,
						  SURFACE_XBL_HW_INIT_RETRIES);
	sxbl->is_customer_mode = surface_xbl_readb(sxbl->regs,
						   SURFACE_XBL_IS_CUSTOMER_MODE);
	sxbl->is_act_mode = surface_xbl_readb(sxbl->regs,
					      SURFACE_XBL_IS_ACT_MODE);
	sxbl->pmic_reset_reason = surface_xbl_readb(sxbl->regs,
						    SURFACE_XBL_PMIC_RESET_REASON);

	for (index = 0; index < SURFACE_XBL_MAX_VERSION_LEN; index++)
		sxbl->touch_fw_version[index] = surface_xbl_readb(sxbl->regs,
							SURFACE_XBL_TOUCH_FW_VERSION + index);

	sxbl->ocp_error_location = surface_xbl_readw(sxbl->regs,
						     SURFACE_XBL_OCP_ERROR_LOCATION);

	retval = sysfs_create_group(&sxbl->dev->kobj, &inputs_attr_group);
	if (retval < 0) {
		dev_dbg(sxbl->dev,
			"Can't register sysfs attr group: %d\n", retval);
		return retval;
	}

	return 0;
}

static int surface_xbl_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &inputs_attr_group);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id surface_xbl_of_match[] = {
	{
		.compatible = "microsoft,sm8150-surface-duo-xbl"
	},
	{  }, /* Terminating Entry */
};
MODULE_DEVICE_TABLE(of, surface_xbl_of_match);
#endif

static struct platform_driver surface_xbl_driver = {
	.probe		= surface_xbl_probe,
	.remove		= surface_xbl_remove,
	.driver		= {
		.name	= "surface-xbl",
		.of_match_table = of_match_ptr(surface_xbl_of_match),
	},
};

module_platform_driver(surface_xbl_driver);

MODULE_AUTHOR("Jarrett Schultz <jaschultz@microsoft.com>");
MODULE_DESCRIPTION("Surface Extensible Bootloader");
MODULE_LICENSE("GPL");
