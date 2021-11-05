/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DRM_H_
#define _DP_DRM_H_

#include <linux/types.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "msm_drv.h"
#include "dp_display.h"

struct drm_connector *dp_drm_connector_init(struct msm_dp *dp_display);
struct msm_dp *msm_dp_from_connector(struct drm_connector *connector);
bool dp_drm_is_connector_msm_dp(struct drm_connector *connector);
void dp_drm_atomic_commit(struct drm_connector *connector,
			  struct drm_connector_state *conn_state,
			  struct drm_atomic_state *state);

#endif /* _DP_DRM_H_ */
