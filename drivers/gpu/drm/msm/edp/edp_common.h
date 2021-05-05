/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 */

#ifndef __EDP_COMMON_CONNECTOR_H__
#define __EDP_COMMON_CONNECTOR_H__

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_dp_helper.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "dpu_io_util.h"

#define MSM_EDP_VERSION_200 200

struct msm_edp {
	struct drm_device *dev;
	struct platform_device *pdev;

	struct drm_connector *connector;
	struct drm_bridge *bridge;

	/* the encoder we are hooked to (outside of eDP block) */
	struct drm_encoder *encoder;

	int version;
};

void __init msm_edp_v200_register(void);
void __exit msm_edp_v200_unregister(void);
int msm_edp_v200_modeset_init(struct msm_edp *edp, struct drm_device *dev,
				struct drm_encoder *encoder);

#endif /* __EDP_COMMON_CONNECTOR_H__ */
