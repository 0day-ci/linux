// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 */

#include "edp_common.h"

void __init msm_edp_register(void)
{
	msm_edp_v200_register();
}

void __exit msm_edp_unregister(void)
{
	msm_edp_v200_unregister();
}

/* Second part of initialization, the drm/kms level modeset_init */
int msm_edp_modeset_init(struct msm_edp *edp, struct drm_device *dev,
				struct drm_encoder *encoder)
{
	int ret = 0;

	if (WARN_ON(!encoder) || WARN_ON(!edp) || WARN_ON(!dev))
		return -EINVAL;

	edp->encoder = encoder;
	edp->dev = dev;

	if (edp->version == MSM_EDP_VERSION_200)
		ret = msm_edp_v200_modeset_init(edp, dev, encoder);

	return ret;
}
