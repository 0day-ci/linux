// SPDX-License-Identifier: GPL-2.0-only
/*
 * Toshiba Visconti PLL driver
 *
 * Copyright (c) 2021 TOSHIBA CORPORATION
 * Copyright (c) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/io.h>

#include "pll.h"

struct visconti_pll {
	struct clk_hw	hw;
	void __iomem	*pll_base;
	spinlock_t	*lock;
	int rate_count;
	unsigned long flags;
	const struct visconti_pll_rate_table *rate_table;
	struct visconti_pll_provider *ctx;
};

#define PLL_CONF_REG		0x0000
#define PLL_CTRL_REG		0x0004
#define PLL_FRACMODE_REG	0x0010
#define PLL_INTIN_REG		0x0014
#define PLL_FRACIN_REG		0x0018
#define PLL_REFDIV_REG		0x001C
#define PLL_POSTDIV_REG		0x0020

#define PLL_CONFIG_SEL		BIT(0)
#define PLL_PLLEN		BIT(4)
#define PLL_BYPASS		BIT(16)
#define PLL_INTIN_MASK		GENMASK(11, 0)
#define PLL_FRACIN_MASK		GENMASK(23, 0)
#define PLL_REFDIV_MASK		GENMASK(5, 0)
#define PLL_POSTDIV_MASK	GENMASK(2, 0)

static inline struct visconti_pll *to_visconti_pll(struct clk_hw *hw)
{
	return container_of(hw, struct visconti_pll, hw);
}

static void visconti_pll_get_params(struct visconti_pll *pll,
				    struct visconti_pll_rate_table *rate_table)
{
	u32 postdiv, val;

	val = readl(pll->pll_base + PLL_FRACMODE_REG);
	rate_table->dacen = (val >> 4) & 0x1;
	rate_table->dsmen = val & 0x1;
	rate_table->fracin = readl(pll->pll_base + PLL_FRACIN_REG) & PLL_FRACIN_MASK;
	rate_table->intin = readl(pll->pll_base + PLL_INTIN_REG) & PLL_INTIN_MASK;
	rate_table->refdiv = readl(pll->pll_base + PLL_REFDIV_REG) & PLL_REFDIV_MASK;

	postdiv = readl(pll->pll_base + PLL_POSTDIV_REG);
	rate_table->postdiv1 = postdiv & PLL_POSTDIV_MASK;
	rate_table->postdiv2 = (postdiv >> 4) & PLL_POSTDIV_MASK;
}

static const struct visconti_pll_rate_table *visconti_get_pll_settings(struct visconti_pll *pll,
								       unsigned long rate)
{
	const struct visconti_pll_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate == rate_table[i].rate)
			return &rate_table[i];
	}

	return NULL;
}

static unsigned long visconti_get_pll_rate_from_data(struct visconti_pll *pll,
						     struct visconti_pll_rate_table *rate)
{
	const struct visconti_pll_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate_table[i].dacen == rate->dacen &&
			rate_table[i].dsmen == rate->dsmen &&
			rate_table[i].fracin == rate->fracin &&
			rate_table[i].intin == rate->intin &&
			rate_table[i].refdiv == rate->refdiv &&
			rate_table[i].postdiv1 == rate->postdiv1 &&
			rate_table[i].postdiv2 == rate->postdiv2)
			return rate_table[i].rate;
	}

	/* set default */
	return rate_table[0].rate;
}

static long visconti_pll_round_rate(struct clk_hw *hw,
			    unsigned long rate, unsigned long *prate)
{
	struct visconti_pll *pll = to_visconti_pll(hw);
	const struct visconti_pll_rate_table *rate_table = pll->rate_table;
	int i;

	/* Assumming rate_table is in descending order */
	for (i = 0; i < pll->rate_count; i++) {
		if (rate >= rate_table[i].rate)
			return rate_table[i].rate;
	}

	/* return minimum supported value */
	return rate_table[i - 1].rate;
}

static unsigned long visconti_pll_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct visconti_pll *pll = to_visconti_pll(hw);
	struct visconti_pll_rate_table rate_table;

	memset(&rate_table, 0, sizeof(rate_table));
	visconti_pll_get_params(pll, &rate_table);

	return visconti_get_pll_rate_from_data(pll, &rate_table);
}

static int visconti_pll_set_params(struct visconti_pll *pll,
				const struct visconti_pll_rate_table *rate_table)
{
	/* update pll values */
	writel(((rate_table->dacen << 4) | rate_table->dsmen),
			pll->pll_base + PLL_FRACMODE_REG);
	writel(((rate_table->postdiv2 << 4) | rate_table->postdiv1),
			pll->pll_base + PLL_POSTDIV_REG);
	writel(rate_table->intin, pll->pll_base + PLL_INTIN_REG);
	writel(rate_table->fracin, pll->pll_base + PLL_FRACIN_REG);
	writel(rate_table->refdiv, pll->pll_base + PLL_REFDIV_REG);

	return 0;
}

static int visconti_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct visconti_pll *pll = to_visconti_pll(hw);
	const struct visconti_pll_rate_table *rate_table;

	/* Get required rate settings from table */
	rate_table = visconti_get_pll_settings(pll, rate);
	if (!rate_table) {
		pr_err("Invalid rate : %lu for pll clk %s\n",
			parent_rate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	return visconti_pll_set_params(pll, rate_table);
}

static int visconti_pll_is_enabled(struct clk_hw *hw)
{
	struct visconti_pll *pll = to_visconti_pll(hw);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(pll->lock, flags);
	reg = readl(pll->pll_base + PLL_CTRL_REG);
	spin_unlock_irqrestore(pll->lock, flags);

	return (reg & PLL_PLLEN);
}

static int visconti_pll_enable(struct clk_hw *hw)
{
	struct visconti_pll *pll = to_visconti_pll(hw);
	const struct visconti_pll_rate_table *rate_table = pll->rate_table;
	unsigned long flags;
	u32 reg;

	if (visconti_pll_is_enabled(hw))
		return 0;

	spin_lock_irqsave(pll->lock, flags);

	/* Change to access via register */
	writel(PLL_CONFIG_SEL, pll->pll_base + PLL_CONF_REG);

	/* Change to BYPASS mode */
	reg = readl(pll->pll_base + PLL_CTRL_REG);
	reg |= PLL_BYPASS;
	writel(reg, pll->pll_base + PLL_CTRL_REG);

	/* Set DIV register .....*/
	visconti_pll_set_params(pll, &rate_table[0]);

	/* Disable  PLL register */
	reg = readl(pll->pll_base + PLL_CTRL_REG);
	reg &= ~PLL_PLLEN;
	writel(reg, pll->pll_base + PLL_CTRL_REG);

	/* Wait 1us */
	udelay(1);

	/* Enable PLL register */
	reg = readl(pll->pll_base + PLL_CTRL_REG);
	reg |= PLL_PLLEN;
	writel(reg, pll->pll_base + PLL_CTRL_REG);

	/* Wait 40us */
	udelay(40);

	/* Change to PLL mode */
	reg = readl(pll->pll_base + PLL_CTRL_REG);
	reg &= ~PLL_BYPASS;
	writel(reg, pll->pll_base + PLL_CTRL_REG);

	spin_unlock_irqrestore(pll->lock, flags);

	return 0;
}

static void visconti_pll_disable(struct clk_hw *hw)
{
	struct visconti_pll *pll = to_visconti_pll(hw);
	unsigned long flags;
	u32 reg;

	if (!visconti_pll_is_enabled(hw))
		return;

	spin_lock_irqsave(pll->lock, flags);

	/* Change to access via register */
	writel(PLL_CONFIG_SEL, pll->pll_base + PLL_CONF_REG);

	/* Change to BYPASS mode */
	reg = readl(pll->pll_base + PLL_CTRL_REG);
	reg |= PLL_BYPASS;
	writel(reg, pll->pll_base + PLL_CTRL_REG);

	/* PLL disable */
	reg = readl(pll->pll_base + PLL_CTRL_REG);
	reg &= ~PLL_PLLEN;
	writel(reg, pll->pll_base + PLL_CTRL_REG);

	spin_unlock_irqrestore(pll->lock, flags);
}

static const struct clk_ops visconti_pll_ops = {
	.enable = visconti_pll_enable,
	.disable = visconti_pll_disable,
	.is_enabled = visconti_pll_is_enabled,
	.round_rate = visconti_pll_round_rate,
	.recalc_rate = visconti_pll_recalc_rate,
	.set_rate = visconti_pll_set_rate,
};

static struct clk *visconti_register_pll(struct visconti_pll_provider *ctx,
					 const char *name,
					 const char *parent_name,
					 int offset,
					 const struct visconti_pll_rate_table *rate_table,
					 u8 clk_pll_flags,
					 spinlock_t *lock)
{
	struct clk_init_data init;
	struct visconti_pll *pll;
	struct clk *pll_clk;
	int len;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	/* Create the actual pll */
	init.name = name;
	init.flags = CLK_IGNORE_UNUSED;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	for (len = 0; rate_table[len].rate != 0; )
		len++;
	pll->rate_count = len;
	pll->rate_table = kmemdup(rate_table,
				  pll->rate_count * sizeof(struct visconti_pll_rate_table),
				  GFP_KERNEL);
	WARN(!pll->rate_table, "%s: could not allocate rate table for %s\n", __func__, name);

	init.ops = &visconti_pll_ops;
	pll->hw.init = &init;
	pll->pll_base = ctx->reg_base + offset;
	pll->flags = clk_pll_flags;
	pll->lock = lock;
	pll->ctx = ctx;

	pll_clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(pll_clk)) {
		pr_err("failed to register pll clock %s : %ld\n", name, PTR_ERR(pll_clk));
		kfree(pll);
	}

	return pll_clk;
}

static void visconti_pll_add_lookup(struct visconti_pll_provider *ctx, struct clk *clk,
				    unsigned int id)
{
	if (ctx->clk_data.clks && id)
		ctx->clk_data.clks[id] = clk;
}

void __init visconti_register_plls(struct visconti_pll_provider *ctx,
				   const struct visconti_pll_info *list,
				   unsigned int nr_plls,
				   spinlock_t *lock)
{
	int idx;

	for (idx = 0; idx < nr_plls; idx++, list++) {
		struct clk *clk;

		clk = visconti_register_pll(ctx,
					    list->name,
					    list->parent,
					    list->base_reg,
					    list->rate_table,
					    list->flags,
					    lock);
		if (IS_ERR(clk)) {
			pr_err("failed to register clock %s\n", list->name);
			continue;
		}

		visconti_pll_add_lookup(ctx, clk, list->id);
	}
}

struct visconti_pll_provider * __init visconti_init_pll(struct device_node *np,
							void __iomem *base,
							unsigned long nr_plls)
{
	struct visconti_pll_provider *ctx;
	struct clk **clk_table;
	int i;

	ctx = kzalloc(sizeof(struct visconti_pll_provider), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	clk_table = kcalloc(nr_plls, sizeof(struct clk *), GFP_KERNEL);
	if (!clk_table)
		goto err;

	for (i = 0; i < nr_plls; ++i)
		clk_table[i] = ERR_PTR(-ENOENT);

	ctx->node = np;
	ctx->reg_base = base;
	ctx->clk_data.clks = clk_table;
	ctx->clk_data.clk_num = nr_plls;

	return ctx;

err:
	kfree(ctx);
	return ERR_PTR(-ENOMEM);
}
