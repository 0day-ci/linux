// SPDX-License-Identifier: GPL-2.0
/*
 * KMS driver for Loongson display controller
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#include "lsdc_drv.h"

static const struct drm_encoder_funcs lsdc_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int lsdc_encoder_init(struct drm_encoder * const encoder,
		      struct drm_connector *connector,
		      struct drm_device *ddev,
		      unsigned int index,
		      unsigned int total)
{
	int ret;
	int type;

	encoder->possible_crtcs = BIT(index);

	if (total == 2)
		encoder->possible_clones = BIT(1) | BIT(0);
	else if (total < 2)
		encoder->possible_clones = 0;

	if (connector->connector_type == DRM_MODE_CONNECTOR_VGA)
		type = DRM_MODE_ENCODER_DAC;
	else if ((connector->connector_type == DRM_MODE_CONNECTOR_HDMIA) ||
		 (connector->connector_type == DRM_MODE_CONNECTOR_HDMIB) ||
		 (connector->connector_type == DRM_MODE_CONNECTOR_DVID))
		type = DRM_MODE_ENCODER_TMDS;
	else if (connector->connector_type == DRM_MODE_CONNECTOR_DPI)
		type = DRM_MODE_ENCODER_DPI;
	else if (connector->connector_type == DRM_MODE_CONNECTOR_VIRTUAL)
		type = DRM_MODE_ENCODER_VIRTUAL;
	else
		type = DRM_MODE_ENCODER_NONE;

	ret = drm_encoder_init(ddev, encoder, &lsdc_encoder_funcs, type, "encoder%d", index);
	if (ret)
		return ret;

	return drm_connector_attach_encoder(connector, encoder);
}
