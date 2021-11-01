// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * SP7021 reset driver
 *
 * Copyright (C) Sunplus Technology Co., Ltd.
 *       All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/reboot.h>

#if defined(CONFIG_SOC_SP7021)
#include <dt-bindings/reset/sp-sp7021.h>
#elif defined(CONFIG_SOC_Q645)
#include <dt-bindings/reset/sp-q645.h>
#endif

#define BITASSERT(id, val)          ((1 << (16 + id)) | (val << id))


struct sp_reset_data {
	struct reset_controller_dev	rcdev;
	void __iomem			*membase;
} sp_reset;


static inline struct sp_reset_data *
to_sp_reset_data(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct sp_reset_data, rcdev);
}

static int sp_reset_update(struct reset_controller_dev *rcdev,
			      unsigned long id, bool assert)
{
	struct sp_reset_data *data = to_sp_reset_data(rcdev);
	int reg_width = sizeof(u32)/2;
	int bank = id / (reg_width * BITS_PER_BYTE);
	int offset = id % (reg_width * BITS_PER_BYTE);
	void __iomem *addr;

	addr = data->membase + (bank * 4);

	if (assert)
		writel(BITASSERT(offset, 1), addr);
	else
		writel(BITASSERT(offset, 0), addr);

	return 0;
}

static int sp_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	return sp_reset_update(rcdev, id, true);
}


static int sp_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return sp_reset_update(rcdev, id, false);
}

static int sp_reset_status(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct sp_reset_data *data = to_sp_reset_data(rcdev);
	int reg_width = sizeof(u32)/2;
	int bank = id / (reg_width * BITS_PER_BYTE);
	int offset = id % (reg_width * BITS_PER_BYTE);
	u32 reg;

	reg = readl(data->membase + (bank * 4));

	return !!(reg & BIT(offset));
}

static int sp_restart(struct notifier_block *this, unsigned long mode,
				void *cmd)
{
	sp_reset_assert(&sp_reset.rcdev, RST_SYSTEM);
	sp_reset_deassert(&sp_reset.rcdev, RST_SYSTEM);

	return NOTIFY_DONE;
}

static struct notifier_block sp_restart_nb = {
	.notifier_call = sp_restart,
	.priority = 192,
};

static const struct reset_control_ops sp_reset_ops = {
	.assert		= sp_reset_assert,
	.deassert	= sp_reset_deassert,
	.status		= sp_reset_status,
};

static const struct of_device_id sp_reset_dt_ids[] = {
	{ .compatible = "sunplus,sp7021-reset", },
	{ .compatible = "sunplus,q645-reset", },
	{ /* sentinel */ },
};

static int sp_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sp_reset_data *data = &sp_reset;
	void __iomem *membase;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	membase = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(membase))
		return PTR_ERR(membase);

	data->membase = membase;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = RST_MAX;
	data->rcdev.ops = &sp_reset_ops;
	data->rcdev.of_node = dev->of_node;
	register_restart_handler(&sp_restart_nb);

	return devm_reset_controller_register(dev, &data->rcdev);
}

static struct platform_driver sp_reset_driver = {
	.probe	= sp_reset_probe,
	.driver = {
		.name = "sunplus-reset",
		.of_match_table	= sp_reset_dt_ids,
	},
};

static int __init sp_reset_init(void)
{
	return platform_driver_register(&sp_reset_driver);
}
arch_initcall(sp_reset_init);

MODULE_AUTHOR("Edwin Chiu <edwin.chiu@sunplus.com>");
MODULE_DESCRIPTION("Sunplus Reset Driver");
MODULE_LICENSE("GPL");
