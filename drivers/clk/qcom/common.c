// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/clk-provider.h>
#include <linux/reset-controller.h>
#include <linux/of.h>

#include "common.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "reset.h"
#include "gdsc.h"

struct qcom_cc {
	struct qcom_reset_controller reset;
	struct clk_regmap **rclks;
	size_t num_rclks;
};

const
struct freq_tbl *qcom_find_freq(const struct freq_tbl *f, unsigned long rate)
{
	if (!f)
		return NULL;

	if (!f->freq)
		return f;

	for (; f->freq; f++)
		if (rate <= f->freq)
			return f;

	/* Default to our fastest rate */
	return f - 1;
}
EXPORT_SYMBOL_GPL(qcom_find_freq);

const struct freq_tbl *qcom_find_freq_floor(const struct freq_tbl *f,
					    unsigned long rate)
{
	const struct freq_tbl *best = NULL;

	for ( ; f->freq; f++) {
		if (rate >= f->freq)
			best = f;
		else
			break;
	}

	return best;
}
EXPORT_SYMBOL_GPL(qcom_find_freq_floor);

int qcom_find_src_index(struct clk_hw *hw, const struct parent_map *map, u8 src)
{
	int i, num_parents = clk_hw_get_num_parents(hw);

	for (i = 0; i < num_parents; i++)
		if (src == map[i].src)
			return i;

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(qcom_find_src_index);

static void qcom_cc_pm_runtime_disable(void *data)
{
	pm_runtime_disable(data);
}

static void qcom_cc_pm_clk_destroy(void *data)
{
	pm_clk_destroy(data);
}

static int
qcom_cc_add_pm_clks(struct platform_device *pdev, const struct qcom_cc_desc *desc)
{
	struct device *dev = &pdev->dev;
	int ret;
	int i;

	if (!desc->num_pm_clks)
		return 0;

	ret = pm_clk_create(dev);
	if (ret < 0)
		return ret;
	ret = devm_add_action_or_reset(dev, qcom_cc_pm_clk_destroy, dev);
	if (ret)
		return ret;

	for (i = 0; i < desc->num_pm_clks; i++) {
		ret = pm_clk_add(dev, desc->pm_clks[i]);
		if (ret < 0) {
			dev_err(dev, "Failed to acquire %s pm clk\n", desc->pm_clks[i]);
			return ret;
		}
	}

	return 0;
}

static int
qcom_cc_manage_pm(struct platform_device *pdev, const struct qcom_cc_desc *desc)
{
	struct device *dev = &pdev->dev;
	int ret;

	/* For now enable runtime PM only if we have PM clocks in use */
	if (desc->num_pm_clks && !pm_runtime_enabled(dev)) {
		pm_runtime_enable(dev);

		ret = devm_add_action_or_reset(dev, qcom_cc_pm_runtime_disable, dev);
		if (ret)
			return ret;
	}

	ret = qcom_cc_add_pm_clks(pdev, desc);
	if (ret)
		return ret;

	/* Other code might have enabled runtime PM, resume device here */
	if (pm_runtime_enabled(dev)) {
		ret = pm_runtime_get_sync(dev);
		if (ret) {
			pm_runtime_put_noidle(dev);
			return ret;
		}
	}

	return 0;
}

static struct regmap *
qcom_cc_map_by_index(struct platform_device *pdev, const struct qcom_cc_desc *desc, int index)
{
	void __iomem *base;
	struct resource *res;
	struct device *dev = &pdev->dev;
	int ret;

	ret = qcom_cc_manage_pm(pdev, desc);
	if (ret)
		return ERR_PTR(ret);

	res = platform_get_resource(pdev, IORESOURCE_MEM, index);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return ERR_CAST(base);

	return devm_regmap_init_mmio(dev, base, desc->config);
}

struct regmap *
qcom_cc_map(struct platform_device *pdev, const struct qcom_cc_desc *desc)
{
	return qcom_cc_map_by_index(pdev, desc, 0);
}
EXPORT_SYMBOL_GPL(qcom_cc_map);

void
qcom_pll_set_fsm_mode(struct regmap *map, u32 reg, u8 bias_count, u8 lock_count)
{
	u32 val;
	u32 mask;

	/* De-assert reset to FSM */
	regmap_update_bits(map, reg, PLL_VOTE_FSM_RESET, 0);

	/* Program bias count and lock count */
	val = bias_count << PLL_BIAS_COUNT_SHIFT |
		lock_count << PLL_LOCK_COUNT_SHIFT;
	mask = PLL_BIAS_COUNT_MASK << PLL_BIAS_COUNT_SHIFT;
	mask |= PLL_LOCK_COUNT_MASK << PLL_LOCK_COUNT_SHIFT;
	regmap_update_bits(map, reg, mask, val);

	/* Enable PLL FSM voting */
	regmap_update_bits(map, reg, PLL_VOTE_FSM_ENA, PLL_VOTE_FSM_ENA);
}
EXPORT_SYMBOL_GPL(qcom_pll_set_fsm_mode);

static void qcom_cc_gdsc_unregister(void *data)
{
	gdsc_unregister(data);
}

/*
 * Backwards compatibility with old DTs. Register a pass-through factor 1/1
 * clock to translate 'path' clk into 'name' clk and register the 'path'
 * clk as a fixed rate clock if it isn't present.
 */
static int _qcom_cc_register_board_clk(struct device *dev, const char *path,
				       const char *name, unsigned long rate,
				       bool add_factor)
{
	struct device_node *node = NULL;
	struct device_node *clocks_node;
	struct clk_fixed_factor *factor;
	struct clk_fixed_rate *fixed;
	struct clk_init_data init_data = { };
	int ret;

	clocks_node = of_find_node_by_path("/clocks");
	if (clocks_node) {
		node = of_get_child_by_name(clocks_node, path);
		of_node_put(clocks_node);
	}

	if (!node) {
		fixed = devm_kzalloc(dev, sizeof(*fixed), GFP_KERNEL);
		if (!fixed)
			return -EINVAL;

		fixed->fixed_rate = rate;
		fixed->hw.init = &init_data;

		init_data.name = path;
		init_data.ops = &clk_fixed_rate_ops;

		ret = devm_clk_hw_register(dev, &fixed->hw);
		if (ret)
			return ret;
	}
	of_node_put(node);

	if (add_factor) {
		factor = devm_kzalloc(dev, sizeof(*factor), GFP_KERNEL);
		if (!factor)
			return -EINVAL;

		factor->mult = factor->div = 1;
		factor->hw.init = &init_data;

		init_data.name = name;
		init_data.parent_names = &path;
		init_data.num_parents = 1;
		init_data.flags = 0;
		init_data.ops = &clk_fixed_factor_ops;

		ret = devm_clk_hw_register(dev, &factor->hw);
		if (ret)
			return ret;
	}

	return 0;
}

int qcom_cc_register_board_clk(struct device *dev, const char *path,
			       const char *name, unsigned long rate)
{
	bool add_factor = true;

	/*
	 * TODO: The RPM clock driver currently does not support the xo clock.
	 * When xo is added to the RPM clock driver, we should change this
	 * function to skip registration of xo factor clocks.
	 */

	return _qcom_cc_register_board_clk(dev, path, name, rate, add_factor);
}
EXPORT_SYMBOL_GPL(qcom_cc_register_board_clk);

int qcom_cc_register_sleep_clk(struct device *dev)
{
	return _qcom_cc_register_board_clk(dev, "sleep_clk", "sleep_clk_src",
					   32768, true);
}
EXPORT_SYMBOL_GPL(qcom_cc_register_sleep_clk);

/* Drop 'protected-clocks' from the list of clocks to register */
static void qcom_cc_drop_protected(struct device *dev, struct qcom_cc *cc)
{
	struct device_node *np = dev->of_node;
	struct property *prop;
	const __be32 *p;
	u32 i;

	of_property_for_each_u32(np, "protected-clocks", prop, p, i) {
		if (i >= cc->num_rclks)
			continue;

		cc->rclks[i] = NULL;
	}
}

static struct clk_hw *qcom_cc_clk_hw_get(struct of_phandle_args *clkspec,
					 void *data)
{
	struct qcom_cc *cc = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= cc->num_rclks) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return cc->rclks[idx] ? &cc->rclks[idx]->hw : NULL;
}

int qcom_cc_really_probe(struct platform_device *pdev,
			 const struct qcom_cc_desc *desc, struct regmap *regmap)
{
	int i, ret;
	struct device *dev = &pdev->dev;
	struct qcom_reset_controller *reset;
	struct qcom_cc *cc;
	struct gdsc_desc *scd;
	size_t num_clks = desc->num_clks;
	struct clk_regmap **rclks = desc->clks;
	size_t num_clk_hws = desc->num_clk_hws;
	struct clk_hw **clk_hws = desc->clk_hws;

	cc = devm_kzalloc(dev, sizeof(*cc), GFP_KERNEL);
	if (!cc) {
		ret = -ENOMEM;
		goto err;
	}

	reset = &cc->reset;
	reset->rcdev.of_node = dev->of_node;
	reset->rcdev.ops = &qcom_reset_ops;
	reset->rcdev.owner = dev->driver->owner;
	reset->rcdev.nr_resets = desc->num_resets;
	reset->regmap = regmap;
	reset->reset_map = desc->resets;

	ret = devm_reset_controller_register(dev, &reset->rcdev);
	if (ret)
		goto err;

	if (desc->gdscs && desc->num_gdscs) {
		scd = devm_kzalloc(dev, sizeof(*scd), GFP_KERNEL);
		if (!scd) {
			ret = -ENOMEM;
			goto err;
		}

		scd->dev = dev;
		scd->scs = desc->gdscs;
		scd->num = desc->num_gdscs;
		ret = gdsc_register(scd, &reset->rcdev, regmap);
		if (ret)
			goto err;
		ret = devm_add_action_or_reset(dev, qcom_cc_gdsc_unregister,
					       scd);
		if (ret)
			goto err;
	}

	cc->rclks = rclks;
	cc->num_rclks = num_clks;

	qcom_cc_drop_protected(dev, cc);

	for (i = 0; i < num_clk_hws; i++) {
		ret = devm_clk_hw_register(dev, clk_hws[i]);
		if (ret)
			goto err;
	}

	for (i = 0; i < num_clks; i++) {
		if (!rclks[i])
			continue;

		ret = devm_clk_register_regmap(dev, rclks[i]);
		if (ret)
			goto err;
	}

	ret = devm_of_clk_add_hw_provider(dev, qcom_cc_clk_hw_get, cc);
	if (ret)
		goto err;

	if (pm_runtime_enabled(dev)) {
		/* for the LPASS on sc7180, which uses autosuspend */
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put(dev);
	}

	return 0;

err:
	if (pm_runtime_enabled(dev))
		pm_runtime_put(dev);

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_cc_really_probe);

int qcom_cc_probe(struct platform_device *pdev, const struct qcom_cc_desc *desc)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return qcom_cc_really_probe(pdev, desc, regmap);
}
EXPORT_SYMBOL_GPL(qcom_cc_probe);

int qcom_cc_probe_by_index(struct platform_device *pdev, int index,
			   const struct qcom_cc_desc *desc)
{
	struct regmap *regmap;

	regmap = qcom_cc_map_by_index(pdev, desc, index);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return qcom_cc_really_probe(pdev, desc, regmap);
}
EXPORT_SYMBOL_GPL(qcom_cc_probe_by_index);

MODULE_LICENSE("GPL v2");
