/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2017-2020 NXP
 */

#ifndef _DPU_DPRC_H_
#define _DPU_DPRC_H_

#include <linux/device.h>
#include <linux/of.h>
#include <linux/types.h>

#include <drm/drm_fourcc.h>

struct dpu_dprc;

void dpu_dprc_configure(struct dpu_dprc *dprc, unsigned int stream_id,
			unsigned int width, unsigned int height,
			unsigned int x_offset, unsigned int y_offset,
			unsigned int stride,
			const struct drm_format_info *format, u64 modifier,
			dma_addr_t baddr, dma_addr_t uv_baddr,
			bool start, bool interlace_frame);

void dpu_dprc_disable_repeat_en(struct dpu_dprc *dprc);

bool dpu_dprc_rtram_width_supported(struct dpu_dprc *dprc, unsigned int width);

bool dpu_dprc_stride_supported(struct dpu_dprc *dprc,
			       unsigned int stride, unsigned int uv_stride,
			       unsigned int width, unsigned int x_offset,
			       const struct drm_format_info *format,
			       u64 modifier,
			       dma_addr_t baddr, dma_addr_t uv_baddr);

struct dpu_dprc *
dpu_dprc_lookup_by_of_node(struct device *dev, struct device_node *dprc_node);

#endif
