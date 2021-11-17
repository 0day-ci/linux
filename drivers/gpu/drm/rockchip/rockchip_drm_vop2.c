// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd.
 * Author: Andy Yan <andy.yan@rock-chips.com>
 */
#include <drm/drm.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_writeback.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_uapi.h>

#include <linux/debugfs.h>
#include <linux/fixp-arith.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/component.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/delay.h>
#include <linux/swab.h>
#include <linux/sort.h>
#include <drm/drm_debugfs.h>
#include <uapi/linux/videodev2.h>
#include <drm/drm_vblank.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_vop.h"
#include "rockchip_vop_reg.h"

#define VOP2_SYS_AXI_BUS_NUM 2

#define VOP2_CLUSTER_YUV444_10 0x12

#define VOP2_COLOR_KEY_MASK		(1 << 31)

#define DSP_BG_SWAP             0x1
#define DSP_RB_SWAP             0x2
#define DSP_RG_SWAP             0x4
#define DSP_DELTA_SWAP          0x8

enum vop2_data_format {
	VOP2_FMT_ARGB8888 = 0,
	VOP2_FMT_RGB888,
	VOP2_FMT_RGB565,
	VOP2_FMT_XRGB101010,
	VOP2_FMT_YUV420SP,
	VOP2_FMT_YUV422SP,
	VOP2_FMT_YUV444SP,
	VOP2_FMT_YUYV422 = 8,
	VOP2_FMT_YUYV420,
	VOP2_FMT_VYUY422,
	VOP2_FMT_VYUY420,
	VOP2_FMT_YUV420SP_TILE_8x4 = 0x10,
	VOP2_FMT_YUV420SP_TILE_16x2,
	VOP2_FMT_YUV422SP_TILE_8x4,
	VOP2_FMT_YUV422SP_TILE_16x2,
	VOP2_FMT_YUV420SP_10,
	VOP2_FMT_YUV422SP_10,
	VOP2_FMT_YUV444SP_10,
};

enum vop2_afbc_format {
	VOP2_AFBC_FMT_RGB565,
	VOP2_AFBC_FMT_ARGB2101010 = 2,
	VOP2_AFBC_FMT_YUV420_10BIT,
	VOP2_AFBC_FMT_RGB888,
	VOP2_AFBC_FMT_ARGB8888,
	VOP2_AFBC_FMT_YUV420 = 9,
	VOP2_AFBC_FMT_YUV422 = 0xb,
	VOP2_AFBC_FMT_YUV422_10BIT = 0xe,
	VOP2_AFBC_FMT_INVALID = -1,
};

enum vop2_hdr_lut_mode {
	VOP2_HDR_LUT_MODE_AXI,
	VOP2_HDR_LUT_MODE_AHB,
};

enum vop2_pending {
	VOP_PENDING_FB_UNREF,
};

struct vop2_zpos {
	struct drm_plane *plane;
	struct vop2_win *win;
	int zpos;
};

union vop2_alpha_ctrl {
	uint32_t val;
	struct {
		/* [0:1] */
		uint32_t color_mode:1;
		uint32_t alpha_mode:1;
		/* [2:3] */
		uint32_t blend_mode:2;
		uint32_t alpha_cal_mode:1;
		/* [5:7] */
		uint32_t factor_mode:3;
		/* [8:9] */
		uint32_t alpha_en:1;
		uint32_t src_dst_swap:1;
		uint32_t reserved:6;
		/* [16:23] */
		uint32_t glb_alpha:8;
	} bits;
};

struct vop2_alpha {
	union vop2_alpha_ctrl src_color_ctrl;
	union vop2_alpha_ctrl dst_color_ctrl;
	union vop2_alpha_ctrl src_alpha_ctrl;
	union vop2_alpha_ctrl dst_alpha_ctrl;
};

struct vop2_alpha_config {
	bool src_premulti_en;
	bool dst_premulti_en;
	bool src_pixel_alpha_en;
	bool dst_pixel_alpha_en;
	uint16_t src_glb_alpha_value;
	uint16_t dst_glb_alpha_value;
};

struct vop2_win {
	const char *name;
	struct vop2 *vop2;
	struct drm_plane base;
	const struct vop2_win_data *data;

	/*
	 * This is for cluster window
	 *
	 * A cluster window can split as two windows:
	 * a main window and a sub window.
	 */
	bool two_win_mode;

	/**
	 * @win_id: graphic window id, a cluster maybe split into two
	 * graphics windows.
	 */
	uint8_t win_id;
	/**
	 * @plane_id: unique plane id.
	 */
	uint8_t plane_id;
	/**
	 * @layer_id: id of the layer which the window attached to
	 */
	uint8_t layer_id;
	int layer_sel_id;
	/**
	 * @vp_mask: Bitmask of video_port0/1/2 this win attached to,
	 * one win can only attach to one vp at the one time.
	 */
	uint8_t vp_mask;
	/**
	 * @old_vp_mask: Bitmask of video_port0/1/2 this win attached of last commit,
	 * this is used for trackng the change of VOP2_PORT_SEL register.
	 */
	uint8_t old_vp_mask;
	uint8_t zpos;
	uint32_t offset;
	enum drm_plane_type type;

	const struct vop2_win_regs *regs;
};

struct vop2_cluster {
	struct vop2_win *main;
	struct vop2_win *sub;
};

struct vop2_layer {
	uint8_t id;
	struct vop2_win *win;
	const struct vop2_layer_regs *regs;
};

struct vop2_video_port {
	struct drm_crtc crtc;
	struct vop2 *vop2;
	struct clk *dclk;
	uint8_t id;
	bool layer_sel_update;
	const struct vop2_video_port_regs *regs;

	struct completion dsp_hold_completion;
	struct completion line_flag_completion;

	/* protected by dev->event_lock */
	struct drm_pending_vblank_event *event;

	struct drm_flip_work fb_unref_work;
	unsigned long pending;

	struct vop2_zpos *zpos;
	int nr_layers;

	/**
	 * @skip_vsync: skip on vsync when port_mux changed on this vp.
	 * a win move from one VP to another need wait one vsync until
	 * port_mut take effect before this win can be enabled.
	 *
	 */
	bool skip_vsync;

	/**
	 * @bg_ovl_dly: The timing delay from background layer
	 * to overlay module.
	 */
	uint8_t bg_ovl_dly;

	/**
	 * @win_mask: Bitmask of wins attached to the video port;
	 */
	uint32_t win_mask;

	/**
	 * @gamma_lut_len: gamma look up table size
	 */
	uint32_t gamma_lut_len;

	/**
	 * @gamma_lut_active: gamma states
	 */
	bool gamma_lut_active;

	struct vop2_win *primary_plane;
};

struct vop2 {
	struct device *dev;
	struct drm_device *drm;
	struct vop2_video_port vps[ROCKCHIP_MAX_CRTC];

	const struct vop2_data *data;
	/* Number of win that registered as plane,
	 * maybe less than the total number of hardware
	 * win.
	 */
	uint32_t registered_num_wins;
	uint8_t used_mixers;

	uint16_t port_mux_cfg;

	uint32_t *regsbak;
	void __iomem *regs;
	struct regmap *grf;

	/* physical map length of vop2 register */
	uint32_t len;

	void __iomem *lut_regs;
	/* one time only one process allowed to config the register */
	spinlock_t reg_lock;
	/* lock vop2 irq reg */
	spinlock_t irq_lock;
	/* protects crtc enable/disable */
	struct mutex vop2_lock;

	int irq;

	/*
	 * Some globle resource are shared between all
	 * the vidoe ports(crtcs), so we need a ref counter here.
	 */
	unsigned int enable_count;
	struct clk *hclk;
	struct clk *aclk;

	struct vop2_layer layers[ROCKCHIP_MAX_LAYER];
	/* must put at the end of the struct */
	struct vop2_win win[];
};

static inline struct vop2_video_port *to_vop2_video_port(struct drm_crtc *crtc)
{
	return container_of(crtc, struct vop2_video_port, crtc);
}

static inline struct vop2_win *to_vop2_win(struct drm_plane *p)
{
	return container_of(p, struct vop2_win, base);
}

static void vop2_lock(struct vop2 *vop2)
{
	mutex_lock(&vop2->vop2_lock);
}

static void vop2_unlock(struct vop2 *vop2)
{
	mutex_unlock(&vop2->vop2_lock);
}

static inline void vop2_grf_writel(struct vop2 *vop2, struct vop_reg reg, uint32_t v)
{
	uint32_t val = 0;

	if (IS_ERR_OR_NULL(vop2->grf))
		return;

	if (reg.mask) {
		val = (v << reg.shift) | (reg.mask << (reg.shift + 16));
		regmap_write(vop2->grf, reg.offset, val);
	}
}

static void vop2_writel(struct vop2 *vop2, uint32_t offset, uint32_t v)
{
	writel(v, vop2->regs + offset);
	vop2->regsbak[offset >> 2] = v;
}

static uint32_t vop2_readl(struct vop2 *vop2, uint32_t offset)
{
	return readl(vop2->regs + offset);
}

static uint32_t vop2_read_reg(struct vop2 *vop2, uint32_t offset,
			      const struct vop_reg *reg)
{
	return (vop2_readl(vop2, offset + reg->offset) >> reg->shift) & reg->mask;
}

static inline uint32_t vop2_read(struct vop2 *vop2, const struct vop_reg *reg)
{
	return (vop2_readl(vop2, reg->offset) >> reg->shift) & reg->mask;
}

static void vop2_write_reg(struct vop2 *vop2, uint32_t offset, const struct vop_reg *reg,
				   uint32_t v, bool relaxed)
{
	uint32_t mask = reg->mask;
	uint32_t shift = reg->shift;

	if (!mask)
		return;

	offset += reg->offset;

	if (reg->write_mask) {
		v = ((v & mask) << shift) | (mask << (shift + 16));
	} else {
		/*
		 * Several registers in the VOP2 are double buffered and read back
		 * the new values only after config_done bits have been set. As we
		 * read-modify-write the hardware registers we need to cache the values
		 * in memory to make sure we do not overwrite previous values when a
		 * hardware register is modified multiple times before config_done is
		 * set.
		 */
		uint32_t cached_val = vop2->regsbak[offset >> 2];

		v = (cached_val & ~(mask << shift)) | ((v & mask) << shift);
		vop2->regsbak[offset >> 2] = v;
	}

	if (relaxed)
		writel_relaxed(v, vop2->regs + offset);
	else
		writel(v, vop2->regs + offset);
}

static void vop2_write(struct vop2 *vop2, const struct vop_reg *reg, uint32_t v)
{
	vop2_write_reg(vop2, 0, reg, v, false);
}

static void vop2_win_write(const struct vop2_win *win, const struct vop_reg *reg, uint32_t v)
{
	vop2_write_reg(win->vop2, win->offset, reg, v, true);
}

static uint32_t vop2_win_read(const struct vop2_win *win, const struct vop_reg *reg)
{
	return vop2_read_reg(win->vop2, win->offset, reg);
}


static void vop2_intr_set_type(struct vop2 *vop2, const struct vop_intr *intr,
			       const struct vop_reg *reg, uint32_t type, bool enable)
{
	uint32_t v = 0, mask = 0;
	int i;

	for (i = 0; i < intr->nintrs; i++) {
		if (intr->intrs[i] & type) {
			if (enable)
				v |= 1 << i;
			mask |= 1 << i;
		}
	}

	writel(v | (mask << 16), vop2->regs + reg->offset);
}

static inline uint32_t vop2_line_to_time(struct drm_display_mode *mode, int line)
{
	u64 val = 1000000000ULL * mode->crtc_htotal * line;

	do_div(val, mode->crtc_clock);
	do_div(val, 1000000);

	return val; /* us */
}

static inline uint32_t vop2_get_intr_type(struct vop2 *vop2, const struct vop_intr *intr,
					  const struct vop_reg *reg, int type)
{
	uint32_t val, i;
	uint32_t ret = 0;

	val = vop2_read_reg(vop2, 0, reg);

	for (i = 0; i < intr->nintrs; i++) {
		if ((type & intr->intrs[i]) && (val & 1 << i))
			ret |= intr->intrs[i];
	}

	return ret;
}

/*
 * phys_id is used to identify a main window(Cluster Win/Smart Win, not
 * include the sub win of a cluster or the multi area) that can do
 * overlay in main overlay stage.
 */
static struct vop2_win *vop2_find_win_by_phys_id(struct vop2 *vop2, uint8_t phys_id)
{
	struct vop2_win *win;
	int i;

	for (i = 0; i < vop2->registered_num_wins; i++) {
		win = &vop2->win[i];
		if (win->data->phys_id == phys_id)
			return win;
	}

	return NULL;
}

static bool vop2_fs_irq_is_pending(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;

	return vop2_get_intr_type(vop2, intr, &intr->status, FS_FIELD_INTR);
}

static uint32_t vop2_read_vcnt(struct vop2_video_port *vp)
{
	struct drm_display_mode *mode = &vp->crtc.state->adjusted_mode;
	uint32_t offset = RK3568_SYS_STATUS0 + (vp->id << 2);
	uint32_t vcnt;

	vcnt = vop2_readl(vp->vop2, offset) >> 16;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vcnt >>= 1;

	return vcnt;
}

static void vop2_wait_for_irq_handler(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	bool pending;
	int ret;

	/*
	 * Spin until frame start interrupt status bit goes low, which means
	 * that interrupt handler was invoked and cleared it. The timeout of
	 * 10 msecs is really too long, but it is just a safety measure if
	 * something goes really wrong. The wait will only happen in the very
	 * unlikely case of a vblank happening exactly at the same time and
	 * shouldn't exceed microseconds range.
	 */
	ret = readx_poll_timeout_atomic(vop2_fs_irq_is_pending, vp, pending,
					!pending, 0, 10 * 1000);
	if (ret)
		drm_err(vop2->drm, "VOP vblank IRQ stuck for 10 ms\n");

	synchronize_irq(vop2->irq);
}

static bool vop2_vp_done_bit_status(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	uint32_t done_bits = vop2_readl(vop2, RK3568_REG_CFG_DONE) & BIT(vp->id);

	/*
	 * When done bit is 0, indicate current frame is take effect.
	 */
	return done_bits == 0 ? true : false;
}

static void vop2_wait_for_fs_by_done_bit_status(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	bool done_bit;
	int ret;

	ret = readx_poll_timeout_atomic(vop2_vp_done_bit_status, vp, done_bit,
					done_bit, 0, 50 * 1000);
	if (ret)
		drm_err(vop2->drm, "wait vp%d done bit status timeout, vcnt: %d\n",
			      vp->id, vop2_read_vcnt(vp));
}

static uint16_t vop2_read_port_mux(struct vop2 *vop2)
{
	return vop2_readl(vop2, RK3568_OVL_PORT_SEL) & 0xffff;
}

static void vop2_wait_for_port_mux_done(struct vop2 *vop2)
{
	uint16_t port_mux_cfg;
	int ret;

	/*
	 * Spin until the previous port_mux figuration
	 * is done.
	 */
	ret = readx_poll_timeout_atomic(vop2_read_port_mux, vop2, port_mux_cfg,
					port_mux_cfg == vop2->port_mux_cfg, 0, 50 * 1000);
	if (ret)
		drm_err(vop2->drm, "wait port_mux done timeout: 0x%x--0x%x\n",
			      port_mux_cfg, vop2->port_mux_cfg);
}


static int32_t vop2_pending_done_bits(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	struct drm_display_mode *adjusted_mode;
	struct vop2_video_port *done_vp;
	uint32_t done_bits, done_bits_bak;
	uint32_t vp_id;
	uint32_t vcnt;
	struct drm_display_mode *first_mode, *second_mode;
	struct vop2_video_port *first_done_vp, *second_done_vp, *wait_vp;
	uint32_t first_vp_id, second_vp_id;
	uint32_t first_vp_vcnt, second_vp_vcnt;
	uint32_t first_vp_left_vcnt, second_vp_left_vcnt;
	uint32_t first_vp_left_time, second_vp_left_time;
	uint32_t first_vp_safe_time, second_vp_safe_time;

	done_bits = vop2_readl(vop2, RK3568_REG_CFG_DONE) & 0x7;
	done_bits_bak = done_bits;

	done_bits &= ~BIT(vp->id);

	/* no done bit, so no need to wait config done take effect */
	if (done_bits == 0)
		return 0;

	/* have the other one different vp, wait for config done take effect */
	if (hweight32(done_bits) == 1) {
		vp_id = ffs(done_bits) - 1;
		done_vp = &vop2->vps[vp_id];
		adjusted_mode = &done_vp->crtc.state->adjusted_mode;
		vcnt = vop2_read_vcnt(done_vp);

		/* if close to the last 1/8 frame, wait to next frame */
		if (vcnt > (adjusted_mode->crtc_vtotal * 7 >> 3)) {
			vop2_wait_for_fs_by_done_bit_status(done_vp);
			return 0;
		}
		return done_bits;
	}

	first_vp_id = ffs(done_bits) - 1;
	first_done_vp = &vop2->vps[first_vp_id];
	first_mode = &first_done_vp->crtc.state->adjusted_mode;
	/* set last 1/8 frame time as safe section */
	first_vp_safe_time = 1000000 / drm_mode_vrefresh(first_mode) >> 3;

	done_bits &= ~BIT(first_vp_id);
	second_vp_id = ffs(done_bits) - 1;
	second_done_vp = &vop2->vps[second_vp_id];
	second_mode = &second_done_vp->crtc.state->adjusted_mode;
	/* set last 1/8 frame time as safe section */
	second_vp_safe_time = 1000000 / drm_mode_vrefresh(second_mode) >> 3;

	first_vp_vcnt = vop2_read_vcnt(first_done_vp);

	second_vp_vcnt = vop2_read_vcnt(second_done_vp);

	first_vp_left_vcnt = first_mode->crtc_vtotal - first_vp_vcnt;
	second_vp_left_vcnt = second_mode->crtc_vtotal - second_vp_vcnt;
	first_vp_left_time = vop2_line_to_time(first_mode, first_vp_left_vcnt);
	second_vp_left_time = vop2_line_to_time(second_mode, second_vp_left_vcnt);

	/* if the two vp both at safe section, no need to wait */
	if (first_vp_left_time > first_vp_safe_time &&
	    second_vp_left_time > second_vp_safe_time)
		return done_bits_bak;

	if (first_vp_left_time > second_vp_left_time)
		wait_vp = first_done_vp;
	else
		wait_vp = second_done_vp;

	vop2_wait_for_fs_by_done_bit_status(wait_vp);

	done_bits = vop2_readl(vop2, RK3568_REG_CFG_DONE) & 0x7;
	if (done_bits) {
		vp_id = ffs(done_bits) - 1;
		done_vp = &vop2->vps[vp_id];
		vop2_wait_for_fs_by_done_bit_status(done_vp);
	}
	return 0;
}

static inline void vop2_cfg_done(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	uint32_t done_bits;
	uint32_t val;
	uint32_t old_layer_sel_val, cfg_layer_sel_val;
	struct vop2_layer *layer = &vop2->layers[0];
	uint32_t layer_sel_offset = layer->regs->layer_sel.offset;

	/*
	 * This is a workaround, the config done bits of VP0,
	 * VP1, VP2 on RK3568 stands on the first three bits
	 * on REG_CFG_DONE register without mask bit.
	 * If two or three config done events happens one after
	 * another in a very shot time, the flowing config done
	 * write may override the previous config done bit before
	 * it take effect:
	 * 1: config done 0x8001 for VP0
	 * 2: config done 0x8002 for VP1
	 *
	 * 0x8002 may override 0x8001 before it take effect.
	 *
	 * So we do a read | write here.
	 *
	 */
	done_bits = vop2_pending_done_bits(vp);
	old_layer_sel_val = vop2_readl(vop2, layer_sel_offset);
	cfg_layer_sel_val = vop2->regsbak[layer_sel_offset >> 2];
	/**
	 * This is rather low probability for miss some done bit.
	 */
	val = RK3568_VOP2_GLB_CFG_DONE_EN | BIT(vp->id) | done_bits |
		(vop2_readl(vop2, RK3568_REG_CFG_DONE) & 0x7);
	vop2_writel(vop2, 0, val);

	/**
	 * Make sure the layer sel is take effect when it's updated.
	 */
	if (old_layer_sel_val != cfg_layer_sel_val) {
		vp->layer_sel_update = true;
		vop2_wait_for_fs_by_done_bit_status(vp);
		drm_dbg(vop2->drm, "vp%d need to wait fs as old layer_sel val[0x%x] != new val[0x%x]\n",
			      vp->id, old_layer_sel_val, cfg_layer_sel_val);
	}
}

static void vop2_win_disable(struct vop2_win *win)
{
	struct vop2 *vop2 = win->vop2;

	vop2_win_write(win, &win->regs->enable, 0);
	if (win->data->feature & WIN_FEATURE_CLUSTER_MAIN) {
		struct vop2_win *sub_win;
		int i = 0;

		for (i = 0; i < vop2->registered_num_wins; i++) {
			sub_win = &vop2->win[i];

			if ((sub_win->data->phys_id == win->data->phys_id) &&
			    (sub_win->data->feature & WIN_FEATURE_CLUSTER_SUB))
				vop2_win_write(sub_win, &sub_win->regs->enable, 0);
		}

		vop2_win_write(win, &win->regs->cluster->enable, 0);
	}
}

static enum vop2_data_format vop2_convert_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return VOP2_FMT_ARGB8888;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return VOP2_FMT_RGB888;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		return VOP2_FMT_RGB565;
	case DRM_FORMAT_NV12:
		return VOP2_FMT_YUV420SP;
	case DRM_FORMAT_NV16:
		return VOP2_FMT_YUV422SP;
	case DRM_FORMAT_NV24:
		return VOP2_FMT_YUV444SP;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
		return VOP2_FMT_VYUY422;
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_UYVY:
		return VOP2_FMT_YUYV422;
	default:
		DRM_ERROR("unsupported format[%08x]\n", format);
		return -EINVAL;
	}
}

static enum vop2_afbc_format vop2_convert_afbc_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return VOP2_AFBC_FMT_ARGB8888;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return VOP2_AFBC_FMT_RGB888;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		return VOP2_AFBC_FMT_RGB565;
	case DRM_FORMAT_NV12:
		return VOP2_AFBC_FMT_YUV420;
	case DRM_FORMAT_NV16:
		return VOP2_AFBC_FMT_YUV422;

		/* either of the below should not be reachable */
	default:
		DRM_WARN_ONCE("unsupported AFBC format[%08x]\n", format);
		return VOP2_AFBC_FMT_INVALID;
	}

	return VOP2_AFBC_FMT_INVALID;
}

static bool vop2_win_rb_swap(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_BGR565:
		return true;
	default:
		return false;
	}
}

static bool vop2_afbc_rb_swap(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV24:
		return true;
	default:
		return false;
	}
}

static bool vop2_afbc_uv_swap(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
		return true;
	default:
		return false;
	}
}

static bool vop2_win_uv_swap(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV24:
		return true;
	default:
		return false;
	}
}

static bool vop2_win_dither_up(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_RGB565:
		return true;
	default:
		return false;
	}
}

static bool vop2_output_uv_swap(uint32_t bus_format, uint32_t output_mode)
{
	/*
	 * FIXME:
	 *
	 * There is no media type for YUV444 output,
	 * so when out_mode is AAAA or P888, assume output is YUV444 on
	 * yuv format.
	 *
	 * From H/W testing, YUV444 mode need a rb swap.
	 */
	if (bus_format == MEDIA_BUS_FMT_YVYU8_1X16 ||
	    bus_format == MEDIA_BUS_FMT_VYUY8_1X16 ||
	    bus_format == MEDIA_BUS_FMT_YVYU8_2X8 ||
	    bus_format == MEDIA_BUS_FMT_VYUY8_2X8 ||
	    ((bus_format == MEDIA_BUS_FMT_YUV8_1X24 ||
	      bus_format == MEDIA_BUS_FMT_YUV10_1X30) &&
	     (output_mode == ROCKCHIP_OUT_MODE_AAAA ||
	      output_mode == ROCKCHIP_OUT_MODE_P888)))
		return true;
	else
		return false;
}

static bool vop2_output_yc_swap(uint32_t bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
		return true;
	default:
		return false;
	}
}

static bool is_yuv_support(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		return true;
	default:
		return false;
	}
}

static bool is_yuv_output(uint32_t bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
		return true;
	default:
		return false;
	}
}

static bool is_alpha_support(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		return true;
	default:
		return false;
	}
}

static bool rockchip_afbc(struct drm_plane *plane, u64 modifier)
{
	int i;

	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return false;

	for (i = 0 ; i < plane->modifier_count; i++)
		if (plane->modifiers[i] == modifier)
			break;

	return (i < plane->modifier_count) ? true : false;

}

static bool rockchip_vop2_mod_supported(struct drm_plane *plane, uint32_t format, u64 modifier)
{
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2 *vop2 = win->vop2;

	if (modifier == DRM_FORMAT_MOD_INVALID)
		return false;

	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (!rockchip_afbc(plane, modifier)) {
		drm_err(vop2->drm, "Unsupported format modifier 0x%llx\n", modifier);

		return false;
	}

	return vop2_convert_afbc_format(format) >= 0;
}

static inline bool vop2_cluster_window(struct vop2_win *win)
{
	return  (win->data->feature & (WIN_FEATURE_CLUSTER_MAIN | WIN_FEATURE_CLUSTER_SUB));
}

static int vop2_afbc_half_block_enable(struct drm_plane_state *pstate)
{
	if ((pstate->rotation & DRM_MODE_ROTATE_270) || (pstate->rotation & DRM_MODE_ROTATE_90))
		return 0;
	else
		return 1;
}

static uint32_t vop2_afbc_transform_offset(struct drm_plane_state *pstate,
					   bool afbc_half_block_en)
{
	struct drm_rect *src = &pstate->src;
	struct drm_framebuffer *fb = pstate->fb;
	uint32_t bpp = fb->format->cpp[0] * 8;
	uint32_t vir_width = (fb->pitches[0] << 3) / bpp;
	uint32_t width = drm_rect_width(src) >> 16;
	uint32_t height = drm_rect_height(src) >> 16;
	uint32_t act_xoffset = src->x1 >> 16;
	uint32_t act_yoffset = src->y1 >> 16;
	uint32_t align16_crop = 0;
	uint32_t align64_crop = 0;
	uint32_t height_tmp = 0;
	uint32_t transform_tmp = 0;
	uint8_t transform_xoffset = 0;
	uint8_t transform_yoffset = 0;
	uint8_t top_crop = 0;
	uint8_t top_crop_line_num = 0;
	uint8_t bottom_crop_line_num = 0;

	/* 16 pixel align */
	if (height & 0xf)
		align16_crop = 16 - (height & 0xf);

	height_tmp = height + align16_crop;

	/* 64 pixel align */
	if (height_tmp & 0x3f)
		align64_crop = 64 - (height_tmp & 0x3f);

	top_crop_line_num = top_crop << 2;
	if (top_crop == 0)
		bottom_crop_line_num = align16_crop + align64_crop;
	else if (top_crop == 1)
		bottom_crop_line_num = align16_crop + align64_crop + 12;
	else if (top_crop == 2)
		bottom_crop_line_num = align16_crop + align64_crop + 8;

	switch (pstate->rotation &
		(DRM_MODE_REFLECT_X |
		 DRM_MODE_REFLECT_Y |
		 DRM_MODE_ROTATE_90 |
		 DRM_MODE_ROTATE_270)) {
	case DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y:
		transform_tmp = act_xoffset + width;
		transform_xoffset = 16 - (transform_tmp & 0xf);
		transform_tmp = bottom_crop_line_num - act_yoffset;

		if (afbc_half_block_en)
			transform_yoffset = transform_tmp & 0x7;
		else
			transform_yoffset = (transform_tmp & 0xf);

		break;
	case DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_90:
		transform_tmp = bottom_crop_line_num - act_yoffset;
		transform_xoffset = transform_tmp & 0xf;
		transform_tmp = vir_width - width - act_xoffset;
		transform_yoffset = transform_tmp & 0xf;
		break;
	case DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_270:
		transform_tmp = top_crop_line_num + act_yoffset;
		transform_xoffset = transform_tmp & 0xf;
		transform_tmp = act_xoffset;
		transform_yoffset = transform_tmp & 0xf;
		break;
	case DRM_MODE_REFLECT_X:
		transform_tmp = act_xoffset + width;
		transform_xoffset = 16 - (transform_tmp & 0xf);
		transform_tmp = top_crop_line_num + act_yoffset;

		if (afbc_half_block_en)
			transform_yoffset = transform_tmp & 0x7;
		else
			transform_yoffset = transform_tmp & 0xf;

		break;
	case DRM_MODE_REFLECT_Y:
		transform_tmp = act_xoffset;
		transform_xoffset = transform_tmp & 0xf;
		transform_tmp = bottom_crop_line_num - act_yoffset;

		if (afbc_half_block_en)
			transform_yoffset = transform_tmp & 0x7;
		else
			transform_yoffset = transform_tmp & 0xf;

		break;
	case DRM_MODE_ROTATE_90:
		transform_tmp = bottom_crop_line_num - act_yoffset;
		transform_xoffset = transform_tmp & 0xf;
		transform_tmp = act_xoffset;
		transform_yoffset = transform_tmp & 0xf;
		break;
	case DRM_MODE_ROTATE_270:
		transform_tmp = top_crop_line_num + act_yoffset;
		transform_xoffset = transform_tmp & 0xf;
		transform_tmp = vir_width - width - act_xoffset;
		transform_yoffset = transform_tmp & 0xf;
		break;
	case 0:
		transform_tmp = act_xoffset;
		transform_xoffset = transform_tmp & 0xf;
		transform_tmp = top_crop_line_num + act_yoffset;

		if (afbc_half_block_en)
			transform_yoffset = transform_tmp & 0x7;
		else
			transform_yoffset = transform_tmp & 0xf;

		break;
	}

	return (transform_xoffset & 0xf) | ((transform_yoffset & 0xf) << 16);
}

/*
 * A Cluster window has 2048 x 16 line buffer, which can
 * works at 2048 x 16(Full) or 4096 x 8 (Half) mode.
 * for Cluster_lb_mode register:
 * 0: half mode, for plane input width range 2048 ~ 4096
 * 1: half mode, for cluster work at 2 * 2048 plane mode
 * 2: half mode, for rotate_90/270 mode
 *
 */
static int vop2_get_cluster_lb_mode(struct vop2_win *win, struct drm_plane_state *pstate)
{
	if ((pstate->rotation & DRM_MODE_ROTATE_270) || (pstate->rotation & DRM_MODE_ROTATE_90))
		return 2;
	else if (win->data->feature & WIN_FEATURE_CLUSTER_SUB)
		return 1;
	else
		return 0;
}

/*
 * bli_sd_factor = (src - 1) / (dst - 1) << 12;
 * avg_sd_factor:
 * bli_su_factor:
 * bic_su_factor:
 * = (src - 1) / (dst - 1) << 16;
 *
 * gt2 enable: dst get one line from two line of the src
 * gt4 enable: dst get one line from four line of the src.
 *
 */

static uint16_t vop2_scale_factor(enum scale_mode mode,
				  int32_t filter_mode,
				  uint32_t src, uint32_t dst)
{
	uint32_t fac;
	int i;

	if (mode == SCALE_NONE)
		return 0;

	/*
	 * A workaround to avoid zero div.
	 */
	if ((dst == 1) || (src == 1)) {
		dst = dst + 1;
		src = src + 1;
	}

	if ((mode == SCALE_DOWN) && (filter_mode == VOP2_SCALE_DOWN_BIL)) {
		fac = ((src - 1) << 12) / (dst - 1);
		for (i = 0; i < 100; i++) {
			if (fac * (dst - 1) >> 12 < (src - 1))
				break;
			fac -= 1;
			DRM_DEBUG("down fac cali: src:%d, dst:%d, fac:0x%x\n", src, dst, fac);
		}
	} else {
		fac = ((src - 1) << 16) / (dst - 1);
		for (i = 0; i < 100; i++) {
			if (fac * (dst - 1) >> 16 < (src - 1))
				break;
			fac -= 1;
			DRM_DEBUG("up fac cali:  src:%d, dst:%d, fac:0x%x\n", src, dst, fac);
		}
	}

	return fac;
}

static void vop2_setup_scale(struct vop2 *vop2, const struct vop2_win *win,
			     uint32_t src_w, uint32_t src_h, uint32_t dst_w,
			     uint32_t dst_h, uint32_t pixel_format)
{
	const struct vop2_win_data *win_data = win->data;
	const struct drm_format_info *info;
	uint16_t cbcr_src_w;
	uint16_t cbcr_src_h;
	uint16_t yrgb_hor_scl_mode, yrgb_ver_scl_mode;
	uint16_t cbcr_hor_scl_mode, cbcr_ver_scl_mode;
	uint16_t hscl_filter_mode, vscl_filter_mode;
	uint8_t gt2 = 0;
	uint8_t gt4 = 0;
	uint32_t val;

	info = drm_format_info(pixel_format);

	cbcr_src_w = src_w / info->hsub;
	cbcr_src_h = src_h / info->vsub;

	if (src_h >= (4 * dst_h)) {
		gt4 = 1;
		src_h >>= 2;
	} else if (src_h >= (2 * dst_h)) {
		gt2 = 1;
		src_h >>= 1;
	}

	yrgb_hor_scl_mode = scl_get_scl_mode(src_w, dst_w);
	yrgb_ver_scl_mode = scl_get_scl_mode(src_h, dst_h);

	if (yrgb_hor_scl_mode == SCALE_UP)
		hscl_filter_mode = win_data->hsu_filter_mode;
	else
		hscl_filter_mode = win_data->hsd_filter_mode;

	if (yrgb_ver_scl_mode == SCALE_UP)
		vscl_filter_mode = win_data->vsu_filter_mode;
	else
		vscl_filter_mode = win_data->vsd_filter_mode;

	/*
	 * RK3568 VOP Esmart/Smart dsp_w should be even pixel
	 * at scale down mode
	 */
	if (!(win->data->feature & WIN_FEATURE_AFBDC)) {
		if ((yrgb_hor_scl_mode == SCALE_DOWN) && (dst_w & 0x1)) {
			drm_dbg(vop2->drm, "%s dst_w[%d] should align as 2 pixel\n", win->data->name, dst_w);
			dst_w += 1;
		}
	}

	val = vop2_scale_factor(yrgb_hor_scl_mode, hscl_filter_mode,
				src_w, dst_w);
	vop2_win_write(win, &win->regs->scl->scale_yrgb_x, val);
	val = vop2_scale_factor(yrgb_ver_scl_mode, vscl_filter_mode,
				src_h, dst_h);
	vop2_win_write(win, &win->regs->scl->scale_yrgb_y, val);

	vop2_win_write(win, &win->regs->scl->vsd_yrgb_gt4, gt4);
	vop2_win_write(win, &win->regs->scl->vsd_yrgb_gt2, gt2);

	vop2_win_write(win, &win->regs->scl->yrgb_hor_scl_mode, yrgb_hor_scl_mode);
	vop2_win_write(win, &win->regs->scl->yrgb_ver_scl_mode, yrgb_ver_scl_mode);

	vop2_win_write(win, &win->regs->scl->yrgb_hscl_filter_mode, hscl_filter_mode);
	vop2_win_write(win, &win->regs->scl->yrgb_vscl_filter_mode, vscl_filter_mode);

	if (info->is_yuv) {
		gt4 = gt2 = 0;

		if (cbcr_src_h >= (4 * dst_h))
			gt4 = 1;
		else if (cbcr_src_h >= (2 * dst_h))
			gt2 = 1;

		if (gt4)
			cbcr_src_h >>= 2;
		else if (gt2)
			cbcr_src_h >>= 1;

		cbcr_hor_scl_mode = scl_get_scl_mode(cbcr_src_w, dst_w);
		cbcr_ver_scl_mode = scl_get_scl_mode(cbcr_src_h, dst_h);

		val = vop2_scale_factor(cbcr_hor_scl_mode, hscl_filter_mode,
					cbcr_src_w, dst_w);
		vop2_win_write(win, &win->regs->scl->scale_cbcr_x, val);
		val = vop2_scale_factor(cbcr_ver_scl_mode, vscl_filter_mode,
					cbcr_src_h, dst_h);
		vop2_win_write(win, &win->regs->scl->scale_cbcr_y, val);

		vop2_win_write(win, &win->regs->scl->vsd_cbcr_gt4, gt4);
		vop2_win_write(win, &win->regs->scl->vsd_cbcr_gt2, gt2);
		vop2_win_write(win, &win->regs->scl->cbcr_hor_scl_mode, cbcr_hor_scl_mode);
		vop2_win_write(win, &win->regs->scl->cbcr_ver_scl_mode, cbcr_ver_scl_mode);
		vop2_win_write(win, &win->regs->scl->cbcr_hscl_filter_mode, hscl_filter_mode);
		vop2_win_write(win, &win->regs->scl->cbcr_vscl_filter_mode, vscl_filter_mode);
	}
}

static int vop2_convert_csc_mode(int csc_mode)
{
	switch (csc_mode) {
	case V4L2_COLORSPACE_SMPTE170M:
	case V4L2_COLORSPACE_470_SYSTEM_M:
	case V4L2_COLORSPACE_470_SYSTEM_BG:
		return CSC_BT601L;
	case V4L2_COLORSPACE_REC709:
	case V4L2_COLORSPACE_SMPTE240M:
	case V4L2_COLORSPACE_DEFAULT:
		return CSC_BT709L;
	case V4L2_COLORSPACE_JPEG:
		return CSC_BT601F;
	case V4L2_COLORSPACE_BT2020:
		return CSC_BT2020;
	default:
		return CSC_BT709L;
	}
}

static bool vop2_is_allwin_disabled(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	unsigned long win_mask = vp->win_mask;
	struct vop2_win *win;
	int phys_id;

	for_each_set_bit(phys_id, &win_mask, ROCKCHIP_MAX_LAYER) {
		win = vop2_find_win_by_phys_id(vop2, phys_id);
		if (vop2_win_read(win, &win->regs->enable) != 0)
			return false;
	}

	return true;
}

static void vop2_disable_all_planes_for_crtc(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	struct vop2_win *win;
	unsigned long win_mask = vp->win_mask;
	int phys_id, ret;
	bool active, need_wait_win_disabled = false;

	for_each_set_bit(phys_id, &win_mask, ROCKCHIP_MAX_LAYER) {
		win = vop2_find_win_by_phys_id(vop2, phys_id);
		need_wait_win_disabled |= vop2_win_read(win, &win->regs->enable);
		vop2_win_disable(win);
	}

	if (need_wait_win_disabled) {
		vop2_cfg_done(vp);
		ret = readx_poll_timeout_atomic(vop2_is_allwin_disabled, vp,
						active, active, 0, 500 * 1000);
		if (ret)
			drm_err(vop2->drm, "wait win close timeout\n");
	}
}

/*
 * colorspace path:
 *      Input        Win csc                     Output
 * 1. YUV(2020)  --> Y2R->2020To709->R2Y   --> YUV_OUTPUT(601/709)
 *    RGB        --> R2Y                  __/
 *
 * 2. YUV(2020)  --> bypasss               --> YUV_OUTPUT(2020)
 *    RGB        --> 709To2020->R2Y       __/
 *
 * 3. YUV(2020)  --> Y2R->2020To709        --> RGB_OUTPUT(709)
 *    RGB        --> R2Y                  __/
 *
 * 4. YUV(601/709)-> Y2R->709To2020->R2Y   --> YUV_OUTPUT(2020)
 *    RGB        --> 709To2020->R2Y       __/
 *
 * 5. YUV(601/709)-> bypass                --> YUV_OUTPUT(709)
 *    RGB        --> R2Y                  __/
 *
 * 6. YUV(601/709)-> bypass                --> YUV_OUTPUT(601)
 *    RGB        --> R2Y(601)             __/
 *
 * 7. YUV        --> Y2R(709)              --> RGB_OUTPUT(709)
 *    RGB        --> bypass               __/
 *
 * 8. RGB        --> 709To2020->R2Y        --> YUV_OUTPUT(2020)
 *
 * 9. RGB        --> R2Y(709)              --> YUV_OUTPUT(709)
 *
 * 10. RGB       --> R2Y(601)              --> YUV_OUTPUT(601)
 *
 * 11. RGB       --> bypass                --> RGB_OUTPUT(709)
 */

static void vop2_setup_csc_mode(struct vop2_video_port *vp,
				struct vop2_win *win,
				struct drm_plane_state *pstate)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(vp->crtc.state);
	int is_input_yuv = is_yuv_support(pstate->fb->format->format);
	int is_output_yuv = is_yuv_output(vcstate->bus_format);
	int input_csc = V4L2_COLORSPACE_DEFAULT;
	int output_csc = vcstate->color_space;
	bool r2y_en, y2r_en;
	int csc_mode;

	if (is_input_yuv && !is_output_yuv) {
		y2r_en = 1;
		r2y_en = 0;
		csc_mode = vop2_convert_csc_mode(input_csc);
	} else if (!is_input_yuv && is_output_yuv) {
		y2r_en = 0;
		r2y_en = 1;
		csc_mode = vop2_convert_csc_mode(output_csc);
	} else {
		y2r_en = 0;
		r2y_en = 0;
		csc_mode = 0;
	}

	vop2_win_write(win, &win->regs->y2r_en, y2r_en);
	vop2_win_write(win, &win->regs->r2y_en, r2y_en);
	vop2_win_write(win, &win->regs->csc_mode, csc_mode);
}

static void vop2_axi_irqs_enable(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop_intr *intr;
	uint32_t irqs = BUS_ERROR_INTR;
	uint32_t i;

	for (i = 0; i < vop2_data->nr_axi_intr; i++) {
		intr = &vop2_data->axi_intr[i];
		vop2_intr_set_type(vop2, intr, &intr->clear, irqs, 1);
		vop2_intr_set_type(vop2, intr, &intr->enable, irqs, 1);
	}
}

static uint32_t vop2_read_and_clear_axi_irqs(struct vop2 *vop2, int index)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop_intr *intr = &vop2_data->axi_intr[index];
	uint32_t irqs = BUS_ERROR_INTR;
	uint32_t val;

	val = vop2_get_intr_type(vop2, intr, &intr->status, irqs);
	if (val)
		vop2_intr_set_type(vop2, intr, &intr->clear, val, 1);

	return val;
}

static void vop2_dsp_hold_valid_irq_enable(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;

	unsigned long flags;

	if (WARN_ON(!vop2->enable_count))
		return;

	spin_lock_irqsave(&vop2->irq_lock, flags);

	vop2_intr_set_type(vop2, intr, &intr->clear, DSP_HOLD_VALID_INTR, 1);
	vop2_intr_set_type(vop2, intr, &intr->enable, DSP_HOLD_VALID_INTR, 1);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);
}

static void vop2_dsp_hold_valid_irq_disable(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	unsigned long flags;

	if (WARN_ON(!vop2->enable_count))
		return;

	spin_lock_irqsave(&vop2->irq_lock, flags);

	vop2_intr_set_type(vop2, intr, &intr->enable, DSP_HOLD_VALID_INTR, 0);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);
}

static void vop2_debug_irq_enable(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	uint32_t irqs = POST_BUF_EMPTY_INTR;

	vop2_intr_set_type(vop2, intr, &intr->clear, irqs, 1);
	vop2_intr_set_type(vop2, intr, &intr->enable, irqs, 1);
}

static bool vop2_dsp_lut_is_enabled(struct vop2_video_port *vp)
{
	return vop2_read(vp->vop2, &vp->regs->dsp_lut_en);
}

/*
 * (1) each frame starts at the start of the Vsync pulse which is signaled by
 *     the "FRAME_SYNC" interrupt.
 * (2) the active data region of each frame ends at dsp_vact_end
 * (3) we should program this same number (dsp_vact_end) into dsp_line_frag_num,
 *      to get "LINE_FLAG" interrupt at the end of the active on screen data.
 *
 * VOP_INTR_CTRL0.dsp_line_frag_num = VOP_DSP_VACT_ST_END.dsp_vact_end
 * Interrupts
 * LINE_FLAG -------------------------------+
 * FRAME_SYNC ----+                         |
 *                |                         |
 *                v                         v
 *                | Vsync | Vbp |  Vactive  | Vfp |
 *                        ^     ^           ^     ^
 *                        |     |           |     |
 *                        |     |           |     |
 * dsp_vs_end ------------+     |           |     |   VOP_DSP_VTOTAL_VS_END
 * dsp_vact_start --------------+           |     |   VOP_DSP_VACT_ST_END
 * dsp_vact_end ----------------------------+     |   VOP_DSP_VACT_ST_END
 * dsp_total -------------------------------------+   VOP_DSP_VTOTAL_VS_END
 */

static void vop2_crtc_load_lut(struct drm_crtc *crtc, struct drm_color_lut *lut)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	int dle = 0, i = 0;
	uint8_t vp_enable_gamma_nr = 0;

	for (i = 0; i < vop2->data->nr_vps; i++) {
		struct vop2_video_port *vp = &vop2->vps[i];

		if (i == vp->id)
			continue;

		if (vp->gamma_lut_active)
			vp_enable_gamma_nr++;
	}

	if (vp_enable_gamma_nr >= vop2->data->nr_gammas) {
		drm_info(vop2->drm, "only support %d gamma\n", vop2->data->nr_gammas);
		return;
	}

	spin_lock(&vop2->reg_lock);
	vop2_write(vop2, &vp->regs->dsp_lut_en, 0);
	vop2_cfg_done(vp);
	spin_unlock(&vop2->reg_lock);

	readx_poll_timeout(vop2_dsp_lut_is_enabled, vp, dle, !dle, 5, 33333);

	for (i = 0; i < vp->gamma_lut_len; i++) {
		uint32_t r, g, b;
		uint32_t ll = vp->gamma_lut_len;

		r = lut[i].red * (ll - 1) / 0xffff;
		g = lut[i].green * (ll - 1) / 0xffff;
		b = lut[i].blue * (ll - 1) / 0xffff;
		writel(b * ll * ll + g * ll + r, vop2->lut_regs + (i << 2));
	}

	spin_lock(&vop2->reg_lock);

	vop2_write(vop2, &vp->regs->dsp_lut_en, 1);
	vop2_write(vop2, &vop2->data->ctrl->gamma_port_sel, vp->id);
	vop2_cfg_done(vp);
	vp->gamma_lut_active = true;

	spin_unlock(&vop2->reg_lock);
}

static int vop2_core_clks_prepare_enable(struct vop2 *vop2)
{
	int ret;

	ret = clk_prepare_enable(vop2->hclk);
	if (ret < 0) {
		drm_err(vop2->drm, "failed to enable hclk - %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vop2->aclk);
	if (ret < 0) {
		drm_err(vop2->drm, "failed to enable aclk - %d\n", ret);
		goto err;
	}

	return 0;
err:
	clk_disable_unprepare(vop2->hclk);

	return ret;
}

/*
 * VOP2 architecture
 *
 +----------+   +-------------+                                                        +-----------+
 |  Cluster |   | Sel 1 from 6|                                                        | 1 from 3  |
 |  window0 |   |    Layer0   |                                                        |    RGB    |
 +----------+   +-------------+              +---------------+    +-------------+      +-----------+
 +----------+   +-------------+              |N from 6 layers|    |             |
 |  Cluster |   | Sel 1 from 6|              |   Overlay0    +--->| Video Port0 |      +-----------+
 |  window1 |   |    Layer1   |              |               |    |             |      | 1 from 3  |
 +----------+   +-------------+              +---------------+    +-------------+      |   LVDS    |
 +----------+   +-------------+                                                        +-----------+
 |  Esmart  |   | Sel 1 from 6|
 |  window0 |   |   Layer2    |              +---------------+    +-------------+      +-----------+
 +----------+   +-------------+              |N from 6 Layers|    |             | +--> | 1 from 3  |
 +----------+   +-------------+   -------->  |   Overlay1    +--->| Video Port1 |      |   MIPI    |
 |  Esmart  |   | Sel 1 from 6|   -------->  |               |    |             |      +-----------+
 |  Window1 |   |   Layer3    |              +---------------+    +-------------+
 +----------+   +-------------+                                                        +-----------+
 +----------+   +-------------+                                                        | 1 from 3  |
 |  Smart   |   | Sel 1 from 6|              +---------------+    +-------------+      |   HDMI    |
 |  Window0 |   |    Layer4   |              |N from 6 Layers|    |             |      +-----------+
 +----------+   +-------------+              |   Overlay2    +--->| Video Port2 |
 +----------+   +-------------+              |               |    |             |      +-----------+
 |  Smart   |   | Sel 1 from 6|              +---------------+    +-------------+      |  1 from 3 |
 |  Window1 |   |    Layer5   |                                                        |    eDP    |
 +----------+   +-------------+                                                        +-----------+
 *
 */

static void vop2_enable(struct vop2 *vop2)
{
	int ret;

	ret = pm_runtime_get_sync(vop2->dev);
	if (ret < 0) {
		drm_err(vop2->drm, "failed to get pm runtime: %d\n", ret);
		return;
	}

	ret = vop2_core_clks_prepare_enable(vop2);
	if (ret) {
		pm_runtime_put_sync(vop2->dev);
		return;
	}

	vop2_write(vop2, &vop2->data->ctrl->dma_stop, 0);

	if (vop2->data->soc_id == 3566)
		vop2_write(vop2, &vop2->data->ctrl->otp_en, 1);

	memcpy(vop2->regsbak, vop2->regs, vop2->len);

	vop2_write(vop2, &vop2->data->ctrl->cfg_done_en, 1);
	/*
	 * Disable auto gating, this is a workaround to
	 * avoid display image shift when a window enabled.
	 */
	vop2_write(vop2, &vop2->data->ctrl->auto_gating_en, 0);
	/*
	 * Register OVERLAY_LAYER_SEL and OVERLAY_PORT_SEL should take effect immediately,
	 * than windows configuration(CLUSTER/ESMART/SMART) can take effect according the
	 * video port mux configuration as we wished.
	 */
	vop2_write(vop2, &vop2->data->ctrl->ovl_port_mux_cfg_done_imd, 1);
	/*
	 * Let SYS_DSP_INFACE_EN/SYS_DSP_INFACE_CTRL/SYS_DSP_INFACE_POL take effect
	 * immediately.
	 */
	vop2_write(vop2, &vop2->data->ctrl->if_ctrl_cfg_done_imd, 1);

	vop2_axi_irqs_enable(vop2);
}

static void vop2_disable(struct vop2 *vop2)
{
	/*
	 * vop2 standby complete, so iommu detach is safe.
	 */
	vop2_write(vop2, &vop2->data->ctrl->dma_stop, 1);

	pm_runtime_put_sync(vop2->dev);

	clk_disable_unprepare(vop2->aclk);
	clk_disable_unprepare(vop2->hclk);
}

static void vop2_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	int ret;

	WARN_ON(vp->event);
	vop2_lock(vop2);

	drm_crtc_vblank_off(crtc);
	vop2_disable_all_planes_for_crtc(vp);

	/*
	 * Vop standby will take effect at end of current frame,
	 * if dsp hold valid irq happen, it means standby complete.
	 *
	 * we must wait standby complete when we want to disable aclk,
	 * if not, memory bus maybe dead.
	 */
	reinit_completion(&vp->dsp_hold_completion);
	vop2_dsp_hold_valid_irq_enable(crtc);

	spin_lock(&vop2->reg_lock);

	vop2_write(vop2, &vp->regs->standby, 1);

	spin_unlock(&vop2->reg_lock);

	ret = wait_for_completion_timeout(&vp->dsp_hold_completion, msecs_to_jiffies(50));
	if (!ret)
		drm_info(vop2->drm, "wait for vp%d dsp_hold timeout\n", vp->id);

	vop2_dsp_hold_valid_irq_disable(crtc);

	clk_disable_unprepare(vp->dclk);

	vop2->enable_count--;

	if (!vop2->enable_count)
		vop2_disable(vop2);

	vop2_unlock(vop2);

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static int vop2_plane_atomic_check(struct drm_plane *plane, struct drm_atomic_state *astate)
{
	struct drm_plane_state *pstate = drm_atomic_get_new_plane_state(astate, plane);
	struct vop2_win *win = to_vop2_win(plane);
	struct drm_framebuffer *fb = pstate->fb;
	struct drm_crtc *crtc = pstate->crtc;
	struct drm_crtc_state *cstate;
	struct vop2_video_port *vp;
	struct vop2 *vop2;
	const struct vop2_data *vop2_data;
	struct drm_rect *dest = &pstate->dst;
	struct drm_rect *src = &pstate->src;
	int min_scale = win->regs->scl ? FRAC_16_16(1, 8) : DRM_PLANE_HELPER_NO_SCALING;
	int max_scale = win->regs->scl ? FRAC_16_16(8, 1) : DRM_PLANE_HELPER_NO_SCALING;
	int format;
	int ret;

	if (!crtc)
		return 0;

	vp = to_vop2_video_port(crtc);
	vop2 = vp->vop2;
	vop2_data = vop2->data;

	cstate = drm_atomic_get_existing_crtc_state(pstate->state, crtc);
	if (WARN_ON(!cstate))
		return -EINVAL;

	ret = drm_atomic_helper_check_plane_state(pstate, cstate,
						  min_scale, max_scale,
						  true, true);
	if (ret)
		return ret;

	if (!pstate->visible)
		return 0;

	format = vop2_convert_format(fb->format->format);
	if (format < 0)
		return format;

	if (drm_rect_width(src) >> 16 < 4 || drm_rect_height(src) >> 16 < 4 ||
	    drm_rect_width(dest) < 4 || drm_rect_width(dest) < 4) {
		drm_err(vop2->drm, "Invalid size: %dx%d->%dx%d, min size is 4x4\n",
			  drm_rect_width(src) >> 16, drm_rect_height(src) >> 16,
			  drm_rect_width(dest), drm_rect_height(dest));
		pstate->visible = false;
		return 0;
	}

	if (drm_rect_width(src) >> 16 > vop2_data->max_input.width ||
	    drm_rect_height(src) >> 16 > vop2_data->max_input.height) {
		drm_err(vop2->drm, "Invalid source: %dx%d. max input: %dx%d\n",
			  drm_rect_width(src) >> 16,
			  drm_rect_height(src) >> 16,
			  vop2_data->max_input.width,
			  vop2_data->max_input.height);
		return -EINVAL;
	}

	/*
	 * Src.x1 can be odd when do clip, but yuv plane start point
	 * need align with 2 pixel.
	 */
	if (fb->format->is_yuv && ((pstate->src.x1 >> 16) % 2)) {
		drm_err(vop2->drm, "Invalid Source: Yuv format not support odd xpos\n");
		return -EINVAL;
	}

	return 0;
}

static void vop2_plane_atomic_disable(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct drm_plane_state *old_pstate = drm_atomic_get_old_plane_state(state, plane);
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2 *vop2 = win->vop2;

	drm_dbg(vop2->drm, "%s disable\n", win->data->name);

	if (!old_pstate->crtc)
		return;

	spin_lock(&vop2->reg_lock);

	vop2_win_disable(win);
	vop2_win_write(win, &win->regs->yuv_clip, 0);

	spin_unlock(&vop2->reg_lock);
}

/*
 * The color key is 10 bit, so all format should
 * convert to 10 bit here.
 */
static void vop2_plane_setup_color_key(struct drm_plane *plane, uint32_t color_key)
{
	struct drm_plane_state *pstate = plane->state;
	struct drm_framebuffer *fb = pstate->fb;
	struct vop2_win *win = to_vop2_win(plane);
	uint32_t color_key_en = 0;
	uint32_t r = 0;
	uint32_t g = 0;
	uint32_t b = 0;

	if (!(color_key & VOP2_COLOR_KEY_MASK) || fb->format->is_yuv) {
		vop2_win_write(win, &win->regs->color_key_en, 0);
		return;
	}

	switch (fb->format->format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		r = (color_key & 0xf800) >> 11;
		g = (color_key & 0x7e0) >> 5;
		b = (color_key & 0x1f);
		r <<= 5;
		g <<= 4;
		b <<= 5;
		color_key_en = 1;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		r = (color_key & 0xff0000) >> 16;
		g = (color_key & 0xff00) >> 8;
		b = (color_key & 0xff);
		r <<= 2;
		g <<= 2;
		b <<= 2;
		color_key_en = 1;
		break;
	}

	vop2_win_write(win, &win->regs->color_key_en, color_key_en);
	vop2_win_write(win, &win->regs->color_key, (r << 20) | (g << 10) | b);
}

static void vop2_plane_atomic_update(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct drm_plane_state *pstate = plane->state;
	struct drm_crtc *crtc = pstate->crtc;
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2 *vop2 = win->vop2;
	struct drm_framebuffer *fb = pstate->fb;
	uint32_t bpp = fb->format->cpp[0] * 8;
	uint32_t actual_w, actual_h, dsp_w, dsp_h;
	uint32_t act_info, dsp_info;
	uint32_t format;
	uint32_t afbc_format;
	uint32_t rb_swap;
	uint32_t uv_swap;
	struct drm_rect *src = &pstate->src;
	struct drm_rect *dest = &pstate->dst;
	uint32_t afbc_tile_num;
	uint32_t afbc_half_block_en;
	uint32_t transform_offset;
	bool dither_up;
	bool xmirror = pstate->rotation & DRM_MODE_REFLECT_X;
	bool ymirror = pstate->rotation & DRM_MODE_REFLECT_Y;
	bool rotate_270 = pstate->rotation & DRM_MODE_ROTATE_270;
	bool rotate_90 = pstate->rotation & DRM_MODE_ROTATE_90;
	struct rockchip_gem_object *rk_obj;
	unsigned long offset;
	bool afbc_en;
	dma_addr_t yrgb_mst;
	dma_addr_t uv_mst;

	/*
	 * can't update plane when vop2 is disabled.
	 */
	if (WARN_ON(!crtc))
		return;

	if (WARN_ON(!vop2->enable_count))
		return;

	if (!pstate->visible) {
		vop2_plane_atomic_disable(plane, state);
		return;
	}

	/*
	 * This means this window is moved from another vp
	 * so the VOP2_PORT_SEL register is changed and
	 * take effect by vop2_wait_for_port_mux_done
	 * in this commit. so we can continue configure
	 * the window and report vsync
	 */
	if (win->old_vp_mask != win->vp_mask) {
		win->old_vp_mask = win->vp_mask;
		vp->skip_vsync = false;
	}

	afbc_en = rockchip_afbc(plane, fb->modifier);

	offset = (src->x1 >> 16) * fb->format->cpp[0];

	/*
	 * AFBC HDR_PTR must set to the zero offset of the framebuffer.
	 */
	if (afbc_en)
		offset = 0;
	else if (pstate->rotation & DRM_MODE_REFLECT_Y)
		offset += ((src->y2 >> 16) - 1) * fb->pitches[0];
	else
		offset += (src->y1 >> 16) * fb->pitches[0];

	rk_obj = to_rockchip_obj(fb->obj[0]);

	yrgb_mst = rk_obj->dma_addr + offset + fb->offsets[0];
	if (fb->format->is_yuv) {
		int hsub = fb->format->hsub;
		int vsub = fb->format->vsub;

		offset = (src->x1 >> 16) * fb->format->cpp[1] / hsub;
		offset += (src->y1 >> 16) * fb->pitches[1] / vsub;

		if ((pstate->rotation & DRM_MODE_REFLECT_Y) && !afbc_en)
			offset += fb->pitches[1] * ((pstate->src_h >> 16) - 2)  / vsub;

		rk_obj = to_rockchip_obj(fb->obj[0]);
		uv_mst = rk_obj->dma_addr + offset + fb->offsets[1];
	}

	actual_w = drm_rect_width(src) >> 16;
	actual_h = drm_rect_height(src) >> 16;
	dsp_w = drm_rect_width(dest);

	if (dest->x1 + dsp_w > adjusted_mode->hdisplay) {
		drm_err(vop2->drm, "vp%d %s dest->x1[%d] + dsp_w[%d] exceed mode hdisplay[%d]\n",
			  vp->id, win->data->name, dest->x1, dsp_w, adjusted_mode->hdisplay);
		dsp_w = adjusted_mode->hdisplay - dest->x1;
		if (dsp_w < 4)
			dsp_w = 4;
		actual_w = dsp_w * actual_w / drm_rect_width(dest);
	}

	dsp_h = drm_rect_height(dest);

	if (dest->y1 + dsp_h > adjusted_mode->vdisplay) {
		drm_err(vop2->drm, "vp%d %s dest->y1[%d] + dsp_h[%d] exceed mode vdisplay[%d]\n",
			  vp->id, win->data->name, dest->y1, dsp_h, adjusted_mode->vdisplay);
		dsp_h = adjusted_mode->vdisplay - dest->y1;
		if (dsp_h < 4)
			dsp_h = 4;
		actual_h = dsp_h * actual_h / drm_rect_height(dest);
	}

	/*
	 * This is workaround solution for IC design:
	 * esmart can't support scale down when actual_w % 16 == 1.
	 */
	if (!(win->data->feature & WIN_FEATURE_AFBDC)) {
		if (actual_w > dsp_w && (actual_w & 0xf) == 1) {
			drm_err(vop2->drm, "vp%d %s act_w[%d] MODE 16 == 1\n", vp->id, win->data->name, actual_w);
			actual_w -= 1;
		}
	}

	if (afbc_en && actual_w % 4) {
		drm_err(vop2->drm, "vp%d %s actual_w[%d] should align as 4 pixel when enable afbc\n",
			  vp->id, win->data->name, actual_w);
		actual_w = ALIGN_DOWN(actual_w, 4);
	}

	act_info = (actual_h - 1) << 16 | ((actual_w - 1) & 0xffff);
	dsp_info = (dsp_h - 1) << 16 | ((dsp_w - 1) & 0xffff);

	format = vop2_convert_format(fb->format->format);

	spin_lock(&vop2->reg_lock);
	drm_dbg(vop2->drm, "vp%d update %s[%dx%d->%dx%d@%dx%d] fmt[%p4cc_%s] addr[%pad]\n",
		      vp->id, win->data->name, actual_w, actual_h, dsp_w, dsp_h,
		      dest->x1, dest->y1,
		      &fb->format->format,
		      afbc_en ? "AFBC" : "", &yrgb_mst);

	if (afbc_en) {
		uint32_t stride;

		/* the afbc superblock is 16 x 16 */
		afbc_format = vop2_convert_afbc_format(fb->format->format);

		/* Enable color transform for YTR */
		if (fb->modifier & AFBC_FORMAT_MOD_YTR)
			afbc_format |= (1 << 4);

		afbc_tile_num = ALIGN(actual_w, 16) >> 4;

		/*
		 * AFBC pic_vir_width is count by pixel, this is different
		 * with WIN_VIR_STRIDE.
		 */
		stride = (fb->pitches[0] << 3) / bpp;
		if ((stride & 0x3f) && (xmirror || rotate_90 || rotate_270))
			drm_err(vop2->drm, "vp%d %s stride[%d] must align as 64 pixel when enable xmirror/rotate_90/rotate_270[0x%x]\n",
				  vp->id, win->data->name, stride, pstate->rotation);

		rb_swap = vop2_afbc_rb_swap(fb->format->format);
		uv_swap = vop2_afbc_uv_swap(fb->format->format);
		/*
		 * This is a workaround for crazy IC design, Cluster
		 * and Esmart/Smart use different format configuration map:
		 * YUV420_10BIT: 0x10 for Cluster, 0x14 for Esmart/Smart.
		 *
		 * This is one thing we can make the convert simple:
		 * AFBCD decode all the YUV data to YUV444. So we just
		 * set all the yuv 10 bit to YUV444_10.
		 */
		if (fb->format->is_yuv && (bpp == 10))
			format = VOP2_CLUSTER_YUV444_10;

		afbc_half_block_en = vop2_afbc_half_block_enable(pstate);
		transform_offset = vop2_afbc_transform_offset(pstate, afbc_half_block_en);
		if (vop2_cluster_window(win))
			vop2_win_write(win, &win->regs->cluster->afbc_enable, 1);
		vop2_win_write(win, &win->regs->afbc->format, afbc_format);
		vop2_win_write(win, &win->regs->afbc->rb_swap, rb_swap);
		vop2_win_write(win, &win->regs->afbc->uv_swap, uv_swap);
		vop2_win_write(win, &win->regs->afbc->auto_gating_en, 0);
		vop2_win_write(win, &win->regs->afbc->block_split_en, 0);
		vop2_win_write(win, &win->regs->afbc->half_block_en, afbc_half_block_en);
		vop2_win_write(win, &win->regs->afbc->hdr_ptr, yrgb_mst);
		vop2_win_write(win, &win->regs->afbc->pic_size, act_info);
		vop2_win_write(win, &win->regs->afbc->transform_offset, transform_offset);
		vop2_win_write(win, &win->regs->afbc->pic_offset, ((src->x1 >> 16) | src->y1));
		vop2_win_write(win, &win->regs->afbc->dsp_offset, (dest->x1 | (dest->y1 << 16)));
		vop2_win_write(win, &win->regs->afbc->pic_vir_width, stride);
		vop2_win_write(win, &win->regs->afbc->tile_num, afbc_tile_num);
		vop2_win_write(win, &win->regs->afbc->xmirror, xmirror);
		vop2_win_write(win, &win->regs->afbc->ymirror, ymirror);
		vop2_win_write(win, &win->regs->afbc->rotate_270, rotate_270);
		vop2_win_write(win, &win->regs->afbc->rotate_90, rotate_90);
	} else {
		if (win->regs->afbc)
			vop2_win_write(win, &win->regs->afbc->enable, 0);
		vop2_win_write(win, &win->regs->ymirror, ymirror);
		vop2_win_write(win, &win->regs->xmirror, xmirror);
		vop2_win_write(win, &win->regs->yrgb_vir, DIV_ROUND_UP(fb->pitches[0], 4));
	}

	if (rotate_90 || rotate_270) {
		act_info = swahw32(act_info);
		actual_w = drm_rect_height(src) >> 16;
		actual_h = drm_rect_width(src) >> 16;
	}

	vop2_win_write(win, &win->regs->format, format);
	vop2_win_write(win, &win->regs->yrgb_mst, yrgb_mst);

	rb_swap = vop2_win_rb_swap(fb->format->format);
	uv_swap = vop2_win_uv_swap(fb->format->format);
	vop2_win_write(win, &win->regs->rb_swap, rb_swap);
	vop2_win_write(win, &win->regs->uv_swap, uv_swap);

	if (fb->format->is_yuv) {
		vop2_win_write(win, &win->regs->uv_vir, DIV_ROUND_UP(fb->pitches[1], 4));
		vop2_win_write(win, &win->regs->uv_mst, uv_mst);
	}

	vop2_setup_scale(vop2, win, actual_w, actual_h, dsp_w, dsp_h, fb->format->format);
	vop2_plane_setup_color_key(plane, 0);
	vop2_win_write(win, &win->regs->act_info, act_info);
	vop2_win_write(win, &win->regs->dsp_info, dsp_info);
	vop2_win_write(win, &win->regs->dsp_st, dest->y1 << 16 | (dest->x1 & 0xffff));

	vop2_setup_csc_mode(vp, win, pstate);

	dither_up = vop2_win_dither_up(fb->format->format);
	vop2_win_write(win, &win->regs->dither_up, dither_up);

	vop2_win_write(win, &win->regs->enable, 1);

	if (vop2_cluster_window(win)) {
		int lb_mode = vop2_get_cluster_lb_mode(win, pstate);

		vop2_win_write(win, &win->regs->cluster->lb_mode, lb_mode);
		vop2_win_write(win, &win->regs->cluster->enable, 1);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_BT1120 ||
	    vcstate->output_if & VOP_OUTPUT_IF_BT656)
		vop2_win_write(win, &win->regs->yuv_clip, 1);

	spin_unlock(&vop2->reg_lock);
}

static const struct drm_plane_helper_funcs vop2_plane_helper_funcs = {
	.atomic_check = vop2_plane_atomic_check,
	.atomic_update = vop2_plane_atomic_update,
	.atomic_disable = vop2_plane_atomic_disable,
};

static const struct drm_plane_funcs vop2_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.format_mod_supported = rockchip_vop2_mod_supported,
};

static int vop2_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	unsigned long flags;

	if (WARN_ON(!vop2->enable_count))
		return -EPERM;

	spin_lock_irqsave(&vop2->irq_lock, flags);

	vop2_intr_set_type(vop2, intr, &intr->clear, FS_FIELD_INTR, 1);
	vop2_intr_set_type(vop2, intr, &intr->enable, FS_FIELD_INTR, 1);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);

	return 0;
}

static void vop2_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	unsigned long flags;

	if (WARN_ON(!vop2->enable_count))
		return;

	spin_lock_irqsave(&vop2->irq_lock, flags);

	vop2_intr_set_type(vop2, intr, &intr->enable, FS_FIELD_INTR, 0);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);
}

static bool vop2_crtc_mode_fixup(struct drm_crtc *crtc,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adj_mode)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);

	drm_mode_set_crtcinfo(adj_mode, CRTC_INTERLACE_HALVE_V | CRTC_STEREO_DOUBLE);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		adj_mode->crtc_clock *= 2;

	adj_mode->crtc_clock = DIV_ROUND_UP(clk_round_rate(vp->dclk,
							   adj_mode->crtc_clock * 1000), 1000);

	return true;
}

static void vop2_dither_setup(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	switch (vcstate->bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		vop2_write(vop2, &vp->regs->dither_down_en, 1);
		vop2_write(vop2, &vp->regs->dither_down_mode, RGB888_TO_RGB565);
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
	case MEDIA_BUS_FMT_RGB666_1X7X3_JEIDA:
		vop2_write(vop2, &vp->regs->dither_down_en, 1);
		vop2_write(vop2, &vp->regs->dither_down_mode, RGB888_TO_RGB666);
		break;
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		vop2_write(vop2, &vp->regs->dither_down_en, 0);
		vop2_write(vop2, &vp->regs->pre_dither_down_en, 1);
		break;
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		vop2_write(vop2, &vp->regs->dither_down_en, 0);
		vop2_write(vop2, &vp->regs->pre_dither_down_en, 0);
		break;
	case MEDIA_BUS_FMT_SRGB888_3X8:
	case MEDIA_BUS_FMT_SRGB888_DUMMY_4X8:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
	default:
		vop2_write(vop2, &vp->regs->dither_down_en, 0);
		vop2_write(vop2, &vp->regs->pre_dither_down_en, 0);
		break;
	}

	vop2_write(vop2, &vp->regs->pre_dither_down_en,
		       vcstate->output_mode == ROCKCHIP_OUT_MODE_AAAA ? 0 : 1);
	vop2_write(vop2, &vp->regs->dither_down_sel, DITHER_DOWN_ALLEGRO);
}

static void vop2_post_config(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate =
			to_rockchip_crtc_state(crtc->state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	uint16_t vtotal = mode->crtc_vtotal;
	uint16_t hdisplay = mode->crtc_hdisplay;
	uint16_t hact_st = mode->crtc_htotal - mode->crtc_hsync_start;
	uint16_t vdisplay = mode->crtc_vdisplay;
	uint16_t vact_st = mode->crtc_vtotal - mode->crtc_vsync_start;
	uint32_t left_margin = 100, right_margin = 100, top_margin = 100, bottom_margin = 100;
	uint16_t hsize = hdisplay * (left_margin + right_margin) / 200;
	uint16_t vsize = vdisplay * (top_margin + bottom_margin) / 200;
	uint16_t hact_end, vact_end;
	uint32_t val;

	vsize = rounddown(vsize, 2);
	hsize = rounddown(hsize, 2);
	hact_st += hdisplay * (100 - left_margin) / 200;
	hact_end = hact_st + hsize;
	val = hact_st << 16;
	val |= hact_end;
	vop2_write(vop2, &vp->regs->hpost_st_end, val);
	vact_st += vdisplay * (100 - top_margin) / 200;
	vact_end = vact_st + vsize;
	val = vact_st << 16;
	val |= vact_end;
	vop2_write(vop2, &vp->regs->vpost_st_end, val);
	val = scl_cal_scale2(vdisplay, vsize) << 16;
	val |= scl_cal_scale2(hdisplay, hsize);
	vop2_write(vop2, &vp->regs->post_scl_factor, val);

#define POST_HORIZONTAL_SCALEDOWN_EN(x)		((x) << 0)
#define POST_VERTICAL_SCALEDOWN_EN(x)		((x) << 1)
	vop2_write(vop2, &vp->regs->post_scl_ctrl,
		       POST_HORIZONTAL_SCALEDOWN_EN(hdisplay != hsize) |
		       POST_VERTICAL_SCALEDOWN_EN(vdisplay != vsize));
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		uint16_t vact_st_f1 = vtotal + vact_st + 1;
		uint16_t vact_end_f1 = vact_st_f1 + vsize;

		val = vact_st_f1 << 16 | vact_end_f1;
		vop2_write(vop2, &vp->regs->vpost_st_end_f1, val);
	}
	vop2_write(vop2, &vp->regs->post_dsp_out_r2y,
		       is_yuv_output(vcstate->bus_format));
}

/*
 * if adjusted mode update, return true, else return false
 */
static bool vop2_crtc_mode_update(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	struct drm_display_mode *mode = &vp->crtc.state->adjusted_mode;
	uint16_t hsync_len = mode->crtc_hsync_end - mode->crtc_hsync_start;
	uint16_t hdisplay = mode->crtc_hdisplay;
	uint16_t htotal = mode->crtc_htotal;
	uint16_t hact_st = mode->crtc_htotal - mode->crtc_hsync_start;
	uint16_t hact_end = hact_st + hdisplay;
	uint16_t vdisplay = mode->crtc_vdisplay;
	uint16_t vtotal = mode->crtc_vtotal;
	uint16_t vsync_len = mode->crtc_vsync_end - mode->crtc_vsync_start;
	uint16_t vact_st = mode->crtc_vtotal - mode->crtc_vsync_start;
	uint16_t vact_end = vact_st + vdisplay;
	uint32_t htotal_sync = htotal << 16 | hsync_len;
	uint32_t hactive_st_end = hact_st << 16 | hact_end;
	uint32_t vtotal_sync = vtotal << 16 | vsync_len;
	uint32_t vactive_st_end = vact_st << 16 | vact_end;
	uint32_t crtc_clock = mode->crtc_clock * 100;

	if (htotal_sync != vop2_read(vop2, &vp->regs->htotal_pw) ||
	    hactive_st_end != vop2_read(vop2, &vp->regs->hact_st_end) ||
	    vtotal_sync != vop2_read(vop2, &vp->regs->vtotal_pw) ||
	    vactive_st_end != vop2_read(vop2, &vp->regs->vact_st_end) ||
	    crtc_clock != clk_get_rate(vp->dclk))
		return true;

	return false;
}

static void vop2_crtc_atomic_enable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct drm_display_mode *mode = &crtc->state->mode;
	uint16_t hsync_len = mode->crtc_hsync_end - mode->crtc_hsync_start;
	uint16_t hdisplay = mode->crtc_hdisplay;
	uint16_t htotal = mode->crtc_htotal;
	uint16_t hact_st = mode->crtc_htotal - mode->crtc_hsync_start;
	uint16_t hact_end = hact_st + hdisplay;
	uint16_t vdisplay = mode->crtc_vdisplay;
	uint16_t vtotal = mode->crtc_vtotal;
	uint16_t vsync_len = mode->crtc_vsync_end - mode->crtc_vsync_start;
	uint16_t vact_st = mode->crtc_vtotal - mode->crtc_vsync_start;
	uint16_t vact_end = vact_st + vdisplay;
	bool interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);
	uint8_t out_mode;
	bool dclk_inv, yc_swap;
	int act_end;
	uint32_t val;
	int ret;

	vop2_lock(vop2);

	drm_info(vop2->drm, "Update mode to %dx%d%s%d, type: %d for vp%d, output 0x%08x %s %s\n",
		     hdisplay, vdisplay, interlaced ? "i" : "p",
		     drm_mode_vrefresh(mode), vcstate->output_type, vp->id,
		     vcstate->output_if,
		     vcstate->output_if & VOP_OUTPUT_IF_MIPI0 ? "MIPI0" : "",
		     vcstate->output_if & VOP_OUTPUT_IF_HDMI0 ? "HDMI0" : ""
		);

	ret = clk_prepare_enable(vp->dclk);
	if (ret < 0) {
		drm_err(vop2->drm, "failed to enable dclk for video port%d - %d\n",
			      vp->id, ret);
		return;
	}

	if (!vop2->enable_count)
		vop2_enable(vop2);

	vop2->enable_count++;

	vop2_debug_irq_enable(vp);

	if (vop2_crtc_mode_update(vp))
		vop2_disable_all_planes_for_crtc(vp);

	dclk_inv = (vcstate->bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE) ? 1 : 0;
	val = (mode->flags & DRM_MODE_FLAG_NHSYNC) ? 0 : BIT(HSYNC_POSITIVE);
	val |= (mode->flags & DRM_MODE_FLAG_NVSYNC) ? 0 : BIT(VSYNC_POSITIVE);

	if (vcstate->output_if & VOP_OUTPUT_IF_RGB) {
		vop2_write(vop2, &vop2->data->ctrl->rgb_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->rgb_mux, vp_data->id);
		vop2_grf_writel(vop2, vop2->data->grf_ctrl->grf_dclk_inv, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_BT1120) {
		vop2_write(vop2, &vop2->data->ctrl->rgb_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->bt1120_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->rgb_mux, vp_data->id);
		vop2_grf_writel(vop2, vop2->data->grf_ctrl->grf_bt1120_clk_inv, !dclk_inv);
		yc_swap = vop2_output_yc_swap(vcstate->bus_format);
		vop2_write(vop2, &vop2->data->ctrl->bt1120_yc_swap, yc_swap);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_BT656) {
		vop2_write(vop2, &vop2->data->ctrl->bt656_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->rgb_mux, vp_data->id);
		vop2_grf_writel(vop2, vop2->data->grf_ctrl->grf_bt656_clk_inv, !dclk_inv);
		yc_swap = vop2_output_yc_swap(vcstate->bus_format);
		vop2_write(vop2, &vop2->data->ctrl->bt656_yc_swap, yc_swap);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_LVDS0) {
		vop2_write(vop2, &vop2->data->ctrl->lvds0_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->lvds0_mux, vp_data->id);
		vop2_write(vop2, &vop2->data->ctrl->lvds_pin_pol, val);
		vop2_write(vop2, &vop2->data->ctrl->lvds_dclk_pol, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_LVDS1) {
		vop2_write(vop2, &vop2->data->ctrl->lvds1_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->lvds1_mux, vp_data->id);
		vop2_write(vop2, &vop2->data->ctrl->lvds_pin_pol, val);
		vop2_write(vop2, &vop2->data->ctrl->lvds_dclk_pol, dclk_inv);
	}

	if (vcstate->output_flags & (ROCKCHIP_OUTPUT_DUAL_CHANNEL_ODD_EVEN_MODE |
	    ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE)) {
		vop2_write(vop2, &vop2->data->ctrl->lvds_dual_en, 1);
		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE)
			vop2_write(vop2, &vop2->data->ctrl->lvds_dual_mode, 1);
		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DATA_SWAP)
			vop2_write(vop2, &vop2->data->ctrl->lvds_dual_channel_swap, 1);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_MIPI0) {
		vop2_write(vop2, &vop2->data->ctrl->mipi0_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->mipi0_mux, vp_data->id);
		vop2_write(vop2, &vop2->data->ctrl->mipi_pin_pol, val);
		vop2_write(vop2, &vop2->data->ctrl->mipi_dclk_pol, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_MIPI1) {
		vop2_write(vop2, &vop2->data->ctrl->mipi1_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->mipi1_mux, vp_data->id);
		vop2_write(vop2, &vop2->data->ctrl->mipi_pin_pol, val);
		vop2_write(vop2, &vop2->data->ctrl->mipi_dclk_pol, dclk_inv);
	}

	if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE) {
		vop2_write(vop2, &vp->regs->mipi_dual_en, 1);
		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DATA_SWAP)
			vop2_write(vop2, &vp->regs->mipi_dual_channel_swap, 1);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_eDP0) {
		vop2_write(vop2, &vop2->data->ctrl->edp0_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->edp0_mux, vp_data->id);
		vop2_write(vop2, &vop2->data->ctrl->edp_pin_pol, val);
		vop2_write(vop2, &vop2->data->ctrl->edp_dclk_pol, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_eDP1) {
		vop2_write(vop2, &vop2->data->ctrl->edp1_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->edp1_mux, vp_data->id);
		vop2_write(vop2, &vop2->data->ctrl->edp_pin_pol, val);
		vop2_write(vop2, &vop2->data->ctrl->edp_dclk_pol, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_DP0) {
		vop2_write(vop2, &vop2->data->ctrl->dp0_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->dp0_mux, vp_data->id);
		vop2_write(vop2, &vop2->data->ctrl->dp_dclk_pol, 0);
		vop2_write(vop2, &vop2->data->ctrl->dp_pin_pol, val);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_DP1) {
		vop2_write(vop2, &vop2->data->ctrl->dp1_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->dp1_mux, vp_data->id);
		vop2_write(vop2, &vop2->data->ctrl->dp_dclk_pol, 0);
		vop2_write(vop2, &vop2->data->ctrl->dp_pin_pol, val);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_HDMI0) {
		vop2_write(vop2, &vop2->data->ctrl->hdmi0_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->hdmi0_mux, vp_data->id);
		vop2_write(vop2, &vop2->data->ctrl->hdmi_pin_pol, val);
		vop2_write(vop2, &vop2->data->ctrl->hdmi_dclk_pol, 1);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_HDMI1) {
		vop2_write(vop2, &vop2->data->ctrl->hdmi1_en, 1);
		vop2_write(vop2, &vop2->data->ctrl->hdmi1_mux, vp_data->id);
		vop2_write(vop2, &vop2->data->ctrl->hdmi_pin_pol, val);
		vop2_write(vop2, &vop2->data->ctrl->hdmi_dclk_pol, 1);
	}

	if ((vcstate->output_mode == ROCKCHIP_OUT_MODE_AAAA &&
	     !(vp_data->feature & VOP_FEATURE_OUTPUT_10BIT)) ||
	    vcstate->output_if & VOP_OUTPUT_IF_BT656)
		out_mode = ROCKCHIP_OUT_MODE_P888;
	else
		out_mode = vcstate->output_mode;

	vop2_write(vop2, &vp->regs->out_mode, out_mode);

	if (vop2_output_uv_swap(vcstate->bus_format, vcstate->output_mode))
		vop2_write(vop2, &vp->regs->dsp_data_swap, DSP_RB_SWAP);
	else
		vop2_write(vop2, &vp->regs->dsp_data_swap, 0);

	vop2_dither_setup(crtc);

	vop2_write(vop2, &vp->regs->htotal_pw, (htotal << 16) | hsync_len);
	val = hact_st << 16;
	val |= hact_end;
	vop2_write(vop2, &vp->regs->hact_st_end, val);

	val = vact_st << 16;
	val |= vact_end;
	vop2_write(vop2, &vp->regs->vact_st_end, val);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		uint16_t vact_st_f1 = vtotal + vact_st + 1;
		uint16_t vact_end_f1 = vact_st_f1 + vdisplay;

		val = vact_st_f1 << 16 | vact_end_f1;
		vop2_write(vop2, &vp->regs->vact_st_end_f1, val);

		val = vtotal << 16 | (vtotal + vsync_len);
		vop2_write(vop2, &vp->regs->vs_st_end_f1, val);
		vop2_write(vop2, &vp->regs->dsp_interlace, 1);
		vop2_write(vop2, &vp->regs->dsp_filed_pol, 1);
		vop2_write(vop2, &vp->regs->p2i_en, 1);
		vtotal += vtotal + 1;
		act_end = vact_end_f1;
	} else {
		vop2_write(vop2, &vp->regs->dsp_interlace, 0);
		vop2_write(vop2, &vp->regs->dsp_filed_pol, 0);
		vop2_write(vop2, &vp->regs->p2i_en, 0);
		act_end = vact_end;
	}

	vop2_write(vop2, &intr->line_flag_num[0], act_end);
	vop2_write(vop2, &intr->line_flag_num[1],
		     act_end - us_to_vertical_line(mode, 0));

	vop2_write(vop2, &vp->regs->vtotal_pw, vtotal << 16 | vsync_len);

	vop2_write(vop2, &vp->regs->core_dclk_div, !!(mode->flags & DRM_MODE_FLAG_DBLCLK));
	if (vcstate->output_mode == ROCKCHIP_OUT_MODE_YUV420) {
		vop2_write(vop2, &vp->regs->dclk_div2, 1);
		vop2_write(vop2, &vp->regs->dclk_div2_phase_lock, 1);
	} else {
		vop2_write(vop2, &vp->regs->dclk_div2, 0);
		vop2_write(vop2, &vp->regs->dclk_div2_phase_lock, 0);
	}

	clk_set_rate(vp->dclk, mode->crtc_clock * 1000);

	vop2_post_config(crtc);

	vop2_cfg_done(vp);

	/*
	 * when clear standby bits, it will take effect immediately,
	 * This means the vp will start scan out immediately with
	 * the timing it been configured before.
	 * So we must make sure release standby after the display
	 * timing is correctly configured.
	 * This is important when switch resolution, such as
	 * 4K-->720P:
	 * if we release standby before 720P timing is configured,
	 * the VP will start scan out immediately with 4K timing,
	 * when we switch dclk to 74.25MHZ, VP timing is still 4K,
	 * so VP scan out with 4K timing at 74.25MHZ dclk, this is
	 * very slow, than this will trigger vblank timeout.
	 *
	 */
	vop2_write(vop2, &vp->regs->standby, 0);

	drm_crtc_vblank_on(crtc);

	vop2_unlock(vop2);
}

static int vop2_zpos_cmp(const void *a, const void *b)
{
	struct vop2_zpos *pa = (struct vop2_zpos *)a;
	struct vop2_zpos *pb = (struct vop2_zpos *)b;

	if (pa->zpos != pb->zpos)
		return pa->zpos - pb->zpos;
	else
		return pa->plane->base.id - pb->plane->base.id;
}

static int vop2_crtc_atomic_check(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane *plane;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);
		struct vop2_win *main_win;

		if (!(win->data->feature & WIN_FEATURE_CLUSTER_SUB))
			continue;

		main_win = vop2_find_win_by_phys_id(vop2, win->data->phys_id);

		if (abs(main_win->base.state->zpos - win->base.state->zpos) != 1) {
			drm_err(vop2->drm, "vp%d Cluster%d win0[zpos:%d] must next to win1[zpos:%d]\n",
				  vp->id, main_win->data->phys_id,
				  main_win->base.state->zpos, win->base.state->zpos);
			return -EINVAL;
		}
	}

	return 0;
}

static bool is_opaque(uint16_t alpha)
{
	return (alpha >> 8) == 0xff;
}

static void vop2_parse_alpha(struct vop2_alpha_config *alpha_config,
			     struct vop2_alpha *alpha)
{
	int src_glb_alpha_en = is_opaque(alpha_config->src_glb_alpha_value) ? 0 : 1;
	int dst_glb_alpha_en = is_opaque(alpha_config->dst_glb_alpha_value) ? 0 : 1;
	int src_color_mode = alpha_config->src_premulti_en ? ALPHA_SRC_PRE_MUL : ALPHA_SRC_NO_PRE_MUL;
	int dst_color_mode = alpha_config->dst_premulti_en ? ALPHA_SRC_PRE_MUL : ALPHA_SRC_NO_PRE_MUL;

	alpha->src_color_ctrl.val = 0;
	alpha->dst_color_ctrl.val = 0;
	alpha->src_alpha_ctrl.val = 0;
	alpha->dst_alpha_ctrl.val = 0;

	if (!alpha_config->src_pixel_alpha_en)
		alpha->src_color_ctrl.bits.blend_mode = ALPHA_GLOBAL;
	else if (alpha_config->src_pixel_alpha_en && !src_glb_alpha_en)
		alpha->src_color_ctrl.bits.blend_mode = ALPHA_PER_PIX;
	else
		alpha->src_color_ctrl.bits.blend_mode = ALPHA_PER_PIX_GLOBAL;

	alpha->src_color_ctrl.bits.alpha_en = 1;

	if (alpha->src_color_ctrl.bits.blend_mode == ALPHA_GLOBAL) {
		alpha->src_color_ctrl.bits.color_mode = src_color_mode;
		alpha->src_color_ctrl.bits.factor_mode = SRC_FAC_ALPHA_SRC_GLOBAL;
	} else if (alpha->src_color_ctrl.bits.blend_mode == ALPHA_PER_PIX) {
		alpha->src_color_ctrl.bits.color_mode = src_color_mode;
		alpha->src_color_ctrl.bits.factor_mode = SRC_FAC_ALPHA_ONE;
	} else {
		alpha->src_color_ctrl.bits.color_mode = ALPHA_SRC_PRE_MUL;
		alpha->src_color_ctrl.bits.factor_mode = SRC_FAC_ALPHA_SRC_GLOBAL;
	}
	alpha->src_color_ctrl.bits.glb_alpha = alpha_config->src_glb_alpha_value >> 8;
	alpha->src_color_ctrl.bits.alpha_mode = ALPHA_STRAIGHT;
	alpha->src_color_ctrl.bits.alpha_cal_mode = ALPHA_SATURATION;

	alpha->dst_color_ctrl.bits.alpha_mode = ALPHA_STRAIGHT;
	alpha->dst_color_ctrl.bits.alpha_cal_mode = ALPHA_SATURATION;
	alpha->dst_color_ctrl.bits.blend_mode = ALPHA_GLOBAL;
	alpha->dst_color_ctrl.bits.glb_alpha = alpha_config->dst_glb_alpha_value >> 8;
	alpha->dst_color_ctrl.bits.color_mode = dst_color_mode;
	alpha->dst_color_ctrl.bits.factor_mode = ALPHA_SRC_INVERSE;

	alpha->src_alpha_ctrl.bits.alpha_mode = ALPHA_STRAIGHT;
	alpha->src_alpha_ctrl.bits.blend_mode = alpha->src_color_ctrl.bits.blend_mode;
	alpha->src_alpha_ctrl.bits.alpha_cal_mode = ALPHA_SATURATION;
	alpha->src_alpha_ctrl.bits.factor_mode = ALPHA_ONE;

	alpha->dst_alpha_ctrl.bits.alpha_mode = ALPHA_STRAIGHT;
	if (alpha_config->dst_pixel_alpha_en && !dst_glb_alpha_en)
		alpha->dst_alpha_ctrl.bits.blend_mode = ALPHA_PER_PIX;
	else
		alpha->dst_alpha_ctrl.bits.blend_mode = ALPHA_PER_PIX_GLOBAL;
	alpha->dst_alpha_ctrl.bits.alpha_cal_mode = ALPHA_NO_SATURATION;
	alpha->dst_alpha_ctrl.bits.factor_mode = ALPHA_SRC_INVERSE;
}

static int vop2_find_start_mixer_id_for_vp(struct vop2 *vop2, uint8_t port_id)
{
	struct vop2_video_port *vp;
	int used_layer = 0;
	int i;

	for (i = 0; i < port_id; i++) {
		vp = &vop2->vps[i];
		used_layer += hweight32(vp->win_mask);
	}

	return used_layer;
}

/*
 * src: top layer
 * dst: bottom layer.
 * Cluster mixer default use win1 as top layer
 */
static void vop2_setup_cluster_alpha(struct vop2 *vop2, struct vop2_cluster *cluster)
{
	uint32_t src_color_ctrl_offset = vop2->data->ctrl->cluster0_src_color_ctrl.offset;
	uint32_t dst_color_ctrl_offset = vop2->data->ctrl->cluster0_dst_color_ctrl.offset;
	uint32_t src_alpha_ctrl_offset = vop2->data->ctrl->cluster0_src_alpha_ctrl.offset;
	uint32_t dst_alpha_ctrl_offset = vop2->data->ctrl->cluster0_dst_alpha_ctrl.offset;
	uint32_t offset = (cluster->main->data->phys_id * 0x10);
	struct vop2_alpha_config alpha_config;
	struct vop2_alpha alpha;
	struct vop2_win *main_win = cluster->main;
	struct vop2_win *sub_win = cluster->sub;
	struct drm_plane *main_plane;
	struct drm_plane_state *top_win_pstate;
	struct drm_plane_state *bottom_win_pstate;
	bool src_pixel_alpha_en = false;
	uint16_t src_glb_alpha_val, dst_glb_alpha_val;
	bool premulti_en = false;
	bool swap = false;

	if (!sub_win) {
		/* At one win mode, win0 is dst/bottom win, and win1 is a all zero src/top win */
		top_win_pstate = NULL;
		bottom_win_pstate = main_win->base.state;
		src_glb_alpha_val = 0;
		dst_glb_alpha_val = main_win->base.state->alpha;
	} else {
		if (main_win->base.state->zpos > sub_win->base.state->zpos) {
			swap = 1;
			top_win_pstate = main_plane->state;
			bottom_win_pstate = sub_win->base.state;
			src_glb_alpha_val = main_win->base.state->alpha;
			dst_glb_alpha_val = sub_win->base.state->alpha;
		} else {
			swap = 0;
			top_win_pstate = sub_win->base.state;
			bottom_win_pstate = main_plane->state;
			src_glb_alpha_val = sub_win->base.state->alpha;
			dst_glb_alpha_val = main_win->base.state->alpha;
		}

		if (!top_win_pstate->fb)
			return;

		if (top_win_pstate->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
			premulti_en = true;
		else
			premulti_en = false;

		src_pixel_alpha_en = is_alpha_support(top_win_pstate->fb->format->format);
	}

	if (!bottom_win_pstate->fb)
		return;

	alpha_config.src_premulti_en = premulti_en;
	alpha_config.dst_premulti_en = false;
	alpha_config.src_pixel_alpha_en = src_pixel_alpha_en;
	alpha_config.dst_pixel_alpha_en = true; /* alpha value need transfer to next mix */
	alpha_config.src_glb_alpha_value = src_glb_alpha_val;
	alpha_config.dst_glb_alpha_value = dst_glb_alpha_val;
	vop2_parse_alpha(&alpha_config, &alpha);

	alpha.src_color_ctrl.bits.src_dst_swap = swap;
	vop2_writel(vop2, src_color_ctrl_offset + offset, alpha.src_color_ctrl.val);
	vop2_writel(vop2, dst_color_ctrl_offset + offset, alpha.dst_color_ctrl.val);
	vop2_writel(vop2, src_alpha_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
	vop2_writel(vop2, dst_alpha_ctrl_offset + offset, alpha.dst_alpha_ctrl.val);
}

static void vop2_setup_alpha(struct vop2_video_port *vp,
			     const struct vop2_zpos *vop2_zpos, int nr_layers)
{
	struct vop2 *vop2 = vp->vop2;
	uint32_t src_color_ctrl_offset = vop2->data->ctrl->src_color_ctrl.offset;
	uint32_t dst_color_ctrl_offset = vop2->data->ctrl->dst_color_ctrl.offset;
	uint32_t src_alpha_ctrl_offset = vop2->data->ctrl->src_alpha_ctrl.offset;
	uint32_t dst_alpha_ctrl_offset = vop2->data->ctrl->dst_alpha_ctrl.offset;
	const struct vop2_zpos *zpos;
	struct drm_framebuffer *fb;
	struct vop2_alpha_config alpha_config;
	struct vop2_alpha alpha;
	struct vop2_win *win;
	struct drm_plane *plane;
	int pixel_alpha_en;
	int premulti_en;
	int mixer_id;
	uint32_t offset;
	int i;
	bool bottom_layer_alpha_en = false;
	uint32_t dst_global_alpha = 0xffff;

	drm_atomic_crtc_for_each_plane(plane, &vp->crtc) {
		struct vop2_win *win = to_vop2_win(plane);

		if (plane->state->zpos == 0 && !is_opaque(plane->state->alpha) &&
		    !vop2_cluster_window(win)) {
			/*
			 * If bottom layer have global alpha effect [except cluster layer,
			 * because cluster have deal with bottom layer global alpha value
			 * at cluster mix], bottom layer mix need deal with global alpha.
			 */
			bottom_layer_alpha_en = true;
			dst_global_alpha = plane->state->alpha;
			break;
		}
	}

	mixer_id = vop2_find_start_mixer_id_for_vp(vop2, vp->id);
	alpha_config.dst_pixel_alpha_en = true; /* alpha value need transfer to next mix */
	for (i = 1; i < nr_layers; i++) {
		zpos = &vop2_zpos[i];
		win = zpos->win;
		plane = &win->base;
		fb = plane->state->fb;
		if (plane->state->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
			premulti_en = 1;
		else
			premulti_en = 0;
		pixel_alpha_en = is_alpha_support(fb->format->format);

		alpha_config.src_premulti_en = premulti_en;
		if (bottom_layer_alpha_en && i == 1) {/* Cd = Cs + (1 - As) * Cd * Agd */
			alpha_config.dst_premulti_en = false;
			alpha_config.src_pixel_alpha_en = pixel_alpha_en;
			alpha_config.src_glb_alpha_value =  plane->state->alpha;
			alpha_config.dst_glb_alpha_value = dst_global_alpha;
		} else if (vop2_cluster_window(win)) {/* Mix output data only have pixel alpha */
			alpha_config.dst_premulti_en = true;
			alpha_config.src_pixel_alpha_en = true;
			alpha_config.src_glb_alpha_value = 0xffff;
			alpha_config.dst_glb_alpha_value = 0xffff;
		} else {/* Cd = Cs + (1 - As) * Cd */
			alpha_config.dst_premulti_en = true;
			alpha_config.src_pixel_alpha_en = pixel_alpha_en;
			alpha_config.src_glb_alpha_value =  plane->state->alpha;
			alpha_config.dst_glb_alpha_value = 0xffff;
		}
		vop2_parse_alpha(&alpha_config, &alpha);

		offset = (mixer_id + i - 1) * 0x10;
		vop2_writel(vop2, src_color_ctrl_offset + offset, alpha.src_color_ctrl.val);
		vop2_writel(vop2, dst_color_ctrl_offset + offset, alpha.dst_color_ctrl.val);
		vop2_writel(vop2, src_alpha_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
		vop2_writel(vop2, dst_alpha_ctrl_offset + offset, alpha.dst_alpha_ctrl.val);

		if (i == 1) {
			if (bottom_layer_alpha_en) {
				/* Transfer pixel alpha to hdr mix */
				alpha_config.src_premulti_en = premulti_en;
				alpha_config.dst_premulti_en = true;
				alpha_config.src_pixel_alpha_en = true;
				alpha_config.src_glb_alpha_value = 0xffff;
				alpha_config.dst_glb_alpha_value = 0xffff;
				vop2_parse_alpha(&alpha_config, &alpha);

				vop2_write(vop2, &vp->regs->hdr_src_color_ctrl,
					       alpha.src_color_ctrl.val);
				vop2_write(vop2, &vp->regs->hdr_dst_color_ctrl,
					       alpha.dst_color_ctrl.val);
				vop2_write(vop2, &vp->regs->hdr_src_alpha_ctrl,
					       alpha.src_alpha_ctrl.val);
				vop2_write(vop2, &vp->regs->hdr_dst_alpha_ctrl,
					       alpha.dst_alpha_ctrl.val);
			} else {
				vop2_write(vop2, &vp->regs->hdr_src_color_ctrl, 0);
			}
		}
	}

	/* Transfer pixel alpha value to next mix */
	alpha_config.src_premulti_en = true;
	alpha_config.dst_premulti_en = true;
	alpha_config.src_pixel_alpha_en = false;
	alpha_config.src_glb_alpha_value = 0xffff;
	alpha_config.dst_glb_alpha_value = 0xffff;
	vop2_parse_alpha(&alpha_config, &alpha);

	for (; i < hweight32(vp->win_mask); i++) {
		offset = (mixer_id + i - 1) * 0x10;

		vop2_writel(vop2, src_color_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
		vop2_writel(vop2, dst_color_ctrl_offset + offset, alpha.dst_color_ctrl.val);
		vop2_writel(vop2, src_alpha_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
		vop2_writel(vop2, dst_alpha_ctrl_offset + offset, alpha.dst_alpha_ctrl.val);
	}
}

static void vop2_setup_layer_mixer_for_vp(struct vop2_video_port *vp,
					  const struct vop2_zpos *vop2_zpos,
					  int nr_layers)
{
	struct vop2_video_port *vpt;
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_zpos *zpos;
	struct vop2_win *win;
	struct vop2_layer *layer;
	uint16_t port_mux_cfg = 0;
	uint8_t port_mux;
	uint8_t used_layers = 0;
	int i, nlayer;

	for (i = 0; i < vop2_data->nr_vps - 1; i++) {
		vpt = &vop2->vps[i];
		used_layers += hweight32(vpt->win_mask);

		if (used_layers == 0)
			port_mux = 8;
		else
			port_mux = used_layers - 1;

		port_mux_cfg |= port_mux << (vpt->id * 4);

		if (port_mux > vop2_data->nr_mixers)
			vpt->bg_ovl_dly = 0;
		else
			vpt->bg_ovl_dly = (vop2_data->nr_mixers - port_mux) << 1;
	}

	if (vop2_data->nr_vps >= 1)
		port_mux_cfg |= 7 << (4 * (vop2_data->nr_vps - 1));

	nlayer = 0;

	for (i = 0; i < vop2_data->nr_vps; i++) {
		int j;

		vpt = &vop2->vps[i];

		for (j = 0; j < vpt->nr_layers; j++) {
			layer = &vop2->layers[nlayer];
			zpos = &vpt->zpos[j];
			win = zpos->win;

			vop2_write(vop2, &vop2->data->ctrl->win_vp_id[win->data->phys_id], vpt->id);
			vop2_write(vop2, &layer->regs->layer_sel, win->data->layer_sel_id);
			nlayer++;
		}
	}

	for (; nlayer < vop2_data->nr_layers; nlayer++) {
		layer = &vop2->layers[nlayer];

		vop2_write(vop2, &layer->regs->layer_sel, 5);
	}

	vop2_write(vop2, &vop2->data->ctrl->ovl_cfg_done_port, vp->id);
	vop2_write(vop2, &vop2->data->ctrl->ovl_port_mux_cfg_done_imd, 0);

	spin_lock(&vop2->reg_lock);
	if (vop2->port_mux_cfg != port_mux_cfg) {
		vop2_write(vop2, &vop2->data->ctrl->ovl_port_mux_cfg, port_mux_cfg);
		vp->skip_vsync = true;
		vop2_cfg_done(vp);
		vop2->port_mux_cfg = port_mux_cfg;
		vop2_wait_for_port_mux_done(vop2);
	}
	spin_unlock(&vop2->reg_lock);
}

/*
 * HDR window is fixed(not move in the overlay path with port_mux change)
 * and is the slowest window. And the bg is the fastest. So other windows
 * and bg need to add delay number to keep align with the slowest window.
 * The delay number list in the trm is a relative value for port_mux set at
 * last level.
 */
static void vop2_setup_dly_for_vp(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	struct drm_crtc *crtc = &vp->crtc;
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	uint16_t hsync_len = adjusted_mode->crtc_hsync_end - adjusted_mode->crtc_hsync_start;
	uint16_t hdisplay = adjusted_mode->crtc_hdisplay;
	uint32_t bg_dly = vp_data->pre_scan_max_dly[0];
	uint32_t pre_scan_dly;

	bg_dly = vp_data->pre_scan_max_dly[3];
	bg_dly -= vp->bg_ovl_dly;

	pre_scan_dly = bg_dly + (hdisplay >> 1) - 1;
	pre_scan_dly = (pre_scan_dly << 16) | hsync_len;
	vop2_write(vop2, &vp->regs->bg_dly, bg_dly);
	vop2_write(vop2, &vp->regs->pre_scan_htiming, pre_scan_dly);
}

static void vop2_setup_dly_for_window(struct vop2_video_port *vp, const struct vop2_zpos *vop2_zpos,
				      int nr_layers)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_zpos *zpos;
	struct drm_plane *plane;
	struct vop2_win *win;
	uint32_t dly;
	int i = 0;

	for (i = 0; i < nr_layers; i++) {
		zpos = &vop2_zpos[i];
		win = zpos->win;
		plane = &win->base;
		dly = win->data->dly[VOP2_DLY_MODE_DEFAULT];

		if (vop2_cluster_window(win))
			dly |= dly << 8;

		vop2_write(vop2, &vop2->data->ctrl->win_dly[win->data->phys_id], dly);
	}

}
static void vop2_crtc_atomic_begin(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane *plane;
	struct vop2_zpos *vop2_zpos = vp->zpos;
	struct vop2_cluster cluster;
	uint8_t nr_layers = 0;

	/* Process cluster sub windows overlay. */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);
		struct vop2_win *main_win;

		win->two_win_mode = false;
		if (!(win->data->feature & WIN_FEATURE_CLUSTER_SUB))
			continue;
		main_win = vop2_find_win_by_phys_id(vop2, win->data->phys_id);
		cluster.main = main_win;
		cluster.sub = win;
		win->two_win_mode = true;
		main_win->two_win_mode = true;
		vop2_setup_cluster_alpha(vop2, &cluster);
	}

	vp->win_mask = 0;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);

		/*
		 * Sub win of a cluster will be handled by pre overlay module automatically
		 * win in multi area share the same overlay zorder with it's parent.
		 */
		if (win->data->feature & WIN_FEATURE_CLUSTER_SUB)
			continue;
		vp->win_mask |=  BIT(win->data->phys_id);
		win->vp_mask = BIT(vp->id);
		vop2_zpos[nr_layers].win = win;
		vop2_zpos[nr_layers].zpos = plane->state->zpos;
		vop2_zpos[nr_layers].plane = plane;
		nr_layers++;
		drm_dbg(vop2->drm, "%s active zpos:%d for vp%d\n",
			     win->data->name, plane->state->zpos, vp->id);
	}

	vp->nr_layers = nr_layers;

	drm_dbg(vop2->drm, "vp%d: %d windows, active layers %d\n",
		      vp->id, hweight32(vp->win_mask), nr_layers);
	if (nr_layers) {
		sort(vop2_zpos, nr_layers, sizeof(vop2_zpos[0]), vop2_zpos_cmp, NULL);

		vop2_setup_layer_mixer_for_vp(vp, vop2_zpos, nr_layers);
		vop2_setup_alpha(vp, vop2_zpos, nr_layers);
		vop2_setup_dly_for_vp(vp);
		vop2_setup_dly_for_window(vp, vop2_zpos, nr_layers);
	}

	/* The pre alpha overlay of Cluster still need process in one win mode. */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);

		if (!(win->data->feature & WIN_FEATURE_CLUSTER_MAIN))
			continue;
		if (win->two_win_mode)
			continue;
		cluster.main = win;
		cluster.sub = NULL;
		vop2_setup_cluster_alpha(vop2, &cluster);
	}
}

static void vop2_cfg_update(struct drm_crtc *crtc,
			    struct drm_crtc_state *old_crtc_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2 *vop2 = vp->vop2;
	uint32_t val;
	bool yuv_overlay = is_yuv_output(vcstate->bus_format);

	spin_lock(&vop2->reg_lock);

	vop2_write(vop2, &vp->regs->overlay_mode, yuv_overlay);

	if (yuv_overlay)
		val = 0x20010200;
	else
		val = 0;

	vop2_write(vop2, &vp->regs->dsp_background, val);

	vop2_post_config(crtc);

	spin_unlock(&vop2->reg_lock);
}

static void vop2_crtc_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_cstate = drm_atomic_get_old_crtc_state(state, crtc);
	struct drm_atomic_state *old_state = old_cstate->state;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_plane_state *old_pstate;
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane *plane;
	unsigned long flags;
	int i;

	vop2_cfg_update(crtc, old_cstate);

	if (crtc->state->color_mgmt_changed || crtc->state->active_changed) {
		if (crtc->state->gamma_lut)
			vop2_crtc_load_lut(crtc, crtc->state->gamma_lut->data);
	} else {
		vop2_write(vop2, &vp->regs->cubic_lut_update_en, 0);
	}

	spin_lock_irqsave(&vop2->irq_lock, flags);
	vop2_cfg_done(vp);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);

	/*
	 * There is a (rather unlikely) possibility that a vblank interrupt
	 * fired before we set the cfg_done bit. To avoid spuriously
	 * signalling flip completion we need to wait for it to finish.
	 */
	vop2_wait_for_irq_handler(crtc);

	/*
	 * move here is to make sure current fs call function is complete,
	 * so when layer_sel_update is true, we can skip current vblank correctly.
	 */
	vp->layer_sel_update = false;

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		WARN_ON(vp->event);

		vp->event = crtc->state->event;
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	for_each_old_plane_in_state(old_state, plane, old_pstate, i) {
		if (!old_pstate->fb)
			continue;

		if (old_pstate->fb == plane->state->fb)
			continue;

		drm_framebuffer_get(old_pstate->fb);
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		drm_flip_work_queue(&vp->fb_unref_work, old_pstate->fb);
		set_bit(VOP_PENDING_FB_UNREF, &vp->pending);
	}
}

static const struct drm_crtc_helper_funcs vop2_crtc_helper_funcs = {
	.mode_fixup = vop2_crtc_mode_fixup,
	.atomic_check = vop2_crtc_atomic_check,
	.atomic_begin = vop2_crtc_atomic_begin,
	.atomic_flush = vop2_crtc_atomic_flush,
	.atomic_enable = vop2_crtc_atomic_enable,
	.atomic_disable = vop2_crtc_atomic_disable,
};

static void vop2_crtc_reset(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);

	if (crtc->state) {
		__drm_atomic_helper_crtc_destroy_state(crtc->state);
		kfree(vcstate);
	}

	vcstate = kzalloc(sizeof(*vcstate), GFP_KERNEL);
	if (!vcstate)
		return;
	crtc->state = &vcstate->base;
	crtc->state->crtc = crtc;
}

static struct drm_crtc_state *vop2_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate, *old_vcstate;

	old_vcstate = to_rockchip_crtc_state(crtc->state);
	vcstate = kmemdup(old_vcstate, sizeof(*old_vcstate), GFP_KERNEL);
	if (!vcstate)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &vcstate->base);

	return &vcstate->base;
}

static void vop2_crtc_destroy_state(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(&vcstate->base);
	kfree(vcstate);
}

static const struct drm_crtc_funcs vop2_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = drm_crtc_cleanup,
	.reset = vop2_crtc_reset,
	.atomic_duplicate_state = vop2_crtc_duplicate_state,
	.atomic_destroy_state = vop2_crtc_destroy_state,
	.enable_vblank = vop2_crtc_enable_vblank,
	.disable_vblank = vop2_crtc_disable_vblank,
};

static void vop2_fb_unref_worker(struct drm_flip_work *work, void *val)
{
	struct vop2_video_port *vp = container_of(work, struct vop2_video_port, fb_unref_work);
	struct drm_framebuffer *fb = val;

	drm_crtc_vblank_put(&vp->crtc);
	drm_framebuffer_put(fb);
}

static void vop2_handle_vblank(struct vop2 *vop2, struct drm_crtc *crtc)
{
	struct drm_device *drm = vop2->drm;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	if (vp->event) {
		drm_crtc_send_vblank_event(crtc, vp->event);
		drm_crtc_vblank_put(crtc);
		vp->event = NULL;
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);

	if (test_and_clear_bit(VOP_PENDING_FB_UNREF, &vp->pending))
		drm_flip_work_commit(&vp->fb_unref_work, system_unbound_wq);
}

static uint32_t vop2_read_and_clear_active_vp_irqs(struct vop2 *vop2, int vp_id)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data;
	const struct vop_intr *intr;
	int val;

	vp_data = &vop2_data->vp[vp_id];
	intr = vp_data->intr;
	val = vop2_get_intr_type(vop2, intr, &intr->status, VOP2_INTR_MASK);
	if (val)
		vop2_intr_set_type(vop2, intr, &intr->clear, val, 1);
	return val;
}

static irqreturn_t vop2_isr(int irq, void *data)
{
	struct vop2 *vop2 = data;
	struct drm_crtc *crtc;
	struct vop2_video_port *vp;
	const struct vop2_data *vop2_data = vop2->data;
	size_t vp_max = min_t(size_t, vop2_data->nr_vps, ROCKCHIP_MAX_CRTC);
	size_t axi_max = min_t(size_t, vop2_data->nr_axi_intr, VOP2_SYS_AXI_BUS_NUM);
	uint32_t vp_irqs[ROCKCHIP_MAX_CRTC];
	uint32_t axi_irqs[VOP2_SYS_AXI_BUS_NUM];
	uint32_t active_irqs;
	unsigned long flags;
	int ret = IRQ_NONE;
	int i;

	/*
	 * The irq is shared with the iommu. If the runtime-pm state of the
	 * vop2-device is disabled the irq has to be targeted at the iommu.
	 */
	if (!pm_runtime_get_if_in_use(vop2->dev))
		return IRQ_NONE;

	/*
	 * interrupt register has interrupt status, enable and clear bits, we
	 * must hold irq_lock to avoid a race with enable/disable_vblank().
	 */
	spin_lock_irqsave(&vop2->irq_lock, flags);
	for (i = 0; i < vp_max; i++)
		vp_irqs[i] = vop2_read_and_clear_active_vp_irqs(vop2, i);
	for (i = 0; i < axi_max; i++)
		axi_irqs[i] = vop2_read_and_clear_axi_irqs(vop2, i);
	spin_unlock_irqrestore(&vop2->irq_lock, flags);

	for (i = 0; i < vp_max; i++) {
		vp = &vop2->vps[i];
		crtc = &vp->crtc;
		active_irqs = vp_irqs[i];
		if (active_irqs & DSP_HOLD_VALID_INTR) {
			complete(&vp->dsp_hold_completion);
			ret = IRQ_HANDLED;
		}

		if (active_irqs & FS_FIELD_INTR) {
			if (likely(!vp->skip_vsync) || (vp->layer_sel_update == false)) {
				drm_crtc_handle_vblank(crtc);
				vop2_handle_vblank(vop2, crtc);
			}
			ret = IRQ_HANDLED;
		}

		if (active_irqs & POST_BUF_EMPTY_INTR) {
			drm_err_ratelimited(vop2->drm,
					    "POST_BUF_EMPTY irq err at vp%d\n",
					    vp->id);
			ret = IRQ_HANDLED;
		}
	}

	for (i = 0; i < axi_max; i++) {
		active_irqs = axi_irqs[i];

		if (active_irqs & BUS_ERROR_INTR) {
			drm_err_ratelimited(vop2->drm, "BUS_ERROR irq err\n");
			ret = IRQ_HANDLED;
		}
	}

	pm_runtime_put(vop2->dev);

	return ret;
}

static int vop2_plane_init(struct vop2 *vop2, struct vop2_win *win, unsigned long possible_crtcs)
{
	const struct vop2_win_data *win_data = win->data;
	unsigned int blend_caps = BIT(DRM_MODE_BLEND_PIXEL_NONE) | BIT(DRM_MODE_BLEND_PREMULTI) |
				  BIT(DRM_MODE_BLEND_COVERAGE);
	int ret;

	ret = drm_universal_plane_init(vop2->drm, &win->base, possible_crtcs,
				       &vop2_plane_funcs, win_data->formats, win_data->nformats,
				       win_data->format_modifiers, win_data->type, win_data->name);
	if (ret) {
		drm_err(vop2->drm, "failed to initialize plane %d\n", ret);
		return ret;
	}

	drm_plane_helper_add(&win->base, &vop2_plane_helper_funcs);

	if (win->data->supported_rotations)
		drm_plane_create_rotation_property(&win->base, DRM_MODE_ROTATE_0,
						   DRM_MODE_ROTATE_0 | win->data->supported_rotations);
	drm_plane_create_alpha_property(&win->base);
	drm_plane_create_blend_mode_property(&win->base, blend_caps);
	drm_plane_create_zpos_property(&win->base, win->win_id, 0, vop2->registered_num_wins - 1);

	return 0;
}

static int vop2_gamma_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data;
	struct vop2_video_port *vp;
	struct drm_crtc *crtc;
	int i = 0;
	uint32_t lut_len = 0;

	for (i = 0; i < vop2_data->nr_vps; i++) {
		vp = &vop2->vps[i];
		crtc = &vp->crtc;
		if (!crtc->dev)
			continue;
		vp_data = &vop2_data->vp[vp->id];
		lut_len = vp_data->gamma_lut_len;
		vp->gamma_lut_len = vp_data->gamma_lut_len;

		drm_mode_crtc_set_gamma_size(crtc, lut_len);
		drm_crtc_enable_color_mgmt(crtc, 0, false, lut_len);
	}

	return 0;
}

static int vop2_create_crtc(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	struct drm_device *drm = vop2->drm;
	struct device *dev = vop2->dev;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct device_node *port;
	struct vop2_win *win = NULL;
	struct vop2_video_port *vp;
	const struct vop2_video_port_data *vp_data;
	uint32_t possible_crtcs;
	int i, nvp = 0;
	int ret;

	for (i = 0; i < vop2->registered_num_wins; i++) {
		win = &vop2->win[i];

		if (win->data->feature & WIN_FEATURE_CLUSTER_SUB)
			continue;

		if (win->type == DRM_PLANE_TYPE_PRIMARY) {
			if (nvp < vop2_data->nr_vps) {
				vp = &vop2->vps[nvp];

				possible_crtcs = BIT(vop2_data->vp[i].id);
				vp->primary_plane = win;

				nvp++;
			} else {
				/* change the unused primary window to overlay window */
				win->type = DRM_PLANE_TYPE_OVERLAY;
			}
		}

		if (win->type == DRM_PLANE_TYPE_OVERLAY)
			possible_crtcs = (1 << vop2_data->nr_vps) - 1;
		ret = vop2_plane_init(vop2, win, possible_crtcs);

		if (ret) {
			drm_err(vop2->drm, "failed to init plane %s: %d\n", win->data->name, ret);
			return ret;
		}
	}

	for (i = 0; i < vop2_data->nr_vps; i++) {
		char dclk_name[9];

		vp_data = &vop2_data->vp[i];
		vp = &vop2->vps[i];
		vp->vop2 = vop2;
		vp->id = vp_data->id;
		vp->regs = vp_data->regs;

		vp->zpos = devm_kzalloc(vop2->dev, vop2->data->win_size * sizeof(*vp->zpos), GFP_KERNEL);
		if (!vp->zpos)
			return -ENOMEM;

		snprintf(dclk_name, sizeof(dclk_name), "dclk_vp%d", vp->id);
		vp->dclk = devm_clk_get(vop2->dev, dclk_name);
		if (IS_ERR(vp->dclk)) {
			drm_err(vop2->drm, "failed to get %s\n", dclk_name);
			return PTR_ERR(vp->dclk);
		}

		crtc = &vp->crtc;

		port = of_graph_get_port_by_id(dev->of_node, i);
		if (!port) {
			drm_err(vop2->drm, "no port node found for video_port%d\n", i);
			return -ENOENT;
		}
		crtc->port = port;

		plane = &vp->primary_plane->base;

		ret = drm_crtc_init_with_planes(drm, crtc, plane, NULL, &vop2_crtc_funcs,
						"video_port%d", vp->id);
		if (ret) {
			drm_err(vop2->drm, "crtc init for video_port%d failed\n", i);
			return ret;
		}

		drm_crtc_helper_add(crtc, &vop2_crtc_helper_funcs);

		drm_flip_work_init(&vp->fb_unref_work, "fb_unref", vop2_fb_unref_worker);

		init_completion(&vp->dsp_hold_completion);
		init_completion(&vp->line_flag_completion);
	}

	return 0;
}

static void vop2_destroy_crtc(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);

	of_node_put(crtc->port);

	/*
	 * Destroy CRTC after vop2_plane_destroy() since vop2_disable_plane()
	 * references the CRTC.
	 */
	drm_crtc_cleanup(crtc);
	drm_flip_work_cleanup(&vp->fb_unref_work);
}

static int vop2_win_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_layer_data *layer_data;
	struct vop2_win *win;
	struct vop2_layer *layer;
	uint8_t plane_id = 0;
	unsigned int i;

	for (i = 0; i < vop2_data->win_size; i++) {
		const struct vop2_win_data *win_data = &vop2_data->win[i];

		win = &vop2->win[i];
		win->data = win_data;
		win->type = win_data->type;
		win->regs = win_data->regs;
		win->offset = win_data->base;
		win->win_id = i;
		win->plane_id = plane_id++;
		win->zpos = i;
		win->vop2 = vop2;
	}

	vop2->registered_num_wins = vop2_data->win_size;

	for (i = 0; i < vop2_data->nr_layers; i++) {
		layer = &vop2->layers[i];
		layer_data = &vop2_data->layer[i];
		layer->id = layer_data->id;
		layer->regs = layer_data->regs;
	}

	return 0;
}

static int vop2_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct vop2_data *vop2_data;
	struct drm_device *drm = data;
	struct vop2 *vop2;
	struct resource *res;
	size_t alloc_size;
	int ret;

	vop2_data = of_device_get_match_data(dev);
	if (!vop2_data)
		return -ENODEV;

	/* Allocate vop2 struct and its vop2_win array */
	alloc_size = sizeof(*vop2) + sizeof(*vop2->win) * vop2_data->win_size;
	vop2 = devm_kzalloc(dev, alloc_size, GFP_KERNEL);
	if (!vop2)
		return -ENOMEM;

	vop2->dev = dev;
	vop2->data = vop2_data;
	vop2->drm = drm;

	dev_set_drvdata(dev, vop2);

	ret = vop2_win_init(vop2);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res) {
		drm_err(vop2->drm, "failed to get vop2 register byname\n");
		return -EINVAL;
	}
	vop2->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(vop2->regs))
		return PTR_ERR(vop2->regs);
	vop2->len = resource_size(res);

	vop2->regsbak = devm_kzalloc(dev, vop2->len, GFP_KERNEL);
	if (!vop2->regsbak)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gamma_lut");
	if (res) {
		vop2->lut_regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(vop2->lut_regs))
			return PTR_ERR(vop2->lut_regs);
	}

	vop2->grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");

	vop2->hclk = devm_clk_get(vop2->dev, "hclk_vop");
	if (IS_ERR(vop2->hclk)) {
		drm_err(vop2->drm, "failed to get hclk source\n");
		return PTR_ERR(vop2->hclk);
	}

	vop2->aclk = devm_clk_get(vop2->dev, "aclk_vop");
	if (IS_ERR(vop2->aclk)) {
		drm_err(vop2->drm, "failed to get aclk source\n");
		return PTR_ERR(vop2->aclk);
	}

	vop2->irq = platform_get_irq(pdev, 0);
	if (vop2->irq < 0) {
		drm_err(vop2->drm, "cannot find irq for vop2\n");
		return vop2->irq;
	}

	spin_lock_init(&vop2->reg_lock);
	spin_lock_init(&vop2->irq_lock);
	mutex_init(&vop2->vop2_lock);

	ret = devm_request_irq(dev, vop2->irq, vop2_isr, IRQF_SHARED, dev_name(dev), vop2);
	if (ret)
		return ret;

	ret = rockchip_drm_dma_attach_device(vop2->drm, vop2->dev);
	if (ret) {
		drm_err(vop2->drm, "failed to attach dma mapping, %d\n", ret);
		return ret;
	}

	ret = vop2_create_crtc(vop2);
	if (ret)
		return ret;

	ret = vop2_gamma_init(vop2);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);

	return 0;
}

static void vop2_unbind(struct device *dev, struct device *master, void *data)
{
	struct vop2 *vop2 = dev_get_drvdata(dev);
	struct drm_device *drm = vop2->drm;
	struct list_head *plane_list = &drm->mode_config.plane_list;
	struct list_head *crtc_list = &drm->mode_config.crtc_list;
	struct drm_crtc *crtc, *tmpc;
	struct drm_plane *plane, *tmpp;

	rockchip_drm_dma_detach_device(vop2->drm, vop2->dev);

	pm_runtime_disable(dev);

	list_for_each_entry_safe(plane, tmpp, plane_list, head)
		drm_plane_cleanup(plane);

	list_for_each_entry_safe(crtc, tmpc, crtc_list, head)
		vop2_destroy_crtc(crtc);
}

const struct component_ops vop2_component_ops = {
	.bind = vop2_bind,
	.unbind = vop2_unbind,
};
EXPORT_SYMBOL_GPL(vop2_component_ops);
