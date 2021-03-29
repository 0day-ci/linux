/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2017-2020 NXP
 */

#ifndef __DPU_CRTC_H__
#define __DPU_CRTC_H__

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of.h>

#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "dpu.h"

#define dpu_crtc_dbg(crtc, fmt, ...)					\
	drm_dbg_kms((crtc)->dev, "[CRTC:%d:%s] " fmt,			\
		    (crtc)->base.id, (crtc)->name, ##__VA_ARGS__)

#define dpu_crtc_err(crtc, fmt, ...)					\
	drm_err((crtc)->dev, "[CRTC:%d:%s] " fmt,			\
		    (crtc)->base.id, (crtc)->name, ##__VA_ARGS__)

#define DPU_CRTC_CNT_IN_GRP	2

struct dpu_crtc {
	struct device		*dev;
	struct device_node	*np;
	struct list_head	node;
	struct drm_crtc		base;
	struct dpu_crtc_grp	*grp;
	struct drm_encoder	*encoder;
	struct dpu_constframe	*cf_cont;
	struct dpu_constframe	*cf_safe;
	struct dpu_disengcfg	*dec;
	struct dpu_extdst	*ed_cont;
	struct dpu_extdst	*ed_safe;
	struct dpu_framegen	*fg;
	struct dpu_gammacor	*gc;
	struct dpu_tcon		*tcon;
	unsigned int		stream_id;
	unsigned int		hw_plane_cnt;
	unsigned int		dec_frame_complete_irq;
	unsigned int		dec_seq_complete_irq;
	unsigned int		dec_shdld_irq;
	unsigned int		ed_cont_shdld_irq;
	unsigned int		ed_safe_shdld_irq;
	struct completion	dec_seq_complete_done;
	struct completion	dec_shdld_done;
	struct completion	ed_safe_shdld_done;
	struct completion	ed_cont_shdld_done;
	struct drm_pending_vblank_event *event;
};

static inline struct dpu_crtc *to_dpu_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct dpu_crtc, base);
}

#endif /* __DPU_CRTC_H__ */
