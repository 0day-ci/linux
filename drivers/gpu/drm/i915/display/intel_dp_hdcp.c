/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2020 Google, Inc.
 *
 * Authors:
 * Sean Paul <seanpaul@chromium.org>
 */

#include <drm/drm_dp_helper.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_hdcp.h>
#include <drm/drm_print.h>

#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_hdcp.h"
#include "intel_hdcp.h"

static unsigned int transcoder_to_stream_enc_status(enum transcoder cpu_transcoder)
{
	u32 stream_enc_mask;

	switch (cpu_transcoder) {
	case TRANSCODER_A:
		stream_enc_mask = HDCP_STATUS_STREAM_A_ENC;
		break;
	case TRANSCODER_B:
		stream_enc_mask = HDCP_STATUS_STREAM_B_ENC;
		break;
	case TRANSCODER_C:
		stream_enc_mask = HDCP_STATUS_STREAM_C_ENC;
		break;
	case TRANSCODER_D:
		stream_enc_mask = HDCP_STATUS_STREAM_D_ENC;
		break;
	default:
		stream_enc_mask = 0;
	}

	return stream_enc_mask;
}

static void intel_dp_hdcp_wait_for_cp_irq(struct intel_hdcp *hdcp, int timeout)
{
	long ret;

#define C (hdcp->cp_irq_count_cached != atomic_read(&hdcp->cp_irq_count))
	ret = wait_event_interruptible_timeout(hdcp->cp_irq_queue, C,
					       msecs_to_jiffies(timeout));

	if (!ret)
		DRM_DEBUG_KMS("Timedout at waiting for CP_IRQ\n");
}

static
int intel_dp_hdcp1_send_an_aksv(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct drm_hdcp_an an;
	u8 aksv[DRM_HDCP_KSV_LEN] = {};
	ssize_t dpcd_ret;
	int ret;

	/* Output An first, that's easy */
	ret = intel_hdcp1_read_an(drm_connector, &an);
	if (ret)
		return ret;

	dpcd_ret = drm_dp_dpcd_write(&dig_port->dp.aux, DP_AUX_HDCP_AN,
				     an.bytes, DRM_HDCP_AN_LEN);
	if (dpcd_ret != DRM_HDCP_AN_LEN) {
		drm_dbg_kms(&i915->drm,
			    "Failed to write An over DP/AUX (%zd)\n",
			    dpcd_ret);
		return dpcd_ret >= 0 ? -EIO : dpcd_ret;
	}

	/*
	 * Since Aksv is Oh-So-Secret, we can't access it in software. So we
	 * send an empty buffer of the correct length through the DP helpers. On
	 * the other side, in the transfer hook, we'll generate a flag based on
	 * the destination address which will tickle the hardware to output the
	 * Aksv on our behalf after the header is sent.
	 */
	dpcd_ret = drm_dp_dpcd_write(&dig_port->dp.aux, DP_AUX_HDCP_AKSV,
				     aksv, DRM_HDCP_KSV_LEN);
	if (dpcd_ret != DRM_HDCP_KSV_LEN) {
		drm_dbg_kms(&i915->drm,
			    "Failed to write Aksv over DP/AUX (%zd)\n",
			    dpcd_ret);
		return dpcd_ret >= 0 ? -EIO : dpcd_ret;
	}
	return 0;
}

static
int intel_dp_hdcp_toggle_signalling(struct intel_digital_port *dig_port,
				    enum transcoder cpu_transcoder,
				    bool enable)
{
	/* Not used for single stream DisplayPort setups */
	return 0;
}

struct hdcp2_dp_errata_stream_type {
	u8	msg_id;
	u8	stream_type;
} __packed;

struct hdcp2_dp_msg_data {
	u8 msg_id;
	u32 offset;
	bool msg_detectable;
	u32 timeout;
	u32 timeout2; /* Added for non_paired situation */
	/* Timeout to read entire msg */
	u32 msg_read_timeout;
};

static const struct hdcp2_dp_msg_data hdcp2_dp_msg_data[] = {
	{ HDCP_2_2_AKE_INIT, DP_HDCP_2_2_AKE_INIT_OFFSET, false, 0, 0, 0},
	{ HDCP_2_2_AKE_SEND_CERT, DP_HDCP_2_2_AKE_SEND_CERT_OFFSET,
	  false, HDCP_2_2_CERT_TIMEOUT_MS, 0, HDCP_2_2_DP_CERT_READ_TIMEOUT_MS},
	{ HDCP_2_2_AKE_NO_STORED_KM, DP_HDCP_2_2_AKE_NO_STORED_KM_OFFSET,
	  false, 0, 0, 0 },
	{ HDCP_2_2_AKE_STORED_KM, DP_HDCP_2_2_AKE_STORED_KM_OFFSET,
	  false, 0, 0, 0 },
	{ HDCP_2_2_AKE_SEND_HPRIME, DP_HDCP_2_2_AKE_SEND_HPRIME_OFFSET,
	  true, HDCP_2_2_HPRIME_PAIRED_TIMEOUT_MS,
	  HDCP_2_2_HPRIME_NO_PAIRED_TIMEOUT_MS, HDCP_2_2_DP_HPRIME_READ_TIMEOUT_MS},
	{ HDCP_2_2_AKE_SEND_PAIRING_INFO,
	  DP_HDCP_2_2_AKE_SEND_PAIRING_INFO_OFFSET, true,
	  HDCP_2_2_PAIRING_TIMEOUT_MS, 0, HDCP_2_2_DP_PAIRING_READ_TIMEOUT_MS },
	{ HDCP_2_2_LC_INIT, DP_HDCP_2_2_LC_INIT_OFFSET, false, 0, 0, 0 },
	{ HDCP_2_2_LC_SEND_LPRIME, DP_HDCP_2_2_LC_SEND_LPRIME_OFFSET,
	  false, HDCP_2_2_DP_LPRIME_TIMEOUT_MS, 0, 0 },
	{ HDCP_2_2_SKE_SEND_EKS, DP_HDCP_2_2_SKE_SEND_EKS_OFFSET, false,
	  0, 0, 0 },
	{ HDCP_2_2_REP_SEND_RECVID_LIST,
	  DP_HDCP_2_2_REP_SEND_RECVID_LIST_OFFSET, true,
	  HDCP_2_2_RECVID_LIST_TIMEOUT_MS, 0, 0 },
	{ HDCP_2_2_REP_SEND_ACK, DP_HDCP_2_2_REP_SEND_ACK_OFFSET, false,
	  0, 0, 0 },
	{ HDCP_2_2_REP_STREAM_MANAGE,
	  DP_HDCP_2_2_REP_STREAM_MANAGE_OFFSET, false,
	  0, 0, 0},
	{ HDCP_2_2_REP_STREAM_READY, DP_HDCP_2_2_REP_STREAM_READY_OFFSET,
	  false, HDCP_2_2_STREAM_READY_TIMEOUT_MS, 0, 0 },
/* local define to shovel this through the write_2_2 interface */
#define HDCP_2_2_ERRATA_DP_STREAM_TYPE	50
	{ HDCP_2_2_ERRATA_DP_STREAM_TYPE,
	  DP_HDCP_2_2_REG_STREAM_TYPE_OFFSET, false,
	  0, 0 },
};

static int
intel_dp_hdcp2_read_rx_status(struct intel_digital_port *dig_port,
			      u8 *rx_status)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	ssize_t ret;

	ret = drm_dp_dpcd_read(&dig_port->dp.aux,
			       DP_HDCP_2_2_REG_RXSTATUS_OFFSET, rx_status,
			       HDCP_2_2_DP_RXSTATUS_LEN);
	if (ret != HDCP_2_2_DP_RXSTATUS_LEN) {
		drm_dbg_kms(&i915->drm,
			    "Read bstatus from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}

	return 0;
}

static
int hdcp2_detect_msg_availability(struct intel_digital_port *dig_port,
				  u8 msg_id, bool *msg_ready)
{
	u8 rx_status;
	int ret;

	*msg_ready = false;
	ret = intel_dp_hdcp2_read_rx_status(dig_port, &rx_status);
	if (ret < 0)
		return ret;

	switch (msg_id) {
	case HDCP_2_2_AKE_SEND_HPRIME:
		if (HDCP_2_2_DP_RXSTATUS_H_PRIME(rx_status))
			*msg_ready = true;
		break;
	case HDCP_2_2_AKE_SEND_PAIRING_INFO:
		if (HDCP_2_2_DP_RXSTATUS_PAIRING(rx_status))
			*msg_ready = true;
		break;
	case HDCP_2_2_REP_SEND_RECVID_LIST:
		if (HDCP_2_2_DP_RXSTATUS_READY(rx_status))
			*msg_ready = true;
		break;
	default:
		DRM_ERROR("Unidentified msg_id: %d\n", msg_id);
		return -EINVAL;
	}

	return 0;
}

static ssize_t
intel_dp_hdcp2_wait_for_msg(struct intel_digital_port *dig_port,
			    const struct hdcp2_dp_msg_data *hdcp2_msg_data)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_dp *dp = &dig_port->dp;
	struct intel_hdcp *hdcp = &dp->attached_connector->hdcp;
	u8 msg_id = hdcp2_msg_data->msg_id;
	int ret, timeout;
	bool msg_ready = false;

	if (msg_id == HDCP_2_2_AKE_SEND_HPRIME && !hdcp->is_paired)
		timeout = hdcp2_msg_data->timeout2;
	else
		timeout = hdcp2_msg_data->timeout;

	/*
	 * There is no way to detect the CERT, LPRIME and STREAM_READY
	 * availability. So Wait for timeout and read the msg.
	 */
	if (!hdcp2_msg_data->msg_detectable) {
		mdelay(timeout);
		ret = 0;
	} else {
		/*
		 * As we want to check the msg availability at timeout, Ignoring
		 * the timeout at wait for CP_IRQ.
		 */
		intel_dp_hdcp_wait_for_cp_irq(hdcp, timeout);
		ret = hdcp2_detect_msg_availability(dig_port,
						    msg_id, &msg_ready);
		if (!msg_ready)
			ret = -ETIMEDOUT;
	}

	if (ret)
		drm_dbg_kms(&i915->drm,
			    "msg_id %d, ret %d, timeout(mSec): %d\n",
			    hdcp2_msg_data->msg_id, ret, timeout);

	return ret;
}

static const struct hdcp2_dp_msg_data *get_hdcp2_dp_msg_data(u8 msg_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hdcp2_dp_msg_data); i++)
		if (hdcp2_dp_msg_data[i].msg_id == msg_id)
			return &hdcp2_dp_msg_data[i];

	return NULL;
}

static
int intel_dp_hdcp2_write_msg(struct intel_digital_port *dig_port,
			     void *buf, size_t size)
{
	unsigned int offset;
	u8 *byte = buf;
	ssize_t ret, bytes_to_write, len;
	const struct hdcp2_dp_msg_data *hdcp2_msg_data;

	hdcp2_msg_data = get_hdcp2_dp_msg_data(*byte);
	if (!hdcp2_msg_data)
		return -EINVAL;

	offset = hdcp2_msg_data->offset;

	/* No msg_id in DP HDCP2.2 msgs */
	bytes_to_write = size - 1;
	byte++;

	while (bytes_to_write) {
		len = bytes_to_write > DP_AUX_MAX_PAYLOAD_BYTES ?
				DP_AUX_MAX_PAYLOAD_BYTES : bytes_to_write;

		ret = drm_dp_dpcd_write(&dig_port->dp.aux,
					offset, (void *)byte, len);
		if (ret < 0)
			return ret;

		bytes_to_write -= ret;
		byte += ret;
		offset += ret;
	}

	return size;
}

static
ssize_t get_receiver_id_list_rx_info(struct intel_digital_port *dig_port, u32 *dev_cnt, u8 *byte)
{
	ssize_t ret;
	u8 *rx_info = byte;

	ret = drm_dp_dpcd_read(&dig_port->dp.aux,
			       DP_HDCP_2_2_REG_RXINFO_OFFSET,
			       (void *)rx_info, HDCP_2_2_RXINFO_LEN);
	if (ret != HDCP_2_2_RXINFO_LEN)
		return ret >= 0 ? -EIO : ret;

	*dev_cnt = (HDCP_2_2_DEV_COUNT_HI(rx_info[0]) << 4 |
		   HDCP_2_2_DEV_COUNT_LO(rx_info[1]));

	if (*dev_cnt > HDCP_2_2_MAX_DEVICE_COUNT)
		*dev_cnt = HDCP_2_2_MAX_DEVICE_COUNT;

	return ret;
}

static
int intel_dp_hdcp2_read_msg(struct intel_digital_port *dig_port,
			    u8 msg_id, void *buf, size_t size)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_dp *dp = &dig_port->dp;
	struct intel_hdcp *hdcp = &dp->attached_connector->hdcp;
	unsigned int offset;
	u8 *byte = buf;
	ssize_t ret, bytes_to_recv, len;
	const struct hdcp2_dp_msg_data *hdcp2_msg_data;
	ktime_t msg_end = ktime_set(0, 0);
	bool msg_expired;
	u32 dev_cnt;

	hdcp2_msg_data = get_hdcp2_dp_msg_data(msg_id);
	if (!hdcp2_msg_data)
		return -EINVAL;
	offset = hdcp2_msg_data->offset;

	ret = intel_dp_hdcp2_wait_for_msg(dig_port, hdcp2_msg_data);
	if (ret < 0)
		return ret;

	hdcp->cp_irq_count_cached = atomic_read(&hdcp->cp_irq_count);

	/* DP adaptation msgs has no msg_id */
	byte++;

	if (msg_id == HDCP_2_2_REP_SEND_RECVID_LIST) {
		ret = get_receiver_id_list_rx_info(dig_port, &dev_cnt, byte);
		if (ret < 0)
			return ret;

		byte += ret;
		size = sizeof(struct hdcp2_rep_send_receiverid_list) -
		HDCP_2_2_RXINFO_LEN - HDCP_2_2_RECEIVER_IDS_MAX_LEN +
		(dev_cnt * HDCP_2_2_RECEIVER_ID_LEN);
		offset += HDCP_2_2_RXINFO_LEN;
	}

	bytes_to_recv = size - 1;

	while (bytes_to_recv) {
		len = bytes_to_recv > DP_AUX_MAX_PAYLOAD_BYTES ?
		      DP_AUX_MAX_PAYLOAD_BYTES : bytes_to_recv;

		/* Entire msg read timeout since initiate of msg read */
		if (bytes_to_recv == size - 1 && hdcp2_msg_data->msg_read_timeout > 0)
			msg_end = ktime_add_ms(ktime_get_raw(),
					       hdcp2_msg_data->msg_read_timeout);

		ret = drm_dp_dpcd_read(&dig_port->dp.aux, offset,
				       (void *)byte, len);
		if (ret < 0) {
			drm_dbg_kms(&i915->drm, "msg_id %d, ret %zd\n",
				    msg_id, ret);
			return ret;
		}

		bytes_to_recv -= ret;
		byte += ret;
		offset += ret;
	}

	if (hdcp2_msg_data->msg_read_timeout > 0) {
		msg_expired = ktime_after(ktime_get_raw(), msg_end);
		if (msg_expired) {
			drm_dbg_kms(&i915->drm, "msg_id %d, entire msg read timeout(mSec): %d\n",
				    msg_id, hdcp2_msg_data->msg_read_timeout);
			return -ETIMEDOUT;
		}
	}

	byte = buf;
	*byte = msg_id;

	return size;
}

static
int intel_dp_hdcp2_config_stream_type(struct intel_digital_port *dig_port,
				      bool is_repeater, u8 content_type)
{
	int ret;
	struct hdcp2_dp_errata_stream_type stream_type_msg;

	if (is_repeater)
		return 0;

	/*
	 * Errata for DP: As Stream type is used for encryption, Receiver
	 * should be communicated with stream type for the decryption of the
	 * content.
	 * Repeater will be communicated with stream type as a part of it's
	 * auth later in time.
	 */
	stream_type_msg.msg_id = HDCP_2_2_ERRATA_DP_STREAM_TYPE;
	stream_type_msg.stream_type = content_type;

	ret =  intel_dp_hdcp2_write_msg(dig_port, &stream_type_msg,
					sizeof(stream_type_msg));

	return ret < 0 ? ret : 0;

}

static
int intel_dp_hdcp2_check_link(struct intel_digital_port *dig_port,
			      struct intel_connector *connector)
{
	u8 rx_status;
	int ret;

	ret = intel_dp_hdcp2_read_rx_status(dig_port, &rx_status);
	if (ret)
		return ret;

	if (HDCP_2_2_DP_RXSTATUS_REAUTH_REQ(rx_status))
		ret = HDCP_REAUTH_REQUEST;
	else if (HDCP_2_2_DP_RXSTATUS_LINK_FAILED(rx_status))
		ret = HDCP_LINK_INTEGRITY_FAILURE;
	else if (HDCP_2_2_DP_RXSTATUS_READY(rx_status))
		ret = HDCP_TOPOLOGY_CHANGE;

	return ret;
}

static int intel_dp_hdcp2_capable(struct drm_connector *drm_connector, bool *capable)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	u8 rx_caps[3];
	int ret;

	ret = intel_hdcp2_capable(drm_connector, capable);
	if (ret || !capable)
		return ret;

	*capable = false;
	ret = drm_dp_dpcd_read(&dig_port->dp.aux,
			       DP_HDCP_2_2_REG_RX_CAPS_OFFSET,
			       rx_caps, HDCP_2_2_RXCAPS_LEN);
	if (ret != HDCP_2_2_RXCAPS_LEN)
		return ret >= 0 ? -EIO : ret;

	if (rx_caps[0] == HDCP_2_2_RX_CAPS_VERSION_VAL &&
	    HDCP_2_2_DP_HDCP_CAPABLE(rx_caps[2]))
		*capable = true;

	return 0;
}

static const struct intel_hdcp_shim intel_dp_hdcp_shim = {
	.toggle_signalling = intel_dp_hdcp_toggle_signalling,
	.write_2_2_msg = intel_dp_hdcp2_write_msg,
	.read_2_2_msg = intel_dp_hdcp2_read_msg,
	.config_stream_type = intel_dp_hdcp2_config_stream_type,
	.check_2_2_link = intel_dp_hdcp2_check_link,
	.protocol = HDCP_PROTOCOL_DP,
};

static int
intel_dp_mst_toggle_hdcp_stream_select(struct intel_connector *connector,
				       bool enable)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	ret = intel_ddi_toggle_hdcp_bits(&dig_port->base,
					 hdcp->stream_transcoder, enable,
					 TRANS_DDI_HDCP_SELECT);
	if (ret)
		drm_err(&i915->drm, "%s HDCP stream select failed (%d)\n",
			enable ? "Enable" : "Disable", ret);
	return ret;
}

static int
intel_dp_mst_hdcp_stream_encryption(struct intel_connector *connector,
				    bool enable)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder = hdcp->stream_transcoder;
	u32 stream_enc_status;
	int ret;

	ret = intel_dp_mst_toggle_hdcp_stream_select(connector, enable);
	if (ret)
		return ret;

	stream_enc_status =  transcoder_to_stream_enc_status(cpu_transcoder);
	if (!stream_enc_status)
		return -EINVAL;

	/* Wait for encryption confirmation */
	if (intel_de_wait_for_register(i915,
				       HDCP_STATUS(i915, cpu_transcoder, port),
				       stream_enc_status,
				       enable ? stream_enc_status : 0,
				       HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS)) {
		drm_err(&i915->drm, "Timed out waiting for transcoder: %s stream encryption %s\n",
			transcoder_name(cpu_transcoder), enable ? "enabled" : "disabled");
		return -ETIMEDOUT;
	}

	return 0;
}

static int intel_dp_mst_hdcp1_post_encryption(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	int ret;

	ret = intel_hdcp1_post_encryption(drm_connector);
	if (ret)
		return ret;

	return intel_dp_mst_hdcp_stream_encryption(connector, true);
}

static int intel_dp_mst_hdcp1_disable(struct drm_connector *drm_connector)
{
	struct intel_connector *connector = to_intel_connector(drm_connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	int ret;

	ret = intel_dp_mst_hdcp_stream_encryption(connector, true);
	if (ret) {
		drm_err(&i915->drm, "[%s:%d] Failed to disable HDCP 1.4 stream enc\n",
			connector->base.name, connector->base.base.id);
		return ret;
	}

	/*
	 * If there are other connectors on this port using HDCP,
	 * don't disable it until it disabled HDCP encryption for
	 * all connectors in MST topology.
	*/
	if (dig_port->num_hdcp_streams > 0)
		return 0;

	return intel_hdcp1_disable(drm_connector);
}

static int
intel_dp_mst_hdcp2_stream_encryption(struct intel_connector *connector,
				     bool enable)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct hdcp_port_data *data = &dig_port->hdcp_port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum transcoder cpu_transcoder = hdcp->stream_transcoder;
	enum pipe pipe = (enum pipe)cpu_transcoder;
	enum port port = dig_port->base.port;
	int ret;

	drm_WARN_ON(&i915->drm, enable &&
		    !!(intel_de_read(i915, HDCP2_AUTH_STREAM(i915, cpu_transcoder, port))
		    & AUTH_STREAM_TYPE) != data->streams[0].stream_type);

	ret = intel_dp_mst_toggle_hdcp_stream_select(connector, enable);
	if (ret)
		return ret;

	/* Wait for encryption confirmation */
	if (intel_de_wait_for_register(i915,
				       HDCP2_STREAM_STATUS(i915, cpu_transcoder, pipe),
				       STREAM_ENCRYPTION_STATUS,
				       enable ? STREAM_ENCRYPTION_STATUS : 0,
				       HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS)) {
		drm_err(&i915->drm, "Timed out waiting for transcoder: %s stream encryption %s\n",
			transcoder_name(cpu_transcoder), enable ? "enabled" : "disabled");
		return -ETIMEDOUT;
	}

	return 0;
}

static
int intel_dp_mst_hdcp2_check_link(struct intel_digital_port *dig_port,
			          struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	/*
	 * We do need to do the Link Check only for the connector involved with
	 * HDCP port authentication and encryption.
	 * We can re-use the hdcp->is_repeater flag to know that the connector
	 * involved with HDCP port authentication and encryption.
	 */
	if (hdcp->is_repeater) {
		ret = intel_dp_hdcp2_check_link(dig_port, connector);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct intel_hdcp_shim intel_dp_mst_hdcp_shim = {
	.toggle_signalling = intel_dp_hdcp_toggle_signalling,
	.write_2_2_msg = intel_dp_hdcp2_write_msg,
	.read_2_2_msg = intel_dp_hdcp2_read_msg,
	.config_stream_type = intel_dp_hdcp2_config_stream_type,
	.stream_2_2_encryption = intel_dp_mst_hdcp2_stream_encryption,
	.check_2_2_link = intel_dp_mst_hdcp2_check_link,
	.protocol = HDCP_PROTOCOL_DP,
};

static const struct drm_hdcp_helper_funcs intel_dp_hdcp_helper_funcs = {
	.setup = intel_hdcp_setup,
	.load_keys = intel_hdcp_load_keys,
	.hdcp2_capable = intel_dp_hdcp2_capable,
	.hdcp2_enable = intel_hdcp2_enable,
	.hdcp2_check_link = intel_hdcp2_check_link,
	.hdcp2_disable = intel_hdcp2_disable,
	.hdcp1_send_an_aksv = intel_dp_hdcp1_send_an_aksv,
	.hdcp1_store_receiver_info = intel_hdcp1_store_receiver_info,
	.hdcp1_enable_encryption = intel_hdcp1_enable_encryption,
	.hdcp1_wait_for_r0 = intel_hdcp1_wait_for_r0,
	.hdcp1_match_ri = intel_hdcp1_match_ri,
	.hdcp1_post_encryption = intel_hdcp1_post_encryption,
	.hdcp1_store_ksv_fifo = intel_hdcp1_store_ksv_fifo,
	.hdcp1_disable = intel_hdcp1_disable,
};

static const struct drm_hdcp_helper_funcs intel_dp_mst_hdcp_helper_funcs = {
	.setup = intel_hdcp_setup,
	.load_keys = intel_hdcp_load_keys,
	.hdcp2_capable = intel_dp_hdcp2_capable,
	.hdcp2_enable = intel_hdcp2_enable,
	.hdcp2_check_link = intel_hdcp2_check_link,
	.hdcp2_disable = intel_hdcp2_disable,
	.hdcp1_send_an_aksv = intel_dp_hdcp1_send_an_aksv,
	.hdcp1_store_receiver_info = intel_hdcp1_store_receiver_info,
	.hdcp1_enable_encryption = intel_hdcp1_enable_encryption,
	.hdcp1_wait_for_r0 = intel_hdcp1_wait_for_r0,
	.hdcp1_match_ri = intel_hdcp1_match_ri,
	.hdcp1_post_encryption = intel_dp_mst_hdcp1_post_encryption,
	.hdcp1_store_ksv_fifo = intel_hdcp1_store_ksv_fifo,
	.hdcp1_disable = intel_dp_mst_hdcp1_disable,
};


int intel_dp_hdcp_init(struct intel_digital_port *dig_port,
		       struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_encoder *intel_encoder = &dig_port->base;
	enum port port = intel_encoder->port;
	struct intel_dp *intel_dp = &dig_port->dp;
	struct drm_hdcp_helper_data *data;
	const struct drm_hdcp_helper_funcs *helper_funcs;
	const struct intel_hdcp_shim *intel_shim;
	int ret;

	if (!is_hdcp_supported(dev_priv, port) || intel_dp_is_edp(intel_dp))
		return 0;

	if (connector->mst_port) {
		helper_funcs = &intel_dp_mst_hdcp_helper_funcs;
		intel_shim = &intel_dp_mst_hdcp_shim;
	} else {
		helper_funcs = &intel_dp_hdcp_helper_funcs;
		intel_shim = &intel_dp_hdcp_shim;
	}

	data = drm_hdcp_helper_initialize_dp(&connector->base,
					     &dig_port->dp.aux, helper_funcs,
					     true);
	if (IS_ERR(data)) {
		drm_dbg_kms(&dev_priv->drm, "HDCP init failed, skipping.\n");
		return PTR_ERR(data);
	}

	ret = intel_hdcp_init(connector, dig_port, intel_shim);
	if (ret) {
		drm_hdcp_helper_destroy(data);
		return ret;
	}

	connector->hdcp_helper_data = data;
	return 0;
}
