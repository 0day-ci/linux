// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Loongson Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

/*
 * Authors:
 *	Sui Jingfeng <suijingfeng@loongson.cn>
 */
#include <drm/drm_print.h>
#include <drm/drm_crtc_helper.h>

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

	ret = drm_encoder_init(ddev,
				encoder,
				&lsdc_encoder_funcs,
				type,
				"encoder%d",
				index);
	if (ret)
		return ret;

	return drm_connector_attach_encoder(connector, encoder);
}
