// SPDX-License-Identifier: GPL-2.0
/*
 * KMS driver for Loongson display controller
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_connector.h>

#include <video/videomode.h>
#include <video/of_display_timing.h>

#include "lsdc_drv.h"
#include "lsdc_i2c.h"
#include "lsdc_connector.h"

static int lsdc_get_modes_from_timings(struct drm_connector *connector)
{
	struct drm_device *ddev = connector->dev;
	struct lsdc_connector *lconn = to_lsdc_connector(connector);
	struct display_timings *disp_tim = lconn->disp_tim;
	unsigned int num = 0;
	unsigned int i;

	for (i = 0; i < disp_tim->num_timings; i++) {
		const struct display_timing *dt = disp_tim->timings[i];
		struct drm_display_mode *mode;
		struct videomode vm;

		videomode_from_timing(dt, &vm);
		mode = drm_mode_create(ddev);
		if (!mode) {
			drm_err(ddev, "failed to add mode %ux%u\n",
				dt->hactive.typ, dt->vactive.typ);
			continue;
		}

		drm_display_mode_from_videomode(&vm, mode);

		mode->type |= DRM_MODE_TYPE_DRIVER;

		if (i == disp_tim->native_mode)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, mode);
		num++;
	}

	drm_dbg_kms(ddev, "%d modes added\n", num);

	return num;
}

static int lsdc_get_modes_from_ddc(struct drm_connector *connector,
				   struct i2c_adapter *ddc)
{
	unsigned int num = 0;
	struct edid *edid;

	edid = drm_get_edid(connector, ddc);
	if (edid) {
		drm_connector_update_edid_property(connector, edid);
		num = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return num;
}

static int lsdc_get_modes(struct drm_connector *connector)
{
	struct lsdc_connector *lconn = to_lsdc_connector(connector);
	unsigned int num = 0;

	if (lconn->has_disp_tim)
		return lsdc_get_modes_from_timings(connector);

	if (IS_ERR_OR_NULL(lconn->ddc) == false)
		return lsdc_get_modes_from_ddc(connector, lconn->ddc);

	if (connector->connector_type == DRM_MODE_CONNECTOR_VIRTUAL) {
		num = drm_add_modes_noedid(connector,
					   connector->dev->mode_config.max_width,
					   connector->dev->mode_config.max_height);

		drm_set_preferred_mode(connector, 1024, 768);

		return num;
	}

	/*
	 * In case we cannot retrieve the EDIDs (broken or missing i2c
	 * bus), fallback on the XGA standards.
	 */
	num = drm_add_modes_noedid(connector, 1920, 1200);

	/* And prefer a mode pretty much anyone can handle */
	drm_set_preferred_mode(connector, 1024, 768);

	return num;
}

static enum drm_connector_status
lsdc_connector_detect(struct drm_connector *connector, bool force)
{
	struct lsdc_connector *lconn = to_lsdc_connector(connector);

	if (lconn->has_disp_tim)
		return connector_status_connected;

	if (lconn->ddc && drm_probe_ddc(lconn->ddc))
		return connector_status_connected;

	if (connector->connector_type == DRM_MODE_CONNECTOR_VIRTUAL)
		return connector_status_connected;

	if (connector->connector_type == DRM_MODE_CONNECTOR_DVIA ||
	    connector->connector_type == DRM_MODE_CONNECTOR_DVID ||
	    connector->connector_type == DRM_MODE_CONNECTOR_DVII)
		return connector_status_disconnected;

	if (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector->connector_type == DRM_MODE_CONNECTOR_HDMIB)
		return connector_status_disconnected;

	return connector_status_unknown;
}

static void lsdc_connector_destroy(struct drm_connector *connector)
{
	struct drm_device *ddev = connector->dev;
	struct lsdc_connector *lconn = to_lsdc_connector(connector);

	if (lconn) {
		if (lconn->ddc)
			lsdc_destroy_i2c(connector->dev, lconn->ddc);

		drm_info(ddev, "destroying connector%u\n", lconn->index);

		devm_kfree(ddev->dev, lconn);
	}

	drm_connector_cleanup(connector);
}

static const struct drm_connector_helper_funcs lsdc_connector_helpers = {
	.get_modes = lsdc_get_modes,
};

static const struct drm_connector_funcs lsdc_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = lsdc_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = lsdc_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* lsdc_get_display_timings_from_dtb - Get display timings from the device tree
 *
 * @np: point to the device node contain the display timings
 * @pptim: point to where the pointer of struct display_timings is store to
 */
static void lsdc_get_display_timings_from_dtb(struct device_node *np,
					      struct display_timings **pptim)
{
	struct display_timings *timings;

	if (!np)
		return;

	timings = of_get_display_timings(np);
	if (timings)
		*pptim = timings;
}

static int lsdc_get_connector_type(struct drm_device *ddev,
				   struct device_node *output,
				   unsigned int index)
{
	const char *name;
	int ret;

	ret = of_property_read_string(output, "connector", &name);
	if (ret < 0)
		return DRM_MODE_CONNECTOR_Unknown;

	if (strncmp(name, "vga-connector", 13) == 0) {
		ret = DRM_MODE_CONNECTOR_VGA;
		drm_info(ddev, "connector%d is VGA\n", index);
	} else if (strncmp(name, "dvi-connector", 13) == 0) {
		bool analog, digital;

		analog = of_property_read_bool(output, "analog");
		digital = of_property_read_bool(output, "digital");

		if (analog && !digital)
			ret = DRM_MODE_CONNECTOR_DVIA;
		else if (analog && digital)
			ret = DRM_MODE_CONNECTOR_DVII;
		else
			ret = DRM_MODE_CONNECTOR_DVID;

		drm_info(ddev, "connector%d is DVI\n", index);
	} else if (strncmp(name, "virtual-connector", 17) == 0) {
		ret = DRM_MODE_CONNECTOR_VIRTUAL;
		drm_info(ddev, "connector%d is virtual\n", index);
	} else if (strncmp(name, "dpi-connector", 13) == 0) {
		ret = DRM_MODE_CONNECTOR_DPI;
		drm_info(ddev, "connector%d is DPI\n", index);
	} else if (strncmp(name, "hdmi-connector", 14) == 0) {
		int res;
		const char *hdmi_type;

		ret = DRM_MODE_CONNECTOR_HDMIA;

		res = of_property_read_string(output, "type", &hdmi_type);
		if (res == 0 && !strcmp(hdmi_type, "b"))
			ret = DRM_MODE_CONNECTOR_HDMIB;

		drm_info(ddev, "connector%d is HDMI, type is %s\n", index, hdmi_type);
	} else {
		ret = DRM_MODE_CONNECTOR_Unknown;
		drm_info(ddev, "The type of connector%d is unknown\n", index);
	}

	return ret;
}

struct lsdc_connector *lsdc_connector_init(struct lsdc_device *ldev, unsigned int index)
{
	struct drm_device *ddev = &ldev->drm;
	struct device_node *np = ddev->dev->of_node;
	struct device_node *output = NULL;
	unsigned int connector_type = DRM_MODE_CONNECTOR_Unknown;
	struct device_node *disp_tims_np;
	struct lsdc_connector *lconn;
	struct drm_connector *connector;
	int ret;

	lconn = devm_kzalloc(ddev->dev, sizeof(*lconn), GFP_KERNEL);
	if (!lconn)
		return ERR_PTR(-ENOMEM);

	lconn->index = index;
	lconn->has_disp_tim = false;
	lconn->ddc = NULL;

	output = of_parse_phandle(np, "output-ports", index);
	if (!output) {
		drm_warn(ddev, "no output-ports property, please update dtb\n");
		/*
		 * Providing a blindly support even though no output-ports
		 * property is provided in the dtb.
		 */
		goto DT_SKIPED;
	}

	if (!of_device_is_available(output)) {
		of_node_put(output);
		drm_info(ddev, "connector%d is not available\n", index);
		return NULL;
	}

	disp_tims_np = of_get_child_by_name(output, "display-timings");
	if (disp_tims_np) {
		lsdc_get_display_timings_from_dtb(output, &lconn->disp_tim);
		lconn->has_disp_tim = true;
		of_node_put(disp_tims_np);
		drm_info(ddev, "Found display timings provided by connector%d\n", index);
	}

	connector_type = lsdc_get_connector_type(ddev, output, index);

	if (output) {
		of_node_put(output);
		output = NULL;
	}

DT_SKIPED:

	/* Only create the i2c channel if display timing is not provided */
	if (!lconn->has_disp_tim) {
		const struct lsdc_chip_desc * const desc = ldev->desc;

		if (desc->have_builtin_i2c)
			lconn->ddc = lsdc_create_i2c_chan(ddev, index);
		else
			lconn->ddc = lsdc_get_i2c_adapter(ddev, index);

		if (IS_ERR(lconn->ddc)) {
			lconn->ddc = NULL;

			drm_err(ddev, "Get i2c adapter failed: %ld\n",
				PTR_ERR(lconn->ddc));
		} else if (lconn->ddc)
			drm_info(ddev, "i2c%d for connector%d created\n",
				 i2c_adapter_id(lconn->ddc), index);
	}

	connector = &lconn->base;
	connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	ret = drm_connector_init_with_ddc(ddev,
					  connector,
					  &lsdc_connector_funcs,
					  connector_type,
					  lconn->ddc);
	if (ret) {
		drm_err(ddev, "init connector%d failed\n", index);
		goto ERR_CONNECTOR_INIT;
	}

	drm_connector_helper_add(connector, &lsdc_connector_helpers);

	return lconn;

ERR_CONNECTOR_INIT:
	if (!IS_ERR_OR_NULL(lconn->ddc))
		lsdc_destroy_i2c(ddev, lconn->ddc);

	return ERR_PTR(ret);
}
