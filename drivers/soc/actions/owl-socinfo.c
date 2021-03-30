// SPDX-License-Identifier: GPL-2.0+
/*
 * Actions Semi Owl SoC information driver
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/soc/actions/owl-serial-number.h>
#include <linux/sys_soc.h>

struct owl_soc_serial_rmem {
	u32 low;
	u32 high;
};

static struct owl_soc_serial_rmem *owl_socinfo_serial;

u32 owl_get_soc_serial_low(void)
{
	return owl_socinfo_serial ? owl_socinfo_serial->low : 0;
}
EXPORT_SYMBOL_GPL(owl_get_soc_serial_low);

u32 owl_get_soc_serial_high(void)
{
	return owl_socinfo_serial ? owl_socinfo_serial->high : 0;
}
EXPORT_SYMBOL_GPL(owl_get_soc_serial_high);

struct owl_soc_info {
	char *name;
	int (*read_soc_serial)(struct device *dev);
};

/*
 * Access SoC's serial number stored by the bootloader in DDR memory.
 */
static int owl_socinfo_read_serial_rmem(struct device *dev)
{
	struct reserved_mem *rmem;
	struct device_node *np;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, "actions,owl-soc-serial");
	if (!np)
		return -ENXIO;

	rmem = of_reserved_mem_lookup(np);
	if (!rmem) {
		dev_err(dev, "failed to acquire reserved memory region\n");
		ret = -EINVAL;
		goto out_put;
	}

	owl_socinfo_serial = memremap(rmem->base, rmem->size, MEMREMAP_WB);
	if (!owl_socinfo_serial)
		ret = -ENOMEM;

out_put:
	of_node_put(np);
	return ret;
}

static int owl_socinfo_probe(struct platform_device *pdev)
{
	const struct owl_soc_info *soc_info;
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;
	const char *sn;
	int ret;

	soc_info = of_device_get_match_data(&pdev->dev);
	if (!soc_info)
		return -ENODEV;

	soc_dev_attr = devm_kzalloc(&pdev->dev, sizeof(*soc_dev_attr),
				    GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	ret = of_property_read_string(of_root, "model", &soc_dev_attr->machine);
	if (ret)
		return ret;

	soc_dev_attr->family = "Actions Semi Owl";
	soc_dev_attr->soc_id = soc_info->name;

	if (soc_info->read_soc_serial) {
		ret = soc_info->read_soc_serial(&pdev->dev);

		if (!ret) {
			sn = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%08x%08x",
					    owl_get_soc_serial_high(),
					    owl_get_soc_serial_low());
			soc_dev_attr->serial_number = sn;
			/* Feed the SoC unique data into entropy pool. */
			add_device_randomness(sn, 16);
		}
	}

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev))
		return dev_err_probe(&pdev->dev, PTR_ERR(soc_dev),
				     "failed to register soc device");

	dev_info(soc_device_to_device(soc_dev),
		 "SoC: %s %s\n",
		 soc_dev_attr->family, soc_dev_attr->soc_id);

	return 0;
}

static const struct owl_soc_info s500_soc_info = {
	.name = "S500",
	.read_soc_serial = owl_socinfo_read_serial_rmem,
};

static const struct owl_soc_info s700_soc_info = {
	.name = "S700",
	/* FIXME: provide read_soc_serial */
};

static const struct owl_soc_info s900_soc_info = {
	.name = "S900",
	/* FIXME: provide read_soc_serial */
};

static const struct of_device_id owl_socinfo_of_match[] = {
	{ .compatible = "actions,s500-soc", .data = &s500_soc_info, },
	{ .compatible = "actions,s700-soc", .data = &s700_soc_info, },
	{ .compatible = "actions,s900-soc", .data = &s900_soc_info, },
	{ }
};

static struct platform_driver owl_socinfo_platform_driver = {
	.probe = owl_socinfo_probe,
	.driver = {
		.name = "owl-socinfo",
		.of_match_table = owl_socinfo_of_match,
	},
};

static int __init owl_socinfo_init(void)
{
	return platform_driver_register(&owl_socinfo_platform_driver);
}
subsys_initcall(owl_socinfo_init);
