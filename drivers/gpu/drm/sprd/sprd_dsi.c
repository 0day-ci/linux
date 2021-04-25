// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_graph.h>
#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include "sprd_drm.h"
#include "sprd_dpu.h"
#include "sprd_dsi.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_ppi.h"

#define host_to_dsi(host) \
	container_of(host, struct sprd_dsi, host)
#define connector_to_dsi(connector) \
	container_of(connector, struct sprd_dsi, connector)

static int regmap_tst_io_write(void *context, u32 reg, u32 val)
{
	struct sprd_dsi *dsi = context;
	struct dsi_context *ctx = &dsi->ctx;

	if (val > 0xff || reg > 0xff)
		return -EINVAL;

	drm_dbg(dsi->drm, "reg = 0x%02x, val = 0x%02x\n", reg, val);

	dsi_phy_test_en(ctx, 1);
	dsi_phy_test_din(ctx, reg);
	dsi_phy_test_clk(ctx, 1);
	dsi_phy_test_clk(ctx, 0);
	dsi_phy_test_en(ctx, 0);
	dsi_phy_test_din(ctx, val);
	dsi_phy_test_clk(ctx, 1);
	dsi_phy_test_clk(ctx, 0);

	return 0;
}

static int regmap_tst_io_read(void *context, u32 reg, u32 *val)
{
	struct sprd_dsi *dsi = context;
	struct dsi_context *ctx = &dsi->ctx;
	int ret;

	if (reg > 0xff)
		return -EINVAL;

	dsi_phy_test_en(ctx, 1);
	dsi_phy_test_din(ctx, reg);
	dsi_phy_test_clk(ctx, 1);
	dsi_phy_test_clk(ctx, 0);
	dsi_phy_test_en(ctx, 0);

	udelay(1);

	ret = dsi_phy_test_dout(ctx);
	if (ret < 0)
		return ret;

	*val = ret;

	drm_dbg(dsi->drm, "reg = 0x%02x, val = 0x%02x\n", reg, *val);
	return 0;
}

static struct regmap_bus regmap_tst_io = {
	.reg_write = regmap_tst_io_write,
	.reg_read = regmap_tst_io_read,
};

static const struct regmap_config byte_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int dphy_wait_pll_locked(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	int i;

	for (i = 0; i < 50000; i++) {
		if (dsi_phy_is_pll_locked(ctx))
			return 0;
		udelay(3);
	}

	drm_err(dsi->drm, "dphy pll can not be locked\n");
	return -ETIMEDOUT;
}

static int dsi_wait_tx_payload_fifo_empty(struct dsi_context *ctx)
{
	int i;

	for (i = 0; i < 5000; i++) {
		if (dsi_is_tx_payload_fifo_empty(ctx))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int dsi_wait_tx_cmd_fifo_empty(struct dsi_context *ctx)
{
	int i;

	for (i = 0; i < 5000; i++) {
		if (dsi_is_tx_cmd_fifo_empty(ctx))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int dsi_wait_rd_resp_completed(struct dsi_context *ctx)
{
	int i;

	for (i = 0; i < 10000; i++) {
		if (dsi_is_bta_returned(ctx))
			return 0;
		udelay(10);
	}

	return -ETIMEDOUT;
}

static u16 calc_bytes_per_pixel_x100(int coding)
{
	u16 Bpp_x100;

	switch (coding) {
	case COLOR_CODE_16BIT_CONFIG1:
	case COLOR_CODE_16BIT_CONFIG2:
	case COLOR_CODE_16BIT_CONFIG3:
		Bpp_x100 = 200;
		break;
	case COLOR_CODE_18BIT_CONFIG1:
	case COLOR_CODE_18BIT_CONFIG2:
		Bpp_x100 = 225;
		break;
	case COLOR_CODE_24BIT:
		Bpp_x100 = 300;
		break;
	case COLOR_CODE_COMPRESSTION:
		Bpp_x100 = 100;
		break;
	case COLOR_CODE_20BIT_YCC422_LOOSELY:
		Bpp_x100 = 250;
		break;
	case COLOR_CODE_24BIT_YCC422:
		Bpp_x100 = 300;
		break;
	case COLOR_CODE_16BIT_YCC422:
		Bpp_x100 = 200;
		break;
	case COLOR_CODE_30BIT:
		Bpp_x100 = 375;
		break;
	case COLOR_CODE_36BIT:
		Bpp_x100 = 450;
		break;
	case COLOR_CODE_12BIT_YCC420:
		Bpp_x100 = 150;
		break;
	default:
		DRM_ERROR("invalid color coding");
		Bpp_x100 = 0;
		break;
	}

	return Bpp_x100;
}

static u8 calc_video_size_step(int coding)
{
	u8 video_size_step;

	switch (coding) {
	case COLOR_CODE_16BIT_CONFIG1:
	case COLOR_CODE_16BIT_CONFIG2:
	case COLOR_CODE_16BIT_CONFIG3:
	case COLOR_CODE_18BIT_CONFIG1:
	case COLOR_CODE_18BIT_CONFIG2:
	case COLOR_CODE_24BIT:
	case COLOR_CODE_COMPRESSTION:
		return video_size_step = 1;
	case COLOR_CODE_20BIT_YCC422_LOOSELY:
	case COLOR_CODE_24BIT_YCC422:
	case COLOR_CODE_16BIT_YCC422:
	case COLOR_CODE_30BIT:
	case COLOR_CODE_36BIT:
	case COLOR_CODE_12BIT_YCC420:
		return video_size_step = 2;
	default:
		DRM_ERROR("invalid color coding");
		return 0;
	}
}

static u16 round_video_size(int coding, u16 video_size)
{
	switch (coding) {
	case COLOR_CODE_16BIT_YCC422:
	case COLOR_CODE_24BIT_YCC422:
	case COLOR_CODE_20BIT_YCC422_LOOSELY:
	case COLOR_CODE_12BIT_YCC420:
		/* round up active H pixels to a multiple of 2 */
		if ((video_size % 2) != 0)
			video_size += 1;
		break;
	default:
		break;
	}

	return video_size;
}

#define SPRD_MIPI_DSI_FMT_DSC 0xff
static u32 fmt_to_coding(u32 fmt)
{
	switch (fmt) {
	case MIPI_DSI_FMT_RGB565:
		return COLOR_CODE_16BIT_CONFIG1;
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB666_PACKED:
		return COLOR_CODE_18BIT_CONFIG1;
	case MIPI_DSI_FMT_RGB888:
		return COLOR_CODE_24BIT;
	case SPRD_MIPI_DSI_FMT_DSC:
		return COLOR_CODE_COMPRESSTION;
	default:
		DRM_ERROR("Unsupported format (%d)\n", fmt);
		return COLOR_CODE_24BIT;
	}
}

#define ns_to_cycle(ns, byte_clk) \
	DIV_ROUND_UP((ns) * (byte_clk), 1000000)

static void sprd_dsi_init(struct dsi_context *ctx)
{
	u16 data_hs2lp, data_lp2hs, clk_hs2lp, clk_lp2hs;
	u16 max_rd_time;
	int div;

	dsi_power_enable(ctx, 0);
	dsi_int0_mask(ctx, 0xffffffff);
	dsi_int1_mask(ctx, 0xffffffff);
	dsi_cmd_mode(ctx);
	dsi_eotp_rx_en(ctx, 0);
	dsi_eotp_tx_en(ctx, 0);
	dsi_ecc_rx_en(ctx, 1);
	dsi_crc_rx_en(ctx, 1);
	dsi_bta_en(ctx, 1);
	dsi_video_vcid(ctx, 0);
	dsi_rx_vcid(ctx, 0);

	div = DIV_ROUND_UP(ctx->byte_clk, ctx->esc_clk);
	dsi_tx_escape_division(ctx, div);

	max_rd_time = ns_to_cycle(ctx->max_rd_time, ctx->byte_clk);
	dsi_max_read_time(ctx, max_rd_time);

	data_hs2lp = ns_to_cycle(ctx->data_hs2lp, ctx->byte_clk);
	data_lp2hs = ns_to_cycle(ctx->data_lp2hs, ctx->byte_clk);
	clk_hs2lp = ns_to_cycle(ctx->clk_hs2lp, ctx->byte_clk);
	clk_lp2hs = ns_to_cycle(ctx->clk_lp2hs, ctx->byte_clk);
	dsi_datalane_hs2lp_config(ctx, data_hs2lp);
	dsi_datalane_lp2hs_config(ctx, data_lp2hs);
	dsi_clklane_hs2lp_config(ctx, clk_hs2lp);
	dsi_clklane_lp2hs_config(ctx, clk_lp2hs);

	dsi_power_enable(ctx, 1);
}

/*
 * Free up resources and shutdown host controller and PHY
 */
static void sprd_dsi_fini(struct dsi_context *ctx)
{
	dsi_int0_mask(ctx, 0xffffffff);
	dsi_int1_mask(ctx, 0xffffffff);
	dsi_power_enable(ctx, 0);
}

/*
 * If not in burst mode, it will compute the video and null packet sizes
 * according to necessity.
 * Configure timers for data lanes and/or clock lane to return to LP when
 * bandwidth is not filled by data.
 */
static int sprd_dsi_dpi_video(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	struct videomode *vm = &ctx->vm;
	u16 Bpp_x100;
	u16 video_size;
	u32 ratio_x1000;
	u16 null_pkt_size = 0;
	u8 video_size_step;
	u32 hs_to;
	u32 total_bytes;
	u32 bytes_per_chunk;
	u32 chunks = 0;
	u32 bytes_left = 0;
	u32 chunk_overhead;
	const u8 pkt_header = 6;
	u8 coding;
	int div;
	u16 hline;

	coding = fmt_to_coding(ctx->format);
	video_size = round_video_size(coding, vm->hactive);
	Bpp_x100 = calc_bytes_per_pixel_x100(coding);
	video_size_step = calc_video_size_step(coding);
	ratio_x1000 = ctx->byte_clk * 1000 / (vm->pixelclock / 1000);
	hline = vm->hactive + vm->hsync_len + vm->hfront_porch +
		vm->hback_porch;

	dsi_power_enable(ctx, 0);
	dsi_dpi_frame_ack_en(ctx, ctx->frame_ack_en);
	dsi_dpi_color_coding(ctx, coding);
	dsi_dpi_video_burst_mode(ctx, ctx->burst_mode);
	dsi_dpi_sig_delay(ctx, 95 * hline * ratio_x1000 / 100000);
	dsi_dpi_hline_time(ctx, hline * ratio_x1000 / 1000);
	dsi_dpi_hsync_time(ctx, vm->hsync_len * ratio_x1000 / 1000);
	dsi_dpi_hbp_time(ctx, vm->hback_porch * ratio_x1000 / 1000);
	dsi_dpi_vact(ctx, vm->vactive);
	dsi_dpi_vfp(ctx, vm->vfront_porch);
	dsi_dpi_vbp(ctx, vm->vback_porch);
	dsi_dpi_vsync(ctx, vm->vsync_len);
	dsi_dpi_hporch_lp_en(ctx, 1);
	dsi_dpi_vporch_lp_en(ctx, 1);

	hs_to = (hline * vm->vactive) + (2 * Bpp_x100) / 100;
	for (div = 0x80; (div < hs_to) && (div > 2); div--) {
		if ((hs_to % div) == 0) {
			dsi_timeout_clock_division(ctx, div);
			dsi_lp_rx_timeout(ctx, hs_to / div);
			dsi_hs_tx_timeout(ctx, hs_to / div);
			break;
		}
	}

	if (ctx->burst_mode == VIDEO_BURST_WITH_SYNC_PULSES) {
		dsi_dpi_video_packet_size(ctx, video_size);
		dsi_dpi_null_packet_size(ctx, 0);
		dsi_dpi_chunk_num(ctx, 0);
	} else {
		/* non burst transmission */
		null_pkt_size = 0;

		/* bytes to be sent - first as one chunk */
		bytes_per_chunk = vm->hactive * Bpp_x100 / 100 + pkt_header;

		/* hline total bytes from the DPI interface */
		total_bytes = (vm->hactive + vm->hfront_porch) *
				ratio_x1000 / ctx->lanes / 1000;

		/* check if the pixels actually fit on the DSI link */
		if (total_bytes < bytes_per_chunk) {
			drm_err(dsi->drm, "current resolution can not be set\n");
			return -EINVAL;
		}

		chunk_overhead = total_bytes - bytes_per_chunk;

		/* overhead higher than 1 -> enable multi packets */
		if (chunk_overhead > 1) {

			/* multi packets */
			for (video_size = video_size_step;
			     video_size < vm->hactive;
			     video_size += video_size_step) {

				if (vm->hactive * 1000 / video_size % 1000)
					continue;

				chunks = vm->hactive / video_size;
				bytes_per_chunk = Bpp_x100 * video_size / 100
						  + pkt_header;
				if (total_bytes >= (bytes_per_chunk * chunks)) {
					bytes_left = total_bytes -
						     bytes_per_chunk * chunks;
					break;
				}
			}

			/* prevent overflow (unsigned - unsigned) */
			if (bytes_left > (pkt_header * chunks)) {
				null_pkt_size = (bytes_left -
						pkt_header * chunks) / chunks;
				/* avoid register overflow */
				if (null_pkt_size > 1023)
					null_pkt_size = 1023;
			}

		} else {

			/* single packet */
			chunks = 1;

			/* must be a multiple of 4 except 18 loosely */
			for (video_size = vm->hactive;
			    (video_size % video_size_step) != 0;
			     video_size++)
				;
		}

		dsi_dpi_video_packet_size(ctx, video_size);
		dsi_dpi_null_packet_size(ctx, null_pkt_size);
		dsi_dpi_chunk_num(ctx, chunks);
	}

	dsi_int0_mask(ctx, ctx->int0_mask);
	dsi_int1_mask(ctx, ctx->int1_mask);
	dsi_power_enable(ctx, 1);

	return 0;
}

static void sprd_dsi_edpi_video(struct dsi_context *ctx)
{
	const u32 fifo_depth = 1096;
	const u32 word_length = 4;
	u32 hactive = ctx->vm.hactive;
	u32 Bpp_x100;
	u32 max_fifo_len;
	u8 coding;

	coding = fmt_to_coding(ctx->format);
	Bpp_x100 = calc_bytes_per_pixel_x100(coding);
	max_fifo_len = word_length * fifo_depth * 100 / Bpp_x100;

	dsi_power_enable(ctx, 0);
	dsi_dpi_color_coding(ctx, coding);
	dsi_tear_effect_ack_en(ctx, ctx->te_ack_en);

	if (max_fifo_len > hactive)
		dsi_edpi_max_pkt_size(ctx, hactive);
	else
		dsi_edpi_max_pkt_size(ctx, max_fifo_len);

	dsi_int0_mask(ctx, ctx->int0_mask);
	dsi_int1_mask(ctx, ctx->int1_mask);
	dsi_power_enable(ctx, 1);
}

/*
 * Send a packet on the generic interface,
 * this function has an active delay to wait for the buffer to clear.
 * The delay is limited to:
 * (param_length / 4) x DSIH_FIFO_ACTIVE_WAIT x register access time
 * the controller restricts the sending of.
 *
 * This function will not be able to send Null and Blanking packets due to
 * controller restriction
 */
static int sprd_dsi_wr_pkt(struct dsi_context *ctx, u8 vc, u8 type,
			const u8 *param, u16 len)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	u8 wc_lsbyte, wc_msbyte;
	u32 payload;
	int i, j, ret;

	if (vc > 3)
		return -EINVAL;


	/* 1st: for long packet, must config payload first */
	ret = dsi_wait_tx_payload_fifo_empty(ctx);
	if (ret) {
		drm_err(dsi->drm, "tx payload fifo is not empty\n");
		return ret;
	}

	if (len > 2) {
		for (i = 0, j = 0; i < len; i += j) {
			payload = 0;
			for (j = 0; (j < 4) && ((j + i) < (len)); j++)
				payload |= param[i + j] << (j * 8);

			dsi_set_packet_payload(ctx, payload);
		}
		wc_lsbyte = len & 0xff;
		wc_msbyte = len >> 8;
	} else {
		wc_lsbyte = (len > 0) ? param[0] : 0;
		wc_msbyte = (len > 1) ? param[1] : 0;
	}

	/* 2nd: then set packet header */
	ret = dsi_wait_tx_cmd_fifo_empty(ctx);
	if (ret) {
		drm_err(dsi->drm, "tx cmd fifo is not empty\n");
		return ret;
	}

	dsi_set_packet_header(ctx, vc, type, wc_lsbyte, wc_msbyte);

	return 0;
}

/*
 * Send READ packet to peripheral using the generic interface,
 * this will force command mode and stop video mode (because of BTA).
 *
 * This function has an active delay to wait for the buffer to clear,
 * the delay is limited to 2 x DSIH_FIFO_ACTIVE_WAIT
 * (waiting for command buffer, and waiting for receiving)
 * @note this function will enable BTA
 */
static int sprd_dsi_rd_pkt(struct dsi_context *ctx, u8 vc, u8 type,
			u8 msb_byte, u8 lsb_byte,
			u8 *buffer, u8 bytes_to_read)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	int i, ret;
	int count = 0;
	u32 temp;

	if (vc > 3)
		return -EINVAL;

	/* 1st: send read command to peripheral */
	if (!dsi_is_tx_cmd_fifo_empty(ctx))
		return -EIO;

	dsi_set_packet_header(ctx, vc, type, lsb_byte, msb_byte);

	/* 2nd: wait peripheral response completed */
	ret = dsi_wait_rd_resp_completed(ctx);
	if (ret) {
		drm_err(dsi->drm, "wait read response time out\n");
		return ret;
	}

	/* 3rd: get data from rx payload fifo */
	if (dsi_is_rx_payload_fifo_empty(ctx)) {
		drm_err(dsi->drm, "rx payload fifo empty\n");
		return -EIO;
	}

	for (i = 0; i < 100; i++) {
		temp = dsi_get_rx_payload(ctx);

		if (count < bytes_to_read)
			buffer[count++] = temp & 0xff;
		if (count < bytes_to_read)
			buffer[count++] = (temp >> 8) & 0xff;
		if (count < bytes_to_read)
			buffer[count++] = (temp >> 16) & 0xff;
		if (count < bytes_to_read)
			buffer[count++] = (temp >> 24) & 0xff;

		if (dsi_is_rx_payload_fifo_empty(ctx))
			return count;
	}

	return 0;
}

static void sprd_dsi_set_work_mode(struct dsi_context *ctx, u8 mode)
{
	if (mode == DSI_MODE_CMD)
		dsi_cmd_mode(ctx);
	else
		dsi_video_mode(ctx);
}

static void sprd_dsi_lp_cmd_enable(struct dsi_context *ctx, bool enable)
{
	if (dsi_is_cmd_mode(ctx))
		dsi_cmd_mode_lp_cmd_en(ctx, enable);
	else
		dsi_video_mode_lp_cmd_en(ctx, enable);
}

static void sprd_dsi_state_reset(struct dsi_context *ctx)
{
	dsi_power_enable(ctx, 0);
	udelay(100);
	dsi_power_enable(ctx, 1);
}

static u32 sprd_dsi_int_status(struct dsi_context *ctx, int index)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	u32 status;

	if (index == 0)
		status = dsi_int0_status(ctx);
	else if (index == 1)
		status = dsi_int1_status(ctx);
	else {
		drm_err(dsi->drm, "invalid dsi IRQ index %d\n", index);
		status = -EINVAL;
	}

	return status;
}

static int sprd_dphy_init(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	int ret;

	dsi_phy_rstz(ctx, 0);
	dsi_phy_shutdownz(ctx, 0);
	dsi_phy_clklane_en(ctx, 0);

	dsi_phy_test_clr(ctx, 0);
	dsi_phy_test_clr(ctx, 1);
	dsi_phy_test_clr(ctx, 0);

	dphy_pll_config(ctx);
	dphy_timing_config(ctx);

	dsi_phy_shutdownz(ctx, 1);
	dsi_phy_rstz(ctx, 1);
	dsi_phy_stop_wait_time(ctx, 0x1C);
	dsi_phy_clklane_en(ctx, 1);
	dsi_phy_datalane_en(ctx);

	ret = dphy_wait_pll_locked(ctx);
	if (ret) {
		drm_err(dsi->drm, "dphy initial failed\n");
		return ret;
	}

	return 0;
}

static void sprd_dphy_fini(struct dsi_context *ctx)
{
	dsi_phy_rstz(ctx, 0);
	dsi_phy_shutdownz(ctx, 0);
	dsi_phy_rstz(ctx, 1);
}

static void sprd_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct sprd_dsi *dsi = encoder_to_dsi(encoder);
	struct sprd_dpu *dpu = to_sprd_crtc(encoder->crtc);
	struct dsi_context *ctx = &dsi->ctx;

	if (ctx->enabled) {
		drm_warn(dsi->drm, "dsi is initialized\n");
		return;
	}

	sprd_dsi_init(ctx);
	if (ctx->work_mode == DSI_MODE_VIDEO)
		sprd_dsi_dpi_video(ctx);
	else
		sprd_dsi_edpi_video(ctx);

	sprd_dphy_init(ctx);

	sprd_dsi_lp_cmd_enable(ctx, true);

	if (dsi->panel) {
		drm_panel_prepare(dsi->panel);
		drm_panel_enable(dsi->panel);
	}

	sprd_dsi_set_work_mode(ctx, ctx->work_mode);
	sprd_dsi_state_reset(ctx);

	if (ctx->nc_clk_en)
		dsi_nc_clk_en(ctx, true);
	else {
		dsi_phy_clk_hs_rqst(ctx, true);
		dphy_wait_pll_locked(ctx);
	}

	sprd_dpu_run(dpu);

	ctx->enabled = true;
}

static void sprd_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct sprd_dsi *dsi = encoder_to_dsi(encoder);
	struct sprd_dpu *dpu = to_sprd_crtc(encoder->crtc);
	struct dsi_context *ctx = &dsi->ctx;

	if (!ctx->enabled) {
		drm_warn(dsi->drm, "dsi isn't initialized\n");
		return;
	}

	sprd_dpu_stop(dpu);
	sprd_dsi_set_work_mode(ctx, DSI_MODE_CMD);
	sprd_dsi_lp_cmd_enable(ctx, true);

	if (dsi->panel) {
		drm_panel_disable(dsi->panel);
		drm_panel_unprepare(dsi->panel);
	}

	sprd_dphy_fini(ctx);
	sprd_dsi_fini(ctx);

	ctx->enabled = false;
}

static void sprd_dsi_encoder_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adj_mode)
{
	struct sprd_dsi *dsi = encoder_to_dsi(encoder);

	drm_dbg(dsi->drm, "%s() set mode: %s\n", __func__, dsi->mode->name);
}

static int sprd_dsi_encoder_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	return 0;
}

static const struct drm_encoder_helper_funcs sprd_encoder_helper_funcs = {
	.atomic_check	= sprd_dsi_encoder_atomic_check,
	.mode_set	= sprd_dsi_encoder_mode_set,
	.enable		= sprd_dsi_encoder_enable,
	.disable	= sprd_dsi_encoder_disable
};

static const struct drm_encoder_funcs sprd_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static struct sprd_dsi *sprd_dsi_encoder_init(struct drm_device *drm,
			       struct device *dev)
{
	struct sprd_dsi *dsi;
	u32 crtc_mask;

	crtc_mask = drm_of_find_possible_crtcs(drm, dev->of_node);
	if (!crtc_mask) {
		drm_err(drm, "failed to find crtc mask\n");
		return ERR_PTR(-EINVAL);
	}

	drm_dbg(drm, "find possible crtcs: 0x%08x\n", crtc_mask);

	dsi = drmm_encoder_alloc(drm, struct sprd_dsi, encoder,
			       &sprd_encoder_funcs, DRM_MODE_ENCODER_DSI, NULL);
	if (IS_ERR(dsi)) {
		drm_err(drm, "failed to init dsi encoder.\n");
		return dsi;
	}

	dsi->encoder.possible_crtcs = crtc_mask;
	drm_encoder_helper_add(&dsi->encoder, &sprd_encoder_helper_funcs);

	return dsi;
}

static int sprd_dsi_find_panel(struct sprd_dsi *dsi)
{
	struct device *dev = dsi->host.dev;
	struct device_node *child, *lcds_node;
	struct drm_panel *panel;

	/* search /lcds child node first */
	lcds_node = of_find_node_by_path("/lcds");
	for_each_child_of_node(lcds_node, child) {
		panel = of_drm_find_panel(child);
		if (!IS_ERR(panel)) {
			dsi->panel = panel;
			return 0;
		}
	}

	/*
	 * If /lcds child node search failed, we search
	 * the child of dsi host node.
	 */
	for_each_child_of_node(dev->of_node, child) {
		panel = of_drm_find_panel(child);
		if (!IS_ERR(panel)) {
			dsi->panel = panel;
			return 0;
		}
	}

	drm_err(dsi->drm, "of_drm_find_panel() failed\n");
	return -ENODEV;
}

static int sprd_dsi_host_attach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *slave)
{
	struct sprd_dsi *dsi = host_to_dsi(host);
	struct dsi_context *ctx = &dsi->ctx;
	int ret;

	dsi->slave = slave;
	ctx->lanes = slave->lanes;
	ctx->format = slave->format;
	ctx->byte_clk = slave->hs_rate / 8;
	ctx->esc_clk = slave->lp_rate;

	if (slave->mode_flags & MIPI_DSI_MODE_VIDEO)
		ctx->work_mode = DSI_MODE_VIDEO;
	else
		ctx->work_mode = DSI_MODE_CMD;

	if (slave->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		ctx->burst_mode = VIDEO_BURST_WITH_SYNC_PULSES;
	else if (slave->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		ctx->burst_mode = VIDEO_NON_BURST_WITH_SYNC_PULSES;
	else
		ctx->burst_mode = VIDEO_NON_BURST_WITH_SYNC_EVENTS;

	if (slave->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
		ctx->nc_clk_en = true;

	ret = sprd_dsi_find_panel(dsi);
	if (ret)
		return ret;

	return 0;
}

static int sprd_dsi_host_detach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *slave)
{
	/* do nothing */
	return 0;
}

static ssize_t sprd_dsi_host_transfer(struct mipi_dsi_host *host,
				const struct mipi_dsi_msg *msg)
{
	struct sprd_dsi *dsi = host_to_dsi(host);
	const u8 *tx_buf = msg->tx_buf;

	if (msg->rx_buf && msg->rx_len) {
		u8 lsb = (msg->tx_len > 0) ? tx_buf[0] : 0;
		u8 msb = (msg->tx_len > 1) ? tx_buf[1] : 0;

		return sprd_dsi_rd_pkt(&dsi->ctx, msg->channel, msg->type,
				msb, lsb, msg->rx_buf, msg->rx_len);
	}

	if (msg->tx_buf && msg->tx_len)
		return sprd_dsi_wr_pkt(&dsi->ctx, msg->channel, msg->type,
					tx_buf, msg->tx_len);

	return 0;
}

static const struct mipi_dsi_host_ops sprd_dsi_host_ops = {
	.attach = sprd_dsi_host_attach,
	.detach = sprd_dsi_host_detach,
	.transfer = sprd_dsi_host_transfer,
};

static int sprd_dsi_host_init(struct sprd_dsi *dsi, struct device *dev)
{
	int ret;

	dsi->host.dev = dev;
	dsi->host.ops = &sprd_dsi_host_ops;

	ret = mipi_dsi_host_register(&dsi->host);
	if (ret)
		drm_err(dsi->drm, "failed to register dsi host\n");

	return ret;
}

static int sprd_dsi_connector_get_modes(struct drm_connector *connector)
{
	struct sprd_dsi *dsi = connector_to_dsi(connector);

	return drm_panel_get_modes(dsi->panel, connector);
}

static enum drm_mode_status
sprd_dsi_connector_mode_valid(struct drm_connector *connector,
			 struct drm_display_mode *mode)
{
	struct sprd_dsi *dsi = connector_to_dsi(connector);

	drm_dbg(dsi->drm, "%s() mode: "DRM_MODE_FMT"\n", __func__, DRM_MODE_ARG(mode));

	if (mode->type & DRM_MODE_TYPE_PREFERRED) {
		dsi->mode = mode;
		drm_display_mode_to_videomode(dsi->mode, &dsi->ctx.vm);
	}

	return MODE_OK;
}

static struct drm_encoder *
sprd_dsi_connector_best_encoder(struct drm_connector *connector)
{
	struct sprd_dsi *dsi = connector_to_dsi(connector);

	return &dsi->encoder;
}

static struct drm_connector_helper_funcs sprd_dsi_connector_helper_funcs = {
	.get_modes = sprd_dsi_connector_get_modes,
	.mode_valid = sprd_dsi_connector_mode_valid,
	.best_encoder = sprd_dsi_connector_best_encoder,
};

static enum drm_connector_status
sprd_dsi_connector_detect(struct drm_connector *connector, bool force)
{
	struct sprd_dsi *dsi = connector_to_dsi(connector);

	if (dsi->panel) {
		drm_panel_add(dsi->panel);
		return connector_status_connected;
	}

	return connector_status_disconnected;
}

static void sprd_dsi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs sprd_dsi_atomic_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = sprd_dsi_connector_detect,
	.destroy = sprd_dsi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int sprd_dsi_connector_init(struct drm_device *drm, struct sprd_dsi *dsi)
{
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_connector *connector = &dsi->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(drm, connector,
				 &sprd_dsi_atomic_connector_funcs,
				 DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		drm_err(drm, "drm_connector_init() failed\n");
		return ret;
	}

	drm_connector_helper_add(connector,
				 &sprd_dsi_connector_helper_funcs);

	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

static irqreturn_t sprd_dsi_isr(int irq, void *data)
{
	struct sprd_dsi *dsi = data;
	u32 status = 0;

	if (dsi->ctx.irq0 == irq)
		status = sprd_dsi_int_status(&dsi->ctx, 0);
	else if (dsi->ctx.irq1 == irq)
		status = sprd_dsi_int_status(&dsi->ctx, 1);

	if (status & DSI_INT_STS_NEED_SOFT_RESET)
		sprd_dsi_state_reset(&dsi->ctx);

	return IRQ_HANDLED;
}

static int sprd_dsi_context_init(struct sprd_dsi *dsi,
			struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dsi_context *ctx = &dsi->ctx;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctx->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!ctx->base) {
		drm_err(dsi->drm, "failed to map dsi host registers\n");
		return -ENXIO;
	}

	ctx->pll = devm_kzalloc(dev, sizeof(*ctx->pll), GFP_KERNEL);
	if (!ctx->pll)
		return -ENOMEM;

	ctx->regmap = devm_regmap_init(dev, &regmap_tst_io, dsi, &byte_config);
	if (IS_ERR(ctx->regmap)) {
		drm_err(dsi->drm, "dphy regmap init failed\n");
		return PTR_ERR(ctx->regmap);
	}

	ctx->irq0 = platform_get_irq(pdev, 0);
	if (ctx->irq0 > 0) {
		ret = request_irq(ctx->irq0, sprd_dsi_isr, 0, "DSI_INT0", dsi);
		if (ret) {
			drm_err(dsi->drm, "failed to request dsi irq int0!\n");
			return -EINVAL;
		}
	}

	ctx->irq1 = platform_get_irq(pdev, 1);
	if (ctx->irq1 > 0) {
		ret = request_irq(ctx->irq1, sprd_dsi_isr, 0, "DSI_INT1", dsi);
		if (ret) {
			drm_err(dsi->drm, "failed to request dsi irq int1!\n");
			return -EINVAL;
		}
	}

	ctx->data_hs2lp = 120;
	ctx->data_lp2hs = 500;
	ctx->clk_hs2lp = 4;
	ctx->clk_lp2hs = 15;
	ctx->max_rd_time = 6000;
	ctx->int0_mask = 0xffffffff;
	ctx->int1_mask = 0xffffffff;
	ctx->enabled = true;

	return 0;
}

static int sprd_dsi_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct sprd_dsi *dsi;
	int ret;

	dsi = sprd_dsi_encoder_init(drm, dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);

	dsi->drm = drm;
	dev_set_drvdata(dev, dsi);

	ret = sprd_dsi_connector_init(drm, dsi);
	if (ret)
		return ret;

	ret = sprd_dsi_context_init(dsi, dev);
	if (ret)
		return ret;

	ret = sprd_dsi_host_init(dsi, dev);
	if (ret)
		return ret;

	return 0;
}

static void sprd_dsi_unbind(struct device *dev,
			struct device *master, void *data)
{
	struct sprd_dsi *dsi = dev_get_drvdata(dev);

	mipi_dsi_host_unregister(&dsi->host);
}

static const struct component_ops dsi_component_ops = {
	.bind	= sprd_dsi_bind,
	.unbind	= sprd_dsi_unbind,
};

static const struct of_device_id dsi_match_table[] = {
	{ .compatible = "sprd,sharkl3-dsi-host" },
	{ /* sentinel */ },
};

static int sprd_dsi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dsi_component_ops);
}

static int sprd_dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dsi_component_ops);

	return 0;
}

struct platform_driver sprd_dsi_driver = {
	.probe = sprd_dsi_probe,
	.remove = sprd_dsi_remove,
	.driver = {
		.name = "sprd-dsi-drv",
		.of_match_table = dsi_match_table,
	},
};

MODULE_AUTHOR("Leon He <leon.he@unisoc.com>");
MODULE_AUTHOR("Kevin Tang <kevin.tang@unisoc.com>");
MODULE_DESCRIPTION("Unisoc MIPI DSI HOST Controller Driver");
MODULE_LICENSE("GPL v2");
