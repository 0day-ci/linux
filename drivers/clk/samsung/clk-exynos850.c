// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Samsung Electronics Co., Ltd.
 * Copyright (C) 2021 Linaro Ltd.
 *
 * Common Clock Framework support for Exynos850 SoC.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk.h"

/* Will be extracted to bindings header once proper clk driver is implemented */
#define OSCCLK		1
#define DOUT_UART	2
#define CLK_NR_CLKS	3

/* Fixed rate clocks generated outside the SoC */
static struct samsung_fixed_rate_clock exynos850_fixed_rate_ext_clks[] __initdata = {
	FRATE(OSCCLK, "fin_pll", NULL, 0, 26000000),
};

/*
 * Model the UART clock as a fixed-rate clock for now, to make serial driver
 * work. This clock is already configured in the bootloader.
 */
static const struct samsung_fixed_rate_clock exynos850_peri_clks[] __initconst = {
	FRATE(DOUT_UART, "DOUT_UART", NULL, 0, 200000000),
};

static const struct of_device_id ext_clk_match[] __initconst = {
	{ .compatible = "samsung,exynos850-oscclk" },
	{}
};

static void __init exynos850_clk_init(struct device_node *np)
{
	void __iomem *reg_base;
	struct samsung_clk_provider *ctx;

	reg_base = of_iomap(np, 0);
	if (!reg_base)
		panic("%s: failed to map registers\n", __func__);

	ctx = samsung_clk_init(np, reg_base, CLK_NR_CLKS);
	if (!ctx)
		panic("%s: unable to allocate ctx\n", __func__);

	samsung_clk_of_register_fixed_ext(ctx,
			exynos850_fixed_rate_ext_clks,
			ARRAY_SIZE(exynos850_fixed_rate_ext_clks),
			ext_clk_match);

	samsung_clk_register_fixed_rate(ctx, exynos850_peri_clks,
			ARRAY_SIZE(exynos850_peri_clks));

	samsung_clk_of_add_provider(np, ctx);
}

CLK_OF_DECLARE(exynos850_clk, "samsung,exynos850-clock", exynos850_clk_init);
