// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/module.h>

#include "dsi_ctrl.h"

/*
 * Modify power status of DSI Host core
 */
void dsi_power_enable(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(enable, &reg->SOFT_RESET);
}
/*
 * Enable/disable DPI video mode
 */
void dsi_video_mode(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(0, &reg->DSI_MODE_CFG);
}
/*
 * Enable command mode (Generic interface)
 */
void dsi_cmd_mode(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(1, &reg->DSI_MODE_CFG);
}

bool dsi_is_cmd_mode(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	return readl(&reg->DSI_MODE_CFG);
}
/*
 * Configure the read back virtual channel for the generic interface
 */
void dsi_rx_vcid(struct dsi_context *ctx, u8 vc)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x1C virtual_channel_id;

	virtual_channel_id.val = readl(&reg->VIRTUAL_CHANNEL_ID);
	virtual_channel_id.bits.gen_rx_vcid = vc;

	writel(virtual_channel_id.val, &reg->VIRTUAL_CHANNEL_ID);
}
/*
 * Write the DPI video virtual channel destination
 */
void dsi_video_vcid(struct dsi_context *ctx, u8 vc)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x1C virtual_channel_id;

	virtual_channel_id.val = readl(&reg->VIRTUAL_CHANNEL_ID);
	virtual_channel_id.bits.video_pkt_vcid = vc;

	writel(virtual_channel_id.val, &reg->VIRTUAL_CHANNEL_ID);
}
/*
 * Set DPI video mode type (burst/non-burst - with sync pulses or events)
 */
void dsi_dpi_video_burst_mode(struct dsi_context *ctx, int mode)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x38 vid_mode_cfg;

	vid_mode_cfg.val = readl(&reg->VID_MODE_CFG);
	vid_mode_cfg.bits.vid_mode_type = mode;

	writel(vid_mode_cfg.val, &reg->VID_MODE_CFG);
}
/*
 * Set DPI video color coding
 */
void dsi_dpi_color_coding(struct dsi_context *ctx, int coding)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x20 dpi_video_format;

	dpi_video_format.val = readl(&reg->DPI_VIDEO_FORMAT);
	dpi_video_format.bits.dpi_video_mode_format = coding;

	writel(dpi_video_format.val, &reg->DPI_VIDEO_FORMAT);
}
/*
 * Configure the Horizontal Line time
 * param "byte_cycle" taken to transmit the total of the horizontal line
 */
void dsi_dpi_hline_time(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x2C video_line_time;

	video_line_time.val = readl(&reg->VIDEO_LINE_TIME);
	video_line_time.bits.video_line_time = byte_cycle;

	writel(video_line_time.val, &reg->VIDEO_LINE_TIME);
}
/*
 * Configure the Horizontal back porch time
 * param "byte_cycle" taken to transmit the horizontal back porch
 */
void dsi_dpi_hbp_time(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x28 video_line_hblk_time;

	video_line_hblk_time.val = readl(&reg->VIDEO_LINE_HBLK_TIME);
	video_line_hblk_time.bits.video_line_hbp_time = byte_cycle;

	writel(video_line_hblk_time.val, &reg->VIDEO_LINE_HBLK_TIME);
}
/*
 * Configure the Horizontal sync time,
 * param "byte_cycle" taken to transmit the horizontal sync
 */
void dsi_dpi_hsync_time(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x28 video_line_hblk_time;

	video_line_hblk_time.val = readl(&reg->VIDEO_LINE_HBLK_TIME);
	video_line_hblk_time.bits.video_line_hsa_time = byte_cycle;

	writel(video_line_hblk_time.val, &reg->VIDEO_LINE_HBLK_TIME);
}
/*
 * Configure the vertical active lines of the video stream
 */
void dsi_dpi_vact(struct dsi_context *ctx, u16 lines)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x34 video_active_lines;

	video_active_lines.val = readl(&reg->VIDEO_VACTIVE_LINES);
	video_active_lines.bits.vactive_lines = lines;

	writel(video_active_lines.val, &reg->VIDEO_VACTIVE_LINES);
}

void dsi_dpi_vfp(struct dsi_context *ctx, u16 lines)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x30 video_vblk_lines;

	video_vblk_lines.val = readl(&reg->VIDEO_VBLK_LINES);
	video_vblk_lines.bits.vfp_lines = lines;

	writel(video_vblk_lines.val, &reg->VIDEO_VBLK_LINES);
}

void dsi_dpi_vbp(struct dsi_context *ctx, u16 lines)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x30 video_vblk_lines;

	video_vblk_lines.val = readl(&reg->VIDEO_VBLK_LINES);
	video_vblk_lines.bits.vbp_lines = lines;

	writel(video_vblk_lines.val, &reg->VIDEO_VBLK_LINES);
}

void dsi_dpi_vsync(struct dsi_context *ctx, u16 lines)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x30 video_vblk_lines;

	video_vblk_lines.val = readl(&reg->VIDEO_VBLK_LINES);
	video_vblk_lines.bits.vsa_lines = lines;

	writel(video_vblk_lines.val, &reg->VIDEO_VBLK_LINES);
}

void dsi_dpi_hporch_lp_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x38 vid_mode_cfg;

	vid_mode_cfg.val = readl(&reg->VID_MODE_CFG);

	vid_mode_cfg.bits.lp_hfp_en = enable;
	vid_mode_cfg.bits.lp_hbp_en = enable;

	writel(vid_mode_cfg.val, &reg->VID_MODE_CFG);
}
/*
 * Enable return to low power mode inside vertical active lines periods when
 * timing allows
 */
void dsi_dpi_vporch_lp_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x38 vid_mode_cfg;

	vid_mode_cfg.val = readl(&reg->VID_MODE_CFG);

	vid_mode_cfg.bits.lp_vact_en = enable;
	vid_mode_cfg.bits.lp_vfp_en = enable;
	vid_mode_cfg.bits.lp_vbp_en = enable;
	vid_mode_cfg.bits.lp_vsa_en = enable;

	writel(vid_mode_cfg.val, &reg->VID_MODE_CFG);
}
/*
 * Enable FRAME BTA ACK
 */
void dsi_dpi_frame_ack_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x38 vid_mode_cfg;

	vid_mode_cfg.val = readl(&reg->VID_MODE_CFG);
	vid_mode_cfg.bits.frame_bta_ack_en = enable;

	writel(vid_mode_cfg.val, &reg->VID_MODE_CFG);
}
/*
 * Write no of chunks to core - taken into consideration only when multi packet
 * is enabled
 */
void dsi_dpi_chunk_num(struct dsi_context *ctx, u16 num)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x24 video_pkt_config;

	video_pkt_config.val = readl(&reg->VIDEO_PKT_CONFIG);
	video_pkt_config.bits.video_line_chunk_num = num;

	writel(video_pkt_config.val, &reg->VIDEO_PKT_CONFIG);
}
/*
 * Write the null packet size - will only be taken into account when null
 * packets are enabled.
 */
void dsi_dpi_null_packet_size(struct dsi_context *ctx, u16 size)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xC0 video_nullpkt_size;

	video_nullpkt_size.val = readl(&reg->VIDEO_NULLPKT_SIZE);
	video_nullpkt_size.bits.video_nullpkt_size = size;

	writel(video_nullpkt_size.val, &reg->VIDEO_NULLPKT_SIZE);
}
/*
 * Write video packet size. obligatory for sending video
 */
void dsi_dpi_video_packet_size(struct dsi_context *ctx, u16 size)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x24 video_pkt_config;

	video_pkt_config.val = readl(&reg->VIDEO_PKT_CONFIG);
	video_pkt_config.bits.video_pkt_size = size;

	writel(video_pkt_config.val, &reg->VIDEO_PKT_CONFIG);
}
/*
 * Specifiy the size of the packet memory write start/continue
 */
void dsi_edpi_max_pkt_size(struct dsi_context *ctx, u16 size)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xC4 dcs_wm_pkt_size;

	dcs_wm_pkt_size.val = readl(&reg->DCS_WM_PKT_SIZE);
	dcs_wm_pkt_size.bits.dcs_wm_pkt_size = size;

	writel(dcs_wm_pkt_size.val, &reg->DCS_WM_PKT_SIZE);
}
/*
 * Enable tear effect acknowledge
 */
void dsi_tear_effect_ack_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x68 cmd_mode_cfg;

	cmd_mode_cfg.val = readl(&reg->CMD_MODE_CFG);
	cmd_mode_cfg.bits.tear_fx_en = enable;

	writel(cmd_mode_cfg.val, &reg->CMD_MODE_CFG);
}
/*
 * Set DCS command packet transmission to transmission type
 */
void dsi_cmd_mode_lp_cmd_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x68 cmd_mode_cfg;

	cmd_mode_cfg.val = readl(&reg->CMD_MODE_CFG);

	cmd_mode_cfg.bits.gen_sw_0p_tx = enable;
	cmd_mode_cfg.bits.gen_sw_1p_tx = enable;
	cmd_mode_cfg.bits.gen_sw_2p_tx = enable;
	cmd_mode_cfg.bits.gen_lw_tx = enable;
	cmd_mode_cfg.bits.dcs_sw_0p_tx = enable;
	cmd_mode_cfg.bits.dcs_sw_1p_tx = enable;
	cmd_mode_cfg.bits.dcs_lw_tx = enable;
	cmd_mode_cfg.bits.max_rd_pkt_size = enable;

	cmd_mode_cfg.bits.gen_sr_0p_tx = enable;
	cmd_mode_cfg.bits.gen_sr_1p_tx = enable;
	cmd_mode_cfg.bits.gen_sr_2p_tx = enable;
	cmd_mode_cfg.bits.dcs_sr_0p_tx = enable;

	writel(cmd_mode_cfg.val, &reg->CMD_MODE_CFG);
}
/*
 * Set DCS read command packet transmission to transmission type
 */
void dsi_video_mode_lp_cmd_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x38 vid_mode_cfg;

	vid_mode_cfg.val = readl(&reg->VID_MODE_CFG);
	vid_mode_cfg.bits.lp_cmd_en = enable;

	writel(vid_mode_cfg.val, &reg->VID_MODE_CFG);
}

/*
 * Write command header in the generic interface (which also sends DCS commands) as a subset
 */
void dsi_set_packet_header(struct dsi_context *ctx,
				   u8 vc,
				   u8 type,
				   u8 wc_lsb,
				   u8 wc_msb)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x6C gen_hdr;

	gen_hdr.bits.gen_dt = type;
	gen_hdr.bits.gen_vc = vc;
	gen_hdr.bits.gen_wc_lsbyte = wc_lsb;
	gen_hdr.bits.gen_wc_msbyte = wc_msb;

	writel(gen_hdr.val, &reg->GEN_HDR);
}
/*
 * Write the payload of the long packet commands
 */
void dsi_set_packet_payload(struct dsi_context *ctx, u32 payload)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(payload, &reg->GEN_PLD_DATA);
}
/*
 * Read the payload of the long packet commands
 */
u32 dsi_get_rx_payload(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	return readl(&reg->GEN_PLD_DATA);
}

/*
 * Enable Bus Turn-around request
 */
void dsi_bta_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(enable, &reg->TA_EN);
}
/*
 * Enable EOTp reception
 */
void dsi_eotp_rx_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xBC eotp_en;

	eotp_en.val = readl(&reg->EOTP_EN);
	eotp_en.bits.rx_eotp_en = enable;

	writel(eotp_en.val, &reg->EOTP_EN);
}
/*
 * Enable EOTp transmission
 */
void dsi_eotp_tx_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xBC eotp_en;

	eotp_en.val = readl(&reg->EOTP_EN);
	eotp_en.bits.tx_eotp_en = enable;

	writel(eotp_en.val, &reg->EOTP_EN);
}
/*
 * Enable ECC reception, error correction and reporting
 */
void dsi_ecc_rx_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xB4 rx_pkt_check_config;

	rx_pkt_check_config.val = readl(&reg->RX_PKT_CHECK_CONFIG);
	rx_pkt_check_config.bits.rx_pkt_ecc_en = enable;

	writel(rx_pkt_check_config.val, &reg->RX_PKT_CHECK_CONFIG);
}
/*
 * Enable CRC reception, error reporting
 */
void dsi_crc_rx_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xB4 rx_pkt_check_config;

	rx_pkt_check_config.val = readl(&reg->RX_PKT_CHECK_CONFIG);
	rx_pkt_check_config.bits.rx_pkt_crc_en = enable;

	writel(rx_pkt_check_config.val, &reg->RX_PKT_CHECK_CONFIG);
}
/*
 * Get status of read command
 */
bool dsi_is_bta_returned(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x98 cmd_mode_status;

	cmd_mode_status.val = readl(&reg->CMD_MODE_STATUS);

	return cmd_mode_status.bits.gen_cmd_rdcmd_done;
}
/*
 * Get the FULL status of generic read payload fifo
 */
bool dsi_is_rx_payload_fifo_full(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x98 cmd_mode_status;

	cmd_mode_status.val = readl(&reg->CMD_MODE_STATUS);

	return cmd_mode_status.bits.gen_cmd_rdata_fifo_full;
}
/*
 * Get the EMPTY status of generic read payload fifo
 */
bool dsi_is_rx_payload_fifo_empty(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x98 cmd_mode_status;

	cmd_mode_status.val = readl(&reg->CMD_MODE_STATUS);

	return cmd_mode_status.bits.gen_cmd_rdata_fifo_empty;
}
/*
 * Get the FULL status of generic write payload fifo
 */
bool dsi_is_tx_payload_fifo_full(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x98 cmd_mode_status;

	cmd_mode_status.val = readl(&reg->CMD_MODE_STATUS);

	return cmd_mode_status.bits.gen_cmd_wdata_fifo_full;
}
/*
 * Get the EMPTY status of generic write payload fifo
 */
bool dsi_is_tx_payload_fifo_empty(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x98 cmd_mode_status;

	cmd_mode_status.val = readl(&reg->CMD_MODE_STATUS);

	return cmd_mode_status.bits.gen_cmd_wdata_fifo_empty;
}
/*
 * Get the EMPTY status of generic command fifo
 */
bool dsi_is_tx_cmd_fifo_empty(struct dsi_context *ctx)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x98 cmd_mode_status;

	cmd_mode_status.val = readl(&reg->CMD_MODE_STATUS);

	return cmd_mode_status.bits.gen_cmd_cmd_fifo_empty;
}
/*
 * DPI interface signal delay config
 * param byte_cycle period for waiting after controller receiving HSYNC from
 * DPI interface to start read pixel data from memory.
 */
void dsi_dpi_sig_delay(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xD0 video_sig_delay_config;

	video_sig_delay_config.val = readl(&reg->VIDEO_SIG_DELAY_CONFIG);
	video_sig_delay_config.bits.video_sig_delay = byte_cycle;

	writel(video_sig_delay_config.val, &reg->VIDEO_SIG_DELAY_CONFIG);
}
/*
 * Configure how many cycles of byte clock would the PHY module take
 * to switch data lane from high speed to low power
 */
void dsi_datalane_hs2lp_config(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xAC phy_datalane_time_config;

	phy_datalane_time_config.val = readl(&reg->PHY_DATALANE_TIME_CONFIG);
	phy_datalane_time_config.bits.phy_datalane_hs_to_lp_time = byte_cycle;

	writel(phy_datalane_time_config.val, &reg->PHY_DATALANE_TIME_CONFIG);
}
/*
 * Configure how many cycles of byte clock would the PHY module take
 * to switch the data lane from to low power high speed
 */
void dsi_datalane_lp2hs_config(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xAC phy_datalane_time_config;

	phy_datalane_time_config.val = readl(&reg->PHY_DATALANE_TIME_CONFIG);
	phy_datalane_time_config.bits.phy_datalane_lp_to_hs_time = byte_cycle;

	writel(phy_datalane_time_config.val, &reg->PHY_DATALANE_TIME_CONFIG);
}
/*
 * Configure how many cycles of byte clock would the PHY module take
 * to switch clock lane from high speed to low power
 */
void dsi_clklane_hs2lp_config(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xA8 phy_clklane_time_config;

	phy_clklane_time_config.val = readl(&reg->PHY_CLKLANE_TIME_CONFIG);
	phy_clklane_time_config.bits.phy_clklane_hs_to_lp_time = byte_cycle;

	writel(phy_clklane_time_config.val, &reg->PHY_CLKLANE_TIME_CONFIG);
}
/*
 * Configure how many cycles of byte clock would the PHY module take
 * to switch clock lane from to low power high speed
 */
void dsi_clklane_lp2hs_config(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0xA8 phy_clklane_time_config;

	phy_clklane_time_config.val = readl(&reg->PHY_CLKLANE_TIME_CONFIG);
	phy_clklane_time_config.bits.phy_clklane_lp_to_hs_time = byte_cycle;

	writel(phy_clklane_time_config.val, &reg->PHY_CLKLANE_TIME_CONFIG);
}
/*
 * Configure how many cycles of byte clock would the PHY module take
 * to turn the bus around to start receiving
 */
void dsi_max_read_time(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(byte_cycle, &reg->MAX_READ_TIME);
}
/*
 * Enable the automatic mechanism to stop providing clock in the clock
 * lane when time allows
 */
void dsi_nc_clk_en(struct dsi_context *ctx, int enable)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x74 phy_clk_lane_lp_ctrl;

	phy_clk_lane_lp_ctrl.val = readl(&reg->PHY_CLK_LANE_LP_CTRL);
	phy_clk_lane_lp_ctrl.bits.auto_clklane_ctrl_en = enable;

	writel(phy_clk_lane_lp_ctrl.val, &reg->PHY_CLK_LANE_LP_CTRL);
}
/*
 * Write transmission escape timeout
 * a safe guard so that the state machine would reset if transmission
 * takes too long
 */
void dsi_tx_escape_division(struct dsi_context *ctx, u8 div)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(div, &reg->TX_ESC_CLK_CONFIG);
}
/*
 * Configure timeout divisions (so they would have more clock ticks)
 * div no of hs cycles before transiting back to LP in
 *  (lane_clk / div)
 */
void dsi_timeout_clock_division(struct dsi_context *ctx, u8 div)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(div, &reg->TIMEOUT_CNT_CLK_CONFIG);
}
/*
 * Configure the Low power receive time out
 */
void dsi_lp_rx_timeout(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(byte_cycle, &reg->LRX_H_TO_CONFIG);
}
/*
 * Configure a high speed transmission time out
 */
void dsi_hs_tx_timeout(struct dsi_context *ctx, u16 byte_cycle)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(byte_cycle, &reg->HTX_TO_CONFIG);
}
/*
 * Get the error 0 interrupt register status
 */
u32 dsi_int0_status(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x08 protocol_int_sts;

	protocol_int_sts.val = readl(&reg->PROTOCOL_INT_STS);
	writel(protocol_int_sts.val, &reg->PROTOCOL_INT_CLR);

	if (protocol_int_sts.bits.dphy_errors_0)
		drm_err(dsi->drm, "dphy_err: escape entry error\n");

	if (protocol_int_sts.bits.dphy_errors_1)
		drm_err(dsi->drm, "dphy_err: lp data transmission sync error\n");

	if (protocol_int_sts.bits.dphy_errors_2)
		drm_err(dsi->drm, "dphy_err: control error\n");

	if (protocol_int_sts.bits.dphy_errors_3)
		drm_err(dsi->drm, "dphy_err: LP0 contention error\n");

	if (protocol_int_sts.bits.dphy_errors_4)
		drm_err(dsi->drm, "dphy_err: LP1 contention error\n");

	if (protocol_int_sts.bits.ack_with_err_0)
		drm_err(dsi->drm, "ack_err: SoT error\n");

	if (protocol_int_sts.bits.ack_with_err_1)
		drm_err(dsi->drm, "ack_err: SoT Sync error\n");

	if (protocol_int_sts.bits.ack_with_err_2)
		drm_err(dsi->drm, "ack_err: EoT Sync error\n");

	if (protocol_int_sts.bits.ack_with_err_3)
		drm_err(dsi->drm, "ack_err: Escape Mode Entry Command error\n");

	if (protocol_int_sts.bits.ack_with_err_4)
		drm_err(dsi->drm, "ack_err: LP Transmit Sync error\n");

	if (protocol_int_sts.bits.ack_with_err_5)
		drm_err(dsi->drm, "ack_err: Peripheral Timeout error\n");

	if (protocol_int_sts.bits.ack_with_err_6)
		drm_err(dsi->drm, "ack_err: False Control error\n");

	if (protocol_int_sts.bits.ack_with_err_7)
		drm_err(dsi->drm, "ack_err: reserved (specific to device)\n");

	if (protocol_int_sts.bits.ack_with_err_8)
		drm_err(dsi->drm, "ack_err: ECC error, single-bit (corrected)\n");

	if (protocol_int_sts.bits.ack_with_err_9)
		drm_err(dsi->drm, "ack_err: ECC error, multi-bit (not corrected)\n");

	if (protocol_int_sts.bits.ack_with_err_10)
		drm_err(dsi->drm, "ack_err: checksum error (long packet only)\n");

	if (protocol_int_sts.bits.ack_with_err_11)
		drm_err(dsi->drm, "ack_err: not recognized DSI data type\n");

	if (protocol_int_sts.bits.ack_with_err_12)
		drm_err(dsi->drm, "ack_err: DSI VC ID Invalid\n");

	if (protocol_int_sts.bits.ack_with_err_13)
		drm_err(dsi->drm, "ack_err: invalid transmission length\n");

	if (protocol_int_sts.bits.ack_with_err_14)
		drm_err(dsi->drm, "ack_err: reserved (specific to device)\n");

	if (protocol_int_sts.bits.ack_with_err_15)
		drm_err(dsi->drm, "ack_err: DSI protocol violation\n");

	return 0;
}
/*
 * Get the error 1 interrupt register status
 */
u32 dsi_int1_status(struct dsi_context *ctx)
{
	struct sprd_dsi *dsi = container_of(ctx, struct sprd_dsi, ctx);
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;
	union _0x10 internal_int_sts;
	u32 status = 0;

	internal_int_sts.val = readl(&reg->INTERNAL_INT_STS);
	writel(internal_int_sts.val, &reg->INTERNAL_INT_CLR);

	if (internal_int_sts.bits.receive_pkt_size_err)
		drm_err(dsi->drm, "receive packet size error\n");

	if (internal_int_sts.bits.eotp_not_receive_err)
		drm_err(dsi->drm, "EoTp packet is not received\n");

	if (internal_int_sts.bits.gen_cmd_cmd_fifo_wr_err)
		drm_err(dsi->drm, "cmd header-fifo is full\n");

	if (internal_int_sts.bits.gen_cmd_rdata_fifo_rd_err)
		drm_err(dsi->drm, "cmd read-payload-fifo is empty\n");

	if (internal_int_sts.bits.gen_cmd_rdata_fifo_wr_err)
		drm_err(dsi->drm, "cmd read-payload-fifo is full\n");

	if (internal_int_sts.bits.gen_cmd_wdata_fifo_wr_err)
		drm_err(dsi->drm, "cmd write-payload-fifo is full\n");

	if (internal_int_sts.bits.gen_cmd_wdata_fifo_rd_err)
		drm_err(dsi->drm, "cmd write-payload-fifo is empty\n");

	if (internal_int_sts.bits.dpi_pix_fifo_wr_err) {
		drm_err(dsi->drm, "DPI pixel-fifo is full\n");
		status |= DSI_INT_STS_NEED_SOFT_RESET;
	}

	if (internal_int_sts.bits.ecc_single_err)
		drm_err(dsi->drm, "ECC single error in a received packet\n");

	if (internal_int_sts.bits.ecc_multi_err)
		drm_err(dsi->drm, "ECC multiple error in a received packet\n");

	if (internal_int_sts.bits.crc_err)
		drm_err(dsi->drm, "CRC error in the received packet payload\n");

	if (internal_int_sts.bits.hs_tx_timeout)
		drm_err(dsi->drm, "high-speed transmission timeout\n");

	if (internal_int_sts.bits.lp_rx_timeout)
		drm_err(dsi->drm, "low-power reception timeout\n");

	return status;
}
/*
 * Configure MASK (hiding) of interrupts coming from error 0 source
 */
void dsi_int0_mask(struct dsi_context *ctx, u32 mask)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(mask, &reg->MASK_PROTOCOL_INT);
}
/*
 * Configure MASK (hiding) of interrupts coming from error 1 source
 */
void dsi_int1_mask(struct dsi_context *ctx, u32 mask)
{
	struct dsi_reg *reg = (struct dsi_reg *)ctx->base;

	writel(mask, &reg->MASK_INTERNAL_INT);
}
