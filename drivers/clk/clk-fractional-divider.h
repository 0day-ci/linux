/* SPDX-License-Identifier: GPL-2.0 */

struct clk_hw;

extern const struct clk_ops clk_fractional_divider_ops;

void clk_fractional_divider_general_approximation(struct clk_hw *hw,
						  unsigned long rate,
						  unsigned long *parent_rate,
						  unsigned long *m,
						  unsigned long *n);
