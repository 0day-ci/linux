// SPDX-License-Identifier: GPL-2.0+
/*
 * Ampere Computing SoC's SMpro Misc Driver
 *
 * Copyright (c) 2021, Ampere Computing LLC
 */
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* Boot Stage/Progress Registers */
#define BOOTSTAGE_SELECT	0xB0
#define BOOTSTAGE_STATUS_LO	0xB1
#define BOOTSTAGE_CUR_STAGE	0xB2
#define BOOTSTAGE_STATUS_HI	0xB3

/* SOC State Registers */
#define SOC_POWER_LIMIT		0xE5

/* Boot stages */
enum {
	BOOTSTAGE_SMPRO = 0,
	BOOTSTAGE_PMPRO,
	BOOTSTAGE_ATF_BL1,
	BOOTSTAGE_DDR_INIT,
	BOOTSTAGE_DDR_INIT_PROGRESS,
	BOOTSTAGE_ATF_BL2,
	BOOTSTAGE_ATF_BL31,
	BOOTSTAGE_ATF_BL32,
	BOOTSTAGE_UEFI,
	BOOTSTAGE_OS,
	BOOTSTAGE_MAX
};

struct smpro_misc {
	struct regmap *regmap;
};

static int boot_progress_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct smpro_misc *misc = dev_get_drvdata(dev);
	u32 boot_progress;
	u8 current_stage;
	u8 boot_status;
	u8 boot_stage;
	u32 select;
	u32 reg_lo;
	u32 reg;
	int ret;

	/* Read current boot stage */
	ret = regmap_read(misc->regmap, BOOTSTAGE_CUR_STAGE, &reg);
	if (ret)
		return ret;

	current_stage = reg & 0xff;

	/* Read the boot progress */
	ret = regmap_read(misc->regmap, BOOTSTAGE_SELECT, &select);
	if (ret)
		return ret;

	boot_stage = (select >> 8) & 0xff;
	boot_status = select & 0xff;

	if (boot_stage > current_stage)
		return -EINVAL;

	ret = regmap_read(misc->regmap,	BOOTSTAGE_STATUS_LO, &reg_lo);
	if (!ret)
		ret = regmap_read(misc->regmap, BOOTSTAGE_STATUS_HI, &reg);
	if (ret)
		return ret;

	boot_progress = swab16(reg) << 16 | swab16(reg_lo);

	/* Tell firmware to provide next boot stage next time */
	if (boot_stage < current_stage) {
		ret = regmap_write(misc->regmap, BOOTSTAGE_SELECT, ((select & 0xff00) | 0x1));
		if (ret)
			return ret;
	}

	return snprintf(buf, PAGE_SIZE, "0x%02x 0x%02x 0x%08x\n",
			boot_stage, boot_status, boot_progress);
}

static DEVICE_ATTR_RO(boot_progress);

static int soc_power_limit_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct smpro_misc *misc = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(misc->regmap, SOC_POWER_LIMIT, &value);
	if (ret)
		return ret;

	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static int soc_power_limit_store(struct device *dev, struct device_attribute *da,
				 const char *buf, size_t count)
{
	struct smpro_misc *misc = dev_get_drvdata(dev);
	unsigned long val;
	s32 ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	ret = regmap_write(misc->regmap, SOC_POWER_LIMIT, (unsigned int)val);
	if (ret)
		return -EPROTO;

	return count;
}

static DEVICE_ATTR_RW(soc_power_limit);

static struct attribute *smpro_misc_attrs[] = {
	&dev_attr_boot_progress.attr,
	&dev_attr_soc_power_limit.attr,
	NULL
};

static const struct attribute_group smpro_misc_attr_group = {
	.attrs = smpro_misc_attrs
};

static int smpro_misc_probe(struct platform_device *pdev)
{
	struct smpro_misc *misc;
	int ret;

	misc = devm_kzalloc(&pdev->dev, sizeof(struct smpro_misc), GFP_KERNEL);
	if (!misc)
		return -ENOMEM;

	platform_set_drvdata(pdev, misc);

	misc->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!misc->regmap)
		return -ENODEV;

	ret = sysfs_create_group(&pdev->dev.kobj, &smpro_misc_attr_group);
	if (ret)
		dev_err(&pdev->dev, "SMPro misc sysfs registration failed\n");

	return 0;
}

static int smpro_misc_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &smpro_misc_attr_group);
	pr_info("SMPro misc sysfs entries removed");

	return 0;
}

static struct platform_driver smpro_misc_driver = {
	.probe		= smpro_misc_probe,
	.remove		= smpro_misc_remove,
	.driver = {
		.name	= "smpro-misc",
	},
};

module_platform_driver(smpro_misc_driver);

MODULE_AUTHOR("Tung Nguyen <tungnguyen@os.amperecomputing.com>");
MODULE_AUTHOR("Quan Nguyen <quan@os.amperecomputing.com>");
MODULE_DESCRIPTION("Ampere Altra SMpro Misc driver");
MODULE_LICENSE("GPL");
