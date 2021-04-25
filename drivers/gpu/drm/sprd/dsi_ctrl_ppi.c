// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/module.h>

#include "dsi_ctrl.h"
#include "dsi_ctrl_ppi.h"

/*
 * Reset D-PHY module
 */
void dsi_phy_rstz(struct dsi_context *ctx, int level)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x78 phy_interface_ctrl;

	phy_interface_ctrl.val = readl(&reg->PHY_INTERFACE_CTRL);
	phy_interface_ctrl.bits.rf_phy_reset_n = level;

	writel(phy_interface_ctrl.val, &reg->PHY_INTERFACE_CTRL);
}

/*
 * Power up/down D-PHY module
 */
void dsi_phy_shutdownz(struct dsi_context *ctx, int level)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x78 phy_interface_ctrl;

	phy_interface_ctrl.val = readl(&reg->PHY_INTERFACE_CTRL);
	phy_interface_ctrl.bits.rf_phy_shutdown = level;

	writel(phy_interface_ctrl.val, &reg->PHY_INTERFACE_CTRL);
}

/*
 * Configure minimum wait period for HS transmission request after a stop state
 */
void dsi_phy_stop_wait_time(struct dsi_context *ctx, u8 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(byte_cycle, &reg->PHY_MIN_STOP_TIME);
}

/*
 * Set number of active lanes
 */
void dsi_phy_datalane_en(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(ctx->lanes - 1, &reg->PHY_LANE_NUM_CONFIG);
}

/*
 * Enable clock lane module
 */
void dsi_phy_clklane_en(struct dsi_context *ctx, int en)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x78 phy_interface_ctrl;

	phy_interface_ctrl.val = readl(&reg->PHY_INTERFACE_CTRL);
	phy_interface_ctrl.bits.rf_phy_clk_en = en;

	writel(phy_interface_ctrl.val, &reg->PHY_INTERFACE_CTRL);
}

/*
 * Request the PHY module to start transmission of high speed clock.
 * This causes the clock lane to start transmitting DDR clock on the
 * lane interconnect.
 */
void dsi_phy_clk_hs_rqst(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x74 phy_clk_lane_lp_ctrl;

	phy_clk_lane_lp_ctrl.val = readl(&reg->PHY_CLK_LANE_LP_CTRL);
	phy_clk_lane_lp_ctrl.bits.auto_clklane_ctrl_en = 0;
	phy_clk_lane_lp_ctrl.bits.phy_clklane_tx_req_hs = enable;

	writel(phy_clk_lane_lp_ctrl.val, &reg->PHY_CLK_LANE_LP_CTRL);
}

/*
 * Get D-PHY PPI status
 */
u8 dsi_phy_is_pll_locked(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x9C phy_status;

	phy_status.val = readl(&reg->PHY_STATUS);

	return phy_status.bits.phy_lock;
}

void dsi_phy_test_clk(struct dsi_context *ctx, u8 value)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xF0 phy_tst_ctrl0;

	phy_tst_ctrl0.val = readl(&reg->PHY_TST_CTRL0);
	phy_tst_ctrl0.bits.phy_testclk = value;

	writel(phy_tst_ctrl0.val, &reg->PHY_TST_CTRL0);
}

void dsi_phy_test_clr(struct dsi_context *ctx, u8 value)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xF0 phy_tst_ctrl0;

	phy_tst_ctrl0.val = readl(&reg->PHY_TST_CTRL0);
	phy_tst_ctrl0.bits.phy_testclr = value;

	writel(phy_tst_ctrl0.val, &reg->PHY_TST_CTRL0);
}

void dsi_phy_test_en(struct dsi_context *ctx, u8 value)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xF4 phy_tst_ctrl1;

	phy_tst_ctrl1.val = readl(&reg->PHY_TST_CTRL1);
	phy_tst_ctrl1.bits.phy_testen = value;

	writel(phy_tst_ctrl1.val, &reg->PHY_TST_CTRL1);
}

u8 dsi_phy_test_dout(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xF4 phy_tst_ctrl1;

	phy_tst_ctrl1.val = readl(&reg->PHY_TST_CTRL1);

	return phy_tst_ctrl1.bits.phy_testdout;
}

void dsi_phy_test_din(struct dsi_context *ctx, u8 data)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xF4 phy_tst_ctrl1;

	phy_tst_ctrl1.val = readl(&reg->PHY_TST_CTRL1);
	phy_tst_ctrl1.bits.phy_testdin = data;

	writel(phy_tst_ctrl1.val, &reg->PHY_TST_CTRL1);
}
