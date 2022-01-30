// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 Loongson Corporation
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
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#include <drm/drm_print.h>
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


static int lsdc_get_modes_from_edid(struct drm_connector *connector)
{
	struct drm_device *ddev = connector->dev;
	struct lsdc_connector *lconn = to_lsdc_connector(connector);
	struct edid *edid_p = (struct edid *)lconn->edid_data;
	int num = drm_add_edid_modes(connector, edid_p);

	if (num)
		drm_connector_update_edid_property(connector, edid_p);

	drm_dbg_kms(ddev, "%d modes added\n", num);

	return num;
}


static int lsdc_get_modes_from_timings(struct drm_connector *connector)
{
	struct drm_device *ddev = connector->dev;
	struct lsdc_connector *lconn = to_lsdc_connector(connector);
	struct display_timings *disp_tim = lconn->disp_tim;
	unsigned int i;
	unsigned int num = 0;

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
	struct drm_device *ddev = connector->dev;
	struct edid *edid = NULL;
	unsigned int num = 0;

	if (ddc) {
		edid = drm_get_edid(connector, ddc);
		if (edid) {
			drm_connector_update_edid_property(connector, edid);
			num = drm_add_edid_modes(connector, edid);
			kfree(edid);
		} else
			goto fallback;

		return num;
	} else
		drm_warn(ddev, "Grab EDID failed because of no DDC\n");

fallback:

	/*
	 * In case we cannot retrieve the EDIDs (broken or missing i2c
	 * bus), fallback on the XGA standards
	 */
	num = drm_add_modes_noedid(connector, 1920, 1200);

	/* And prefer a mode pretty much anyone can handle */
	drm_set_preferred_mode(connector, 1024, 768);

	return num;
}


static int lsdc_get_modes(struct drm_connector *connector)
{
	struct lsdc_connector *lconn = to_lsdc_connector(connector);

	if (lconn->has_edid)
		return lsdc_get_modes_from_edid(connector);

	if (lconn->has_disp_tim)
		return lsdc_get_modes_from_timings(connector);

	if (connector->connector_type == DRM_MODE_CONNECTOR_VIRTUAL) {
		struct drm_device *ddev = connector->dev;
		int count;

		count = drm_add_modes_noedid(connector,
				     ddev->mode_config.max_width,
				     ddev->mode_config.max_height);

		drm_set_preferred_mode(connector, 1024, 768);

		return count;
	}

	return lsdc_get_modes_from_ddc(connector, lconn->ddc);
}


static enum drm_connector_status
lsdc_connector_detect(struct drm_connector *connector, bool force)
{
	struct lsdc_connector *lconn = to_lsdc_connector(connector);

	if (lconn->has_edid == true)
		return connector_status_connected;

	if (lconn->has_disp_tim == true)
		return connector_status_connected;

	if (lconn->ddc && drm_probe_ddc(lconn->ddc))
		return connector_status_connected;

	if ((connector->connector_type == DRM_MODE_CONNECTOR_DVIA) ||
	    (connector->connector_type == DRM_MODE_CONNECTOR_DVID) ||
	    (connector->connector_type == DRM_MODE_CONNECTOR_DVII))
		return connector_status_disconnected;

	if ((connector->connector_type == DRM_MODE_CONNECTOR_HDMIA) ||
	    (connector->connector_type == DRM_MODE_CONNECTOR_HDMIB))
		return connector_status_disconnected;

	if (connector->connector_type == DRM_MODE_CONNECTOR_VIRTUAL)
		return connector_status_connected;

	return connector_status_unknown;
}


/*
 * @connector: point to the drm_connector structure
 *
 * Clean up connector resources
 */
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

/**
 * These provide the minimum set of functions required to handle a connector
 *
 * Control connectors on a given device.
 *
 * Each CRTC may have one or more connectors attached to it.
 * The functions below allow the core DRM code to control
 * connectors, enumerate available modes, etc.
 */
static const struct drm_connector_funcs lsdc_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = lsdc_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = lsdc_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};


/* Get the simple EDID data from the device tree
 * the length must be EDID_LENGTH, since it is simple.
 *
 * @np: device node contain edid data
 * @edid_data: where the edid data to store to
 */
static bool lsdc_get_edid_from_dtb(struct device_node *np,
				   unsigned char *edid_data)
{
	int length;
	const void *prop;

	if (np == NULL)
		return false;

	prop = of_get_property(np, "edid", &length);
	if (prop && (length == EDID_LENGTH)) {
		memcpy(edid_data, prop, EDID_LENGTH);
		return true;
	}

	return false;
}

/* Get display timings from the device tree
 *
 * @np: device node contain the display timings
 * @pptim: where the pointer of struct display_timings to store to
 */
static void lsdc_get_display_timings_from_dtb(struct device_node *np,
					      struct display_timings **pptim)
{
	struct display_timings *timings;

	if (np == NULL)
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
	if (ret < 0) {
		drm_warn(ddev, "please give a valid connector property\n");
		return DRM_MODE_CONNECTOR_Unknown;
	}

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

		res = of_property_read_string(output, "type", &hdmi_type);
		if (res == 0) {
			if (!strcmp(hdmi_type, "b"))
				ret = DRM_MODE_CONNECTOR_HDMIB;
			else
				ret = DRM_MODE_CONNECTOR_HDMIA;
		} else
			ret = DRM_MODE_CONNECTOR_HDMIA;

		drm_info(ddev, "connector%d is HDMI, type is %s\n",
			index, hdmi_type);
	} else {
		ret = DRM_MODE_CONNECTOR_Unknown;
		drm_info(ddev, "The type of connector%d unknown\n", index);
	}

	return ret;
}


struct lsdc_connector *lsdc_connector_init(struct lsdc_device *ldev,
					   unsigned int index)
{
	struct drm_device *ddev = &ldev->drm;
	struct device_node *np = ddev->dev->of_node;
	struct device_node *output = NULL;
	struct lsdc_connector *lconn;
	struct drm_connector *connector;
	bool available = false;
	unsigned int connector_type;
	int ret;

	lconn = devm_kzalloc(ddev->dev, sizeof(*lconn), GFP_KERNEL);
	if (lconn == NULL)
		return ERR_PTR(-ENOMEM);

	lconn->index = index;

	output = of_parse_phandle(np, "output-ports", index);
	if (output) {
		struct device_node *disp_tims_np;

		available = of_device_is_available(output);

		if (available == false) {
			drm_info(ddev, "connector%d is not available\n", index);
			of_node_put(output);
			return NULL;
		}

		lconn->has_edid = of_property_read_bool(output, "edid");
		disp_tims_np = of_get_child_by_name(output, "display-timings");
		if (disp_tims_np) {
			of_node_put(disp_tims_np);
			lconn->has_disp_tim = true;
		} else
			lconn->has_disp_tim = false;
	} else
		drm_warn(ddev, "no output-ports property, please update dtb\n");

	/*
	 * Providing a blindly support even through there is
	 * no output-ports property in the dtb.
	 */
	if (lconn->has_edid) {
		lsdc_get_edid_from_dtb(output, lconn->edid_data);
		drm_info(ddev, "connector%d provide edid\n", index);
	}

	if (lconn->has_disp_tim) {
		lsdc_get_display_timings_from_dtb(output, &lconn->disp_tim);
		drm_info(ddev, "connector%d provide display timings\n", index);
	}

	connector_type = lsdc_get_connector_type(ddev, output, index);

	if (output)
		of_node_put(output);

	connector = &lconn->base;

	/* bypass the ddc creation if the edid or display timing is provided */
	if ((lconn->has_edid == false) &&
	    (lconn->has_disp_tim == false) &&
	    (connector_type != DRM_MODE_CONNECTOR_VIRTUAL)) {
		const struct lsdc_chip_desc * const dc = ldev->desc;

		if (dc->have_builtin_i2c)
			lconn->ddc = lsdc_create_i2c_chan(ddev, index);
		else
			lconn->ddc = lsdc_get_i2c_adapter(ddev, index);

		if (lconn->ddc && (IS_ERR(lconn->ddc) == false)) {
			drm_info(ddev, "i2c%d for connector%d created\n",
					i2c_adapter_id(lconn->ddc), index);

			/* only pull if the connector have a ddc */
			connector->polled = DRM_CONNECTOR_POLL_CONNECT |
					    DRM_CONNECTOR_POLL_DISCONNECT;
		}
	}

	ret = drm_connector_init_with_ddc(ddev,
					  connector,
					  &lsdc_connector_funcs,
					  connector_type,
					  lconn->ddc);
	if (ret) {
		drm_err(ddev, "init connector%d failed\n", index);
		goto err_i2c_destroy;
	}

	drm_connector_helper_add(connector, &lsdc_connector_helpers);

	return lconn;

err_i2c_destroy:
	lsdc_destroy_i2c(ddev, lconn->ddc);
	return NULL;
}
