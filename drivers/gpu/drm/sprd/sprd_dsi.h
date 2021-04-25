/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef __SPRD_DSI_H__
#define __SPRD_DSI_H__

#include <linux/of.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <video/videomode.h>

#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_print.h>
#include <drm/drm_panel.h>

#include "megacores_pll.h"

#define encoder_to_dsi(encoder) \
	container_of(encoder, struct sprd_dsi, encoder)

#define DSI_INT_STS_NEED_SOFT_RESET	BIT(0)
#define DSI_INT_STS_NEED_HARD_RESET	BIT(1)

enum dsi_work_mode {
	DSI_MODE_CMD = 0,
	DSI_MODE_VIDEO
};

enum video_burst_mode {
	VIDEO_NON_BURST_WITH_SYNC_PULSES = 0,
	VIDEO_NON_BURST_WITH_SYNC_EVENTS,
	VIDEO_BURST_WITH_SYNC_PULSES
};

enum dsi_color_coding {
	COLOR_CODE_16BIT_CONFIG1 = 0,
	COLOR_CODE_16BIT_CONFIG2,
	COLOR_CODE_16BIT_CONFIG3,
	COLOR_CODE_18BIT_CONFIG1,
	COLOR_CODE_18BIT_CONFIG2,
	COLOR_CODE_24BIT,
	COLOR_CODE_20BIT_YCC422_LOOSELY,
	COLOR_CODE_24BIT_YCC422,
	COLOR_CODE_16BIT_YCC422,
	COLOR_CODE_30BIT,
	COLOR_CODE_36BIT,
	COLOR_CODE_12BIT_YCC420,
	COLOR_CODE_COMPRESSTION,
	COLOR_CODE_MAX
};

struct dsi_context {
	void __iomem *base;
	struct regmap *regmap;
	struct dphy_pll *pll;
	struct videomode vm;
	bool enabled;

	u8 lanes;
	u32 format;
	u8 work_mode;
	u8 burst_mode;

	int irq0;
	int irq1;
	u32 int0_mask;
	u32 int1_mask;

	/* byte clock [KHz] */
	u32 byte_clk;
	/* escape clock [KHz] */
	u32 esc_clk;
	/* maximum time (ns) for data lanes from HS to LP */
	u16 data_hs2lp;
	/* maximum time (ns) for data lanes from LP to HS */
	u16 data_lp2hs;
	/* maximum time (ns) for clk lanes from HS to LP */
	u16 clk_hs2lp;
	/* maximum time (ns) for clk lanes from LP to HS */
	u16 clk_lp2hs;
	/* maximum time (ns) for BTA operation - REQUIRED */
	u16 max_rd_time;
	/* enable receiving frame ack packets - for video mode */
	bool frame_ack_en;
	/* enable receiving tear effect ack packets - for cmd mode */
	bool te_ack_en;
	/* enable non coninuous clock for energy saving */
	bool nc_clk_en;
};

struct sprd_dsi {
	struct drm_device *drm;
	struct mipi_dsi_host host;
	struct mipi_dsi_device *slave;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_panel *panel;
	struct drm_display_mode *mode;
	struct dsi_context ctx;
};

#endif /* __SPRD_DSI_H__ */
