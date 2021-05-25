// SPDX-License-Identifier: GPL-2.0-only
/*
 * Toshiba Visconti PLL controller
 *
 * Copyright (c) 2021 TOSHIBA CORPORATION
 * Copyright (c) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#include <dt-bindings/clock/toshiba,tmpv770x.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "pll.h"

static DEFINE_SPINLOCK(tmpv7708_pll_lock);

static const struct visconti_pll_rate_table pipll0_rates[] __initconst = {
	VISCONTI_PLL_RATE(840000000, 0x1, 0x0, 0x1, 0x54, 0x000000, 0x2, 0x1),
	VISCONTI_PLL_RATE(780000000, 0x1, 0x0, 0x1, 0x4E, 0x000000, 0x2, 0x1),
	VISCONTI_PLL_RATE(600000000, 0x1, 0x0, 0x1, 0x3C, 0x000000, 0x2, 0x1),
	{ /* sentinel */ },
};

static const struct visconti_pll_rate_table piddrcpll_rates[] __initconst = {
	VISCONTI_PLL_RATE(780000000, 0x1, 0x0, 0x1, 0x4E, 0x000000, 0x2, 0x1),
	VISCONTI_PLL_RATE(760000000, 0x1, 0x0, 0x1, 0x4C, 0x000000, 0x2, 0x1),
	{ /* sentinel */ },
};

static const struct visconti_pll_rate_table pivoifpll_rates[] __initconst = {
	VISCONTI_PLL_RATE(165000000, 0x1, 0x0, 0x1, 0x42, 0x000000, 0x4, 0x2),
	VISCONTI_PLL_RATE(148500000, 0x1, 0x1, 0x1, 0x3B, 0x666666, 0x4, 0x2),
	VISCONTI_PLL_RATE(96000000, 0x1, 0x0, 0x1, 0x30, 0x000000, 0x5, 0x2),
	VISCONTI_PLL_RATE(74250000, 0x1, 0x1, 0x1, 0x3B, 0x666666, 0x4, 0x4),
	VISCONTI_PLL_RATE(54000000, 0x1, 0x0, 0x1, 0x36, 0x000000, 0x5, 0x4),
	VISCONTI_PLL_RATE(48000000, 0x1, 0x0, 0x1, 0x30, 0x000000, 0x5, 0x4),
	VISCONTI_PLL_RATE(35750000, 0x1, 0x1, 0x1, 0x32, 0x0CCCCC, 0x7, 0x4),
	{ /* sentinel */ },
};

static const struct visconti_pll_rate_table piimgerpll_rates[] __initconst = {
	VISCONTI_PLL_RATE(165000000, 0x1, 0x0, 0x1, 0x42, 0x000000, 0x4, 0x2),
	VISCONTI_PLL_RATE(96000000, 0x1, 0x0, 0x1, 0x30, 0x000000, 0x5, 0x2),
	VISCONTI_PLL_RATE(54000000, 0x1, 0x0, 0x1, 0x36, 0x000000, 0x5, 0x4),
	VISCONTI_PLL_RATE(48000000, 0x1, 0x0, 0x1, 0x30, 0x000000, 0x5, 0x4),
	{ /* sentinel */ },
};

static const struct visconti_pll_info pll_info[] __initconst = {
	{ TMPV770X_PLL_PIPLL0, "pipll0", "osc2_clk", 0, 0x0, pipll0_rates },
	{ TMPV770X_PLL_PIDDRCPLL, "piddrcpll", "osc2_clk", 0, 0x500, piddrcpll_rates },
	{ TMPV770X_PLL_PIVOIFPLL, "pivoifpll", "osc2_clk", 0, 0x600, pivoifpll_rates },
	{ TMPV770X_PLL_PIIMGERPLL, "piimgerpll", "osc2_clk", 0, 0x700, piimgerpll_rates },
};

static void __init tmpv7708_setup_plls(struct device_node *np)
{
	struct visconti_pll_provider *ctx;
	void __iomem *reg_base;
	struct clk *clk;
	int ret;
	u32 tmp;

	reg_base = of_iomap(np, 0);
	if (!reg_base)
		return;

	ctx = visconti_init_pll(np, reg_base, TMPV770X_NR_PLL);
	if (IS_ERR(ctx)) {
		iounmap(reg_base);
		return;
	}

	ret = of_property_read_u32(np, "osc2-clk-frequency", &tmp);
	if (ret) {
		pr_warn("osc2 clk frequency not specified, using 20 MHz.\n");
		tmp = 20000000;
	}

	clk = clk_register_fixed_rate(NULL, "osc2_clk", NULL, 0, tmp);
	ctx->clk_data.clks[TMPV770X_PLL_PIPLL1] =
		clk_register_fixed_rate(NULL, "pipll1", NULL, 0, 600000000);
	ctx->clk_data.clks[TMPV770X_PLL_PIDNNPLL] =
		clk_register_fixed_rate(NULL, "pidnnpll", NULL, 0, 500000000);
	ctx->clk_data.clks[TMPV770X_PLL_PIETHERPLL] =
		clk_register_fixed_rate(NULL, "pietherpll", NULL, 0, 500000000);

	visconti_register_plls(ctx, pll_info, ARRAY_SIZE(pll_info), &tmpv7708_pll_lock);
}

CLK_OF_DECLARE_DRIVER(tmpv7708_plls, "toshiba,tmpv7708-pipllct", tmpv7708_setup_plls);
