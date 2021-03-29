/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2020 NXP
 */

#ifndef __DPU_DRV_H__
#define __DPU_DRV_H__

#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <drm/drm_device.h>

struct dpu_drm_device {
	struct drm_device base;
	struct list_head crtc_list;
};

extern const struct of_device_id dpu_dt_ids[];

extern struct platform_driver dpu_prg_driver;
extern struct platform_driver dpu_dprc_driver;
extern struct platform_driver dpu_core_driver;
extern struct platform_driver dpu_crtc_driver;

#endif /* __DPU_DRV_H__ */
