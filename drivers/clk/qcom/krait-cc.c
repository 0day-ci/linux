// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>

#include "clk-krait.h"

static unsigned int sec_mux_map[] = {
	2,
	0,
};

static unsigned int pri_mux_map[] = {
	1,
	2,
	0,
};

static u8 krait_get_mux_sel(struct krait_mux_clk *mux, struct clk *safe_clk)
{
	struct clk_hw *safe_hw = __clk_get_hw(safe_clk);

	return clk_hw_get_parent_index(&mux->hw, safe_hw);
}

/*
 * Notifier function for switching the muxes to safe parent
 * while the hfpll is getting reprogrammed.
 */
static int krait_notifier_cb(struct notifier_block *nb,
			     unsigned long event,
			     void *data)
{
	struct krait_mux_clk *mux = container_of(nb, struct krait_mux_clk,
						 clk_nb);
	int ret = 0;

	/* Switch to safe parent */
	if (event == PRE_RATE_CHANGE) {
		mux->old_index = krait_mux_clk_ops.get_parent(&mux->hw);
		ret = krait_mux_clk_ops.set_parent(&mux->hw, mux->safe_sel);
		mux->reparent = false;
	/*
	 * By the time POST_RATE_CHANGE notifier is called,
	 * clk framework itself would have changed the parent for the new rate.
	 * Only otherwise, put back to the old parent.
	 */
	} else if (event == POST_RATE_CHANGE) {
		if (!mux->reparent)
			ret = krait_mux_clk_ops.set_parent(&mux->hw,
							   mux->old_index);
	}

	return notifier_from_errno(ret);
}

static int krait_notifier_register(struct device *dev, struct clk *clk,
				   struct krait_mux_clk *mux)
{
	int ret;

	mux->clk_nb.notifier_call = krait_notifier_cb;
	ret = clk_notifier_register(clk, &mux->clk_nb);
	if (ret)
		dev_err(dev, "failed to register clock notifier: %d\n", ret);

	return ret;
}

static struct clk *
krait_add_div(struct device *dev, int id, const char *s, unsigned int offset)
{
	static struct clk_parent_data p_data[1];
	struct clk_init_data init = {
		.num_parents = ARRAY_SIZE(p_data),
		.ops = &krait_div2_clk_ops,
		.flags = CLK_SET_RATE_PARENT,
	};
	struct krait_div2_clk *div;
	char *parent_name;
	struct clk *clk;

	div = devm_kzalloc(dev, sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	div->width = 2;
	div->shift = 6;
	div->lpl = id >= 0;
	div->offset = offset;
	div->hw.init = &init;

	init.name = kasprintf(GFP_KERNEL, "hfpll%s_div", s);
	if (!init.name)
		return ERR_PTR(-ENOMEM);

	init.parent_data = p_data;
	parent_name = kasprintf(GFP_KERNEL, "hfpll%s", s);
	if (!parent_name) {
		clk = ERR_PTR(-ENOMEM);
		goto err_parent_name;
	}

	p_data[0].fw_name = parent_name;
	p_data[0].name = parent_name;

	clk = devm_clk_register(dev, &div->hw);

	kfree(parent_name);
err_parent_name:
	kfree(init.name);

	return clk;
}

static struct clk *
krait_add_sec_mux(struct device *dev, struct clk *qsb, int id,
		  const char *s, unsigned int offset, bool unique_aux)
{
	static struct clk_parent_data sec_mux_list[2] = {
		{ .name = "qsb", .fw_name = "qsb" },
		{},
	};
	struct clk_init_data init = {
		.parent_data = sec_mux_list,
		.num_parents = ARRAY_SIZE(sec_mux_list),
		.ops = &krait_mux_clk_ops,
		.flags = CLK_SET_RATE_PARENT,
	};
	struct krait_mux_clk *mux;
	char *parent_name;
	struct clk *clk;
	int ret;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	mux->offset = offset;
	mux->lpl = id >= 0;
	mux->mask = 0x3;
	mux->shift = 2;
	mux->parent_map = sec_mux_map;
	mux->hw.init = &init;

	init.name = kasprintf(GFP_KERNEL, "krait%s_sec_mux", s);
	if (!init.name)
		return ERR_PTR(-ENOMEM);

	if (unique_aux) {
		parent_name = kasprintf(GFP_KERNEL, "acpu%s_aux", s);
		if (!parent_name) {
			clk = ERR_PTR(-ENOMEM);
			goto err_aux;
		}
		sec_mux_list[1].fw_name = parent_name;
		sec_mux_list[1].name = parent_name;
	} else {
		sec_mux_list[1].name = "apu_aux";
	}

	clk = devm_clk_register(dev, &mux->hw);
	if (IS_ERR(clk))
		goto err_clk;

	mux->safe_sel = krait_get_mux_sel(mux, qsb);
	ret = krait_notifier_register(dev, clk, mux);
	if (ret)
		clk = ERR_PTR(ret);

err_clk:
	if (unique_aux)
		kfree(parent_name);
err_aux:
	kfree(init.name);
	return clk;
}

static struct clk *
krait_add_pri_mux(struct device *dev, struct clk *hfpll_div, struct clk *sec_mux,
		  int id, const char *s, unsigned int offset)
{
	static struct clk_parent_data p_data[3];
	struct clk_init_data init = {
		.parent_data = p_data,
		.num_parents = ARRAY_SIZE(p_data),
		.ops = &krait_mux_clk_ops,
		.flags = CLK_SET_RATE_PARENT,
	};
	struct krait_mux_clk *mux;
	char *hfpll_name;
	struct clk *clk;
	int ret;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	mux->mask = 0x3;
	mux->shift = 0;
	mux->offset = offset;
	mux->lpl = id >= 0;
	mux->parent_map = pri_mux_map;
	mux->hw.init = &init;

	init.name = kasprintf(GFP_KERNEL, "krait%s_pri_mux", s);
	if (!init.name)
		return ERR_PTR(-ENOMEM);

	hfpll_name = kasprintf(GFP_KERNEL, "hfpll%s", s);
	if (!hfpll_name) {
		clk = ERR_PTR(-ENOMEM);
		goto err_hfpll;
	}

	p_data[0].fw_name = hfpll_name;
	p_data[0].name = hfpll_name;

	p_data[1].hw = __clk_get_hw(hfpll_div);
	p_data[2].hw = __clk_get_hw(sec_mux);

	clk = devm_clk_register(dev, &mux->hw);
	if (IS_ERR(clk))
		goto err_clk;

	mux->safe_sel = krait_get_mux_sel(mux, sec_mux);
	ret = krait_notifier_register(dev, clk, mux);
	if (ret)
		clk = ERR_PTR(ret);

err_clk:
	kfree(hfpll_name);
err_hfpll:
	kfree(init.name);
	return clk;
}

/* id < 0 for L2, otherwise id == physical CPU number */
static struct clk *
krait_add_clks(struct device *dev, struct clk *qsb, int id,
	       bool unique_aux)
{
	struct clk *hfpll_div, *sec_mux, *clk;
	unsigned int offset;
	void *p = NULL;
	const char *s;

	if (id >= 0) {
		offset = 0x4501 + (0x1000 * id);
		s = p = kasprintf(GFP_KERNEL, "%d", id);
		if (!s)
			return ERR_PTR(-ENOMEM);
	} else {
		offset = 0x500;
		s = "_l2";
	}

	hfpll_div = krait_add_div(dev, id, s, offset);
	if (IS_ERR(hfpll_div)) {
		clk = hfpll_div;
		goto err;
	}

	sec_mux = krait_add_sec_mux(dev, qsb, id, s, offset, unique_aux);
	if (IS_ERR(sec_mux)) {
		clk = sec_mux;
		goto err;
	}

	clk = krait_add_pri_mux(dev, hfpll_div, sec_mux, id, s, offset);
err:
	kfree(p);
	return clk;
}

static struct clk *krait_of_get(struct of_phandle_args *clkspec, void *data)
{
	unsigned int idx = clkspec->args[0];
	struct clk **clks = data;

	if (idx >= 5) {
		pr_err("%s: invalid clock index %d\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return clks[idx] ? : ERR_PTR(-ENODEV);
}

static const struct of_device_id krait_cc_match_table[] = {
	{ .compatible = "qcom,krait-cc-v1", (void *)1UL },
	{ .compatible = "qcom,krait-cc-v2" },
	{}
};
MODULE_DEVICE_TABLE(of, krait_cc_match_table);

static int krait_cc_probe(struct platform_device *pdev)
{
	struct clk *l2_pri_mux_clk, *qsb, *clk;
	unsigned long cur_rate, aux_rate;
	struct device *dev = &pdev->dev;
	const struct of_device_id *id;
	struct clk **clks;
	int cpu;

	id = of_match_device(krait_cc_match_table, dev);
	if (!id)
		return -ENODEV;

	/* Rate is 1 because 0 causes problems for __clk_mux_determine_rate */
	qsb = clk_get(dev, "qsb");
	if (IS_ERR(qsb))
		qsb = clk_register_fixed_rate(dev, "qsb", NULL, 0, 1);

	if (IS_ERR(qsb))
		return PTR_ERR(qsb);

	if (!id->data) {
		clk = clk_register_fixed_factor(dev, "acpu_aux",
						"gpll0_vote", 0, 1, 2);
		if (IS_ERR(clk))
			return PTR_ERR(clk);
	}

	/* Krait configurations have at most 4 CPUs and one L2 */
	clks = devm_kcalloc(dev, 5, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		clk = krait_add_clks(dev, qsb, cpu, id->data);
		if (IS_ERR(clk))
			return PTR_ERR(clk);
		clks[cpu] = clk;
	}

	l2_pri_mux_clk = krait_add_clks(dev, qsb, -1, id->data);
	if (IS_ERR(l2_pri_mux_clk))
		return PTR_ERR(l2_pri_mux_clk);
	clks[4] = l2_pri_mux_clk;

	/*
	 * We don't want the CPU or L2 clocks to be turned off at late init
	 * if CPUFREQ or HOTPLUG configs are disabled. So, bump up the
	 * refcount of these clocks. Any cpufreq/hotplug manager can assume
	 * that the clocks have already been prepared and enabled by the time
	 * they take over.
	 */
	for_each_online_cpu(cpu) {
		clk_prepare_enable(l2_pri_mux_clk);
		WARN(clk_prepare_enable(clks[cpu]),
		     "Unable to turn on CPU%d clock", cpu);
	}

	/*
	 * Force reinit of HFPLLs and muxes to overwrite any potential
	 * incorrect configuration of HFPLLs and muxes by the bootloader.
	 * While at it, also make sure the cores are running at known rates
	 * and print the current rate.
	 *
	 * The clocks are set to aux clock rate first to make sure the
	 * secondary mux is not sourcing off of QSB. The rate is then set to
	 * two different rates to force a HFPLL reinit under all
	 * circumstances.
	 */
	cur_rate = clk_get_rate(l2_pri_mux_clk);
	aux_rate = 384000000;
	if (cur_rate == 1) {
		dev_info(dev, "L2 @ QSB rate. Forcing new rate.\n");
		cur_rate = aux_rate;
	}
	clk_set_rate(l2_pri_mux_clk, aux_rate);
	clk_set_rate(l2_pri_mux_clk, 2);
	clk_set_rate(l2_pri_mux_clk, cur_rate);
	dev_info(dev, "L2 @ %lu KHz\n", clk_get_rate(l2_pri_mux_clk) / 1000);
	for_each_possible_cpu(cpu) {
		clk = clks[cpu];
		cur_rate = clk_get_rate(clk);
		if (cur_rate == 1) {
			dev_info(dev, "CPU%d @ QSB rate. Forcing new rate.\n", cpu);
			cur_rate = aux_rate;
		}

		clk_set_rate(clk, aux_rate);
		clk_set_rate(clk, 2);
		clk_set_rate(clk, cur_rate);
		dev_info(dev, "CPU%d @ %lu KHz\n", cpu, clk_get_rate(clk) / 1000);
	}

	of_clk_add_provider(dev->of_node, krait_of_get, clks);

	return 0;
}

static struct platform_driver krait_cc_driver = {
	.probe = krait_cc_probe,
	.driver = {
		.name = "krait-cc",
		.of_match_table = krait_cc_match_table,
	},
};
module_platform_driver(krait_cc_driver);

MODULE_DESCRIPTION("Krait CPU Clock Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:krait-cc");
