/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2020 NXP
 */

#ifndef __DPU_H__
#define __DPU_H__

#include <linux/of.h>
#include <linux/types.h>

#include <drm/drm_color_mgmt.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_modes.h>

#define DPU_FRAMEGEN_MAX_FRAME_INDEX	0x3ffff
#define DPU_FRAMEGEN_MAX_CLOCK		300000	/* in KHz */

#define DPU_FETCHUNIT_CAP_USE_FETCHECO	BIT(0)
#define DPU_FETCHUNIT_CAP_USE_SCALER	BIT(1)
#define DPU_FETCHUNIT_CAP_PACKED_YUV422	BIT(2)

struct dpu_dprc;
struct dpu_fetchunit;
struct dpu_soc;

enum dpu_link_id {
	LINK_ID_NONE		= 0x00,
	LINK_ID_FETCHDECODE9	= 0x01,
	LINK_ID_FETCHWARP9	= 0x02,
	LINK_ID_FETCHECO9	= 0x03,
	LINK_ID_ROP9		= 0x04,
	LINK_ID_CLUT9		= 0x05,
	LINK_ID_MATRIX9		= 0x06,
	LINK_ID_HSCALER9	= 0x07,
	LINK_ID_VSCALER9	= 0x08,
	LINK_ID_FILTER9		= 0x09,
	LINK_ID_BLITBLEND9	= 0x0a,
	LINK_ID_CONSTFRAME0	= 0x0c,
	LINK_ID_CONSTFRAME4	= 0x0e,
	LINK_ID_CONSTFRAME1	= 0x10,
	LINK_ID_CONSTFRAME5	= 0x12,
	LINK_ID_FETCHWARP2	= 0x14,
	LINK_ID_FETCHECO2	= 0x15,
	LINK_ID_FETCHDECODE0	= 0x16,
	LINK_ID_FETCHECO0	= 0x17,
	LINK_ID_FETCHDECODE1	= 0x18,
	LINK_ID_FETCHECO1	= 0x19,
	LINK_ID_FETCHLAYER0	= 0x1a,
	LINK_ID_MATRIX4		= 0x1b,
	LINK_ID_HSCALER4	= 0x1c,
	LINK_ID_VSCALER4	= 0x1d,
	LINK_ID_MATRIX5		= 0x1e,
	LINK_ID_HSCALER5	= 0x1f,
	LINK_ID_VSCALER5	= 0x20,
	LINK_ID_LAYERBLEND0	= 0x21,
	LINK_ID_LAYERBLEND1	= 0x22,
	LINK_ID_LAYERBLEND2	= 0x23,
	LINK_ID_LAYERBLEND3	= 0x24,
};

enum dpu_fg_syncmode {
	FG_SYNCMODE_OFF,	/* No side-by-side synchronization. */
	FG_SYNCMODE_MASTER,	/* Framegen is master. */
	FG_SYNCMODE_SLAVE_CYC,	/* Runs in cyclic synchronization mode. */
	FG_SYNCMODE_SLAVE_ONCE,	/* Runs in one time synchronization mode. */
};

enum dpu_fg_dm {
	FG_DM_BLACK,
	FG_DM_CONSTCOL,	/* Constant Color Background is shown. */
	FG_DM_PRIM,
	FG_DM_SEC,
	FG_DM_PRIM_ON_TOP,
	FG_DM_SEC_ON_TOP,
	FG_DM_TEST,	/* White color background with test pattern is shown. */
};

enum dpu_gc_mode {
	GC_NEUTRAL,	/* Input data is bypassed to the output. */
	GC_GAMMACOR,
};

enum dpu_lb_mode {
	LB_NEUTRAL,	/* Output is same as primary input. */
	LB_BLEND,
};

enum dpu_scaler_field_mode {
	/* Constant 0 indicates frame or top field. */
	SCALER_ALWAYS0,
	/* Constant 1 indicates bottom field. */
	SCALER_ALWAYS1,
	/* Output field polarity is taken from input field polarity. */
	SCALER_INPUT,
	/* Output field polarity toggles, starting with 0 after reset. */
	SCALER_TOGGLE,
};

enum dpu_scaler_filter_mode {
	SCALER_NEAREST,	/* pointer-sampling */
	SCALER_LINEAR,	/* box filter */
};

enum dpu_scaler_scale_mode {
	SCALER_DOWNSCALE,
	SCALER_UPSCALE,
};

enum dpu_scaler_mode {
	/* Pixel by-pass the scaler, all other settings are ignored. */
	SCALER_NEUTRAL,
	/* Scaler is active. */
	SCALER_ACTIVE,
};

enum dpu_pec_clken {
	CLKEN_DISABLE = 0x0,
	CLKEN_AUTOMATIC = 0x1,
	CLKEN_FULL = 0x3,
};

int dpu_map_irq(struct dpu_soc *dpu, int irq);

/* Constant Frame Unit */
struct dpu_constframe;
enum dpu_link_id dpu_cf_get_link_id(struct dpu_constframe *cf);
void dpu_cf_framedimensions(struct dpu_constframe *cf, unsigned int w,
			    unsigned int h);
void dpu_cf_constantcolor_black(struct dpu_constframe *cf);
void dpu_cf_constantcolor_blue(struct dpu_constframe *cf);
struct dpu_constframe *dpu_cf_safe_get(struct dpu_soc *dpu,
				       unsigned int stream_id);
void dpu_cf_safe_put(struct dpu_constframe *cf);
struct dpu_constframe *dpu_cf_cont_get(struct dpu_soc *dpu,
				       unsigned int stream_id);
void dpu_cf_cont_put(struct dpu_constframe *cf);

/* Display Engine Configuration Unit */
struct dpu_disengcfg;
struct dpu_disengcfg *dpu_dec_get(struct dpu_soc *dpu, unsigned int id);
void dpu_dec_put(struct dpu_disengcfg *dec);

/* External Destination Unit */
struct dpu_extdst;
void dpu_ed_pec_poweron(struct dpu_extdst *ed);
void dpu_ed_pec_src_sel(struct dpu_extdst *ed, enum dpu_link_id src);
void dpu_ed_pec_sync_trigger(struct dpu_extdst *ed);
struct dpu_extdst *dpu_ed_safe_get(struct dpu_soc *dpu,
				   unsigned int stream_id);
void dpu_ed_safe_put(struct dpu_extdst *ed);
struct dpu_extdst *dpu_ed_cont_get(struct dpu_soc *dpu,
				   unsigned int stream_id);
void dpu_ed_cont_put(struct dpu_extdst *ed);

/* Fetch Decode Unit */
struct dpu_fetchunit *dpu_fd_get(struct dpu_soc *dpu, unsigned int id);
void dpu_fd_put(struct dpu_fetchunit *fu);

/* Fetch ECO Unit */
struct dpu_fetchunit *dpu_fe_get(struct dpu_soc *dpu, unsigned int id);
void dpu_fe_put(struct dpu_fetchunit *fu);

/* Fetch Layer Unit */
struct dpu_fetchunit *dpu_fl_get(struct dpu_soc *dpu, unsigned int id);
void dpu_fl_put(struct dpu_fetchunit *fu);

/* Fetch Warp Unit */
struct dpu_fetchunit *dpu_fw_get(struct dpu_soc *dpu, unsigned int id);
void dpu_fw_put(struct dpu_fetchunit *fu);

/* Frame Generator Unit */
struct dpu_framegen;
void dpu_fg_syncmode(struct dpu_framegen *fg, enum dpu_fg_syncmode mode);
void dpu_fg_cfg_videomode(struct dpu_framegen *fg, struct drm_display_mode *m);
void dpu_fg_displaymode(struct dpu_framegen *fg, enum dpu_fg_dm mode);
void dpu_fg_panic_displaymode(struct dpu_framegen *fg, enum dpu_fg_dm mode);
void dpu_fg_enable(struct dpu_framegen *fg);
void dpu_fg_disable(struct dpu_framegen *fg);
void dpu_fg_shdtokgen(struct dpu_framegen *fg);
u32 dpu_fg_get_frame_index(struct dpu_framegen *fg);
int dpu_fg_get_line_index(struct dpu_framegen *fg);
int dpu_fg_wait_for_frame_counter_moving(struct dpu_framegen *fg);
bool dpu_fg_secondary_requests_to_read_empty_fifo(struct dpu_framegen *fg);
void dpu_fg_secondary_clear_channel_status(struct dpu_framegen *fg);
int dpu_fg_wait_for_secondary_syncup(struct dpu_framegen *fg);
void dpu_fg_enable_clock(struct dpu_framegen *fg);
void dpu_fg_disable_clock(struct dpu_framegen *fg);
struct dpu_framegen *dpu_fg_get(struct dpu_soc *dpu, unsigned int id);
void dpu_fg_put(struct dpu_framegen *fg);

/* Gamma Correction Unit */
struct dpu_gammacor;
void dpu_gc_enable_rgb_write(struct dpu_gammacor *gc);
void dpu_gc_disable_rgb_write(struct dpu_gammacor *gc);
void dpu_gc_start_rgb(struct dpu_gammacor *gc, const struct drm_color_lut *lut);
void dpu_gc_delta_rgb(struct dpu_gammacor *gc, const struct drm_color_lut *lut);
void dpu_gc_mode(struct dpu_gammacor *gc, enum dpu_gc_mode mode);
struct dpu_gammacor *dpu_gc_get(struct dpu_soc *dpu, unsigned int id);
void dpu_gc_put(struct dpu_gammacor *gc);

/* Horizontal Scaler Unit */
struct dpu_hscaler;
enum dpu_link_id dpu_hs_get_link_id(struct dpu_hscaler *hs);
void dpu_hs_pec_dynamic_src_sel(struct dpu_hscaler *hs, enum dpu_link_id src);
void dpu_hs_pec_clken(struct dpu_hscaler *hs, enum dpu_pec_clken clken);
void dpu_hs_setup1(struct dpu_hscaler *hs,
		   unsigned int src_w, unsigned int dst_w);
void dpu_hs_setup2(struct dpu_hscaler *hs, u32 phase_offset);
void dpu_hs_output_size(struct dpu_hscaler *hs, u32 line_num);
void dpu_hs_filter_mode(struct dpu_hscaler *hs, enum dpu_scaler_filter_mode m);
void dpu_hs_scale_mode(struct dpu_hscaler *hs, enum dpu_scaler_scale_mode m);
void dpu_hs_mode(struct dpu_hscaler *hs, enum dpu_scaler_mode m);
unsigned int dpu_hs_get_id(struct dpu_hscaler *hs);
struct dpu_hscaler *dpu_hs_get(struct dpu_soc *dpu, unsigned int id);
void dpu_hs_put(struct dpu_hscaler *hs);

/* Layer Blend Unit */
struct dpu_layerblend;
enum dpu_link_id dpu_lb_get_link_id(struct dpu_layerblend *lb);
void dpu_lb_pec_dynamic_prim_sel(struct dpu_layerblend *lb,
				 enum dpu_link_id prim);
void dpu_lb_pec_dynamic_sec_sel(struct dpu_layerblend *lb,
				enum dpu_link_id sec);
void dpu_lb_pec_clken(struct dpu_layerblend *lb, enum dpu_pec_clken clken);
void dpu_lb_mode(struct dpu_layerblend *lb, enum dpu_lb_mode mode);
void dpu_lb_blendcontrol(struct dpu_layerblend *lb, unsigned int zpos,
			 unsigned int pixel_blend_mode, u16 alpha);
void dpu_lb_position(struct dpu_layerblend *lb, int x, int y);
unsigned int dpu_lb_get_id(struct dpu_layerblend *lb);
struct dpu_layerblend *dpu_lb_get(struct dpu_soc *dpu, unsigned int id);
void dpu_lb_put(struct dpu_layerblend *lb);

/* Timing Controller Unit */
struct dpu_tcon;
void dpu_tcon_set_fmt(struct dpu_tcon *tcon);
void dpu_tcon_set_operation_mode(struct dpu_tcon *tcon);
void dpu_tcon_cfg_videomode(struct dpu_tcon *tcon, struct drm_display_mode *m);
struct dpu_tcon *dpu_tcon_get(struct dpu_soc *dpu, unsigned int id);
void dpu_tcon_put(struct dpu_tcon *tcon);

/* Vertical Scaler Unit */
struct dpu_vscaler;
enum dpu_link_id dpu_vs_get_link_id(struct dpu_vscaler *vs);
void dpu_vs_pec_dynamic_src_sel(struct dpu_vscaler *vs, enum dpu_link_id src);
void dpu_vs_pec_clken(struct dpu_vscaler *vs, enum dpu_pec_clken clken);
void dpu_vs_setup1(struct dpu_vscaler *vs,
		   unsigned int src_w, unsigned int dst_w, bool deinterlace);
void dpu_vs_setup2(struct dpu_vscaler *vs, bool deinterlace);
void dpu_vs_setup3(struct dpu_vscaler *vs, bool deinterlace);
void dpu_vs_setup4(struct dpu_vscaler *vs, u32 phase_offset);
void dpu_vs_setup5(struct dpu_vscaler *vs, u32 phase_offset);
void dpu_vs_output_size(struct dpu_vscaler *vs, u32 line_num);
void dpu_vs_field_mode(struct dpu_vscaler *vs, enum dpu_scaler_field_mode m);
void dpu_vs_filter_mode(struct dpu_vscaler *vs, enum dpu_scaler_filter_mode m);
void dpu_vs_scale_mode(struct dpu_vscaler *vs, enum dpu_scaler_scale_mode m);
void dpu_vs_mode(struct dpu_vscaler *vs, enum dpu_scaler_mode m);
unsigned int dpu_vs_get_id(struct dpu_vscaler *vs);
struct dpu_vscaler *dpu_vs_get(struct dpu_soc *dpu, unsigned int id);
void dpu_vs_put(struct dpu_vscaler *vs);

/* Fetch Units */
struct dpu_fetchunit_ops {
	void (*set_pec_dynamic_src_sel)(struct dpu_fetchunit *fu,
					enum dpu_link_id src);

	bool (*is_enabled)(struct dpu_fetchunit *fu);

	void (*set_stream_id)(struct dpu_fetchunit *fu, unsigned int stream_id);

	unsigned int (*get_stream_id)(struct dpu_fetchunit *fu);

	void (*set_no_stream_id)(struct dpu_fetchunit *fu);

	bool (*has_stream_id)(struct dpu_fetchunit *fu);

	void (*set_numbuffers)(struct dpu_fetchunit *fu, unsigned int num);

	void (*set_burstlength)(struct dpu_fetchunit *fu,
				unsigned int x_offset, unsigned int mt_w,
				int bpp, dma_addr_t baddr);

	void (*set_baseaddress)(struct dpu_fetchunit *fu, unsigned int width,
				unsigned int x_offset, unsigned int y_offset,
				unsigned int mt_w, unsigned int mt_h,
				int bpp, dma_addr_t baddr);

	void (*set_src_stride)(struct dpu_fetchunit *fu,
			       unsigned int width, unsigned int x_offset,
			       unsigned int mt_w, int bpp, unsigned int stride,
			       dma_addr_t baddr);

	void (*set_src_buf_dimensions)(struct dpu_fetchunit *fu,
				       unsigned int w, unsigned int h,
				       const struct drm_format_info *format,
				       bool deinterlace);

	void (*set_fmt)(struct dpu_fetchunit *fu,
			const struct drm_format_info *format,
			enum drm_color_encoding color_encoding,
			enum drm_color_range color_range,
			bool deinterlace);

	void (*set_pixel_blend_mode)(struct dpu_fetchunit *fu,
				     unsigned int pixel_blend_mode, u16 alpha,
				     bool fb_format_has_alpha);

	void (*enable_src_buf)(struct dpu_fetchunit *fu);
	void (*disable_src_buf)(struct dpu_fetchunit *fu);

	void (*set_framedimensions)(struct dpu_fetchunit *fu,
				    unsigned int w, unsigned int h,
				    bool deinterlace);

	struct dpu_dprc *(*get_dprc)(struct dpu_fetchunit *fu);
	struct dpu_fetchunit *(*get_fetcheco)(struct dpu_fetchunit *fu);
	struct dpu_hscaler *(*get_hscaler)(struct dpu_fetchunit *fu);
	struct dpu_vscaler *(*get_vscaler)(struct dpu_fetchunit *fu);

	void (*set_layerblend)(struct dpu_fetchunit *fu,
			       struct dpu_layerblend *lb);

	bool (*is_available)(struct dpu_fetchunit *fu);
	void (*set_available)(struct dpu_fetchunit *fu);
	void (*set_inavailable)(struct dpu_fetchunit *fu);

	enum dpu_link_id (*get_link_id)(struct dpu_fetchunit *fu);

	u32 (*get_cap_mask)(struct dpu_fetchunit *fu);

	const char *(*get_name)(struct dpu_fetchunit *fu);
};

const struct dpu_fetchunit_ops *dpu_fu_get_ops(struct dpu_fetchunit *fu);
struct dpu_fetchunit *dpu_fu_get_from_list(struct list_head *l);
void dpu_fu_add_to_list(struct dpu_fetchunit *fu, struct list_head *l);

/* HW resources for a plane group */
struct dpu_plane_res {
	struct dpu_fetchunit	**fd;
	struct dpu_fetchunit	**fe;
	struct dpu_fetchunit	**fl;
	struct dpu_fetchunit	**fw;
	struct dpu_layerblend	**lb;
	unsigned int		fd_cnt;
	unsigned int		fe_cnt;
	unsigned int		fl_cnt;
	unsigned int		fw_cnt;
	unsigned int		lb_cnt;
};

/*
 * fetchunit/scaler/layerblend resources of a plane group are
 * shared by the two CRTCs in a CRTC group
 */
struct dpu_plane_grp {
	struct dpu_plane_res	res;
	struct list_head	node;
	struct list_head	fu_list;
	unsigned int		hw_plane_cnt;
	struct dpu_constframe	*cf[2];
	struct dpu_extdst	*ed[2];
};

/* the two CRTCs of one DPU are in a CRTC group */
struct dpu_crtc_grp {
	u32			crtc_mask;
	struct dpu_plane_grp	*plane_grp;
};

struct dpu_client_platformdata {
	const unsigned int	stream_id;
	const unsigned int	dec_frame_complete_irq;
	const unsigned int	dec_seq_complete_irq;
	const unsigned int	dec_shdld_irq;
	const unsigned int	ed_cont_shdld_irq;
	const unsigned int	ed_safe_shdld_irq;
	struct dpu_crtc_grp	*crtc_grp;

	struct device_node	*of_node;
};

#endif /* __DPU_H__ */
