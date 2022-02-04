// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "dpu_writeback.h"

static int dpu_wb_conn_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	return drm_add_modes_noedid(connector, dev->mode_config.max_width,
			dev->mode_config.max_height);
}

static const struct drm_connector_funcs dpu_wb_conn_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int dpu_wb_conn_prepare_job(struct drm_writeback_connector *connector,
		struct drm_writeback_job *job)
{
	if (!job->fb)
		return 0;

	dpu_encoder_prepare_wb_job(connector->encoder, job);

	return 0;
}

static void dpu_wb_conn_cleanup_job(struct drm_writeback_connector *connector,
		struct drm_writeback_job *job)
{
	if (!job->fb)
		return;

	dpu_encoder_cleanup_wb_job(connector->encoder, job);
}

static const struct drm_connector_helper_funcs dpu_wb_conn_helper_funcs = {
	.get_modes = dpu_wb_conn_get_modes,
	.prepare_writeback_job = dpu_wb_conn_prepare_job,
	.cleanup_writeback_job = dpu_wb_conn_cleanup_job,
};

int dpu_writeback_init(struct drm_device *dev, struct drm_encoder *enc,
		const struct drm_encoder_helper_funcs *enc_helper_funcs, const u32 *format_list,
		u32 num_formats)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct dpu_wb_connector *dpu_wb_conn;
	int rc = 0;

	dpu_wb_conn = devm_kzalloc(dev->dev, sizeof(*dpu_wb_conn), GFP_KERNEL);
	dpu_wb_conn->base.base = &dpu_wb_conn->connector;
	dpu_wb_conn->base.encoder = enc;

	drm_connector_helper_add(dpu_wb_conn->base.base, &dpu_wb_conn_helper_funcs);

	rc = drm_writeback_connector_init(dev, &dpu_wb_conn->base,
			&dpu_wb_conn_funcs, enc_helper_funcs,
			format_list, num_formats);

	priv->connectors[priv->num_connectors++] = dpu_wb_conn->base.base;

	return rc;
}
