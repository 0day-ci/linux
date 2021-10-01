/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_HDCP_H__
#define __INTEL_HDCP_H__

#include <linux/types.h>

#define HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS	50

struct drm_atomic_state;
struct drm_connector;
struct drm_connector_state;
struct drm_hdcp_an;
struct drm_i915_private;
struct intel_atomic_state;
struct intel_connector;
struct intel_crtc_state;
struct intel_encoder;
struct intel_hdcp_shim;
struct intel_digital_port;
enum port;
enum transcoder;

int intel_hdcp_init(struct intel_connector *connector,
		    struct intel_digital_port *dig_port,
		    const struct intel_hdcp_shim *hdcp_shim);
int intel_hdcp_setup(struct drm_connector *drm_connector,
		     struct drm_atomic_state *state);
int intel_hdcp_load_keys(struct drm_connector *drm_connector);
bool is_hdcp_supported(struct drm_i915_private *dev_priv, enum port port);
int intel_hdcp_capable(struct intel_connector *connector, bool *capable);
int intel_hdcp2_capable(struct drm_connector *drm_connector, bool *capable);
int intel_hdcp2_enable(struct drm_connector *drm_connector);
int intel_hdcp2_disable(struct drm_connector *drm_connector);
int intel_hdcp2_check_link(struct drm_connector *drm_connector);
int intel_hdcp1_store_receiver_info(struct drm_connector *drm_connector,
				    u32 *ksv, u32 status, u8 caps,
				    bool repeater_present);
int intel_hdcp1_read_an(struct drm_connector *drm_connector,
			struct drm_hdcp_an *an);
int intel_hdcp1_enable_encryption(struct drm_connector *drm_connector);
int intel_hdcp1_wait_for_r0(struct drm_connector *drm_connector);
int intel_hdcp1_match_ri(struct drm_connector *drm_connector, u32 ri_prime);
int intel_hdcp1_post_encryption(struct drm_connector *drm_connector);
int intel_hdcp1_store_ksv_fifo(struct drm_connector *drm_connector,
			       u8 *ksv_fifo, u8 num_downstream, u8 *bstatus,
			       u32 *v_prime);
int intel_hdcp1_check_link(struct drm_connector *drm_connector);
int intel_hdcp1_disable(struct drm_connector *drm_connector);
void intel_hdcp_component_init(struct drm_i915_private *dev_priv);
void intel_hdcp_component_fini(struct drm_i915_private *dev_priv);
void intel_hdcp_cleanup(struct intel_connector *connector);
void intel_hdcp_handle_cp_irq(struct intel_connector *connector);

#endif /* __INTEL_HDCP_H__ */
