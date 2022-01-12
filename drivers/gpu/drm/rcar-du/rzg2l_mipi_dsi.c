// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G2L MIPI DSI Encoder Driver
 *
 * Copyright (C) 2021 Renesas Electronics Corporation
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include "rzg2l_mipi_dsi_regs.h"

struct rzg2l_mipi_dsi {
	struct device *dev;
	void __iomem *link_mmio;
	void __iomem *phy_mmio;

	struct reset_control *rstc;
	struct reset_control *arstc;
	struct reset_control *prstc;

	struct mipi_dsi_host host;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;

	struct clk *vclk;

	enum mipi_dsi_pixel_format format;
	unsigned int num_data_lanes;
	unsigned int lanes;
	unsigned long mode_flags;

	bool hsclkmode;	/* 0 for non-continuous and 1 for continuous clock mode */
};

static inline struct rzg2l_mipi_dsi *
bridge_to_rzg2l_mipi_dsi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rzg2l_mipi_dsi, bridge);
}

static inline struct rzg2l_mipi_dsi *
host_to_rzg2l_mipi_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct rzg2l_mipi_dsi, host);
}

static void rzg2l_mipi_dsi_write(void __iomem *mem, u32 reg, u32 data)
{
	iowrite32(data, mem + reg);
}

static u32 rzg2l_mipi_dsi_read(void __iomem *mem, u32 reg)
{
	return ioread32(mem + reg);
}

static void rzg2l_mipi_dsi_clr(void __iomem *mem, u32 reg, u32 clr)
{
	rzg2l_mipi_dsi_write(mem, reg, rzg2l_mipi_dsi_read(mem, reg) & ~clr);
}

/* -----------------------------------------------------------------------------
 * Hardware Setup
 */

static int rzg2l_mipi_dsi_startup(struct rzg2l_mipi_dsi *dsi,
				  const struct drm_display_mode *mode)
{
	unsigned long hsfreq;
	u32 txsetr;
	u32 clstptsetr;
	u32 lptrnstsetr;
	u8 max_num_lanes;
	u32 clkkpt;
	u32 clkbfht;
	u32 clkstpt;
	u32 golpbkt;
	unsigned int bpp;
	struct {
		u32 tclk_miss;
		u32 t_init;
		u32 tclk_prepare;
		u32 tclk_settle;
		u32 tclk_trail;
		u32 tclk_post;
		u32 tclk_pre;
		u32 tclk_zero;
		u32 tlpx;
		u32 ths_prepare;
		u32 ths_settle;
		u32 ths_exit;
		u32 ths_trail;
		u32 ths_zero;
	} timings;
	u32 dphyctrl0;
	u32 dphytim0;
	u32 dphytim1;
	u32 dphytim2;
	u32 dphytim3;

	/* Relationship between hsclk and vclk must follow
	 * vclk * bpp = hsclk * 8 * lanes
	 * where vclk: video clock (Hz)
	 *       bpp: video pixel bit depth
	 *       hsclk: DSI HS Byte clock frequency (Hz)
	 *       lanes: number of data lanes
	 *
	 * hsclk(bit) = hsclk(byte) * 8
	 */

	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	hsfreq = (mode->clock * bpp * 8) / (8 * dsi->lanes);

	/* Initializing DPHY before accessing LINK */

	/* All DSI global operation timings are set with recommended setting */
	if (hsfreq > 250000) {
		timings.tclk_miss = 1;
		timings.t_init = 79801;
		timings.tclk_prepare = 8;
		timings.tclk_settle = 9;
		timings.tclk_trail = 7;
		timings.tclk_post = 35;
		timings.tclk_pre = 4;
		timings.tclk_zero = 33;
		timings.tlpx = 6;
		timings.ths_prepare = 9;
		timings.ths_settle = 9;
		timings.ths_exit = 13;
		timings.ths_trail = 9;
		timings.ths_zero = 16;
	} else {
		timings.tclk_miss = 1;
		timings.t_init = 79801;
		timings.tclk_prepare = 8;
		timings.tclk_settle = 9;
		timings.tclk_trail = 10;
		timings.tclk_post = 94;
		timings.tclk_pre = 13;
		timings.tclk_zero = 33;
		timings.tlpx = 6;
		timings.ths_prepare = 12;
		timings.ths_settle = 9;
		timings.ths_exit = 13;
		timings.ths_trail = 17;
		timings.ths_zero = 23;
	}

	reset_control_deassert(dsi->arstc);
	reset_control_deassert(dsi->prstc);

	pm_runtime_get_sync(dsi->dev);

	clk_set_rate(dsi->vclk, mode->clock * 1000);

	dphyctrl0 = DSIDPHYCTRL0_CAL_EN_HSRX_OFS | DSIDPHYCTRL0_CMN_MASTER_EN |
		    DSIDPHYCTRL0_RE_VDD_DETVCCQLV18 | DSIDPHYCTRL0_EN_BGR;

	rzg2l_mipi_dsi_write(dsi->phy_mmio, DSIDPHYCTRL0, dphyctrl0);
	usleep_range(20, 30);

	dphyctrl0 |= DSIDPHYCTRL0_EN_LDO1200;
	rzg2l_mipi_dsi_write(dsi->phy_mmio, DSIDPHYCTRL0, dphyctrl0);
	usleep_range(10, 20);

	dphytim0 = DSIDPHYTIM0_TCLK_MISS(timings.tclk_miss) |
		   DSIDPHYTIM0_T_INIT(timings.t_init);
	dphytim1 = DSIDPHYTIM1_THS_PREPARE(timings.ths_prepare) |
		   DSIDPHYTIM1_TCLK_PREPARE(timings.tclk_prepare) |
		   DSIDPHYTIM1_THS_SETTLE(timings.ths_settle) |
		   DSIDPHYTIM1_TCLK_SETTLE(timings.tclk_settle);
	dphytim2 = DSIDPHYTIM2_TCLK_TRAIL(timings.tclk_trail) |
		   DSIDPHYTIM2_TCLK_POST(timings.tclk_post) |
		   DSIDPHYTIM2_TCLK_PRE(timings.tclk_pre) |
		   DSIDPHYTIM2_TCLK_ZERO(timings.tclk_zero);
	dphytim3 = DSIDPHYTIM3_TLPX(timings.tlpx) |
		   DSIDPHYTIM3_THS_EXIT(timings.ths_exit) |
		   DSIDPHYTIM3_THS_TRAIL(timings.ths_trail) |
		   DSIDPHYTIM3_THS_ZERO(timings.ths_zero);

	rzg2l_mipi_dsi_write(dsi->phy_mmio, DSIDPHYTIM0, dphytim0);
	rzg2l_mipi_dsi_write(dsi->phy_mmio, DSIDPHYTIM1, dphytim1);
	rzg2l_mipi_dsi_write(dsi->phy_mmio, DSIDPHYTIM2, dphytim2);
	rzg2l_mipi_dsi_write(dsi->phy_mmio, DSIDPHYTIM3, dphytim3);

	reset_control_deassert(dsi->rstc);
	udelay(1);

	/* Check number of lanes capability */
	max_num_lanes = rzg2l_mipi_dsi_read(dsi->link_mmio, TXSETR) >> 16;
	max_num_lanes = (max_num_lanes & 0x3) + 1;
	if (max_num_lanes < dsi->lanes) {
		dev_err(dsi->dev, "DPHY can not support %d lanes", dsi->lanes);
		return -EINVAL;
	}

	/* Enable Data lanes and Clock lanes */
	txsetr = TXSETR_DLEN | TXSETR_NUMLANEUSE(dsi->lanes - 1) | TXSETR_CLEN;
	rzg2l_mipi_dsi_write(dsi->link_mmio, TXSETR, txsetr);

	/*
	 * Global timings characteristic depends on high speed Clock Frequency
	 * Currently MIPI DSI-IF just supports maximum FHD@60 with:
	 * - videoclock = 148.5 (MHz)
	 * - bpp: maximum 24bpp
	 * - data lanes: maximum 4 lanes
	 * Therefore maximum hsclk will be 891 Mbps.
	 */

	if (hsfreq > 445500) {
		clkkpt = 12;
		clkbfht = 15;
		clkstpt = 48;
		golpbkt = 75;
	} else if (hsfreq > 250000) {
		clkkpt = 7;
		clkbfht = 8;
		clkstpt = 27;
		golpbkt = 40;
	} else {
		clkkpt = 8;
		clkbfht = 6;
		clkstpt = 24;
		golpbkt = 29;
	}

	clstptsetr = CLSTPTSETR_CLKKPT(clkkpt) | CLSTPTSETR_CLKBFHT(clkbfht) |
		     CLSTPTSETR_CLKSTPT(clkstpt);
	rzg2l_mipi_dsi_write(dsi->link_mmio, CLSTPTSETR, clstptsetr);

	lptrnstsetr = LPTRNSTSETR_GOLPBKT(golpbkt);
	rzg2l_mipi_dsi_write(dsi->link_mmio, LPTRNSTSETR, lptrnstsetr);

	return 0;
}

static void rzg2l_mipi_dsi_set_display_timing(struct rzg2l_mipi_dsi *dsi,
					      const struct drm_display_mode *mode)
{
	u32 vich1ppsetr;
	u32 vich1vssetr;
	u32 vich1vpsetr;
	u32 vich1hssetr;
	u32 vich1hpsetr;
	int dsi_format;
	u32 delay[2];

	/* Configuration for Pixel Packet */
	dsi_format = mipi_dsi_pixel_format_to_bpp(dsi->format);
	switch (dsi_format) {
	case 24:
		vich1ppsetr = VICH1PPSETR_DT_RGB24;
		break;
	case 18:
		vich1ppsetr = VICH1PPSETR_DT_RGB18;
		break;
	case 16:
		vich1ppsetr = VICH1PPSETR_DT_RGB16;
		break;
	default:
		dev_warn(dsi->dev, "unsupported format");
		return;
	}

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		vich1ppsetr |= VICH1PPSETR_TXESYNC_PULSE;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		vich1ppsetr &= ~VICH1PPSETR_TXESYNC_PULSE;

	rzg2l_mipi_dsi_write(dsi->link_mmio, VICH1PPSETR, vich1ppsetr);

	/* Configuration for Video Parameters */
	vich1vssetr = VICH1VSSETR_VACTIVE(mode->vdisplay) |
		      VICH1VSSETR_VSA(mode->vsync_end - mode->vsync_start);
	vich1vssetr |= (mode->flags & DRM_MODE_FLAG_PVSYNC) ?
			VICH1VSSETR_VSPOL_HIGH : VICH1VSSETR_VSPOL_LOW;

	vich1vpsetr = VICH1VPSETR_VFP(mode->vsync_start - mode->vdisplay) |
		      VICH1VPSETR_VBP(mode->vtotal - mode->vsync_end);

	vich1hssetr = VICH1HSSETR_HACTIVE(mode->hdisplay) |
		      VICH1HSSETR_HSA(mode->hsync_end - mode->hsync_start);
	vich1hssetr |= (mode->flags & DRM_MODE_FLAG_PHSYNC) ?
			VICH1HSSETR_HSPOL_HIGH : VICH1HSSETR_HSPOL_LOW;

	vich1hpsetr = VICH1HPSETR_HFP(mode->hsync_start - mode->hdisplay) |
		      VICH1HPSETR_HBP(mode->htotal - mode->hsync_end);

	rzg2l_mipi_dsi_write(dsi->link_mmio, VICH1VSSETR, vich1vssetr);
	rzg2l_mipi_dsi_write(dsi->link_mmio, VICH1VPSETR, vich1vpsetr);
	rzg2l_mipi_dsi_write(dsi->link_mmio, VICH1HSSETR, vich1hssetr);
	rzg2l_mipi_dsi_write(dsi->link_mmio, VICH1HPSETR, vich1hpsetr);

	if (dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
		dsi->hsclkmode = 0;
	else
		dsi->hsclkmode = 1;

	/*
	 * Configuration for Delay Value
	 * Delay value based on 2 ranges of video clock.
	 * 74.25MHz is videoclock of HD@60p or FHD@30p
	 */

	if (mode->clock > 74250) {
		delay[0] = 231;
		delay[1] = 216;
	} else {
		delay[0] = 220;
		delay[1] = 212;
	}

	rzg2l_mipi_dsi_write(dsi->link_mmio, VICH1SET1R,
			     VICH1SET1R_DLY(delay[dsi->hsclkmode]));
}

static int rzg2l_mipi_dsi_start_hs_clock(struct rzg2l_mipi_dsi *dsi)
{
	u32 hsclksetr;
	u32 status;
	int ret;

	/* Start HS clock */
	hsclksetr = HSCLKSETR_HSCLKRUN_HS |
		 (dsi->hsclkmode ? HSCLKSETR_HSCLKMODE_CONT :
					HSCLKSETR_HSCLKMODE_NON_CONT);
	rzg2l_mipi_dsi_write(dsi->link_mmio, HSCLKSETR, hsclksetr);

	if (dsi->hsclkmode) {
		ret = read_poll_timeout(rzg2l_mipi_dsi_read, status,
					status & PLSR_CLLP2HS,
					2000, 20000, false, dsi->link_mmio, PLSR);
		if (ret < 0) {
			dev_err(dsi->dev, "failed to start HS clock\n");
			return ret;
		}
	}

	dev_dbg(dsi->dev, "Start High Speed Clock with %s clock mode",
		dsi->hsclkmode ? "continuous" : "non-continuous");

	return 0;
}

static int rzg2l_mipi_dsi_stop_hs_clock(struct rzg2l_mipi_dsi *dsi)
{
	u32 status;
	int ret;

	/* Stop HS clock */
	rzg2l_mipi_dsi_clr(dsi->link_mmio, HSCLKSETR, HSCLKSETR_HSCLKRUN_HS);

	if (dsi->hsclkmode) {
		ret = read_poll_timeout(rzg2l_mipi_dsi_read, status,
					status & PLSR_CLHS2LP,
					2000, 20000, false, dsi->link_mmio, PLSR);
		if (ret < 0) {
			dev_err(dsi->dev, "failed to stop HS clock\n");
			return ret;
		}
	}

	return 0;
}

static int rzg2l_mipi_dsi_start_video(struct rzg2l_mipi_dsi *dsi)
{
	u32 vich1set0r;
	u32 status;
	int ret;

	/* Configuration for Blanking sequence and start video input*/
	vich1set0r = VICH1SET0R_HFPNOLP | VICH1SET0R_HBPNOLP |
		     VICH1SET0R_HSANOLP | VICH1SET0R_VSTART;
	rzg2l_mipi_dsi_write(dsi->link_mmio, VICH1SET0R, vich1set0r);

	ret = read_poll_timeout(rzg2l_mipi_dsi_read, status,
				status & VICH1SR_VIRDY,
				2000, 20000, false, dsi->link_mmio, VICH1SR);
	if (ret < 0) {
		dev_err(dsi->dev, "Failed to start video signal input\n");
		return ret;
	}

	dev_dbg(dsi->dev, "Start video transferring");

	return 0;
}

static int rzg2l_mipi_dsi_stop_video(struct rzg2l_mipi_dsi *dsi)
{
	u32 status;
	int ret;

	rzg2l_mipi_dsi_write(dsi->link_mmio, VICH1SET0R, VICH1SET0R_VSTPAFT);
	ret = read_poll_timeout(rzg2l_mipi_dsi_read, status,
				(status & VICH1SR_STOP) && (!(status & VICH1SR_RUNNING)),
				2000, 20000, false, dsi->link_mmio, VICH1SR);
	if (ret < 0)
		goto err;

	ret = read_poll_timeout(rzg2l_mipi_dsi_read, status,
				!(status & LINKSR_HSBUSY),
				2000, 20000, false, dsi->link_mmio, LINKSR);
	if (ret < 0)
		goto err;

	return 0;
err:
	dev_err(dsi->dev, "Failed to stop video signal input\n");
	return ret;
}

/* -----------------------------------------------------------------------------
 * Bridge
 */

static int rzg2l_mipi_dsi_attach(struct drm_bridge *bridge,
				 enum drm_bridge_attach_flags flags)
{
	struct rzg2l_mipi_dsi *dsi = bridge_to_rzg2l_mipi_dsi(bridge);

	return drm_bridge_attach(bridge->encoder, dsi->next_bridge, bridge,
				 flags);
}

static void rzg2l_mipi_dsi_atomic_enable(struct drm_bridge *bridge,
					 struct drm_bridge_state *old_bridge_state)
{
	struct drm_atomic_state *state = old_bridge_state->base.state;
	struct rzg2l_mipi_dsi *dsi = bridge_to_rzg2l_mipi_dsi(bridge);
	const struct drm_display_mode *mode;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	int ret;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	crtc = drm_atomic_get_new_connector_state(state, connector)->crtc;
	mode = &drm_atomic_get_new_crtc_state(state, crtc)->adjusted_mode;

	ret = rzg2l_mipi_dsi_startup(dsi, mode);
	if (ret < 0)
		return;

	rzg2l_mipi_dsi_set_display_timing(dsi, mode);

	ret = rzg2l_mipi_dsi_start_hs_clock(dsi);
	if (ret < 0)
		return;

	ret = rzg2l_mipi_dsi_start_video(dsi);
	if (ret < 0)
		return;
}

static void rzg2l_mipi_dsi_atomic_disable(struct drm_bridge *bridge,
					  struct drm_bridge_state *old_bridge_state)
{
	struct rzg2l_mipi_dsi *dsi = bridge_to_rzg2l_mipi_dsi(bridge);

	rzg2l_mipi_dsi_stop_video(dsi);
	rzg2l_mipi_dsi_stop_hs_clock(dsi);
	pm_runtime_put(dsi->dev);
	reset_control_assert(dsi->rstc);
	reset_control_assert(dsi->arstc);
	reset_control_assert(dsi->prstc);
}

static enum drm_mode_status
rzg2l_mipi_dsi_bridge_mode_valid(struct drm_bridge *bridge,
				 const struct drm_display_info *info,
				 const struct drm_display_mode *mode)
{
	if (mode->clock > 148500)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_bridge_funcs rzg2l_mipi_dsi_bridge_ops = {
	.attach = rzg2l_mipi_dsi_attach,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_enable = rzg2l_mipi_dsi_atomic_enable,
	.atomic_disable = rzg2l_mipi_dsi_atomic_disable,
	.mode_valid = rzg2l_mipi_dsi_bridge_mode_valid,
};

/* -----------------------------------------------------------------------------
 * Host setting
 */

static int rzg2l_mipi_dsi_host_attach(struct mipi_dsi_host *host,
				      struct mipi_dsi_device *device)
{
	struct rzg2l_mipi_dsi *dsi = host_to_rzg2l_mipi_dsi(host);
	int ret;

	if (device->lanes > dsi->num_data_lanes)
		return -EINVAL;

	dsi->lanes = device->lanes;
	dsi->format = device->format;
	dsi->mode_flags = device->mode_flags;

	dsi->next_bridge = devm_drm_of_get_bridge(dsi->dev, dsi->dev->of_node,
						  1, 0);
	if (IS_ERR(dsi->next_bridge)) {
		ret = PTR_ERR(dsi->next_bridge);
		dev_err(dsi->dev, "failed to get next bridge: %d\n", ret);
		return ret;
	}

	/* Initialize the DRM bridge. */
	dsi->bridge.funcs = &rzg2l_mipi_dsi_bridge_ops;
	dsi->bridge.of_node = dsi->dev->of_node;
	drm_bridge_add(&dsi->bridge);

	return 0;
}

static int rzg2l_mipi_dsi_host_detach(struct mipi_dsi_host *host,
				      struct mipi_dsi_device *device)
{
	struct rzg2l_mipi_dsi *dsi = host_to_rzg2l_mipi_dsi(host);

	drm_bridge_remove(&dsi->bridge);
	return 0;
}

static const struct mipi_dsi_host_ops rzg2l_mipi_dsi_host_ops = {
	.attach = rzg2l_mipi_dsi_host_attach,
	.detach = rzg2l_mipi_dsi_host_detach,
};

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int rzg2l_mipi_dsi_parse_dt(struct rzg2l_mipi_dsi *dsi)
{
	struct device_node *ep;
	u32 data_lanes[4];
	int ret;

	ep = of_graph_get_endpoint_by_regs(dsi->dev->of_node, 1, 0);
	if (!ep) {
		dev_dbg(dsi->dev, "unconnected port@1\n");
		return -ENODEV;
	}

	ret = of_property_read_variable_u32_array(ep, "data-lanes", data_lanes,
						  1, 4);
	of_node_put(ep);

	if (ret < 0) {
		dev_err(dsi->dev, "missing or invalid data-lanes property\n");
		return -ENODEV;
	}

	dsi->num_data_lanes = ret;
	return 0;
}

static int rzg2l_mipi_dsi_probe(struct platform_device *pdev)
{
	struct rzg2l_mipi_dsi *dsi;
	struct resource *mem;
	int ret;

	dsi = devm_kzalloc(&pdev->dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	platform_set_drvdata(pdev, dsi);
	dsi->dev = &pdev->dev;

	ret = rzg2l_mipi_dsi_parse_dt(dsi);
	if (ret < 0)
		return ret;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->link_mmio = devm_ioremap_resource(dsi->dev, mem);
	if (IS_ERR(dsi->link_mmio))
		return PTR_ERR(dsi->link_mmio);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	dsi->phy_mmio = devm_ioremap_resource(dsi->dev, mem);
	if (IS_ERR(dsi->phy_mmio))
		return PTR_ERR(dsi->phy_mmio);

	dsi->vclk = devm_clk_get(dsi->dev, "vclk");
	if (IS_ERR(dsi->vclk))
		return PTR_ERR(dsi->vclk);

	dsi->rstc = devm_reset_control_get(dsi->dev, "rst");
	if (IS_ERR(dsi->rstc))
		return dev_err_probe(dsi->dev, PTR_ERR(dsi->rstc), "failed to get cpg reset\n");

	dsi->arstc = devm_reset_control_get(dsi->dev, "arst");
	if (IS_ERR(dsi->arstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(dsi->arstc), "failed to get cpg reset\n");

	dsi->prstc = devm_reset_control_get(dsi->dev, "prst");
	if (IS_ERR(dsi->arstc))
		return dev_err_probe(dsi->dev, PTR_ERR(dsi->prstc), "failed to get cpg reset\n");

	pm_runtime_enable(dsi->dev);

	/* Init host device */
	dsi->host.dev = dsi->dev;
	dsi->host.ops = &rzg2l_mipi_dsi_host_ops;
	ret = mipi_dsi_host_register(&dsi->host);
	if (ret < 0)
		goto out_pm_get;

	return 0;
out_pm_get:
	pm_runtime_disable(dsi->dev);
	reset_control_assert(dsi->rstc);
	reset_control_assert(dsi->arstc);
	reset_control_assert(dsi->prstc);
	return ret;
}

static int rzg2l_mipi_dsi_remove(struct platform_device *pdev)
{
	struct rzg2l_mipi_dsi *dsi = platform_get_drvdata(pdev);

	mipi_dsi_host_unregister(&dsi->host);
	pm_runtime_disable(&pdev->dev);
	reset_control_assert(dsi->rstc);
	reset_control_assert(dsi->arstc);
	reset_control_assert(dsi->prstc);
	return 0;
}

static const struct of_device_id rzg2l_mipi_dsi_of_table[] = {
	{ .compatible = "renesas,r9a07g044-mipi-dsi" },
	{ /* sentinel */ }
};

static struct platform_driver rzg2l_mipi_dsi_platform_driver = {
	.driver	= {
		.name = "rzg2l-mipi-dsi",
		.of_match_table = rzg2l_mipi_dsi_of_table,
	},
	.probe	= rzg2l_mipi_dsi_probe,
	.remove	= rzg2l_mipi_dsi_remove,
};

module_platform_driver(rzg2l_mipi_dsi_platform_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com");
MODULE_DESCRIPTION("Renesas RZ/G2L MIPI DSI Encoder Driver");
MODULE_LICENSE("GPL");
