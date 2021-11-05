/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2017 Google, Inc.
 * Copyright _ 2017-2019, Intel Corporation.
 *
 * Authors:
 * Sean Paul <seanpaul@chromium.org>
 * Ramalingam C <ramalingam.c@intel.com>
 */

#include <linux/component.h>
#include <linux/i2c.h>
#include <linux/random.h>

#include <drm/drm_hdcp.h>
#include <drm/i915_component.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_connector.h"
#include "intel_de.h"
#include "intel_display_power.h"
#include "intel_display_types.h"
#include "intel_hdcp.h"
#include "intel_pcode.h"

#define KEY_LOAD_TRIES	5
#define HDCP2_LC_RETRY_CNT			3

static int intel_conn_to_vcpi(struct intel_connector *connector)
{
	/* For HDMI this is forced to be 0x0. For DP SST also this is 0x0. */
	return connector->port	? connector->port->vcpi.vcpi : 0;
}

/*
 * intel_hdcp_required_content_stream selects the most highest common possible HDCP
 * content_type for all streams in DP MST topology because security f/w doesn't
 * have any provision to mark content_type for each stream separately, it marks
 * all available streams with the content_type proivided at the time of port
 * authentication. This may prohibit the userspace to use type1 content on
 * HDCP 2.2 capable sink because of other sink are not capable of HDCP 2.2 in
 * DP MST topology. Though it is not compulsory, security fw should change its
 * policy to mark different content_types for different streams.
 */
static int
intel_hdcp_required_content_stream(struct intel_digital_port *dig_port)
{
	struct drm_connector_list_iter conn_iter;
	struct intel_digital_port *conn_dig_port;
	struct intel_connector *connector;
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	bool enforce_type0 = false;
	int k;

	data->k = 0;

	if (dig_port->hdcp_auth_status)
		return 0;

	drm_connector_list_iter_begin(&i915->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		if (connector->base.status == connector_status_disconnected)
			continue;

		if (!intel_encoder_is_mst(intel_attached_encoder(connector)))
			continue;

		conn_dig_port = intel_attached_dig_port(connector);
		if (conn_dig_port != dig_port)
			continue;

		if (!enforce_type0 && !dig_port->hdcp_mst_type1_capable)
			enforce_type0 = true;

		data->streams[data->k].stream_id = intel_conn_to_vcpi(connector);
		data->k++;

		/* if there is only one active stream */
		if (dig_port->dp.active_mst_links <= 1)
			break;
	}
	drm_connector_list_iter_end(&conn_iter);

	if (drm_WARN_ON(&i915->drm, data->k > INTEL_NUM_PIPES(i915) || data->k == 0))
		return -EINVAL;

	/*
	 * Apply common protection level across all streams in DP MST Topology.
	 * Use highest supported content type for all streams in DP MST Topology.
	 */
	for (k = 0; k < data->k; k++)
		data->streams[k].stream_type =
			enforce_type0 ? DRM_MODE_HDCP_CONTENT_TYPE0 : DRM_MODE_HDCP_CONTENT_TYPE1;

	return 0;
}

static int intel_hdcp_prepare_streams(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	if (!intel_encoder_is_mst(intel_attached_encoder(connector))) {
		data->k = 1;
		data->streams[0].stream_type = hdcp->content_type;
	} else {
		ret = intel_hdcp_required_content_stream(dig_port);
		if (ret)
			return ret;
	}

	return 0;
}

/* Is HDCP2.2 capable on Platform and Sink */
int intel_hdcp2_capable(struct drm_connector *drm_connector, bool *capable)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;

	*capable = false;

	/* I915 support for HDCP2.2 */
	if (!hdcp->hdcp2_supported)
		return 0;

	/* MEI interface is solid */
	mutex_lock(&dev_priv->hdcp_comp_mutex);
	if (!dev_priv->hdcp_comp_added ||  !dev_priv->hdcp_master) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return 0;
	}
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return 0;
}

int intel_hdcp1_check_link(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	enum port port = dig_port->base.port;
	u32 val;

	val = intel_de_read(dev_priv, HDCP_STATUS(dev_priv, cpu_transcoder, port));

	if (val & HDCP_STATUS_ENC)
		return 0;

	return -EINVAL;
}

static bool intel_hdcp2_in_use(struct drm_i915_private *dev_priv,
			       enum transcoder cpu_transcoder, enum port port)
{
	return intel_de_read(dev_priv,
	                     HDCP2_STATUS(dev_priv, cpu_transcoder, port)) &
	       LINK_ENCRYPTION_STATUS;
}

static bool hdcp_key_loadable(struct drm_i915_private *dev_priv)
{
	enum i915_power_well_id id;
	intel_wakeref_t wakeref;
	bool enabled = false;

	/*
	 * On HSW and BDW, Display HW loads the Key as soon as Display resumes.
	 * On all BXT+, SW can load the keys only when the PW#1 is turned on.
	 */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		id = HSW_DISP_PW_GLOBAL;
	else
		id = SKL_DISP_PW_1;

	/* PG1 (power well #1) needs to be enabled */
	with_intel_runtime_pm(&dev_priv->runtime_pm, wakeref)
		enabled = intel_display_power_well_is_enabled(dev_priv, id);

	/*
	 * Another req for hdcp key loadability is enabled state of pll for
	 * cdclk. Without active crtc we wont land here. So we are assuming that
	 * cdclk is already on.
	 */

	return enabled;
}

static void intel_hdcp_clear_keys(struct drm_i915_private *dev_priv)
{
	intel_de_write(dev_priv, HDCP_KEY_CONF, HDCP_CLEAR_KEYS_TRIGGER);
	intel_de_write(dev_priv, HDCP_KEY_STATUS,
		       HDCP_KEY_LOAD_DONE | HDCP_KEY_LOAD_STATUS | HDCP_FUSE_IN_PROGRESS | HDCP_FUSE_ERROR | HDCP_FUSE_DONE);
}

int intel_hdcp_load_keys(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	int ret;
	u32 val;

	if (!hdcp_key_loadable(dev_priv)) {
		drm_err(&dev_priv->drm, "HDCP key Load is not possible\n");
		return -ENXIO;
	}

	val = intel_de_read(dev_priv, HDCP_KEY_STATUS);
	if ((val & HDCP_KEY_LOAD_DONE) && (val & HDCP_KEY_LOAD_STATUS))
		return 0;

	/*
	 * On HSW and BDW HW loads the HDCP1.4 Key when Display comes
	 * out of reset. So if Key is not already loaded, its an error state.
	 */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		if (!(intel_de_read(dev_priv, HDCP_KEY_STATUS) & HDCP_KEY_LOAD_DONE)) {
			ret = -ENXIO;
			goto err;
		}

	/*
	 * Initiate loading the HDCP key from fuses.
	 *
	 * BXT+ platforms, HDCP key needs to be loaded by SW. Only display
	 * version 9 platforms (minus BXT) differ in the key load trigger
	 * process from other platforms. These platforms use the GT Driver
	 * Mailbox interface.
	 */
	if (DISPLAY_VER(dev_priv) == 9 && !IS_BROXTON(dev_priv)) {
		ret = sandybridge_pcode_write(dev_priv,
					      SKL_PCODE_LOAD_HDCP_KEYS, 1);
		if (ret) {
			drm_err(&dev_priv->drm,
				"Failed to initiate HDCP key load (%d)\n",
				ret);
			goto err;
		}
	} else {
		intel_de_write(dev_priv, HDCP_KEY_CONF, HDCP_KEY_LOAD_TRIGGER);
	}

	/* Wait for the keys to load (500us) */
	ret = __intel_wait_for_register(&dev_priv->uncore, HDCP_KEY_STATUS,
					HDCP_KEY_LOAD_DONE, HDCP_KEY_LOAD_DONE,
					10, 1, &val);
	if (ret) {
		goto err;
	} else if (!(val & HDCP_KEY_LOAD_STATUS)) {
		ret = -ENXIO;
		goto err;
	}

	/* Send Aksv over to PCH display for use in authentication */
	intel_de_write(dev_priv, HDCP_KEY_CONF, HDCP_AKSV_SEND_TRIGGER);

	return 0;

err:
	intel_hdcp_clear_keys(dev_priv);
	return ret;
}

/* Returns updated SHA-1 index */
static int intel_write_sha_text(struct drm_i915_private *dev_priv, u32 sha_text)
{
	intel_de_write(dev_priv, HDCP_SHA_TEXT, sha_text);
	if (intel_de_wait_for_set(dev_priv, HDCP_REP_CTL, HDCP_SHA1_READY, 1)) {
		drm_err(&dev_priv->drm, "Timed out waiting for SHA1 ready\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static
u32 intel_hdcp_get_repeater_ctl(struct drm_i915_private *dev_priv,
				enum transcoder cpu_transcoder, enum port port)
{
	if (DISPLAY_VER(dev_priv) >= 12) {
		switch (cpu_transcoder) {
		case TRANSCODER_A:
			return HDCP_TRANSA_REP_PRESENT |
			       HDCP_TRANSA_SHA1_M0;
		case TRANSCODER_B:
			return HDCP_TRANSB_REP_PRESENT |
			       HDCP_TRANSB_SHA1_M0;
		case TRANSCODER_C:
			return HDCP_TRANSC_REP_PRESENT |
			       HDCP_TRANSC_SHA1_M0;
		case TRANSCODER_D:
			return HDCP_TRANSD_REP_PRESENT |
			       HDCP_TRANSD_SHA1_M0;
		default:
			drm_err(&dev_priv->drm, "Unknown transcoder %d\n",
				cpu_transcoder);
			return -EINVAL;
		}
	}

	switch (port) {
	case PORT_A:
		return HDCP_DDIA_REP_PRESENT | HDCP_DDIA_SHA1_M0;
	case PORT_B:
		return HDCP_DDIB_REP_PRESENT | HDCP_DDIB_SHA1_M0;
	case PORT_C:
		return HDCP_DDIC_REP_PRESENT | HDCP_DDIC_SHA1_M0;
	case PORT_D:
		return HDCP_DDID_REP_PRESENT | HDCP_DDID_SHA1_M0;
	case PORT_E:
		return HDCP_DDIE_REP_PRESENT | HDCP_DDIE_SHA1_M0;
	default:
		drm_err(&dev_priv->drm, "Unknown port %d\n", port);
		return -EINVAL;
	}
}

int intel_hdcp1_store_ksv_fifo(struct drm_connector *drm_connector,
			       u8 *ksv_fifo, u8 num_downstream, u8 *bstatus,
			       u32 *v_prime)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	enum port port = dig_port->base.port;
	u32 sha_text, sha_leftovers, rep_ctl;
	int ret, i, j, sha_idx;

	/* Process V' values from the receiver */
	for (i = 0; i < DRM_HDCP_V_PRIME_NUM_PARTS; i++)
		intel_de_write(dev_priv, HDCP_SHA_V_PRIME(i), v_prime[i]);

	/*
	 * We need to write the concatenation of all device KSVs, BINFO (DP) ||
	 * BSTATUS (HDMI), and M0 (which is added via HDCP_REP_CTL). This byte
	 * stream is written via the HDCP_SHA_TEXT register in 32-bit
	 * increments. Every 64 bytes, we need to write HDCP_REP_CTL again. This
	 * index will keep track of our progress through the 64 bytes as well as
	 * helping us work the 40-bit KSVs through our 32-bit register.
	 *
	 * NOTE: data passed via HDCP_SHA_TEXT should be big-endian
	 */
	sha_idx = 0;
	sha_text = 0;
	sha_leftovers = 0;
	rep_ctl = intel_hdcp_get_repeater_ctl(dev_priv, cpu_transcoder, port);
	intel_de_write(dev_priv, HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_32);
	for (i = 0; i < num_downstream; i++) {
		unsigned int sha_empty;
		u8 *ksv = &ksv_fifo[i * DRM_HDCP_KSV_LEN];

		/* Fill up the empty slots in sha_text and write it out */
		sha_empty = sizeof(sha_text) - sha_leftovers;
		for (j = 0; j < sha_empty; j++) {
			u8 off = ((sizeof(sha_text) - j - 1 - sha_leftovers) * 8);
			sha_text |= ksv[j] << off;
		}

		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;

		/* Programming guide writes this every 64 bytes */
		sha_idx += sizeof(sha_text);
		if (!(sha_idx % 64))
			intel_de_write(dev_priv, HDCP_REP_CTL,
				       rep_ctl | HDCP_SHA1_TEXT_32);

		/* Store the leftover bytes from the ksv in sha_text */
		sha_leftovers = DRM_HDCP_KSV_LEN - sha_empty;
		sha_text = 0;
		for (j = 0; j < sha_leftovers; j++)
			sha_text |= ksv[sha_empty + j] <<
					((sizeof(sha_text) - j - 1) * 8);

		/*
		 * If we still have room in sha_text for more data, continue.
		 * Otherwise, write it out immediately.
		 */
		if (sizeof(sha_text) > sha_leftovers)
			continue;

		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;
		sha_leftovers = 0;
		sha_text = 0;
		sha_idx += sizeof(sha_text);
	}

	/*
	 * We need to write BINFO/BSTATUS, and M0 now. Depending on how many
	 * bytes are leftover from the last ksv, we might be able to fit them
	 * all in sha_text (first 2 cases), or we might need to split them up
	 * into 2 writes (last 2 cases).
	 */
	if (sha_leftovers == 0) {
		/* Write 16 bits of text, 16 bits of M0 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_16);
		ret = intel_write_sha_text(dev_priv,
					   bstatus[0] << 8 | bstatus[1]);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 32 bits of M0 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_0);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 16 bits of M0 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_16);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

	} else if (sha_leftovers == 1) {
		/* Write 24 bits of text, 8 bits of M0 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_24);
		sha_text |= bstatus[0] << 16 | bstatus[1] << 8;
		/* Only 24-bits of data, must be in the LSB */
		sha_text = (sha_text & 0xffffff00) >> 8;
		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 32 bits of M0 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_0);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 24 bits of M0 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_8);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

	} else if (sha_leftovers == 2) {
		/* Write 32 bits of text */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_32);
		sha_text |= bstatus[0] << 8 | bstatus[1];
		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 64 bits of M0 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_0);
		for (i = 0; i < 2; i++) {
			ret = intel_write_sha_text(dev_priv, 0);
			if (ret < 0)
				return ret;
			sha_idx += sizeof(sha_text);
		}

		/*
		 * Terminate the SHA-1 stream by hand. For the other leftover
		 * cases this is appended by the hardware.
		 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_32);
		sha_text = DRM_HDCP_SHA1_TERMINATOR << 24;
		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);
	} else if (sha_leftovers == 3) {
		/* Write 32 bits of text (filled from LSB) */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_32);
		sha_text |= bstatus[0];
		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 8 bits of text (filled from LSB), 24 bits of M0 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_8);
		ret = intel_write_sha_text(dev_priv, bstatus[1]);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 32 bits of M0 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_0);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 8 bits of M0 */
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_24);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);
	} else {
		drm_dbg_kms(&dev_priv->drm, "Invalid number of leftovers %d\n",
			    sha_leftovers);
		return -EINVAL;
	}

	intel_de_write(dev_priv, HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_32);
	/* Fill up to 64-4 bytes with zeros (leave the last write for length) */
	while ((sha_idx % 64) < (64 - sizeof(sha_text))) {
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);
	}

	/*
	 * Last write gets the length of the concatenation in bits. That is:
	 *  - 5 bytes per device
	 *  - 10 bytes for BINFO/BSTATUS(2), M0(8)
	 */
	sha_text = (num_downstream * 5 + 10) * 8;
	ret = intel_write_sha_text(dev_priv, sha_text);
	if (ret < 0)
		return ret;

	/* Tell the HW we're done with the hash and wait for it to ACK */
	intel_de_write(dev_priv, HDCP_REP_CTL,
		       rep_ctl | HDCP_SHA1_COMPLETE_HASH);
	if (intel_de_wait_for_set(dev_priv, HDCP_REP_CTL,
				  HDCP_SHA1_COMPLETE, 1)) {
		drm_err(&dev_priv->drm, "Timed out waiting for SHA1 complete\n");
		return -ETIMEDOUT;
	}
	if (!(intel_de_read(dev_priv, HDCP_REP_CTL) & HDCP_SHA1_V_MATCH)) {
		drm_dbg_kms(&dev_priv->drm, "SHA-1 mismatch, HDCP failed\n");
		return -ENXIO;
	}

	return 0;
}

int intel_hdcp1_store_receiver_info(struct drm_connector *drm_connector,
				    u32 *ksv, u32 status, u8 caps,
				    bool repeater_present)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	enum port port = dig_port->base.port;

	intel_de_write(dev_priv, HDCP_BKSVLO(dev_priv, cpu_transcoder, port),
		       ksv[0]);
	intel_de_write(dev_priv, HDCP_BKSVHI(dev_priv, cpu_transcoder, port),
		       ksv[1]);

	if (repeater_present)
		intel_de_write(dev_priv, HDCP_REP_CTL,
			       intel_hdcp_get_repeater_ctl(dev_priv,
							   cpu_transcoder,
							   port));

	return 0;
}

int intel_hdcp1_read_an(struct drm_connector *drm_connector,
			struct drm_hdcp_an *an)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	enum port port = dig_port->base.port;
	int i;

	/* Initialize An with 2 random values and acquire it */
	for (i = 0; i < 2; i++)
		intel_de_write(dev_priv,
			       HDCP_ANINIT(dev_priv, cpu_transcoder, port),
			       get_random_u32());
	intel_de_write(dev_priv, HDCP_CONF(dev_priv, cpu_transcoder, port),
		       HDCP_CONF_CAPTURE_AN);

	/* Wait for An to be acquired */
	if (intel_de_wait_for_set(dev_priv,
				  HDCP_STATUS(dev_priv, cpu_transcoder, port),
				  HDCP_STATUS_AN_READY, 1)) {
		drm_err(&dev_priv->drm, "Timed out waiting for An\n");
		return -ETIMEDOUT;
	}

	an->words[0] = intel_de_read(dev_priv,
				     HDCP_ANLO(dev_priv, cpu_transcoder, port));
	an->words[1] = intel_de_read(dev_priv,
				     HDCP_ANHI(dev_priv, cpu_transcoder, port));

	return 0;
}

int intel_hdcp1_enable_encryption(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	enum port port = dig_port->base.port;

	intel_de_write(dev_priv, HDCP_CONF(dev_priv, cpu_transcoder, port),
		       HDCP_CONF_AUTH_AND_ENC);

	return 0;
}

int intel_hdcp1_wait_for_r0(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	enum port port = dig_port->base.port;

	/* Wait for R0 ready */
	if (wait_for((intel_de_read(dev_priv, HDCP_STATUS(dev_priv, cpu_transcoder, port))) &
		     (HDCP_STATUS_R0_READY | HDCP_STATUS_ENC), 1)) {
		drm_err(&dev_priv->drm, "Timed out waiting for R0 ready\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int intel_hdcp1_match_ri(struct drm_connector *drm_connector, u32 ri_prime)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	enum port port = dig_port->base.port;

	intel_de_write(dev_priv, HDCP_RPRIME(dev_priv, cpu_transcoder, port),
		       ri_prime);

	/* Wait for Ri prime match */
	if (wait_for(intel_de_read(dev_priv, HDCP_STATUS(dev_priv, cpu_transcoder, port)) &
		      (HDCP_STATUS_RI_MATCH | HDCP_STATUS_ENC), 1))
		return -ETIMEDOUT;

	return 0;
}

int intel_hdcp1_post_encryption(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	enum port port = dig_port->base.port;

	/* Wait for encryption confirmation */
	if (intel_de_wait_for_set(dev_priv,
				  HDCP_STATUS(dev_priv, cpu_transcoder, port),
				  HDCP_STATUS_ENC,
				  HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS)) {
		drm_err(&dev_priv->drm, "Timed out waiting for encryption\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int intel_hdcp1_disable(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder = hdcp->cpu_transcoder;
	u32 repeater_ctl;

	drm_dbg_kms(&dev_priv->drm, "[%s:%d] HDCP is being disabled...\n",
		    connector->base.name, connector->base.base.id);

	hdcp->hdcp_encrypted = false;
	intel_de_write(dev_priv, HDCP_CONF(dev_priv, cpu_transcoder, port), 0);
	if (intel_de_wait_for_clear(dev_priv,
				    HDCP_STATUS(dev_priv, cpu_transcoder, port),
				    ~0, HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS)) {
		drm_err(&dev_priv->drm,
			"Failed to disable HDCP, timeout clearing status\n");
		return -ETIMEDOUT;
	}

	repeater_ctl = intel_hdcp_get_repeater_ctl(dev_priv, cpu_transcoder,
						   port);
	intel_de_write(dev_priv, HDCP_REP_CTL,
		       intel_de_read(dev_priv, HDCP_REP_CTL) & ~repeater_ctl);

	return 0;
}

bool is_hdcp_supported(struct drm_i915_private *dev_priv, enum port port)
{
	return INTEL_INFO(dev_priv)->display.has_hdcp &&
			(DISPLAY_VER(dev_priv) >= 12 || port < PORT_E);
}

static int
hdcp2_prepare_ake_init(struct intel_connector *connector,
		       struct hdcp2_ake_init *ake_data)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->initiate_hdcp2_session(comp->mei_dev, data, ake_data);
	if (ret)
		drm_dbg_kms(&dev_priv->drm, "Prepare_ake_init failed. %d\n",
			    ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_verify_rx_cert_prepare_km(struct intel_connector *connector,
				struct hdcp2_ake_send_cert *rx_cert,
				bool *paired,
				struct hdcp2_ake_no_stored_km *ek_pub_km,
				size_t *msg_sz)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->verify_receiver_cert_prepare_km(comp->mei_dev, data,
							 rx_cert, paired,
							 ek_pub_km, msg_sz);
	if (ret < 0)
		drm_dbg_kms(&dev_priv->drm, "Verify rx_cert failed. %d\n",
			    ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int hdcp2_verify_hprime(struct intel_connector *connector,
			       struct hdcp2_ake_send_hprime *rx_hprime)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->verify_hprime(comp->mei_dev, data, rx_hprime);
	if (ret < 0)
		drm_dbg_kms(&dev_priv->drm, "Verify hprime failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_store_pairing_info(struct intel_connector *connector,
			 struct hdcp2_ake_send_pairing_info *pairing_info)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->store_pairing_info(comp->mei_dev, data, pairing_info);
	if (ret < 0)
		drm_dbg_kms(&dev_priv->drm, "Store pairing info failed. %d\n",
			    ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_prepare_lc_init(struct intel_connector *connector,
		      struct hdcp2_lc_init *lc_init)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->initiate_locality_check(comp->mei_dev, data, lc_init);
	if (ret < 0)
		drm_dbg_kms(&dev_priv->drm, "Prepare lc_init failed. %d\n",
			    ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_verify_lprime(struct intel_connector *connector,
		    struct hdcp2_lc_send_lprime *rx_lprime)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->verify_lprime(comp->mei_dev, data, rx_lprime);
	if (ret < 0)
		drm_dbg_kms(&dev_priv->drm, "Verify L_Prime failed. %d\n",
			    ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int hdcp2_prepare_skey(struct intel_connector *connector,
			      struct hdcp2_ske_send_eks *ske_data)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->get_session_key(comp->mei_dev, data, ske_data);
	if (ret < 0)
		drm_dbg_kms(&dev_priv->drm, "Get session key failed. %d\n",
			    ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_verify_rep_topology_prepare_ack(struct intel_connector *connector,
				      struct hdcp2_rep_send_receiverid_list
								*rep_topology,
				      struct hdcp2_rep_send_ack *rep_send_ack)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->repeater_check_flow_prepare_ack(comp->mei_dev, data,
							 rep_topology,
							 rep_send_ack);
	if (ret < 0)
		drm_dbg_kms(&dev_priv->drm,
			    "Verify rep topology failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_verify_mprime(struct intel_connector *connector,
		    struct hdcp2_rep_stream_ready *stream_ready)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->verify_mprime(comp->mei_dev, data, stream_ready);
	if (ret < 0)
		drm_dbg_kms(&dev_priv->drm, "Verify mprime failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int hdcp2_authenticate_port(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->enable_hdcp_authentication(comp->mei_dev, data);
	if (ret < 0)
		drm_dbg_kms(&dev_priv->drm, "Enable hdcp auth failed. %d\n",
			    ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int hdcp2_close_mei_session(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->close_hdcp_session(comp->mei_dev,
					     &dig_port->hdcp_port_data);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int hdcp2_deauthenticate_port(struct intel_connector *connector)
{
	return hdcp2_close_mei_session(connector);
}

/* Authentication flow starts from here */
static int hdcp2_authentication_key_exchange(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_ake_init ake_init;
		struct hdcp2_ake_send_cert send_cert;
		struct hdcp2_ake_no_stored_km no_stored_km;
		struct hdcp2_ake_send_hprime send_hprime;
		struct hdcp2_ake_send_pairing_info pairing_info;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	size_t size;
	int ret;

	/* Init for seq_num */
	hdcp->seq_num_v = 0;
	hdcp->seq_num_m = 0;

	ret = hdcp2_prepare_ake_init(connector, &msgs.ake_init);
	if (ret < 0)
		return ret;

	ret = shim->write_2_2_msg(dig_port, &msgs.ake_init,
				  sizeof(msgs.ake_init));
	if (ret < 0)
		return ret;

	ret = shim->read_2_2_msg(dig_port, HDCP_2_2_AKE_SEND_CERT,
				 &msgs.send_cert, sizeof(msgs.send_cert));
	if (ret < 0)
		return ret;

	if (msgs.send_cert.rx_caps[0] != HDCP_2_2_RX_CAPS_VERSION_VAL) {
		drm_dbg_kms(&dev_priv->drm, "cert.rx_caps dont claim HDCP2.2\n");
		return -EINVAL;
	}

	hdcp->is_repeater = HDCP_2_2_RX_REPEATER(msgs.send_cert.rx_caps[2]);

	if (drm_hdcp_check_ksvs_revoked(&dev_priv->drm,
					msgs.send_cert.cert_rx.receiver_id,
					1) > 0) {
		drm_err(&dev_priv->drm, "Receiver ID is revoked\n");
		return -EPERM;
	}

	/*
	 * Here msgs.no_stored_km will hold msgs corresponding to the km
	 * stored also.
	 */
	ret = hdcp2_verify_rx_cert_prepare_km(connector, &msgs.send_cert,
					      &hdcp->is_paired,
					      &msgs.no_stored_km, &size);
	if (ret < 0)
		return ret;

	ret = shim->write_2_2_msg(dig_port, &msgs.no_stored_km, size);
	if (ret < 0)
		return ret;

	ret = shim->read_2_2_msg(dig_port, HDCP_2_2_AKE_SEND_HPRIME,
				 &msgs.send_hprime, sizeof(msgs.send_hprime));
	if (ret < 0)
		return ret;

	ret = hdcp2_verify_hprime(connector, &msgs.send_hprime);
	if (ret < 0)
		return ret;

	if (!hdcp->is_paired) {
		/* Pairing is required */
		ret = shim->read_2_2_msg(dig_port,
					 HDCP_2_2_AKE_SEND_PAIRING_INFO,
					 &msgs.pairing_info,
					 sizeof(msgs.pairing_info));
		if (ret < 0)
			return ret;

		ret = hdcp2_store_pairing_info(connector, &msgs.pairing_info);
		if (ret < 0)
			return ret;
		hdcp->is_paired = true;
	}

	return 0;
}

static int hdcp2_locality_check(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_lc_init lc_init;
		struct hdcp2_lc_send_lprime send_lprime;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	int tries = HDCP2_LC_RETRY_CNT, ret, i;

	for (i = 0; i < tries; i++) {
		ret = hdcp2_prepare_lc_init(connector, &msgs.lc_init);
		if (ret < 0)
			continue;

		ret = shim->write_2_2_msg(dig_port, &msgs.lc_init,
				      sizeof(msgs.lc_init));
		if (ret < 0)
			continue;

		ret = shim->read_2_2_msg(dig_port,
					 HDCP_2_2_LC_SEND_LPRIME,
					 &msgs.send_lprime,
					 sizeof(msgs.send_lprime));
		if (ret < 0)
			continue;

		ret = hdcp2_verify_lprime(connector, &msgs.send_lprime);
		if (!ret)
			break;
	}

	return ret;
}

static int hdcp2_session_key_exchange(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	struct hdcp2_ske_send_eks send_eks;
	int ret;

	ret = hdcp2_prepare_skey(connector, &send_eks);
	if (ret < 0)
		return ret;

	ret = hdcp->shim->write_2_2_msg(dig_port, &send_eks,
					sizeof(send_eks));
	if (ret < 0)
		return ret;

	return 0;
}

static
int _hdcp2_propagate_stream_management_info(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_rep_stream_manage stream_manage;
		struct hdcp2_rep_stream_ready stream_ready;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	int ret, streams_size_delta, i;

	if (connector->hdcp.seq_num_m > HDCP_2_2_SEQ_NUM_MAX)
		return -ERANGE;

	/* Prepare RepeaterAuth_Stream_Manage msg */
	msgs.stream_manage.msg_id = HDCP_2_2_REP_STREAM_MANAGE;
	drm_hdcp_cpu_to_be24(msgs.stream_manage.seq_num_m, hdcp->seq_num_m);

	msgs.stream_manage.k = cpu_to_be16(data->k);

	for (i = 0; i < data->k; i++) {
		msgs.stream_manage.streams[i].stream_id = data->streams[i].stream_id;
		msgs.stream_manage.streams[i].stream_type = data->streams[i].stream_type;
	}

	streams_size_delta = (HDCP_2_2_MAX_CONTENT_STREAMS_CNT - data->k) *
				sizeof(struct hdcp2_streamid_type);
	/* Send it to Repeater */
	ret = shim->write_2_2_msg(dig_port, &msgs.stream_manage,
				  sizeof(msgs.stream_manage) - streams_size_delta);
	if (ret < 0)
		goto out;

	ret = shim->read_2_2_msg(dig_port, HDCP_2_2_REP_STREAM_READY,
				 &msgs.stream_ready, sizeof(msgs.stream_ready));
	if (ret < 0)
		goto out;

	data->seq_num_m = hdcp->seq_num_m;

	ret = hdcp2_verify_mprime(connector, &msgs.stream_ready);

out:
	hdcp->seq_num_m++;

	return ret;
}

static
int hdcp2_authenticate_repeater_topology(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_rep_send_receiverid_list recvid_list;
		struct hdcp2_rep_send_ack rep_ack;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	u32 seq_num_v, device_cnt;
	u8 *rx_info;
	int ret;

	ret = shim->read_2_2_msg(dig_port, HDCP_2_2_REP_SEND_RECVID_LIST,
				 &msgs.recvid_list, sizeof(msgs.recvid_list));
	if (ret < 0)
		return ret;

	rx_info = msgs.recvid_list.rx_info;

	if (HDCP_2_2_MAX_CASCADE_EXCEEDED(rx_info[1]) ||
	    HDCP_2_2_MAX_DEVS_EXCEEDED(rx_info[1])) {
		drm_dbg_kms(&dev_priv->drm, "Topology Max Size Exceeded\n");
		return -EINVAL;
	}

	/*
	 * MST topology is not Type 1 capable if it contains a downstream
	 * device that is only HDCP 1.x or Legacy HDCP 2.0/2.1 compliant.
	 */
	dig_port->hdcp_mst_type1_capable =
		!HDCP_2_2_HDCP1_DEVICE_CONNECTED(rx_info[1]) &&
		!HDCP_2_2_HDCP_2_0_REP_CONNECTED(rx_info[1]);

	/* Converting and Storing the seq_num_v to local variable as DWORD */
	seq_num_v =
		drm_hdcp_be24_to_cpu((const u8 *)msgs.recvid_list.seq_num_v);

	if (!hdcp->hdcp2_encrypted && seq_num_v) {
		drm_dbg_kms(&dev_priv->drm,
			    "Non zero Seq_num_v at first RecvId_List msg\n");
		return -EINVAL;
	}

	if (seq_num_v < hdcp->seq_num_v) {
		/* Roll over of the seq_num_v from repeater. Reauthenticate. */
		drm_dbg_kms(&dev_priv->drm, "Seq_num_v roll over.\n");
		return -EINVAL;
	}

	device_cnt = (HDCP_2_2_DEV_COUNT_HI(rx_info[0]) << 4 |
		      HDCP_2_2_DEV_COUNT_LO(rx_info[1]));
	if (drm_hdcp_check_ksvs_revoked(&dev_priv->drm,
					msgs.recvid_list.receiver_ids,
					device_cnt) > 0) {
		drm_err(&dev_priv->drm, "Revoked receiver ID(s) is in list\n");
		return -EPERM;
	}

	ret = hdcp2_verify_rep_topology_prepare_ack(connector,
						    &msgs.recvid_list,
						    &msgs.rep_ack);
	if (ret < 0)
		return ret;

	hdcp->seq_num_v = seq_num_v;
	ret = shim->write_2_2_msg(dig_port, &msgs.rep_ack,
				  sizeof(msgs.rep_ack));
	if (ret < 0)
		return ret;

	return 0;
}

static int hdcp2_authenticate_sink(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	int ret;

	ret = hdcp2_authentication_key_exchange(connector);
	if (ret < 0) {
		drm_dbg_kms(&i915->drm, "AKE Failed. Err : %d\n", ret);
		return ret;
	}

	ret = hdcp2_locality_check(connector);
	if (ret < 0) {
		drm_dbg_kms(&i915->drm,
			    "Locality Check failed. Err : %d\n", ret);
		return ret;
	}

	ret = hdcp2_session_key_exchange(connector);
	if (ret < 0) {
		drm_dbg_kms(&i915->drm, "SKE Failed. Err : %d\n", ret);
		return ret;
	}

	if (shim->config_stream_type) {
		ret = shim->config_stream_type(dig_port,
					       hdcp->is_repeater,
					       hdcp->content_type);
		if (ret < 0)
			return ret;
	}

	if (hdcp->is_repeater) {
		ret = hdcp2_authenticate_repeater_topology(connector);
		if (ret < 0) {
			drm_dbg_kms(&i915->drm,
				    "Repeater Auth Failed. Err: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static int hdcp2_enable_stream_encryption(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum transcoder cpu_transcoder = hdcp->cpu_transcoder;
	enum port port = dig_port->base.port;
	int ret = 0;

	if (!(intel_de_read(dev_priv, HDCP2_STATUS(dev_priv, cpu_transcoder, port)) &
			    LINK_ENCRYPTION_STATUS)) {
		drm_err(&dev_priv->drm, "[%s:%d] HDCP 2.2 Link is not encrypted\n",
			connector->base.name, connector->base.base.id);
		ret = -EPERM;
		goto link_recover;
	}

	if (hdcp->shim->stream_2_2_encryption) {
		ret = hdcp->shim->stream_2_2_encryption(connector, true);
		if (ret) {
			drm_err(&dev_priv->drm, "[%s:%d] Failed to enable HDCP 2.2 stream enc\n",
				connector->base.name, connector->base.base.id);
			return ret;
		}
		drm_dbg_kms(&dev_priv->drm, "HDCP 2.2 transcoder: %s stream encrypted\n",
			    transcoder_name(hdcp->stream_transcoder));
	}

	return 0;

link_recover:
	if (hdcp2_deauthenticate_port(connector) < 0)
		drm_dbg_kms(&dev_priv->drm, "Port deauth failed.\n");

	dig_port->hdcp_auth_status = false;
	data->k = 0;

	return ret;
}

static int hdcp2_enable_encryption(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder = hdcp->cpu_transcoder;
	int ret;

	drm_WARN_ON(&dev_priv->drm,
		    intel_de_read(dev_priv, HDCP2_STATUS(dev_priv, cpu_transcoder, port)) &
		    LINK_ENCRYPTION_STATUS);
	if (hdcp->shim->toggle_signalling) {
		ret = hdcp->shim->toggle_signalling(dig_port, cpu_transcoder,
						    true);
		if (ret) {
			drm_err(&dev_priv->drm,
				"Failed to enable HDCP signalling. %d\n",
				ret);
			return ret;
		}
	}

	if (intel_de_read(dev_priv, HDCP2_STATUS(dev_priv, cpu_transcoder, port)) &
	    LINK_AUTH_STATUS) {
		/* Link is Authenticated. Now set for Encryption */
		intel_de_write(dev_priv,
			       HDCP2_CTL(dev_priv, cpu_transcoder, port),
			       intel_de_read(dev_priv, HDCP2_CTL(dev_priv, cpu_transcoder, port)) | CTL_LINK_ENCRYPTION_REQ);
	}

	ret = intel_de_wait_for_set(dev_priv,
				    HDCP2_STATUS(dev_priv, cpu_transcoder,
						 port),
				    LINK_ENCRYPTION_STATUS,
				    HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS);
	dig_port->hdcp_auth_status = true;

	return ret;
}

static int hdcp2_disable_encryption(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder = hdcp->cpu_transcoder;
	int ret;

	drm_WARN_ON(&dev_priv->drm, !(intel_de_read(dev_priv, HDCP2_STATUS(dev_priv, cpu_transcoder, port)) &
				      LINK_ENCRYPTION_STATUS));

	intel_de_write(dev_priv, HDCP2_CTL(dev_priv, cpu_transcoder, port),
		       intel_de_read(dev_priv, HDCP2_CTL(dev_priv, cpu_transcoder, port)) & ~CTL_LINK_ENCRYPTION_REQ);

	ret = intel_de_wait_for_clear(dev_priv,
				      HDCP2_STATUS(dev_priv, cpu_transcoder,
						   port),
				      LINK_ENCRYPTION_STATUS,
				      HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS);
	if (ret == -ETIMEDOUT)
		drm_dbg_kms(&dev_priv->drm, "Disable Encryption Timedout");

	if (hdcp->shim->toggle_signalling) {
		ret = hdcp->shim->toggle_signalling(dig_port, cpu_transcoder,
						    false);
		if (ret) {
			drm_err(&dev_priv->drm,
				"Failed to disable HDCP signalling. %d\n",
				ret);
			return ret;
		}
	}

	return ret;
}

static int
hdcp2_propagate_stream_management_info(struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	int i, tries = 3, ret;

	if (!connector->hdcp.is_repeater)
		return 0;

	for (i = 0; i < tries; i++) {
		ret = _hdcp2_propagate_stream_management_info(connector);
		if (!ret)
			break;

		/* Lets restart the auth incase of seq_num_m roll over */
		if (connector->hdcp.seq_num_m > HDCP_2_2_SEQ_NUM_MAX) {
			drm_dbg_kms(&i915->drm,
				    "seq_num_m roll over.(%d)\n", ret);
			break;
		}

		drm_dbg_kms(&i915->drm,
			    "HDCP2 stream management %d of %d Failed.(%d)\n",
			    i + 1, tries, ret);
	}

	return ret;
}

static int hdcp2_authenticate_and_encrypt(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	int ret = 0, i, tries = 3;

	for (i = 0; i < tries && !dig_port->hdcp_auth_status; i++) {
		ret = hdcp2_authenticate_sink(connector);
		if (!ret) {
			ret = intel_hdcp_prepare_streams(connector);
			if (ret) {
				drm_dbg_kms(&i915->drm,
					    "Prepare streams failed.(%d)\n",
					    ret);
				break;
			}

			ret = hdcp2_propagate_stream_management_info(connector);
			if (ret) {
				drm_dbg_kms(&i915->drm,
					    "Stream management failed.(%d)\n",
					    ret);
				break;
			}

			ret = hdcp2_authenticate_port(connector);
			if (!ret)
				break;
			drm_dbg_kms(&i915->drm, "HDCP2 port auth failed.(%d)\n",
				    ret);
		}

		/* Clearing the mei hdcp session */
		drm_dbg_kms(&i915->drm, "HDCP2.2 Auth %d of %d Failed.(%d)\n",
			    i + 1, tries, ret);
		if (hdcp2_deauthenticate_port(connector) < 0)
			drm_dbg_kms(&i915->drm, "Port deauth failed.\n");
	}

	if (!ret && !dig_port->hdcp_auth_status) {
		/*
		 * Ensuring the required 200mSec min time interval between
		 * Session Key Exchange and encryption.
		 */
		msleep(HDCP_2_2_DELAY_BEFORE_ENCRYPTION_EN);
		ret = hdcp2_enable_encryption(connector);
		if (ret < 0) {
			drm_dbg_kms(&i915->drm,
				    "Encryption Enable Failed.(%d)\n", ret);
			if (hdcp2_deauthenticate_port(connector) < 0)
				drm_dbg_kms(&i915->drm, "Port deauth failed.\n");
		}
	}

	if (!ret)
		ret = hdcp2_enable_stream_encryption(connector);

	return ret;
}

int intel_hdcp2_enable(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	drm_dbg_kms(&i915->drm, "[%s:%d] HDCP2.2 is being enabled. Type: %d\n",
		    connector->base.name, connector->base.base.id,
		    hdcp->content_type);

	ret = hdcp2_authenticate_and_encrypt(connector);
	if (ret) {
		drm_dbg_kms(&i915->drm, "HDCP2 Type%d  Enabling Failed. (%d)\n",
			    hdcp->content_type, ret);
		return ret;
	}

	drm_dbg_kms(&i915->drm, "[%s:%d] HDCP2.2 is enabled. Type %d\n",
		    connector->base.name, connector->base.base.id,
		    hdcp->content_type);

	hdcp->hdcp2_encrypted = true;
	return 0;
}

static int
_intel_hdcp2_disable(struct intel_connector *connector, bool hdcp2_link_recovery)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	drm_dbg_kms(&i915->drm, "[%s:%d] HDCP2.2 is being Disabled\n",
		    connector->base.name, connector->base.base.id);

	if (hdcp->shim->stream_2_2_encryption) {
		ret = hdcp->shim->stream_2_2_encryption(connector, false);
		if (ret) {
			drm_err(&i915->drm, "[%s:%d] Failed to disable HDCP 2.2 stream enc\n",
				connector->base.name, connector->base.base.id);
			return ret;
		}
		drm_dbg_kms(&i915->drm, "HDCP 2.2 transcoder: %s stream encryption disabled\n",
			    transcoder_name(hdcp->stream_transcoder));

		if (dig_port->num_hdcp_streams > 0 && !hdcp2_link_recovery)
			return 0;
	}

	ret = hdcp2_disable_encryption(connector);

	if (hdcp2_deauthenticate_port(connector) < 0)
		drm_dbg_kms(&i915->drm, "Port deauth failed.\n");

	connector->hdcp.hdcp2_encrypted = false;
	dig_port->hdcp_auth_status = false;
	data->k = 0;

	return ret;
}

int intel_hdcp2_disable(struct drm_connector *drm_connector)
{
	return _intel_hdcp2_disable(to_intel_connector(drm_connector), false);
}

/* Implements the Link Integrity Check for HDCP2.2 */
int intel_hdcp2_check_link(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder;
	int ret = 0;

	cpu_transcoder = hdcp->cpu_transcoder;

	/* hdcp2_check_link is expected only when HDCP2.2 is Enabled */
	if (!hdcp->hdcp2_encrypted)
		return -EINVAL;

	if (drm_WARN_ON(&dev_priv->drm,
			!intel_hdcp2_in_use(dev_priv, cpu_transcoder, port))) {
		drm_err(&dev_priv->drm,
			"HDCP2.2 link stopped the encryption, %x\n",
			intel_de_read(dev_priv, HDCP2_STATUS(dev_priv, cpu_transcoder, port)));
		return -ENXIO;
	}

	ret = hdcp->shim->check_2_2_link(dig_port, connector);
	if (ret == HDCP_LINK_PROTECTED)
		return 0;

	if (ret == HDCP_TOPOLOGY_CHANGE) {
		drm_dbg_kms(&dev_priv->drm,
			    "HDCP2.2 Downstream topology change\n");
		ret = hdcp2_authenticate_repeater_topology(connector);
		if (!ret)
			return 0;

		drm_dbg_kms(&dev_priv->drm,
			    "[%s:%d] Repeater topology auth failed.(%d)\n",
			    connector->base.name, connector->base.base.id,
			    ret);
	}

	return ret;
}

static int i915_hdcp_component_bind(struct device *i915_kdev,
				    struct device *mei_kdev, void *data)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(i915_kdev);

	drm_dbg(&dev_priv->drm, "I915 HDCP comp bind\n");
	mutex_lock(&dev_priv->hdcp_comp_mutex);
	dev_priv->hdcp_master = (struct i915_hdcp_comp_master *)data;
	dev_priv->hdcp_master->mei_dev = mei_kdev;
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return 0;
}

static void i915_hdcp_component_unbind(struct device *i915_kdev,
				       struct device *mei_kdev, void *data)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(i915_kdev);

	drm_dbg(&dev_priv->drm, "I915 HDCP comp unbind\n");
	mutex_lock(&dev_priv->hdcp_comp_mutex);
	dev_priv->hdcp_master = NULL;
	mutex_unlock(&dev_priv->hdcp_comp_mutex);
}

static const struct component_ops i915_hdcp_component_ops = {
	.bind   = i915_hdcp_component_bind,
	.unbind = i915_hdcp_component_unbind,
};

static enum mei_fw_ddi intel_get_mei_fw_ddi_index(enum port port)
{
	switch (port) {
	case PORT_A:
		return MEI_DDI_A;
	case PORT_B ... PORT_F:
		return (enum mei_fw_ddi)port;
	default:
		return MEI_DDI_INVALID_PORT;
	}
}

static enum mei_fw_tc intel_get_mei_fw_tc(enum transcoder cpu_transcoder)
{
	switch (cpu_transcoder) {
	case TRANSCODER_A ... TRANSCODER_D:
		return (enum mei_fw_tc)(cpu_transcoder | 0x10);
	default: /* eDP, DSI TRANSCODERS are non HDCP capable */
		return MEI_INVALID_TRANSCODER;
	}
}

int intel_hdcp_setup(struct drm_connector *connector,
		     struct drm_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(intel_connector);
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct intel_crtc_state *pipe_config;
	struct intel_hdcp *hdcp = &intel_connector->hdcp;
	int ret = 0;

	if (!intel_connector->encoder) {
		drm_err(&dev_priv->drm, "[%s:%d] encoder is not initialized\n",
			connector->name, connector->base.id);
		return -ENODEV;
	}

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	pipe_config = to_intel_crtc_state(crtc_state);

	if (intel_crtc_has_type(pipe_config, INTEL_OUTPUT_DP_MST)) {
		hdcp->cpu_transcoder = pipe_config->mst_master_transcoder;
		hdcp->stream_transcoder = pipe_config->cpu_transcoder;
	} else {
		hdcp->cpu_transcoder = pipe_config->cpu_transcoder;
		hdcp->stream_transcoder = INVALID_TRANSCODER;
	}

	if (DISPLAY_VER(dev_priv) >= 12)
		dig_port->hdcp_port_data.fw_tc = intel_get_mei_fw_tc(hdcp->cpu_transcoder);

	return ret;
}

static int initialize_hdcp_port_data(struct intel_connector *connector,
				     struct intel_digital_port *dig_port,
				     const struct intel_hdcp_shim *shim)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;

	if (DISPLAY_VER(dev_priv) < 12)
		data->fw_ddi = intel_get_mei_fw_ddi_index(port);
	else
		/*
		 * As per ME FW API expectation, for GEN 12+, fw_ddi is filled
		 * with zero(INVALID PORT index).
		 */
		data->fw_ddi = MEI_DDI_INVALID_PORT;

	/*
	 * As associated transcoder is set and modified at modeset, here fw_tc
	 * is initialized to zero (invalid transcoder index). This will be
	 * retained for <Gen12 forever.
	 */
	data->fw_tc = MEI_INVALID_TRANSCODER;

	data->port_type = (u8)HDCP_PORT_TYPE_INTEGRATED;
	data->protocol = (u8)shim->protocol;

	if (!data->streams)
		data->streams = kcalloc(INTEL_NUM_PIPES(dev_priv),
					sizeof(struct hdcp2_streamid_type),
					GFP_KERNEL);
	if (!data->streams) {
		drm_err(&dev_priv->drm, "Out of Memory\n");
		return -ENOMEM;
	}
	/* For SST */
	data->streams[0].stream_id = 0;
	data->streams[0].stream_type = hdcp->content_type;

	return 0;
}

static bool is_hdcp2_supported(struct drm_i915_private *dev_priv)
{
	if (!IS_ENABLED(CONFIG_INTEL_MEI_HDCP))
		return false;

	return (DISPLAY_VER(dev_priv) >= 10 ||
		IS_KABYLAKE(dev_priv) ||
		IS_COFFEELAKE(dev_priv) ||
		IS_COMETLAKE(dev_priv));
}

void intel_hdcp_component_init(struct drm_i915_private *dev_priv)
{
	int ret;

	if (!is_hdcp2_supported(dev_priv))
		return;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	drm_WARN_ON(&dev_priv->drm, dev_priv->hdcp_comp_added);

	dev_priv->hdcp_comp_added = true;
	mutex_unlock(&dev_priv->hdcp_comp_mutex);
	ret = component_add_typed(dev_priv->drm.dev, &i915_hdcp_component_ops,
				  I915_COMPONENT_HDCP);
	if (ret < 0) {
		drm_dbg_kms(&dev_priv->drm, "Failed at component add(%d)\n",
			    ret);
		mutex_lock(&dev_priv->hdcp_comp_mutex);
		dev_priv->hdcp_comp_added = false;
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return;
	}
}

static void intel_hdcp2_init(struct intel_connector *connector,
			     struct intel_digital_port *dig_port,
			     const struct intel_hdcp_shim *shim)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	ret = initialize_hdcp_port_data(connector, dig_port, shim);
	if (ret) {
		drm_dbg_kms(&i915->drm, "Mei hdcp data init failed\n");
		return;
	}

	hdcp->hdcp2_supported = true;
}

int intel_hdcp_init(struct intel_connector *connector,
		    struct intel_digital_port *dig_port,
		    const struct intel_hdcp_shim *shim)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;

	if (!shim)
		return -EINVAL;

	if (is_hdcp2_supported(dev_priv))
		intel_hdcp2_init(connector, dig_port, shim);

	hdcp->shim = shim;
	init_waitqueue_head(&hdcp->cp_irq_queue);

	return 0;
}

void intel_hdcp_component_fini(struct drm_i915_private *dev_priv)
{
	mutex_lock(&dev_priv->hdcp_comp_mutex);
	if (!dev_priv->hdcp_comp_added) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return;
	}

	dev_priv->hdcp_comp_added = false;
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	component_del(dev_priv->drm.dev, &i915_hdcp_component_ops);
}

void intel_hdcp_cleanup(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;

	if (!hdcp->shim)
		return;

	drm_hdcp_helper_destroy(connector->hdcp_helper_data);
	hdcp->shim = NULL;
}

/* Handles the CP_IRQ raised from the DP HDCP sink */
void intel_hdcp_handle_cp_irq(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;

	if (!hdcp->shim)
		return;

	atomic_inc(&connector->hdcp.cp_irq_count);
	wake_up_all(&connector->hdcp.cp_irq_queue);

	drm_hdcp_helper_schedule_hdcp_check(connector->hdcp_helper_data);
}
