// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Sean Anderson <sean.anderson@seco.com>
 *
 * For Xilinx LogiCORE IP AXI Timer documentation, refer to DS764:
 * https://www.xilinx.com/support/documentation/ip_documentation/axi_timer/v1_03_a/axi_timer_ds764.pdf
 */

#include <clocksource/timer-xilinx.h>
#include <linux/clk.h>
#include <linux/of.h>

u32 xilinx_timer_tlr_cycles(struct xilinx_timer_priv *priv, u32 tcsr,
			    u64 cycles)
{
	WARN_ON(cycles < 2 || cycles - 2 > priv->max);

	if (tcsr & TCSR_UDT)
		return cycles - 2;
	else
		return priv->max - cycles + 2;
}
EXPORT_SYMBOL_GPL(xilinx_timer_tlr_cycles);

unsigned int xilinx_timer_get_period(struct xilinx_timer_priv *priv,
				     u32 tlr, u32 tcsr)
{
	u64 cycles;

	if (tcsr & TCSR_UDT)
		cycles = tlr + 2;
	else
		cycles = (u64)priv->max - tlr + 2;

	/* cycles has a max of 2^32 + 2 */
	return DIV64_U64_ROUND_CLOSEST(cycles * NSEC_PER_SEC,
				       clk_get_rate(priv->clk));
}
EXPORT_SYMBOL_GPL(xilinx_timer_get_period);

int xilinx_timer_common_init(struct device_node *np,
			     struct xilinx_timer_priv *priv,
			     u32 *one_timer)
{
	int ret;
	u32 width;

	ret = of_property_read_u32(np, "xlnx,one-timer-only", one_timer);
	if (ret) {
		pr_err("%pOF: err %d: xlnx,one-timer-only\n", np, ret);
		return ret;
	} else if (*one_timer && *one_timer != 1) {
		pr_err("%pOF: xlnx,one-timer-only must be 0 or 1\n", np);
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "xlnx,count-width", &width);
	if (ret == -EINVAL) {
		width = 32;
	} else if (ret) {
		pr_err("%pOF: err %d: xlnx,count-width\n", np, ret);
		return ret;
	} else if (width < 8 || width > 32) {
		pr_err("%pOF: invalid counter width\n", np);
		return -EINVAL;
	}
	priv->max = BIT_ULL(width) - 1;

	return 0;
}
EXPORT_SYMBOL_GPL(xilinx_timer_common_init);
