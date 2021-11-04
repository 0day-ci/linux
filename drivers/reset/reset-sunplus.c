// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * SP7021 reset driver
 *
 * Copyright (C) Sunplus Technology Co., Ltd.
 *       All rights reserved.
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

/* HIWORD_MASK */
#define BITASSERT(id, val)	((1 << (16 + id)) | (val << id))

struct sp_reset_data {
	struct reset_controller_dev	rcdev;
	void __iomem *membase;
} *sp_reset;

static inline struct sp_reset_data *
to_sp_reset_data(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct sp_reset_data, rcdev);
}

static int sp_reset_update(struct reset_controller_dev *rcdev,
			      unsigned long id, bool assert)
{
	struct sp_reset_data *data = to_sp_reset_data(rcdev);
	int reg_width = sizeof(u32) / 2;
	int bank = id / (reg_width * BITS_PER_BYTE);
	int offset = id % (reg_width * BITS_PER_BYTE);
	void __iomem *addr = data->membase + (bank * 4);

	writel(BITASSERT(offset, assert), addr);

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
	int reg_width = sizeof(u32) / 2;
	int bank = id / (reg_width * BITS_PER_BYTE);
	int offset = id % (reg_width * BITS_PER_BYTE);
	u32 reg;

	reg = readl(data->membase + (bank * 4));

	return !!(reg & BIT(offset));
}

static int sp_restart(struct notifier_block *this, unsigned long mode,
				void *cmd)
{
	sp_reset_assert(&sp_reset->rcdev, 0);
	sp_reset_deassert(&sp_reset->rcdev, 0);

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
	{ /* sentinel */ },
};

static int sp_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *membase;
	struct resource *res;

	sp_reset = devm_kzalloc(&pdev->dev, sizeof(*sp_reset), GFP_KERNEL);
	if (!sp_reset)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	membase = devm_ioremap_resource(dev, res);
	if (IS_ERR(membase))
		return PTR_ERR(membase);

	sp_reset->membase = membase;
	sp_reset->rcdev.owner = THIS_MODULE;
	sp_reset->rcdev.nr_resets = resource_size(res) / 4 * 16; /* HIWORD_MASK */
	sp_reset->rcdev.ops = &sp_reset_ops;
	sp_reset->rcdev.of_node = dev->of_node;
	register_restart_handler(&sp_restart_nb);

	return devm_reset_controller_register(dev, &sp_reset->rcdev);
}

static struct platform_driver sp_reset_driver = {
	.probe	= sp_reset_probe,
	.driver = {
		.name = "sunplus-reset",
		.of_match_table	= sp_reset_dt_ids,
	},
};
module_platform_driver(sp_reset_driver);

MODULE_AUTHOR("Edwin Chiu <edwin.chiu@sunplus.com>");
MODULE_DESCRIPTION("Sunplus Reset Driver");
MODULE_LICENSE("GPL v2");
