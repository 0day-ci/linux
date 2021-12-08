// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Andy Yan <andy.yan@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/component.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>

#include "rockchip_drm_vop2.h"
#include "rockchip_vop_reg.h"

#define _VOP_REG(off, _mask, _shift, _write_mask) \
	{ \
		.offset = off, \
		.mask = _mask, \
		.shift = _shift, \
		.write_mask = _write_mask, \
	}

#define VOP_REG(off, _mask, _shift) \
		_VOP_REG(off, _mask, _shift, false)

#define VOP_REG_MASK(off, _mask, s) \
		_VOP_REG(off, _mask, s, true)

static const uint32_t formats_win_full_10bit[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV24,
};

static const uint32_t formats_win_full_10bit_yuyv[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV24,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_VYUY,
};

static const uint32_t formats_win_lite[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
};

static const uint64_t format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const uint64_t format_modifiers_afbc[] = {
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_CBR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_CBR |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_CBR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_CBR |
				AFBC_FORMAT_MOD_SPARSE),

	/* SPLIT mandates SPARSE, RGB modes mandates YTR */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_SPLIT),
	DRM_FORMAT_MOD_INVALID,
};

static const struct vop2_video_port_regs rk3568_vop_vp0_regs = {
	.dsp_background = RK3568_VP0_DSP_BG,
	.pre_scan_htiming = RK3568_VP0_PRE_SCAN_HTIMING,
	.hpost_st_end = RK3568_VP0_POST_DSP_HACT_INFO,
	.vpost_st_end = RK3568_VP0_POST_DSP_VACT_INFO,
	.htotal_pw = RK3568_VP0_DSP_HTOTAL_HS_END,
	.post_scl_factor = RK3568_VP0_POST_SCL_FACTOR_YRGB,
	.hact_st_end = RK3568_VP0_DSP_HACT_ST_END,
	.vtotal_pw = RK3568_VP0_DSP_VTOTAL_VS_END,
	.vact_st_end = RK3568_VP0_DSP_VACT_ST_END,
	.vact_st_end_f1 = RK3568_VP0_DSP_VACT_ST_END_F1,
	.vs_st_end_f1 = RK3568_VP0_DSP_VS_ST_END_F1,
	.vpost_st_end_f1 = RK3568_VP0_POST_DSP_VACT_INFO_F1,

	.dsp_ctrl = RK3568_VP0_DSP_CTRL,
	.mipi_ctrl = RK3568_VP0_MIPI_CTRL,
	.bg_mix_ctrl = RK3568_VP0_BG_MIX_CTRL,
	.irq_status = RK3568_VP0_INT_STATUS,
	.irq_enable = RK3568_VP0_INT_EN,
	.irq_clear = RK3568_VP0_INT_CLR,
	.line_flag = RK3568_VP0_LINE_FLAG,
};

static const struct vop2_video_port_regs rk3568_vop_vp1_regs = {
	.dsp_background = RK3568_VP1_DSP_BG,
	.pre_scan_htiming = RK3568_VP1_PRE_SCAN_HTIMING,
	.hpost_st_end = RK3568_VP1_POST_DSP_HACT_INFO,
	.vpost_st_end = RK3568_VP1_POST_DSP_VACT_INFO,
	.htotal_pw = RK3568_VP1_DSP_HTOTAL_HS_END,
	.post_scl_factor = RK3568_VP1_POST_SCL_FACTOR_YRGB,
	.hact_st_end = RK3568_VP1_DSP_HACT_ST_END,
	.vtotal_pw = RK3568_VP1_DSP_VTOTAL_VS_END,
	.vact_st_end = RK3568_VP1_DSP_VACT_ST_END,
	.vact_st_end_f1 = RK3568_VP1_DSP_VACT_ST_END_F1,
	.vs_st_end_f1 = RK3568_VP1_DSP_VS_ST_END_F1,
	.vpost_st_end_f1 = RK3568_VP1_POST_DSP_VACT_INFO_F1,
	.dsp_ctrl = RK3568_VP1_DSP_CTRL,
	.mipi_ctrl = RK3568_VP1_MIPI_CTRL,
	.bg_mix_ctrl = RK3568_VP1_BG_MIX_CTRL,
	.irq_status = RK3568_VP1_INT_STATUS,
	.irq_enable = RK3568_VP1_INT_EN,
	.irq_clear = RK3568_VP1_INT_CLR,
	.line_flag = RK3568_VP1_LINE_FLAG,
};

static const struct vop2_video_port_regs rk3568_vop_vp2_regs = {
	.dsp_background = RK3568_VP2_DSP_BG,
	.pre_scan_htiming = RK3568_VP2_PRE_SCAN_HTIMING,
	.hpost_st_end = RK3568_VP2_POST_DSP_HACT_INFO,
	.vpost_st_end = RK3568_VP2_POST_DSP_VACT_INFO,
	.post_scl_factor = RK3568_VP2_POST_SCL_FACTOR_YRGB,
	.htotal_pw = RK3568_VP2_DSP_HTOTAL_HS_END,
	.hact_st_end = RK3568_VP2_DSP_HACT_ST_END,
	.vtotal_pw = RK3568_VP2_DSP_VTOTAL_VS_END,
	.vact_st_end = RK3568_VP2_DSP_VACT_ST_END,
	.vact_st_end_f1 = RK3568_VP2_DSP_VACT_ST_END_F1,
	.vs_st_end_f1 = RK3568_VP2_DSP_VS_ST_END_F1,
	.vpost_st_end_f1 = RK3568_VP2_POST_DSP_VACT_INFO_F1,
	.dsp_ctrl = RK3568_VP2_DSP_CTRL,
	.mipi_ctrl = RK3568_VP2_MIPI_CTRL,
	.bg_mix_ctrl = RK3568_VP2_BG_MIX_CTRL,
	.irq_status = RK3568_VP2_INT_STATUS,
	.irq_enable = RK3568_VP2_INT_EN,
	.irq_clear = RK3568_VP2_INT_CLR,
	.line_flag = RK3568_VP2_LINE_FLAG,
};

static const struct vop2_video_port_data rk3568_vop_video_ports[] = {
	{
		.id = 0,
		.feature = VOP_FEATURE_OUTPUT_10BIT,
		.gamma_lut_len = 1024,
		.cubic_lut_len = 9 * 9 * 9,
		.max_output = { 4096, 2304 },
		.pre_scan_max_dly = { 69, 53, 53, 42 },
		.regs = &rk3568_vop_vp0_regs,
	}, {
		.id = 1,
		.gamma_lut_len = 1024,
		.max_output = { 2048, 1536 },
		.pre_scan_max_dly = { 40, 40, 40, 40 },
		.regs = &rk3568_vop_vp1_regs,
	}, {
		.id = 2,
		.gamma_lut_len = 1024,
		.max_output = { 1920, 1080 },
		.pre_scan_max_dly = { 40, 40, 40, 40 },
		.regs = &rk3568_vop_vp2_regs,
	},
};

static const struct vop2_cluster_regs rk3568_vop_cluster0 =  {
	.afbc_enable = VOP_REG(RK3568_CLUSTER_CTRL, 0x1, 1),
	.enable = VOP_REG(RK3568_CLUSTER_CTRL, 1, 0),
	.lb_mode = VOP_REG(RK3568_CLUSTER_CTRL, 0xf, 4),
};

static const struct vop2_afbc rk3568_cluster_afbc = {
	.format = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_CTRL, 0x1f, 2),
	.rb_swap = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_CTRL, 0x1, 9),
	.uv_swap = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_CTRL, 0x1, 10),
	.auto_gating_en = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_OUTPUT_CTRL, 0x1, 4),
	.half_block_en = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_CTRL, 0x1, 7),
	.block_split_en = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_CTRL, 0x1, 8),
	.hdr_ptr = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_HDR_PTR, 0xffffffff, 0),
	.pic_size = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_PIC_SIZE, 0xffffffff, 0),
	.pic_vir_width = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_VIR_WIDTH, 0xffff, 0),
	.tile_num = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_VIR_WIDTH, 0xffff, 16),
	.pic_offset = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_PIC_OFFSET, 0xffffffff, 0),
	.dsp_offset = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_DSP_OFFSET, 0xffffffff, 0),
	.transform_offset = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_TRANSFORM_OFFSET, 0xffffffff, 0),
	.rotate_90 = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_ROTATE_MODE, 0x1, 0),
	.rotate_270 = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_ROTATE_MODE, 0x1, 1),
	.xmirror = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_ROTATE_MODE, 0x1, 2),
	.ymirror = VOP_REG(RK3568_CLUSTER_WIN_AFBCD_ROTATE_MODE, 0x1, 3),
};

static const struct vop2_scl_regs rk3568_cluster_win_scl = {
	.scale_yrgb_x = VOP_REG(RK3568_CLUSTER_WIN_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3568_CLUSTER_WIN_SCL_FACTOR_YRGB, 0xffff, 16),
	.yrgb_ver_scl_mode = VOP_REG(RK3568_CLUSTER_WIN_CTRL1, 0x3, 14),
	.yrgb_hor_scl_mode = VOP_REG(RK3568_CLUSTER_WIN_CTRL1, 0x3, 12),
	.bic_coe_sel = VOP_REG(RK3568_CLUSTER_WIN_CTRL1, 0x3, 2),
	.vsd_yrgb_gt2 = VOP_REG(RK3568_CLUSTER_WIN_CTRL1, 0x1, 28),
	.vsd_yrgb_gt4 = VOP_REG(RK3568_CLUSTER_WIN_CTRL1, 0x1, 29),
};

static const struct vop2_scl_regs rk3568_esmart_win_scl = {
	.scale_yrgb_x = VOP_REG(RK3568_SMART_REGION0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3568_SMART_REGION0_SCL_FACTOR_YRGB, 0xffff, 16),
	.scale_cbcr_x = VOP_REG(RK3568_SMART_REGION0_SCL_FACTOR_CBR, 0xffff, 0x0),
	.scale_cbcr_y = VOP_REG(RK3568_SMART_REGION0_SCL_FACTOR_CBR, 0xffff, 16),
	.yrgb_hor_scl_mode = VOP_REG(RK3568_SMART_REGION0_SCL_CTRL, 0x3, 0),
	.yrgb_hscl_filter_mode = VOP_REG(RK3568_SMART_REGION0_SCL_CTRL, 0x3, 2),
	.yrgb_ver_scl_mode = VOP_REG(RK3568_SMART_REGION0_SCL_CTRL, 0x3, 4),
	.yrgb_vscl_filter_mode = VOP_REG(RK3568_SMART_REGION0_SCL_CTRL, 0x3, 6),
	.cbcr_hor_scl_mode = VOP_REG(RK3568_SMART_REGION0_SCL_CTRL, 0x3, 8),
	.cbcr_hscl_filter_mode = VOP_REG(RK3568_SMART_REGION0_SCL_CTRL, 0x3, 10),
	.cbcr_ver_scl_mode = VOP_REG(RK3568_SMART_REGION0_SCL_CTRL, 0x3, 12),
	.cbcr_vscl_filter_mode = VOP_REG(RK3568_SMART_REGION0_SCL_CTRL, 0x3, 14),
	.bic_coe_sel = VOP_REG(RK3568_SMART_REGION0_SCL_CTRL, 0x3, 16),
	.vsd_yrgb_gt2 = VOP_REG(RK3568_SMART_REGION0_CTRL, 0x1, 8),
	.vsd_yrgb_gt4 = VOP_REG(RK3568_SMART_REGION0_CTRL, 0x1, 9),
	.vsd_cbcr_gt2 = VOP_REG(RK3568_SMART_REGION0_CTRL, 0x1, 10),
	.vsd_cbcr_gt4 = VOP_REG(RK3568_SMART_REGION0_CTRL, 0x1, 11),
};

static const struct vop2_win_regs rk3568_cluster_win_data = {
	.scl = &rk3568_cluster_win_scl,
	.afbc = &rk3568_cluster_afbc,
	.cluster = &rk3568_vop_cluster0,
	.enable = VOP_REG(RK3568_CLUSTER_WIN_CTRL0, 0x1, 0),
	.format = VOP_REG(RK3568_CLUSTER_WIN_CTRL0, 0x1f, 1),
	.rb_swap = VOP_REG(RK3568_CLUSTER_WIN_CTRL0, 0x1, 14),
	.dither_up = VOP_REG(RK3568_CLUSTER_WIN_CTRL0, 0x1, 18),
	.act_info = VOP_REG(RK3568_CLUSTER_WIN_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_CLUSTER_WIN_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3568_CLUSTER_WIN_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_CLUSTER_WIN_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_CLUSTER_WIN_CBR_MST, 0xffffffff, 0),
	.yuv_clip = VOP_REG(RK3568_CLUSTER_WIN_CTRL0, 0x1, 19),
	.yrgb_vir = VOP_REG(RK3568_CLUSTER_WIN_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_CLUSTER_WIN_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3568_CLUSTER_WIN_CTRL0, 0x1, 8),
	.r2y_en = VOP_REG(RK3568_CLUSTER_WIN_CTRL0, 0x1, 9),
	.csc_mode = VOP_REG(RK3568_CLUSTER_WIN_CTRL0, 0x3, 10),
};

static const struct vop2_win_regs rk3568_esmart_win_data = {
	.scl = &rk3568_esmart_win_scl,
	.enable = VOP_REG(RK3568_SMART_REGION0_CTRL, 0x1, 0),
	.format = VOP_REG(RK3568_SMART_REGION0_CTRL, 0x1f, 1),
	.dither_up = VOP_REG(RK3568_SMART_REGION0_CTRL, 0x1, 12),
	.rb_swap = VOP_REG(RK3568_SMART_REGION0_CTRL, 0x1, 14),
	.uv_swap = VOP_REG(RK3568_SMART_REGION0_CTRL, 0x1, 16),
	.act_info = VOP_REG(RK3568_SMART_REGION0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3568_SMART_REGION0_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3568_SMART_REGION0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3568_SMART_REGION0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3568_SMART_REGION0_CBR_MST, 0xffffffff, 0),
	.yuv_clip = VOP_REG(RK3568_SMART_REGION0_CTRL, 0x1, 17),
	.yrgb_vir = VOP_REG(RK3568_SMART_REGION0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3568_SMART_REGION0_VIR, 0xffff, 16),
	.y2r_en = VOP_REG(RK3568_SMART_CTRL0, 0x1, 0),
	.r2y_en = VOP_REG(RK3568_SMART_CTRL0, 0x1, 1),
	.csc_mode = VOP_REG(RK3568_SMART_CTRL0, 0x3, 2),
	.ymirror = VOP_REG(RK3568_SMART_CTRL1, 0x1, 31),
	.color_key = VOP_REG(RK3568_SMART_COLOR_KEY_CTRL, 0x3fffffff, 0),
	.color_key_en = VOP_REG(RK3568_SMART_COLOR_KEY_CTRL, 0x1, 31),
};

/*
 * rk3568 vop with 2 cluster, 2 esmart win, 2 smart win.
 * Every cluster can work as 4K win or split into two win.
 * All win in cluster support AFBCD.
 *
 * Every esmart win and smart win support 4 Multi-region.
 *
 * Scale filter mode:
 *
 * * Cluster:  bicubic for horizontal scale up, others use bilinear
 * * ESmart:
 *    * nearest-neighbor/bilinear/bicubic for scale up
 *    * nearest-neighbor/bilinear/average for scale down
 *
 *
 * @TODO describe the wind like cpu-map dt nodes;
 */
static const struct vop2_win_data rk3568_vop_win_data[] = {
	{
		.name = "Smart0-win0",
		.phys_id = ROCKCHIP_VOP2_SMART0,
		.base = 0x1c00,
		.formats = formats_win_lite,
		.nformats = ARRAY_SIZE(formats_win_lite),
		.format_modifiers = format_modifiers,
		.layer_sel_id = 3,
		.supported_rotations = DRM_MODE_REFLECT_Y,
		.hsu_filter_mode = VOP2_SCALE_UP_BIC,
		.hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.vsu_filter_mode = VOP2_SCALE_UP_BIL,
		.vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.regs = &rk3568_esmart_win_data,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.max_upscale_factor = 8,
		.max_downscale_factor = 8,
		.dly = { 20, 47, 41 },
	}, {
		.name = "Smart1-win0",
		.phys_id = ROCKCHIP_VOP2_SMART1,
		.formats = formats_win_lite,
		.nformats = ARRAY_SIZE(formats_win_lite),
		.format_modifiers = format_modifiers,
		.base = 0x1e00,
		.layer_sel_id = 7,
		.supported_rotations = DRM_MODE_REFLECT_Y,
		.hsu_filter_mode = VOP2_SCALE_UP_BIC,
		.hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.vsu_filter_mode = VOP2_SCALE_UP_BIL,
		.vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.regs = &rk3568_esmart_win_data,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.max_upscale_factor = 8,
		.max_downscale_factor = 8,
		.dly = { 20, 47, 41 },
	}, {
		.name = "Esmart1-win0",
		.phys_id = ROCKCHIP_VOP2_ESMART1,
		.formats = formats_win_full_10bit_yuyv,
		.nformats = ARRAY_SIZE(formats_win_full_10bit_yuyv),
		.format_modifiers = format_modifiers,
		.base = 0x1a00,
		.layer_sel_id = 6,
		.supported_rotations = DRM_MODE_REFLECT_Y,
		.hsu_filter_mode = VOP2_SCALE_UP_BIC,
		.hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.vsu_filter_mode = VOP2_SCALE_UP_BIL,
		.vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.regs = &rk3568_esmart_win_data,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.max_upscale_factor = 8,
		.max_downscale_factor = 8,
		.dly = { 20, 47, 41 },
	}, {
		.name = "Esmart0-win0",
		.phys_id = ROCKCHIP_VOP2_ESMART0,
		.formats = formats_win_full_10bit_yuyv,
		.nformats = ARRAY_SIZE(formats_win_full_10bit_yuyv),
		.format_modifiers = format_modifiers,
		.base = 0x1800,
		.layer_sel_id = 2,
		.supported_rotations = DRM_MODE_REFLECT_Y,
		.hsu_filter_mode = VOP2_SCALE_UP_BIC,
		.hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.vsu_filter_mode = VOP2_SCALE_UP_BIL,
		.vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.regs = &rk3568_esmart_win_data,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.max_upscale_factor = 8,
		.max_downscale_factor = 8,
		.dly = { 20, 47, 41 },
	}, {
		.name = "Cluster0-win0",
		.phys_id = ROCKCHIP_VOP2_CLUSTER0,
		.base = 0x1000,
		.formats = formats_win_full_10bit,
		.nformats = ARRAY_SIZE(formats_win_full_10bit),
		.format_modifiers = format_modifiers_afbc,
		.layer_sel_id = 0,
		.supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
					DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
		.hsu_filter_mode = VOP2_SCALE_UP_BIC,
		.hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.vsu_filter_mode = VOP2_SCALE_UP_BIL,
		.vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.regs = &rk3568_cluster_win_data,
		.max_upscale_factor = 4,
		.max_downscale_factor = 4,
		.dly = { 0, 27, 21 },
		.type = DRM_PLANE_TYPE_OVERLAY,
		.feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER,
	}, {
		.name = "Cluster1-win0",
		.phys_id = ROCKCHIP_VOP2_CLUSTER1,
		.base = 0x1200,
		.formats = formats_win_full_10bit,
		.nformats = ARRAY_SIZE(formats_win_full_10bit),
		.format_modifiers = format_modifiers_afbc,
		.layer_sel_id = 1,
		.supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
					DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
		.hsu_filter_mode = VOP2_SCALE_UP_BIC,
		.hsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.vsu_filter_mode = VOP2_SCALE_UP_BIL,
		.vsd_filter_mode = VOP2_SCALE_DOWN_BIL,
		.regs = &rk3568_cluster_win_data,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.max_upscale_factor = 4,
		.max_downscale_factor = 4,
		.dly = { 0, 27, 21 },
		.feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER,
	},
};

static const struct vop_grf_ctrl rk3568_grf_ctrl = {
	.grf_bt656_clk_inv = VOP_REG(RK3568_GRF_VO_CON1, 0x1, 1),
	.grf_bt1120_clk_inv = VOP_REG(RK3568_GRF_VO_CON1, 0x1, 2),
	.grf_dclk_inv = VOP_REG(RK3568_GRF_VO_CON1, 0x1, 3),
};

static const struct vop2_data rk3566_vop = {
	.nr_vps = 3,
	.nr_mixers = 5,
	.nr_gammas = 1,
	.max_input = { 4096, 2304 },
	.max_output = { 4096, 2304 },
	.grf_ctrl = &rk3568_grf_ctrl,
	.vp = rk3568_vop_video_ports,
	.win = rk3568_vop_win_data,
	.win_size = ARRAY_SIZE(rk3568_vop_win_data),
	.soc_id = 3566,
};

static const struct vop2_data rk3568_vop = {
	.nr_vps = 3,
	.nr_mixers = 5,
	.nr_gammas = 1,
	.max_input = { 4096, 2304 },
	.max_output = { 4096, 2304 },
	.grf_ctrl = &rk3568_grf_ctrl,
	.vp = rk3568_vop_video_ports,
	.win = rk3568_vop_win_data,
	.win_size = ARRAY_SIZE(rk3568_vop_win_data),
	.soc_id = 3568,
};

static const struct of_device_id vop2_dt_match[] = {
	{
		.compatible = "rockchip,rk3568-vop",
		.data = &rk3568_vop
	}, {
		.compatible = "rockchip,rk3568-vop",
		.data = &rk3566_vop
	}, {
	},
};
MODULE_DEVICE_TABLE(of, vop2_dt_match);

static int vop2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	return component_add(dev, &vop2_component_ops);
}

static int vop2_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vop2_component_ops);

	return 0;
}

struct platform_driver vop2_platform_driver = {
	.probe = vop2_probe,
	.remove = vop2_remove,
	.driver = {
		.name = "rockchip-vop2",
		.of_match_table = of_match_ptr(vop2_dt_match),
	},
};
