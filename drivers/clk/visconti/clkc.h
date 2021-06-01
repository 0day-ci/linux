/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Toshiba Visconti clock controller
 *
 * Copyright (c) 2021 TOSHIBA CORPORATION
 * Copyright (c) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#ifndef _VISCONTI_GATE_H_
#define _VISCONTI_GATE_H_

#include <linux/mfd/syscon.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#include "reset.h"

struct visconti_clk_provider {
	struct device_node	*node;
	struct regmap		*regmap;
	struct clk_onecell_data clk_data;
};

struct visconti_clk_gate_table {
	unsigned int	id;
	const char	*name;
	const char	*parent;
	u8		flags;
	u32		ckon_offset;
	u32		ckoff_offset;
	u8		ck_idx;
	unsigned int	div;
	u8		rs_id;
};

struct visconti_fixed_clk {
	unsigned int	id;
	const char	*name;
	const char	*parent;
	unsigned long	flag;
	unsigned int	mult;
	unsigned int	div;
};

struct visconti_clk_gate {
	struct clk_hw	hw;
	struct regmap	*regmap;
	u32		ckon_offset;
	u32		ckoff_offset;
	u8		ck_idx;
	u8		flags;
	u32		rson_offset;
	u32		rsoff_offset;
	u8		rs_idx;
	spinlock_t	*lock;
};

struct visconti_clk_provider *visconti_init_clk(struct device_node *np,
						struct regmap *regmap,
						unsigned long nr_clks);
int visconti_clk_register_gates(struct visconti_clk_provider *data,
				 const struct visconti_clk_gate_table *clks,
				 int num_gate,
				 const struct visconti_reset_data *reset,
				 spinlock_t *lock);
#endif /* _VISCONTI_GATE_H_ */
