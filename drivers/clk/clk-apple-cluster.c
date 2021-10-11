// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple SoC CPU cluster performance state driver
 *
 * Copyright The Asahi Linux Contributors
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>

#define APPLE_CLUSTER_PSTATE    0x20
#define APPLE_CLUSTER_PSTATE_BUSY	BIT(31)
#define APPLE_CLUSTER_PSTATE_SET	BIT(25)
#define APPLE_CLUSTER_PSTATE_DESIRED2	GENMASK(15, 12)
#define APPLE_CLUSTER_PSTATE_DESIRED1	GENMASK(3, 0)

struct apple_cluster_clk {
	struct clk_hw hw;
	struct device *dev;
	void __iomem *reg_base;
	bool has_pd;
};

#define to_apple_cluster_clk(_hw) container_of(_hw, struct apple_cluster_clk, hw)

#define APPLE_CLUSTER_SWITCH_TIMEOUT 100

static int apple_cluster_clk_set_rate(struct clk_hw *hw, unsigned long rate,
					 unsigned long parent_rate)
{
	struct apple_cluster_clk *cluster = to_apple_cluster_clk(hw);
	struct dev_pm_opp *opp;
	unsigned int level;
	u64 reg;

	opp = dev_pm_opp_find_freq_floor(cluster->dev, &rate);

	if (IS_ERR(opp))
		return PTR_ERR(opp);

	level = dev_pm_opp_get_level(opp);

	dev_dbg(cluster->dev, "set_rate: %ld -> %d\n", rate, level);

	if (readq_poll_timeout(cluster->reg_base + APPLE_CLUSTER_PSTATE, reg,
			       !(reg & APPLE_CLUSTER_PSTATE_BUSY), 2,
			       APPLE_CLUSTER_SWITCH_TIMEOUT)) {
		dev_err(cluster->dev, "timed out waiting for busy flag\n");
		return -EIO;
	}

	reg &= ~(APPLE_CLUSTER_PSTATE_DESIRED1 | APPLE_CLUSTER_PSTATE_DESIRED2);
	reg |= FIELD_PREP(APPLE_CLUSTER_PSTATE_DESIRED1, level);
	reg |= FIELD_PREP(APPLE_CLUSTER_PSTATE_DESIRED2, level);
	reg |= APPLE_CLUSTER_PSTATE_SET;

	writeq_relaxed(reg, cluster->reg_base + APPLE_CLUSTER_PSTATE);

	if (cluster->has_pd)
		dev_pm_genpd_set_performance_state(cluster->dev,
						   dev_pm_opp_get_required_pstate(opp, 0));

	return 0;
}

static unsigned long apple_cluster_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct apple_cluster_clk *cluster = to_apple_cluster_clk(hw);
	struct dev_pm_opp *opp;
	u64 reg;

	reg = readq_relaxed(cluster->reg_base + APPLE_CLUSTER_PSTATE);

	opp = dev_pm_opp_find_level_exact(cluster->dev,
					  FIELD_GET(APPLE_CLUSTER_PSTATE_DESIRED1, reg));

	if (IS_ERR(opp)) {
		dev_err(cluster->dev, "failed to find level: 0x%llx (%ld)\n", reg, PTR_ERR(opp));
		return 0;
	}

	return dev_pm_opp_get_freq(opp);
}

static long apple_cluster_clk_round_rate(struct clk_hw *hw, unsigned long rate,
						  unsigned long *parent_rate)
{
	struct apple_cluster_clk *cluster = to_apple_cluster_clk(hw);
	struct dev_pm_opp *opp;

	opp = dev_pm_opp_find_freq_floor(cluster->dev, &rate);

	if (IS_ERR(opp)) {
		dev_err(cluster->dev, "failed to find rate: %ld (%ld)\n", rate, PTR_ERR(opp));
		return PTR_ERR(opp);
	}

	return rate;
}

static const struct clk_ops apple_cluster_clk_ops = {
	.set_rate = apple_cluster_clk_set_rate,
	.recalc_rate = apple_cluster_clk_recalc_rate,
	.round_rate = apple_cluster_clk_round_rate,
};

static int apple_cluster_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct apple_cluster_clk *cluster;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	memset(&init, 0, sizeof(init));
	cluster = devm_kzalloc(dev, sizeof(*cluster), GFP_KERNEL);
	if (!cluster)
		return -ENOMEM;

	cluster->dev = dev;
	cluster->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(cluster->reg_base))
		return PTR_ERR(cluster->reg_base);

	hw = &cluster->hw;
	hw->init = &init;

	init.name = pdev->name;
	init.num_parents = 0;
	init.ops = &apple_cluster_clk_ops;
	init.flags = 0;

	ret = dev_pm_opp_of_add_table_noclk(dev, 0);
	if (ret < 0) {
		dev_err(dev, "failed to get opp table\n");
		return ret;
	}

	cluster->has_pd = of_property_read_bool(node, "power-domains");

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
	if (ret < 0)
		return ret;

	ret = devm_clk_hw_register(dev, hw);
	if (ret) {
		dev_err(dev, "failed to register clock\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id apple_cluster_clk_of_match[] = {
	{ .compatible = "apple,cluster-clk" },
	{}
};

MODULE_DEVICE_TABLE(of, apple_cluster_clk_of_match);

static struct platform_driver apple_cluster_clk_driver = {
	.probe = apple_cluster_clk_probe,
	.driver = {
		.name = "apple-cluster-clk",
		.of_match_table = apple_cluster_clk_of_match,
	},
};

MODULE_AUTHOR("Hector Martin <marcan@marcan.st>");
MODULE_DESCRIPTION("CPU cluster performance state driver for Apple SoCs");
MODULE_LICENSE("GPL v2");

module_platform_driver(apple_cluster_clk_driver);
