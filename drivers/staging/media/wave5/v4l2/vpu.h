/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Wave5 series multi-standard codec IP - basic types
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */
#ifndef __VPU_DRV_H__
#define __VPU_DRV_H__

#include <media/v4l2-ctrls.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>
#include "../vpuapi/vpuconfig.h"
#include "../vpuapi/vpuapi.h"

#define DPRINTK(dev, level, fmt, args...) \
	v4l2_dbg(level, vpu_debug, &(dev)->v4l2_dev, "[%s]" fmt, __func__, ##args)

#define VPU_BUF_SYNC_TO_DEVICE 0
#define VPU_BUF_SYNC_FROM_DEVICE 1
struct vpu_platform_data {
	struct vb2_mem_ops *mem_ops;
	int (*pre_fw_init)(struct device *dev, void __iomem *base);
	uint32_t (*read_register)(struct device *dev, void __iomem *base, uint32_t reg);
	void (*write_register)(struct device *dev, void __iomem *base, uint32_t reg, uint32_t data);
	int (*buffer_sync)(struct device *dev, void __iomem *base, struct vpu_buf *vb, size_t offset, uint32_t len, int dir);
	int (*buffer_alloc)(struct device *dev, struct vpu_buf *vb);
	void (*buffer_free)(struct device *dev, struct vpu_buf *vb);
	int (*reset)(struct device *dev, void __iomem *base);
	uint32_t (*get_hwoption)(struct device *dev);
};

struct vpu_buffer {
	struct v4l2_m2m_buffer v4l2_m2m_buf;
	bool                   consumed;
};

enum vpu_format_type {
	VPU_FMT_TYPE_CODEC = 0,
	VPU_FMT_TYPE_RAW   = 1
};

struct vpu_format {
	unsigned int v4l2_pix_fmt;
	unsigned int num_planes;
	unsigned int max_width;
	unsigned int min_width;
	unsigned int max_height;
	unsigned int min_height;
};

extern unsigned int vpu_debug;

static inline struct vpu_instance *to_vpu_inst(struct v4l2_fh *vfh)
{
	return container_of(vfh, struct vpu_instance, v4l2_fh);
}

static inline struct vpu_instance *ctrl_to_vpu_inst(struct v4l2_ctrl *vctrl)
{
	return container_of(vctrl->handler, struct vpu_instance, v4l2_ctrl_hdl);
}

static inline struct vpu_buffer *to_vpu_buf(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct vpu_buffer, v4l2_m2m_buf.vb);
}

int vpu_wait_interrupt(struct vpu_instance *inst, unsigned int timeout);

#endif

