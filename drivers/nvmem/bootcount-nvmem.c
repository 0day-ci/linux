// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Vaisala Oyj. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Default magic values from u-boot bootcount drivers */
#define BOOTCOUNT_NVMEM_DEFAULT_MAGIC_VAL16 0xBC00
#define BOOTCOUNT_NVMEM_DEFAULT_MAGIC_VAL32 0xB001C041

struct bootcount_nvmem {
	struct nvmem_cell *nvmem;
	u32 magic;
	u32 mask;
	size_t bytes_count;
};

static ssize_t value_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct bootcount_nvmem *bootcount = dev_get_drvdata(dev);
	u32 regval;
	int ret;

	ret = kstrtou32(buf, 0, &regval);
	if (ret < 0)
		return ret;

	/* Check if the value fits */
	if ((regval & ~(bootcount->mask)) != 0)
		return -EINVAL;

	/*
	 * In case we use 2 bytes for saving the value we need to take
	 * in consideration the endianness of the system. Because of this
	 * we mirror the 2 bytes from one side to another.
	 * This way, regardless of endianness, the value will be written
	 * in the correct order.
	 */
	if (bootcount->bytes_count == 2) {
		regval &= 0xffff;
		regval |= (regval & 0xffff) << 16;
	}

	regval = (~bootcount->mask & bootcount->magic) |
		 (regval & bootcount->mask);
	ret = nvmem_cell_write(bootcount->nvmem, &regval,
			       bootcount->bytes_count);
	if (ret < 0)
		return ret;
	else if (ret != bootcount->bytes_count)
		ret = -EIO;
	else
		ret = count;

	return ret;
}

static ssize_t value_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct bootcount_nvmem *bootcount = dev_get_drvdata(dev);
	u32 regval;
	void *val;
	size_t len;
	int ret;

	val = nvmem_cell_read(bootcount->nvmem, &len);
	if (IS_ERR(val))
		return PTR_ERR(val);

	if (len != bootcount->bytes_count) {
		kfree(val);
		return -EINVAL;
	}

	if (bootcount->bytes_count == 2)
		regval = *(u16 *)val;
	else
		regval = *(u32 *)val;

	kfree(val);

	if ((regval & ~bootcount->mask) == bootcount->magic)
		ret = scnprintf(buf, PAGE_SIZE, "%u\n",
				(unsigned int)(regval & bootcount->mask));
	else {
		dev_warn(dev, "invalid magic value\n");
		ret = -EINVAL;
	}

	return ret;
}

static DEVICE_ATTR_RW(value);

static int bootcount_nvmem_probe(struct platform_device *pdev)
{
	struct bootcount_nvmem *bootcount;
	int ret;
	u32 bits;
	void *val = NULL;
	size_t len;

	bootcount = devm_kzalloc(&pdev->dev, sizeof(struct bootcount_nvmem),
				 GFP_KERNEL);
	if (!bootcount)
		return -ENOMEM;

	bootcount->nvmem = devm_nvmem_cell_get(&pdev->dev, "bootcount-regs");
	if (IS_ERR(bootcount->nvmem)) {
		if (PTR_ERR(bootcount->nvmem) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "cannot get 'bootcount-regs'\n");
		return PTR_ERR(bootcount->nvmem);
	}

	/* detect cell dimensions */
	val = nvmem_cell_read(bootcount->nvmem, &len);
	if (IS_ERR(val))
		return PTR_ERR(val);
	kfree(val);
	val = NULL;

	if (len != 2 && len != 4) {
		dev_err(&pdev->dev, "unsupported register size\n");
		return -EINVAL;
	}

	bootcount->bytes_count = len;

	platform_set_drvdata(pdev, bootcount);

	ret = device_create_file(&pdev->dev, &dev_attr_value);
	if (ret) {
		dev_err(&pdev->dev, "failed to export bootcount value\n");
		return ret;
	}

	bits = bootcount->bytes_count << 3;
	bootcount->mask = GENMASK((bits >> 1) - 1, 0);

	ret = of_property_read_u32(pdev->dev.of_node, "linux,bootcount-magic",
				   &bootcount->magic);
	if (ret == -EINVAL) {
		if (bootcount->bytes_count == 2)
			bootcount->magic = BOOTCOUNT_NVMEM_DEFAULT_MAGIC_VAL16;
		else
			bootcount->magic = BOOTCOUNT_NVMEM_DEFAULT_MAGIC_VAL32;
		ret = 0;
	} else if (ret) {
		dev_err(&pdev->dev,
			"failed to parse linux,bootcount-magic, error: %d\n",
			ret);
		return ret;
	}

	bootcount->magic &= ~bootcount->mask;

	return ret;
}

static int bootcount_nvmem_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_value);

	return 0;
}

static const struct of_device_id bootcount_nvmem_match[] = {
	{ .compatible = "linux,bootcount-nvmem" },
	{},
};

static struct platform_driver bootcount_nvmem_driver = {
	.driver = {
		.name = "bootcount-nvmem",
		.of_match_table = bootcount_nvmem_match,
	},
	.probe = bootcount_nvmem_probe,
	.remove = bootcount_nvmem_remove,
};

module_platform_driver(bootcount_nvmem_driver);

MODULE_DEVICE_TABLE(of, bootcount_nvmem_match);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Vaisala Oyj");
MODULE_DESCRIPTION("Bootcount driver using nvmem compatible registers");
