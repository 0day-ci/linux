// SPDX-License-Identifier: GPL-2.0-or-later

#include "loongson_drv.h"

static int loongson_get_modes(struct drm_connector *connector)
{
	struct loongson_connector *lconnector;
	struct i2c_adapter *adapter;
	struct edid *edid = NULL;
	u32 ret;

	lconnector = to_loongson_connector(connector);
	adapter = lconnector->i2c->adapter;

	if (adapter != NULL)
		edid = drm_get_edid(connector, adapter);
	else
		DRM_DEBUG_KMS("get loongson connector adapter err\n");

	if (edid) {
		drm_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
	} else {
		DRM_ERROR("Failed to read EDID\n");
		ret = drm_add_modes_noedid(connector, 1024, 768);
	}

	return ret;
}

static bool is_connected(struct loongson_connector *ls_connector)
{
	unsigned char start = 0x0;
	struct i2c_adapter *adapter;
	struct i2c_msg msgs = {
		.addr = DDC_ADDR,
		.flags = 0,
		.len = 1,
		.buf = &start,
	};

	if (!ls_connector->i2c)
		return false;

	adapter = ls_connector->i2c->adapter;
	if (i2c_transfer(adapter, &msgs, 1) != 1) {
		DRM_DEBUG_KMS("display-%d not connect\n", ls_connector->id);
		return false;
	}

	return true;
}

static enum drm_connector_status
loongson_detect(struct drm_connector *connector, bool force)
{
	struct loongson_connector *lconnector;
	enum drm_connector_status ret = connector_status_disconnected;

	lconnector = to_loongson_connector(connector);

	if (is_connected(lconnector))
		ret = connector_status_connected;

	return ret;
}

static const struct drm_connector_helper_funcs loongson_connector_helper = {
	.get_modes = loongson_get_modes,
};

static const struct drm_connector_funcs loongson_connector_funcs = {
	.detect = loongson_detect,
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
	lconnector->i2c_id = index + DC_I2C_BASE;

	lconnector->i2c = loongson_i2c_bus_match(ldev, lconnector->i2c_id);
	if (!lconnector->i2c)
		DRM_INFO("connector-%d match i2c-%d err\n", index,
			 lconnector->i2c_id);

	ldev->mode_info[index].connector = lconnector;
	connector = &lconnector->base;
	drm_connector_init(ldev->dev, connector, &loongson_connector_funcs,
			DRM_MODE_CONNECTOR_Unknown);
	drm_connector_helper_add(connector, &loongson_connector_helper);

	return 0;
}
