/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Wave5 series multi-standard codec IP - encoder interface
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */
#ifndef __VPU_ENC_DRV_H__
#define __VPU_ENC_DRV_H__

#include "vpu.h"

#define VPU_ENC_DEV_NAME "C&M VPU encoder"
#define VPU_ENC_DRV_NAME "vpu-enc"

int  vpu_enc_register_device(struct vpu_device *dev);
void vpu_enc_unregister_device(struct vpu_device *dev);
#endif

