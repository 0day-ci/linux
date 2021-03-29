/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2017-2020 NXP
 */

#ifndef _DPU_PRG_H_
#define _DPU_PRG_H_

#include <linux/device.h>
#include <linux/types.h>

#include <drm/drm_fourcc.h>

struct dpu_prg;

void dpu_prg_enable(struct dpu_prg *prg);

void dpu_prg_disable(struct dpu_prg *prg);

void dpu_prg_configure(struct dpu_prg *prg,
		       unsigned int width, unsigned int height,
		       unsigned int x_offset, unsigned int y_offset,
		       unsigned int stride, unsigned int bits_per_pixel,
		       dma_addr_t baddr,
		       const struct drm_format_info *format, u64 modifier,
		       bool start);

void dpu_prg_reg_update(struct dpu_prg *prg);

void dpu_prg_shadow_enable(struct dpu_prg *prg);

bool dpu_prg_stride_supported(struct dpu_prg *prg,
			      unsigned int x_offset,
			      unsigned int bits_per_pixel, u64 modifier,
			      unsigned int stride, dma_addr_t baddr);

void dpu_prg_set_auxiliary(struct dpu_prg *prg);

void dpu_prg_set_primary(struct dpu_prg *prg);

struct dpu_prg *
dpu_prg_lookup_by_phandle(struct device *dev, const char *name, int index);

#endif
