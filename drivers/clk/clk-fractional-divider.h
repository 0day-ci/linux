/* SPDX-License-Identifier: GPL-2.0 */

struct clk_hw;

void clk_fractional_divider_general_approximation(struct clk_hw *hw,
						  unsigned long rate,
						  unsigned long *parent_rate,
						  unsigned long *m,
						  unsigned long *n);
