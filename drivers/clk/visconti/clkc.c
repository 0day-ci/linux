// SPDX-License-Identifier: GPL-2.0-only
/*
 * Toshiba Visconti clock controller
 *
 * Copyright (c) 2021 TOSHIBA CORPORATION
 * Copyright (c) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "clkc.h"

static inline struct visconti_clk_gate *to_visconti_clk_gate(struct clk_hw *hw)
{
	return container_of(hw, struct visconti_clk_gate, hw);
}

static int visconti_gate_clk_is_enabled(struct clk_hw *hw)
{
	struct visconti_clk_gate *gate = to_visconti_clk_gate(hw);
	u32 clk = BIT(gate->ck_idx);
	u32 val;

	regmap_read(gate->regmap, gate->ckon_offset, &val);
	return (val & clk) ? 1 : 0;
}

static void visconti_gate_clk_disable(struct clk_hw *hw)
{
	struct visconti_clk_gate *gate = to_visconti_clk_gate(hw);
	u32 clk = BIT(gate->ck_idx);
	u32 rst = BIT(gate->rs_idx);
	unsigned long flags;

	spin_lock_irqsave(gate->lock, flags);

	if (visconti_gate_clk_is_enabled(hw)) {
		spin_unlock_irqrestore(gate->lock, flags);
		return;
	}

	/* Reset release */
	regmap_update_bits(gate->regmap, gate->rson_offset, rst, 1);

	udelay(100);

	/* Disable clock */
	regmap_update_bits(gate->regmap, gate->ckoff_offset, clk, 1);
	spin_unlock_irqrestore(gate->lock, flags);
}

static int visconti_gate_clk_enable(struct clk_hw *hw)
{
	struct visconti_clk_gate *gate = to_visconti_clk_gate(hw);
	u32 clk = BIT(gate->ck_idx);
	u32 rst = BIT(gate->rs_idx);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(gate->lock, flags);

	if (visconti_gate_clk_is_enabled(hw)) {
		spin_unlock_irqrestore(gate->lock, flags);
		return 0;
	}

	regmap_update_bits(gate->regmap, gate->ckon_offset, clk, 1);
	/* Need read back */
	regmap_read(gate->regmap, gate->ckon_offset, &val);

	udelay(100);
	/* Reset release */
	regmap_update_bits(gate->regmap, gate->rsoff_offset, rst, 1);
	/* Need read back */
	regmap_read(gate->regmap, gate->ckoff_offset, &val);
	spin_unlock_irqrestore(gate->lock, flags);

	return 0;
}

static const struct clk_ops visconti_clk_gate_ops = {
	.enable = visconti_gate_clk_enable,
	.disable = visconti_gate_clk_disable,
	.is_enabled = visconti_gate_clk_is_enabled,
};

static struct clk_hw *visconti_clk_register_gate(struct device *dev,
						 const char *name,
						 const char *parent_name,
						 struct regmap *regmap,
						 const struct visconti_clk_gate_table *clks,
						 u32	rson_offset,
						 u32	rsoff_offset,
						 u8	rs_idx,
						 spinlock_t *lock)
{
	struct visconti_clk_gate *gate;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &visconti_clk_gate_ops;
	init.flags = clks->flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	gate->regmap = regmap;
	gate->ckon_offset = clks->ckon_offset;
	gate->ckoff_offset = clks->ckoff_offset;
	gate->ck_idx = clks->ck_idx;
	gate->rson_offset = rson_offset;
	gate->rsoff_offset = rsoff_offset;
	gate->rs_idx = rs_idx;
	gate->lock = lock;
	gate->hw.init = &init;

	hw = &gate->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(gate);
		hw = ERR_PTR(ret);
	}

	return hw;
}

int visconti_clk_register_gates(struct visconti_clk_provider *data,
				 const struct visconti_clk_gate_table *clks,
				 int num_gate, const struct visconti_reset_data *reset,
				 spinlock_t *lock)
{
	u32 rson_offset, rsoff_offset;
	struct clk_hw *hw_clk;
	u8 rs_idx;
	int i;

	for (i = 0; i < num_gate; i++) {
		struct clk *clk;
		char *div_name;

		div_name = kasprintf(GFP_KERNEL, "%s_div", clks[i].name);
		if (!div_name)
			return -ENOMEM;

		if (clks[i].rs_id >= 0) {
			rson_offset = reset[clks[i].rs_id].rson_offset;
			rsoff_offset = reset[clks[i].rs_id].rsoff_offset;
			rs_idx = reset[clks[i].rs_id].rs_idx;
		} else {
			rson_offset = rsoff_offset = rs_idx = -1;
		}

		clk = clk_register_fixed_factor(NULL, div_name, clks[i].parent,
						0, 1, clks[i].div);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		hw_clk = visconti_clk_register_gate(NULL,
						clks[i].name,
						div_name,
						data->regmap,
						&clks[i],
						rson_offset,
						rsoff_offset,
						rs_idx,
						lock);
		kfree(div_name);
		if (IS_ERR(hw_clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			return PTR_ERR(hw_clk);
		}

		data->clk_data.clks[clks[i].id] = hw_clk->clk;
	}

	return 0;
}

struct visconti_clk_provider *visconti_init_clk(struct device_node *np,
						struct regmap *regmap,
						unsigned long nr_clks)
{
	struct visconti_clk_provider *ctx;
	struct clk **clk_table;
	int i;

	ctx = kzalloc(sizeof(struct visconti_clk_provider), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	clk_table = kcalloc(nr_clks, sizeof(struct clk *), GFP_KERNEL);
	if (!clk_table)
		goto err;

	for (i = 0; i < nr_clks; ++i)
		clk_table[i] = ERR_PTR(-ENOENT);
	ctx->node = np;
	ctx->regmap = regmap;
	ctx->clk_data.clks = clk_table;
	ctx->clk_data.clk_num = nr_clks;

	return ctx;
err:
	kfree(ctx);
	return ERR_PTR(-ENOMEM);
}
