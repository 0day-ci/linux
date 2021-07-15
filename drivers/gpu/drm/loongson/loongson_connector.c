// SPDX-License-Identifier: GPL-2.0-or-later

#include "loongson_drv.h"

static int loongson_get_modes(struct drm_connector *connector)
{
	int count;

	count = drm_add_modes_noedid(connector, 1920, 1080);
	drm_set_preferred_mode(connector, 1024, 768);

	return count;
}

static const struct drm_connector_helper_funcs loongson_connector_helper = {
	.get_modes = loongson_get_modes,
};

static const struct drm_connector_funcs loongson_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

int loongson_connector_init(struct loongson_device *ldev, int index)
{
	struct drm_connector *connector;
	struct loongson_connector *lconnector;

	lconnector = kzalloc(sizeof(struct loongson_connector), GFP_KERNEL);
	if (!lconnector) {
		DRM_INFO("loongson connector kzalloc failed\n");
		return -1;
	}

	lconnector->ldev = ldev;
	lconnector->id = index;

	ldev->mode_info[index].connector = lconnector;
	connector = &lconnector->base;
	drm_connector_init(ldev->dev, connector, &loongson_connector_funcs,
			   DRM_MODE_CONNECTOR_Unknown);
	drm_connector_helper_add(connector, &loongson_connector_helper);

	return 0;
}
