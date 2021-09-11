// SPDX-License-Identifier: GPL-2.0-or-later

#include <drm/drm_probe_helper.h>

#include "loongson_drv.h"

static int loongson_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct loongson_connector *lconnector =
				to_loongson_connector(connector);
	struct i2c_adapter *adapter = lconnector->i2c->adapter;
	struct edid *edid = NULL;
	u32 ret;

	edid = drm_get_edid(connector, adapter);
	if (edid) {
		drm_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
	} else {
		drm_warn(dev, "Failed to read EDID\n");
		ret = drm_add_modes_noedid(connector, 1920, 1080);
		drm_set_preferred_mode(connector, 1024, 768);
	}

	return ret;
}

static bool is_connected(struct loongson_connector *lconnector)
{
	struct i2c_adapter *adapter = lconnector->i2c->adapter;
	unsigned char start = 0x0;
	struct i2c_msg msgs = {
		.addr = DDC_ADDR,
		.flags = 0,
		.len = 1,
		.buf = &start,
	};

	if (!lconnector->i2c)
		return false;

	if (i2c_transfer(adapter, &msgs, 1) != 1)
		return false;

	return true;
}

static enum drm_connector_status
loongson_detect(struct drm_connector *connector, bool force)
{
	struct loongson_connector *lconnector =
				to_loongson_connector(connector);

	if (is_connected(lconnector))
		return connector_status_connected;

	return connector_status_disconnected;
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
	struct drm_device *dev = &ldev->dev;
	struct drm_connector *connector;
	struct loongson_connector *lconnector;

	lconnector = kzalloc(sizeof(struct loongson_connector), GFP_KERNEL);
	if (!lconnector)
		return -ENOMEM;

	lconnector->ldev = ldev;
	lconnector->id = index;
	lconnector->i2c_id = index;

	lconnector->i2c = &ldev->i2c_bus[lconnector->i2c_id];
	if (!lconnector->i2c)
		drm_err(dev, "connector-%d match i2c-%d err\n", index,
			lconnector->i2c_id);

	ldev->mode_info[index].connector = lconnector;
	connector = &lconnector->base;
	drm_connector_init(dev, connector, &loongson_connector_funcs,
			   DRM_MODE_CONNECTOR_Unknown);
	drm_connector_helper_add(connector, &loongson_connector_helper);

	return 0;
}
