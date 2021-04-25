/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _DSI_CTRL_PPI_H_
#define _DSI_CTRL_PPI_H_

#include "sprd_dsi.h"

void dsi_phy_rstz(struct dsi_context *ctx, int level);
void dsi_phy_shutdownz(struct dsi_context *ctx, int level);
void dsi_phy_force_pll(struct dsi_context *ctx, int force);
void dsi_phy_stop_wait_time(struct dsi_context *ctx, u8 byte_clk);
void dsi_phy_datalane_en(struct dsi_context *ctx);
void dsi_phy_clklane_en(struct dsi_context *ctx, int en);
void dsi_phy_clk_hs_rqst(struct dsi_context *ctx, int en);
u8 dsi_phy_is_pll_locked(struct dsi_context *ctx);
void dsi_phy_test_clk(struct dsi_context *ctx, u8 level);
void dsi_phy_test_clr(struct dsi_context *ctx, u8 level);
void dsi_phy_test_en(struct dsi_context *ctx, u8 level);
u8 dsi_phy_test_dout(struct dsi_context *ctx);
void dsi_phy_test_din(struct dsi_context *ctx, u8 data);
void dsi_phy_bist_en(struct dsi_context *ctx, int en);

#endif /* _DSI_CTRL_PPI_H_ */
