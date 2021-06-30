// SPDX-License-Identifier: GPL-2.0-or-later

#include "loongson_drv.h"

static void loongson_encoder_destroy(struct drm_encoder *encoder)
{
	struct loongson_encoder *lencoder = to_loongson_encoder(encoder);

	drm_encoder_cleanup(encoder);
	kfree(lencoder);
}

static const struct drm_encoder_funcs loongson_encoder_funcs = {
	.destroy = loongson_encoder_destroy,
};

int loongson_encoder_init(struct loongson_device *ldev, int index)
{
	struct drm_encoder *encoder;
	struct loongson_encoder *lencoder;

	lencoder = kzalloc(sizeof(struct loongson_encoder), GFP_KERNEL);
	if (!lencoder)
		return -1;

	lencoder->lcrtc = ldev->mode_info[index].crtc;
	lencoder->ldev = ldev;
	encoder = &lencoder->base;
	encoder->possible_crtcs = 1 << index;

	drm_encoder_init(ldev->dev, encoder, &loongson_encoder_funcs,
			 DRM_MODE_ENCODER_DAC, NULL);

	ldev->mode_info[index].encoder = lencoder;

	return 0;
}
