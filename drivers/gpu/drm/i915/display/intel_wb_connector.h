/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_WB_CONNECTOR_H__
#define __INTEL_WB_CONNECTOR_H__

#include "intel_display.h"

struct intel_wb_connector *intel_wb_connector_alloc(void);
void intel_wb_connector_free(struct intel_wb_connector *connector);
void intel_wb_connector_destroy(struct drm_connector *connector);
bool intel_wb_connector_get_hw_state(struct intel_wb_connector *connector);
enum pipe intel_wb_connector_get_pipe(struct intel_wb_connector *connector);
void intel_wb_connector_attach_encoder(struct intel_wb_connector *connector,
					struct intel_encoder *encoder);

#endif /* __INTEL_WB_CONNECTOR_H__ */

