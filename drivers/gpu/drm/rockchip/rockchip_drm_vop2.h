/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 */

#ifndef _ROCKCHIP_DRM_VOP2_H
#define _ROCKCHIP_DRM_VOP2_H

#include "rockchip_drm_vop.h"

#include <drm/drm_modes.h>

#define VOP_FEATURE_OUTPUT_10BIT        BIT(0)

#define WIN_FEATURE_AFBDC		BIT(0)
#define WIN_FEATURE_CLUSTER		BIT(1)

/*
 *  the delay number of a window in different mode.
 */
enum win_dly_mode {
	VOP2_DLY_MODE_DEFAULT,   /**< default mode */
	VOP2_DLY_MODE_HISO_S,    /** HDR in SDR out mode, as a SDR window */
	VOP2_DLY_MODE_HIHO_H,    /** HDR in HDR out mode, as a HDR window */
	VOP2_DLY_MODE_MAX,
};

struct vop_rect {
	int width;
	int height;
};

struct vop_grf_ctrl {
	struct vop_reg grf_dclk_inv;
	struct vop_reg grf_bt1120_clk_inv;
	struct vop_reg grf_bt656_clk_inv;
};

struct vop2_afbc {
	struct vop_reg enable;
	struct vop_reg format;
	struct vop_reg rb_swap;
	struct vop_reg uv_swap;
	struct vop_reg auto_gating_en;
	struct vop_reg block_split_en;
	struct vop_reg pic_vir_width;
	struct vop_reg tile_num;
	struct vop_reg pic_offset;
	struct vop_reg pic_size;
	struct vop_reg dsp_offset;
	struct vop_reg transform_offset;
	struct vop_reg hdr_ptr;
	struct vop_reg half_block_en;
	struct vop_reg xmirror;
	struct vop_reg ymirror;
	struct vop_reg rotate_270;
	struct vop_reg rotate_90;
};

enum vop2_scale_up_mode {
	VOP2_SCALE_UP_NRST_NBOR,
	VOP2_SCALE_UP_BIL,
	VOP2_SCALE_UP_BIC,
};

enum vop2_scale_down_mode {
	VOP2_SCALE_DOWN_NRST_NBOR,
	VOP2_SCALE_DOWN_BIL,
	VOP2_SCALE_DOWN_AVG,
};

struct vop2_cluster_regs {
	struct vop_reg enable;
	struct vop_reg afbc_enable;
	struct vop_reg lb_mode;
};

struct vop2_scl_regs {
	struct vop_reg scale_yrgb_x;
	struct vop_reg scale_yrgb_y;
	struct vop_reg scale_cbcr_x;
	struct vop_reg scale_cbcr_y;
	struct vop_reg yrgb_hor_scl_mode;
	struct vop_reg yrgb_hscl_filter_mode;
	struct vop_reg yrgb_ver_scl_mode;
	struct vop_reg yrgb_vscl_filter_mode;
	struct vop_reg cbcr_ver_scl_mode;
	struct vop_reg cbcr_hscl_filter_mode;
	struct vop_reg cbcr_hor_scl_mode;
	struct vop_reg cbcr_vscl_filter_mode;
	struct vop_reg vsd_cbcr_gt2;
	struct vop_reg vsd_cbcr_gt4;
	struct vop_reg vsd_yrgb_gt2;
	struct vop_reg vsd_yrgb_gt4;
	struct vop_reg bic_coe_sel;
};

struct vop2_win_regs {
	const struct vop2_scl_regs *scl;
	const struct vop2_cluster_regs *cluster;
	const struct vop2_afbc *afbc;

	struct vop_reg gate;
	struct vop_reg enable;
	struct vop_reg format;
	struct vop_reg csc_mode;
	struct vop_reg xmirror;
	struct vop_reg ymirror;
	struct vop_reg rb_swap;
	struct vop_reg uv_swap;
	struct vop_reg act_info;
	struct vop_reg dsp_info;
	struct vop_reg dsp_st;
	struct vop_reg yrgb_mst;
	struct vop_reg uv_mst;
	struct vop_reg yrgb_vir;
	struct vop_reg uv_vir;
	struct vop_reg yuv_clip;
	struct vop_reg lb_mode;
	struct vop_reg y2r_en;
	struct vop_reg r2y_en;
	struct vop_reg channel;
	struct vop_reg dst_alpha_ctl;
	struct vop_reg src_alpha_ctl;
	struct vop_reg alpha_mode;
	struct vop_reg alpha_en;
	struct vop_reg global_alpha_val;
	struct vop_reg color_key;
	struct vop_reg color_key_en;
	struct vop_reg dither_up;
};

struct vop2_video_port_regs {
	int dsp_background;
	int pre_scan_htiming;
	int htotal_pw;
	int hact_st_end;
	int vtotal_pw;
	int vact_st_end;
	int vact_st_end_f1;
	int vs_st_end_f1;
	int hpost_st_end;
	int vpost_st_end;
	int vpost_st_end_f1;
	int post_scl_factor;
	int dsp_ctrl;
	int mipi_ctrl;
	int bg_mix_ctrl;
	int hdr2sdr_eetf_oetf_y0_offset;
	int hdr2sdr_sat_y0_offset;
	int sdr2hdr_eotf_oetf_y0_offset;
	int sdr2hdr_oetf_dx_pow1_offset;
	int sdr2hdr_oetf_xn1_offset;
	int irq_enable;
	int irq_status;
	int irq_clear;
	int line_flag;
};

struct vop2_wb_regs {
	struct vop_reg enable;
	struct vop_reg format;
	struct vop_reg dither_en;
	struct vop_reg r2y_en;
	struct vop_reg yrgb_mst;
	struct vop_reg uv_mst;
	struct vop_reg vp_id;
	struct vop_reg fifo_throd;
	struct vop_reg scale_x_factor;
	struct vop_reg scale_x_en;
	struct vop_reg scale_y_en;
	struct vop_reg axi_yrgb_id;
	struct vop_reg axi_uv_id;
};

struct vop2_win_data {
	const char *name;
	uint8_t phys_id;

	uint32_t base;
	enum drm_plane_type type;

	uint32_t nformats;
	const uint32_t *formats;
	const uint64_t *format_modifiers;
	const unsigned int supported_rotations;

	const struct vop2_win_regs *regs;

	/*
	 * vertical/horizontal scale up/down filter mode
	 */
	const u8 hsu_filter_mode;
	const u8 hsd_filter_mode;
	const u8 vsu_filter_mode;
	const u8 vsd_filter_mode;
	/**
	 * @layer_sel_id: defined by register OVERLAY_LAYER_SEL of VOP2
	 */
	int layer_sel_id;
	uint64_t feature;

	unsigned int max_upscale_factor;
	unsigned int max_downscale_factor;
	const uint8_t dly[VOP2_DLY_MODE_MAX];
};

struct vop2_wb_data {
	uint32_t nformats;
	const uint32_t *formats;
	struct vop_rect max_output;
	const struct vop2_wb_regs *regs;
};

struct vop2_video_port_data {
	char id;
	uint32_t feature;
	uint16_t gamma_lut_len;
	uint16_t cubic_lut_len;
	struct vop_rect max_output;
	const u8 pre_scan_max_dly[4];
	const struct vop2_video_port_regs *regs;
};

/**
 * VOP2 data struct
 *
 * @version: VOP IP version
 * @win_size: hardware win number
 */
struct vop2_data {
	uint8_t nr_vps;
	uint8_t nr_mixers;
	uint8_t nr_layers;
	uint8_t nr_gammas;
	const struct vop2_ctrl *ctrl;
	const struct vop2_win_data *win;
	const struct vop2_video_port_data *vp;
	const struct vop_csc_table *csc_table;
	const struct vop_grf_ctrl *grf_ctrl;
	struct vop_rect max_input;
	struct vop_rect max_output;

	unsigned int win_size;
	unsigned int soc_id;
};

/* interrupt define */
#define FS_NEW_INTR			BIT(4)
#define ADDR_SAME_INTR			BIT(5)
#define LINE_FLAG1_INTR			BIT(6)
#define WIN0_EMPTY_INTR			BIT(7)
#define WIN1_EMPTY_INTR			BIT(8)
#define WIN2_EMPTY_INTR			BIT(9)
#define WIN3_EMPTY_INTR			BIT(10)
#define HWC_EMPTY_INTR			BIT(11)
#define POST_BUF_EMPTY_INTR		BIT(12)
#define PWM_GEN_INTR			BIT(13)
#define DMA_FINISH_INTR			BIT(14)
#define FS_FIELD_INTR			BIT(15)
#define FE_INTR				BIT(16)
#define WB_UV_FIFO_FULL_INTR		BIT(17)
#define WB_YRGB_FIFO_FULL_INTR		BIT(18)
#define WB_COMPLETE_INTR		BIT(19)

/*
 * display output interface supported by rockchip lcdc
 */
#define ROCKCHIP_OUT_MODE_P888		0
#define ROCKCHIP_OUT_MODE_BT1120	0
#define ROCKCHIP_OUT_MODE_P666		1
#define ROCKCHIP_OUT_MODE_P565		2
#define ROCKCHIP_OUT_MODE_BT656		5
#define ROCKCHIP_OUT_MODE_S888		8
#define ROCKCHIP_OUT_MODE_S888_DUMMY	12
#define ROCKCHIP_OUT_MODE_YUV420	14
/* for use special outface */
#define ROCKCHIP_OUT_MODE_AAAA		15

enum vop_csc_format {
	CSC_BT601L,
	CSC_BT709L,
	CSC_BT601F,
	CSC_BT2020,
};

enum src_factor_mode {
	SRC_FAC_ALPHA_ZERO,
	SRC_FAC_ALPHA_ONE,
	SRC_FAC_ALPHA_DST,
	SRC_FAC_ALPHA_DST_INVERSE,
	SRC_FAC_ALPHA_SRC,
	SRC_FAC_ALPHA_SRC_GLOBAL,
};

enum dst_factor_mode {
	DST_FAC_ALPHA_ZERO,
	DST_FAC_ALPHA_ONE,
	DST_FAC_ALPHA_SRC,
	DST_FAC_ALPHA_SRC_INVERSE,
	DST_FAC_ALPHA_DST,
	DST_FAC_ALPHA_DST_GLOBAL,
};

#define RK3568_GRF_VO_CON1			0x0364
/* System registers definition */
#define RK3568_REG_CFG_DONE			0x000
#define RK3568_VERSION_INFO			0x004
#define RK3568_SYS_AUTO_GATING_CTRL		0x008
#define RK3568_SYS_AXI_LUT_CTRL			0x024
#define RK3568_DSP_IF_EN			0x028
#define RK3568_DSP_IF_CTRL			0x02c
#define RK3568_DSP_IF_POL			0x030
#define RK3568_WB_CTRL				0x40
#define RK3568_WB_XSCAL_FACTOR			0x44
#define RK3568_WB_YRGB_MST			0x48
#define RK3568_WB_CBR_MST			0x4C
#define RK3568_OTP_WIN_EN			0x050
#define RK3568_LUT_PORT_SEL			0x058
#define RK3568_SYS_STATUS0			0x060
#define RK3568_VP0_LINE_FLAG			0x70
#define RK3568_VP1_LINE_FLAG			0x74
#define RK3568_VP2_LINE_FLAG			0x78
#define RK3568_SYS0_INT_EN			0x80
#define RK3568_SYS0_INT_CLR			0x84
#define RK3568_SYS0_INT_STATUS			0x88
#define RK3568_SYS1_INT_EN			0x90
#define RK3568_SYS1_INT_CLR			0x94
#define RK3568_SYS1_INT_STATUS			0x98
#define RK3568_VP0_INT_EN			0xA0
#define RK3568_VP0_INT_CLR			0xA4
#define RK3568_VP0_INT_STATUS			0xA8
#define RK3568_VP0_INT_RAW_STATUS		0xAC
#define RK3568_VP1_INT_EN			0xB0
#define RK3568_VP1_INT_CLR			0xB4
#define RK3568_VP1_INT_STATUS			0xB8
#define RK3568_VP1_INT_RAW_STATUS		0xBC
#define RK3568_VP2_INT_EN			0xC0
#define RK3568_VP2_INT_CLR			0xC4
#define RK3568_VP2_INT_STATUS			0xC8
#define RK3568_VP2_INT_RAW_STATUS		0xCC

/* Video Port registers definition */
#define RK3568_VP0_DSP_CTRL			0xC00
#define RK3568_VP0_MIPI_CTRL			0xC04
#define RK3568_VP0_COLOR_BAR_CTRL		0xC08
#define RK3568_VP0_3D_LUT_CTRL			0xC10
#define RK3568_VP0_3D_LUT_MST			0xC20
#define RK3568_VP0_DSP_BG			0xC2C
#define RK3568_VP0_PRE_SCAN_HTIMING		0xC30
#define RK3568_VP0_POST_DSP_HACT_INFO		0xC34
#define RK3568_VP0_POST_DSP_VACT_INFO		0xC38
#define RK3568_VP0_POST_SCL_FACTOR_YRGB		0xC3C
#define RK3568_VP0_POST_SCL_CTRL		0xC40
#define RK3568_VP0_POST_DSP_VACT_INFO_F1	0xC44
#define RK3568_VP0_DSP_HTOTAL_HS_END		0xC48
#define RK3568_VP0_DSP_HACT_ST_END		0xC4C
#define RK3568_VP0_DSP_VTOTAL_VS_END		0xC50
#define RK3568_VP0_DSP_VACT_ST_END		0xC54
#define RK3568_VP0_DSP_VS_ST_END_F1		0xC58
#define RK3568_VP0_DSP_VACT_ST_END_F1		0xC5C
#define RK3568_VP0_BCSH_CTRL			0xC60
#define RK3568_VP0_BCSH_BCS			0xC64
#define RK3568_VP0_BCSH_H			0xC68
#define RK3568_VP0_BCSH_COLOR_BAR		0xC6C

#define RK3568_VP1_DSP_CTRL			0xD00
#define RK3568_VP1_MIPI_CTRL			0xD04
#define RK3568_VP1_COLOR_BAR_CTRL		0xD08
#define RK3568_VP1_DSP_BG			0xD2C
#define RK3568_VP1_PRE_SCAN_HTIMING		0xD30
#define RK3568_VP1_POST_DSP_HACT_INFO		0xD34
#define RK3568_VP1_POST_DSP_VACT_INFO		0xD38
#define RK3568_VP1_POST_SCL_FACTOR_YRGB		0xD3C
#define RK3568_VP1_POST_SCL_CTRL		0xD40
#define RK3568_VP1_DSP_HACT_INFO		0xD34
#define RK3568_VP1_DSP_VACT_INFO		0xD38
#define RK3568_VP1_POST_DSP_VACT_INFO_F1	0xD44
#define RK3568_VP1_DSP_HTOTAL_HS_END		0xD48
#define RK3568_VP1_DSP_HACT_ST_END		0xD4C
#define RK3568_VP1_DSP_VTOTAL_VS_END		0xD50
#define RK3568_VP1_DSP_VACT_ST_END		0xD54
#define RK3568_VP1_DSP_VS_ST_END_F1		0xD58
#define RK3568_VP1_DSP_VACT_ST_END_F1		0xD5C
#define RK3568_VP1_BCSH_CTRL			0xD60
#define RK3568_VP1_BCSH_BCS			0xD64
#define RK3568_VP1_BCSH_H			0xD68
#define RK3568_VP1_BCSH_COLOR_BAR		0xD6C

#define RK3568_VP2_DSP_CTRL			0xE00
#define RK3568_VP2_MIPI_CTRL			0xE04
#define RK3568_VP2_COLOR_BAR_CTRL		0xE08
#define RK3568_VP2_DSP_BG			0xE2C
#define RK3568_VP2_PRE_SCAN_HTIMING		0xE30
#define RK3568_VP2_POST_DSP_HACT_INFO		0xE34
#define RK3568_VP2_POST_DSP_VACT_INFO		0xE38
#define RK3568_VP2_POST_SCL_FACTOR_YRGB		0xE3C
#define RK3568_VP2_POST_SCL_CTRL		0xE40
#define RK3568_VP2_DSP_HACT_INFO		0xE34
#define RK3568_VP2_DSP_VACT_INFO		0xE38
#define RK3568_VP2_POST_DSP_VACT_INFO_F1	0xE44
#define RK3568_VP2_DSP_HTOTAL_HS_END		0xE48
#define RK3568_VP2_DSP_HACT_ST_END		0xE4C
#define RK3568_VP2_DSP_VTOTAL_VS_END		0xE50
#define RK3568_VP2_DSP_VACT_ST_END		0xE54
#define RK3568_VP2_DSP_VS_ST_END_F1		0xE58
#define RK3568_VP2_DSP_VACT_ST_END_F1		0xE5C
#define RK3568_VP2_BCSH_CTRL			0xE60
#define RK3568_VP2_BCSH_BCS			0xE64
#define RK3568_VP2_BCSH_H			0xE68
#define RK3568_VP2_BCSH_COLOR_BAR		0xE6C

/* Overlay registers definition    */
#define RK3568_OVL_CTRL				0x600
#define RK3568_OVL_LAYER_SEL			0x604
#define RK3568_OVL_PORT_SEL			0x608
#define RK3568_CLUSTER0_MIX_SRC_COLOR_CTRL	0x610
#define RK3568_CLUSTER0_MIX_DST_COLOR_CTRL	0x614
#define RK3568_CLUSTER0_MIX_SRC_ALPHA_CTRL	0x618
#define RK3568_CLUSTER0_MIX_DST_ALPHA_CTRL	0x61C
#define RK3568_MIX0_SRC_COLOR_CTRL		0x650
#define RK3568_MIX0_DST_COLOR_CTRL		0x654
#define RK3568_MIX0_SRC_ALPHA_CTRL		0x658
#define RK3568_MIX0_DST_ALPHA_CTRL		0x65C
#define RK3568_HDR0_SRC_COLOR_CTRL		0x6C0
#define RK3568_HDR0_DST_COLOR_CTRL		0x6C4
#define RK3568_HDR0_SRC_ALPHA_CTRL		0x6C8
#define RK3568_HDR0_DST_ALPHA_CTRL		0x6CC
#define RK3568_VP0_BG_MIX_CTRL			0x6E0
#define RK3568_VP1_BG_MIX_CTRL			0x6E4
#define RK3568_VP2_BG_MIX_CTRL			0x6E8
#define RK3568_CLUSTER_DLY_NUM			0x6F0
#define RK3568_SMART_DLY_NUM			0x6F8

/* Cluster register definition, offset relative to window base */
#define RK3568_CLUSTER_WIN_CTRL0		0x00
#define RK3568_CLUSTER_WIN_CTRL1		0x04
#define RK3568_CLUSTER_WIN_YRGB_MST		0x10
#define RK3568_CLUSTER_WIN_CBR_MST		0x14
#define RK3568_CLUSTER_WIN_VIR			0x18
#define RK3568_CLUSTER_WIN_ACT_INFO		0x20
#define RK3568_CLUSTER_WIN_DSP_INFO		0x24
#define RK3568_CLUSTER_WIN_DSP_ST		0x28
#define RK3568_CLUSTER_WIN_SCL_FACTOR_YRGB	0x30
#define RK3568_CLUSTER_WIN_AFBCD_TRANSFORM_OFFSET	0x3C
#define RK3568_CLUSTER_WIN_AFBCD_OUTPUT_CTRL	0x50
#define RK3568_CLUSTER_WIN_AFBCD_ROTATE_MODE	0x54
#define RK3568_CLUSTER_WIN_AFBCD_HDR_PTR	0x58
#define RK3568_CLUSTER_WIN_AFBCD_VIR_WIDTH	0x5C
#define RK3568_CLUSTER_WIN_AFBCD_PIC_SIZE	0x60
#define RK3568_CLUSTER_WIN_AFBCD_PIC_OFFSET	0x64
#define RK3568_CLUSTER_WIN_AFBCD_DSP_OFFSET	0x68
#define RK3568_CLUSTER_WIN_AFBCD_CTRL		0x6C

#define RK3568_CLUSTER_CTRL			0x100

/* (E)smart register definition, offset relative to window base */
#define RK3568_SMART_CTRL0			0x00
#define RK3568_SMART_CTRL1			0x04
#define RK3568_SMART_REGION0_CTRL		0x10
#define RK3568_SMART_REGION0_YRGB_MST		0x14
#define RK3568_SMART_REGION0_CBR_MST		0x18
#define RK3568_SMART_REGION0_VIR		0x1C
#define RK3568_SMART_REGION0_ACT_INFO		0x20
#define RK3568_SMART_REGION0_DSP_INFO		0x24
#define RK3568_SMART_REGION0_DSP_ST		0x28
#define RK3568_SMART_REGION0_SCL_CTRL		0x30
#define RK3568_SMART_REGION0_SCL_FACTOR_YRGB	0x34
#define RK3568_SMART_REGION0_SCL_FACTOR_CBR	0x38
#define RK3568_SMART_REGION0_SCL_OFFSET		0x3C
#define RK3568_SMART_REGION1_CTRL		0x40
#define RK3568_SMART_REGION1_YRGB_MST		0x44
#define RK3568_SMART_REGION1_CBR_MST		0x48
#define RK3568_SMART_REGION1_VIR		0x4C
#define RK3568_SMART_REGION1_ACT_INFO		0x50
#define RK3568_SMART_REGION1_DSP_INFO		0x54
#define RK3568_SMART_REGION1_DSP_ST		0x58
#define RK3568_SMART_REGION1_SCL_CTRL		0x60
#define RK3568_SMART_REGION1_SCL_FACTOR_YRGB	0x64
#define RK3568_SMART_REGION1_SCL_FACTOR_CBR	0x68
#define RK3568_SMART_REGION1_SCL_OFFSET		0x6C
#define RK3568_SMART_REGION2_CTRL		0x70
#define RK3568_SMART_REGION2_YRGB_MST		0x74
#define RK3568_SMART_REGION2_CBR_MST		0x78
#define RK3568_SMART_REGION2_VIR		0x7C
#define RK3568_SMART_REGION2_ACT_INFO		0x80
#define RK3568_SMART_REGION2_DSP_INFO		0x84
#define RK3568_SMART_REGION2_DSP_ST		0x88
#define RK3568_SMART_REGION2_SCL_CTRL		0x90
#define RK3568_SMART_REGION2_SCL_FACTOR_YRGB	0x94
#define RK3568_SMART_REGION2_SCL_FACTOR_CBR	0x98
#define RK3568_SMART_REGION2_SCL_OFFSET		0x9C
#define RK3568_SMART_REGION3_CTRL		0xA0
#define RK3568_SMART_REGION3_YRGB_MST		0xA4
#define RK3568_SMART_REGION3_CBR_MST		0xA8
#define RK3568_SMART_REGION3_VIR		0xAC
#define RK3568_SMART_REGION3_ACT_INFO		0xB0
#define RK3568_SMART_REGION3_DSP_INFO		0xB4
#define RK3568_SMART_REGION3_DSP_ST		0xB8
#define RK3568_SMART_REGION3_SCL_CTRL		0xC0
#define RK3568_SMART_REGION3_SCL_FACTOR_YRGB	0xC4
#define RK3568_SMART_REGION3_SCL_FACTOR_CBR	0xC8
#define RK3568_SMART_REGION3_SCL_OFFSET		0xCC
#define RK3568_SMART_COLOR_KEY_CTRL		0xD0

/* HDR register definition */
#define RK3568_HDR_LUT_CTRL			0x2000
#define RK3568_HDR_LUT_MST			0x2004
#define RK3568_SDR2HDR_CTRL			0x2010
#define RK3568_HDR2SDR_CTRL			0x2020
#define RK3568_HDR2SDR_SRC_RANGE		0x2024
#define RK3568_HDR2SDR_NORMFACEETF		0x2028
#define RK3568_HDR2SDR_DST_RANGE		0x202C
#define RK3568_HDR2SDR_NORMFACCGAMMA		0x2030
#define RK3568_HDR_EETF_OETF_Y0			0x203C
#define RK3568_HDR_SAT_Y0			0x20C0
#define RK3568_HDR_EOTF_OETF_Y0			0x20F0
#define RK3568_HDR_OETF_DX_POW1			0x2200
#define RK3568_HDR_OETF_XN1			0x2300

#define RK3568_REG_CFG_DONE__GLB_CFG_DONE_EN		BIT(15)

#define RK3568_VP_DSP_CTRL__STANDBY			BIT(31)
#define RK3568_VP_DSP_CTRL__DITHER_DOWN_MODE		BIT(20)
#define RK3568_VP_DSP_CTRL__DITHER_DOWN_SEL		GENMASK(19, 18)
#define RK3568_VP_DSP_CTRL__DITHER_DOWN_EN		BIT(17)
#define RK3568_VP_DSP_CTRL__PRE_DITHER_DOWN_EN		BIT(16)
#define RK3568_VP_DSP_CTRL__POST_DSP_OUT_R2Y		BIT(15)
#define RK3568_VP_DSP_CTRL__DSP_RB_SWAP			BIT(9)
#define RK3568_VP_DSP_CTRL__DSP_INTERLACE		BIT(7)
#define RK3568_VP_DSP_CTRL__DSP_FILED_POL		BIT(6)
#define RK3568_VP_DSP_CTRL__P2I_EN			BIT(5)
#define RK3568_VP_DSP_CTRL__CORE_DCLK_DIV		BIT(4)
#define RK3568_VP_DSP_CTRL__OUT_MODE			GENMASK(3, 0)

#define RK3568_VP_POST_SCL_CTRL__VSCALEDOWN		BIT(1)
#define RK3568_VP_POST_SCL_CTRL__HSCALEDOWN		BIT(0)

#define RK3568_SYS_DSP_INFACE_EN_LVDS1_MUX		GENMASK(26, 25)
#define RK3568_SYS_DSP_INFACE_EN_LVDS1			BIT(24)
#define RK3568_SYS_DSP_INFACE_EN_MIPI1_MUX		GENMASK(22, 21)
#define RK3568_SYS_DSP_INFACE_EN_MIPI1			BIT(20)
#define RK3568_SYS_DSP_INFACE_EN_LVDS0_MUX		GENMASK(19, 18)
#define RK3568_SYS_DSP_INFACE_EN_MIPI0_MUX		GENMASK(17, 16)
#define RK3568_SYS_DSP_INFACE_EN_EDP_MUX		GENMASK(15, 14)
#define RK3568_SYS_DSP_INFACE_EN_HDMI_MUX		GENMASK(11, 10)
#define RK3568_SYS_DSP_INFACE_EN_RGB_MUX		GENMASK(9, 8)
#define RK3568_SYS_DSP_INFACE_EN_LVDS0			BIT(5)
#define RK3568_SYS_DSP_INFACE_EN_MIPI0			BIT(4)
#define RK3568_SYS_DSP_INFACE_EN_EDP			BIT(3)
#define RK3568_SYS_DSP_INFACE_EN_HDMI			BIT(1)
#define RK3568_SYS_DSP_INFACE_EN_RGB			BIT(0)

#define RK3568_DSP_IF_POL__MIPI_PIN_POL			GENMASK(19, 16)
#define RK3568_DSP_IF_POL__EDP_PIN_POL			GENMASK(15, 12)
#define RK3568_DSP_IF_POL__HDMI_PIN_POL			GENMASK(7, 4)
#define RK3568_DSP_IF_POL__RGB_LVDS_PIN_POL		GENMASK(3, 0)

#define RK3568_VP0_MIPI_CTRL__DCLK_DIV2_PHASE_LOCK	BIT(5)
#define RK3568_VP0_MIPI_CTRL__DCLK_DIV2			BIT(4)

#define RK3568_SYS_AUTO_GATING_CTRL__AUTO_GATING_EN	BIT(31)

#define RK3568_DSP_IF_POL__CFG_DONE_IMD			BIT(28)

#define VOP2_SYS_AXI_BUS_NUM				2

#define VOP2_CLUSTER_YUV444_10				0x12

#define VOP2_COLOR_KEY_MASK				BIT(31)

#define RK3568_OVL_CTRL__LAYERSEL_REGDONE_IMD		BIT(28)

#define RK3568_VP_BG_MIX_CTRL__BG_DLY			GENMASK(31, 24)

#define RK3568_OVL_PORT_SEL__SEL_PORT			GENMASK(31, 16)
#define RK3568_OVL_PORT_SEL__SMART1			GENMASK(31, 30)
#define RK3568_OVL_PORT_SEL__SMART0			GENMASK(29, 28)
#define RK3568_OVL_PORT_SEL__ESMART1			GENMASK(27, 26)
#define RK3568_OVL_PORT_SEL__ESMART0			GENMASK(25, 24)
#define RK3568_OVL_PORT_SEL__CLUSTER1			GENMASK(19, 18)
#define RK3568_OVL_PORT_SEL__CLUSTER0			GENMASK(17, 16)
#define RK3568_OVL_PORT_SET__PORT2_MUX			GENMASK(11, 8)
#define RK3568_OVL_PORT_SET__PORT1_MUX			GENMASK(7, 4)
#define RK3568_OVL_PORT_SET__PORT0_MUX			GENMASK(3, 0)
#define RK3568_OVL_LAYER_SEL__LAYER(layer, x)		((x) << ((layer) * 4))

#define RK3568_CLUSTER_DLY_NUM__CLUSTER1_1		GENMASK(31, 24)
#define RK3568_CLUSTER_DLY_NUM__CLUSTER1_0		GENMASK(23, 16)
#define RK3568_CLUSTER_DLY_NUM__CLUSTER0_1		GENMASK(15, 8)
#define RK3568_CLUSTER_DLY_NUM__CLUSTER0_0		GENMASK(7, 0)

#define RK3568_SMART_DLY_NUM__SMART1			GENMASK(31, 24)
#define RK3568_SMART_DLY_NUM__SMART0			GENMASK(23, 16)
#define RK3568_SMART_DLY_NUM__ESMART1			GENMASK(15, 8)
#define RK3568_SMART_DLY_NUM__ESMART0			GENMASK(7, 0)

#define VP_INT_DSP_HOLD_VALID	BIT(6)
#define VP_INT_FS_FIELD		BIT(5)
#define VP_INT_POST_BUF_EMPTY	BIT(4)
#define VP_INT_LINE_FLAG1	BIT(3)
#define VP_INT_LINE_FLAG0	BIT(2)
#define VOP2_INT_BUS_ERRPR	BIT(1)
#define VP_INT_FS		BIT(0)

#define POLFLAG_DCLK_INV	BIT(3)

enum vop2_layer_phy_id {
	ROCKCHIP_VOP2_CLUSTER0 = 0,
	ROCKCHIP_VOP2_CLUSTER1,
	ROCKCHIP_VOP2_ESMART0,
	ROCKCHIP_VOP2_ESMART1,
	ROCKCHIP_VOP2_SMART0,
	ROCKCHIP_VOP2_SMART1,
	ROCKCHIP_VOP2_CLUSTER2,
	ROCKCHIP_VOP2_CLUSTER3,
	ROCKCHIP_VOP2_ESMART2,
	ROCKCHIP_VOP2_ESMART3,
	ROCKCHIP_VOP2_PHY_ID_INVALID = -1,
};

extern const struct component_ops vop2_component_ops;

#endif /* _ROCKCHIP_DRM_VOP2_H */
