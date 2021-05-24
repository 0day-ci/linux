// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple SoC clock/power gating driver
 *
 * Copyright The Asahi Linux Contributors
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/module.h>

#define CLOCK_TARGET_MODE_MASK 0x0f
#define CLOCK_TARGET_MODE(m) (((m)&0xf))
#define CLOCK_ACTUAL_MODE_MASK 0xf0
#define CLOCK_ACTUAL_MODE(m) (((m)&0xf) << 4)

#define CLOCK_MODE_ENABLE 0xf
#define CLOCK_MODE_DISABLE 0

#define CLOCK_ENDISABLE_TIMEOUT 100

struct apple_clk_gate {
	struct clk_hw hw;
	void __iomem *reg;
};

#define to_apple_clk_gate(_hw) container_of(_hw, struct apple_clk_gate, hw)

static int apple_clk_gate_endisable(struct clk_hw *hw, int enable)
{
	struct apple_clk_gate *gate = to_apple_clk_gate(hw);
	u32 reg;
	u32 mode;

	if (enable)
		mode = CLOCK_MODE_ENABLE;
	else
		mode = CLOCK_MODE_DISABLE;

	reg = readl(gate->reg);
	reg &= ~CLOCK_TARGET_MODE_MASK;
	reg |= CLOCK_TARGET_MODE(mode);
	writel(reg, gate->reg);

	return readl_poll_timeout_atomic(gate->reg, reg,
					 (reg & CLOCK_ACTUAL_MODE_MASK) ==
						 CLOCK_ACTUAL_MODE(mode),
					 1, CLOCK_ENDISABLE_TIMEOUT);
}

static int apple_clk_gate_enable(struct clk_hw *hw)
{
	return apple_clk_gate_endisable(hw, 1);
}

static void apple_clk_gate_disable(struct clk_hw *hw)
{
	apple_clk_gate_endisable(hw, 0);
}

static int apple_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct apple_clk_gate *gate = to_apple_clk_gate(hw);
	u32 reg;

	reg = readl(gate->reg);

	if ((reg & CLOCK_ACTUAL_MODE_MASK) == CLOCK_ACTUAL_MODE(CLOCK_MODE_ENABLE))
		return 1;
	return 0;
}

static const struct clk_ops apple_clk_gate_ops = {
	.enable = apple_clk_gate_enable,
	.disable = apple_clk_gate_disable,
	.is_enabled = apple_clk_gate_is_enabled,
};

static int apple_gate_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	const struct clk_parent_data parent_data[] = {
		{ .index = 0 },
	};
	struct apple_clk_gate *data;
	struct clk_hw *hw;
	struct clk_init_data init;
	struct resource *res;
	int num_parents;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->reg))
		return PTR_ERR(data->reg);

	num_parents = of_clk_get_parent_count(node);
	if (num_parents > 1) {
		dev_err(dev, "clock supports at most one parent\n");
		return -EINVAL;
	}

	init.name = dev->of_node->name;
	init.ops = &apple_clk_gate_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.parent_data = parent_data;
	init.num_parents = num_parents;

	data->hw.init = &init;
	hw = &data->hw;

	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
}

static const struct of_device_id apple_gate_clk_of_match[] = {
	{ .compatible = "apple,t8103-gate-clock" },
	{ .compatible = "apple,gate-clock" },
	{}
};

MODULE_DEVICE_TABLE(of, apple_gate_clk_of_match);

static struct platform_driver apple_gate_clkdriver = {
	.probe = apple_gate_clk_probe,
	.driver = {
		.name = "apple-gate-clock",
		.of_match_table = apple_gate_clk_of_match,
	},
};

MODULE_AUTHOR("Sven Peter <sven@svenpeter.dev>");
MODULE_DESCRIPTION("Clock gating driver for Apple SoCs");
MODULE_LICENSE("GPL v2");

module_platform_driver(apple_gate_clkdriver);
