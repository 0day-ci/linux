/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2019,2020 NXP
 */

#ifndef __DPU_KMS_H__
#define __DPU_KMS_H__

#include <linux/of.h>
#include <linux/types.h>

#include "dpu-drv.h"

struct dpu_crtc_of_node {
	struct device_node *np;
	struct list_head list;
};

int dpu_kms_prepare(struct dpu_drm_device *dpu_drm,
		    struct list_head *crtc_np_list);

#endif
