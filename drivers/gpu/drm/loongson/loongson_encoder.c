// SPDX-License-Identifier: GPL-2.0-or-later

#include <drm/drm_simple_kms_helper.h>

#include "loongson_drv.h"

int loongson_encoder_init(struct loongson_device *ldev, int index)
{
	struct drm_device *dev = &ldev->dev;
	struct loongson_encoder *lencoder;

	lencoder = drmm_simple_encoder_alloc(dev, struct loongson_encoder,
					     base, DRM_MODE_ENCODER_DAC);
	if (IS_ERR(lencoder))
		return PTR_ERR(lencoder);

	lencoder->base.possible_crtcs = 1 << index;
	ldev->mode_info[index].encoder = lencoder;

	return 0;
}
