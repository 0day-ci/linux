/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Wave5 series multi-standard codec IP - decoder interface
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */
#ifndef __VPU_DEC_DRV_H__
#define __VPU_DEC_DRV_H__

#include "vpu.h"

#define VPU_DEC_DEV_NAME "C&M VPU decoder"
#define VPU_DEC_DRV_NAME "vpu-dec"

#define V4L2_CID_VPU_THUMBNAIL_MODE (V4L2_CID_USER_BASE + 0x1001)

int  vpu_dec_register_device(struct vpu_device *dev);
void vpu_dec_unregister_device(struct vpu_device *dev);
#endif

