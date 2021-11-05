// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2021 Google, Inc.
 *
 * Authors:
 * Sean Paul <seanpaul@chromium.org>
 */

#include "dp_display.h"
#include "dp_drm.h"
#include "dp_hdcp.h"
#include "dp_reg.h"

#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_hdcp.h>
#include <drm/drm_print.h>

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/iopoll.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/slab.h>

/* Offsets based on hdcp_ksv mmio */
#define DP_HDCP_KSV_AN_LSB			0x0
#define DP_HDCP_KSV_AN_MSB			0x4
#define DP_HDCP_KSV_AKSV_MSB			0x1D8
#define DP_HDCP_KSV_AKSV_LSB			0x1DC

/* Key offsets based on hdcp_key mmio */
#define DP_HDCP_KEY_BASE			0x30
#define  DP_HDCP_KEY_MSB(x) 			(DP_HDCP_KEY_BASE + (x * 8))
#define  DP_HDCP_KEY_LSB(x) 			(DP_HDCP_KEY_MSB(x) + 4)
#define DP_HDCP_KEY_VALID			0x170
#define  DP_HDCP_SW_KEY_VALID			BIT(0)

/* Timeouts */
#define DP_KEYS_VALID_SLEEP_US			(20 * 1000)
#define DP_KEYS_VALID_TIMEOUT_US		(100 * 1000)
#define DP_AN_READY_SLEEP_US			100
#define DP_AN_READY_TIMEOUT_US			(10 * 1000)
#define DP_R0_READY_SLEEP_US			100
#define DP_R0_READY_TIMEOUT_US			(10 * 1000)
#define DP_RI_MATCH_SLEEP_US			(20 * 1000)
#define DP_RI_MATCH_TIMEOUT_US			(100 * 1000)
#define DP_KSV_WRITTEN_SLEEP_US			100
#define DP_KSV_WRITTEN_TIMEOUT_US		(100 * 1000)
#define DP_SHA_COMPUTATION_SLEEP_US		100
#define DP_SHA_COMPUTATION_TIMEOUT_US		(100 * 1000)
#define DP_AN_READ_DELAY_US			1

/*
 * dp_hdcp_key - structure which contains an HDCP key set
 * @ksv: The key selection vector
 * @keys: Contains 40 keys
 */
struct dp_hdcp_key {
	struct drm_hdcp_ksv ksv;
	union {
		u32 words[2];
		u8 bytes[DP_HDCP_KEY_LEN];
	} keys[DP_HDCP_NUM_KEYS];
	bool valid;
};

struct dp_hdcp {
	struct drm_device *dev;
	struct drm_connector *connector;

	struct drm_dp_aux *aux;
	struct dp_parser *parser;

	struct drm_hdcp_helper_data *helper_data;

	struct mutex key_lock;
	struct dp_hdcp_key key;
};

static inline void dp_hdcp_write_ahb(struct dp_hdcp *hdcp, u32 offset, u32 val)
{
	writel(val, hdcp->parser->io.dp_controller.ahb.base + offset);
}

static inline u32 dp_hdcp_read_ahb(struct dp_hdcp *hdcp, u32 offset)
{
	return readl(hdcp->parser->io.dp_controller.ahb.base + offset);
}

static inline void dp_hdcp_write_aux(struct dp_hdcp *hdcp, u32 offset, u32 val)
{
	writel(val, hdcp->parser->io.dp_controller.aux.base + offset);
}

static inline u32 dp_hdcp_read_aux(struct dp_hdcp *hdcp, u32 offset)
{
	return readl(hdcp->parser->io.dp_controller.aux.base + offset);
}

static inline void dp_hdcp_write_link(struct dp_hdcp *hdcp, u32 offset, u32 val)
{
	writel(val, hdcp->parser->io.dp_controller.link.base + offset);
}

static inline u32 dp_hdcp_read_link(struct dp_hdcp *hdcp, u32 offset)
{
	return readl(hdcp->parser->io.dp_controller.link.base + offset);
}

static inline void dp_hdcp_write_key(struct dp_hdcp *hdcp, u32 offset, u32 val)
{
	writel(val, hdcp->parser->io.dp_controller.hdcp_key.base + offset);
}

static inline void dp_hdcp_write_tz_hlos(struct dp_hdcp *hdcp, u32 offset,
					 u32 val)
{
	writel(val, hdcp->parser->io.dp_controller.hdcp_tz.base + offset);
}

int dp_hdcp_ingest_key(struct dp_hdcp *hdcp, const u8 *raw_key, int raw_len)
{
	unsigned int ksv_weight;
	int i, ret = 0;

	if (raw_len != (DRM_HDCP_KSV_LEN + DP_HDCP_NUM_KEYS * DP_HDCP_KEY_LEN)) {
		DRM_ERROR("Invalid HDCP key length expected=%d actual=%d\n",
			  (DRM_HDCP_KSV_LEN + DP_HDCP_NUM_KEYS * DP_HDCP_KEY_LEN),
			  raw_len);
		return -EINVAL;
	}

	mutex_lock(&hdcp->key_lock);

	memcpy(hdcp->key.ksv.bytes, raw_key, DRM_HDCP_KSV_LEN);
	ksv_weight = hweight32(hdcp->key.ksv.words[0]) +
		     hweight32(hdcp->key.ksv.words[1]);
	if (ksv_weight != 20) {
		DRM_ERROR("Invalid ksv weight, expected=20 actual=%d\n",
			  ksv_weight);
		ret = -EINVAL;
		goto out;
	}

	raw_key += DRM_HDCP_KSV_LEN;
	for (i = 0; i < DP_HDCP_NUM_KEYS; i++) {
		memcpy(hdcp->key.keys[i].bytes, raw_key, DP_HDCP_KEY_LEN);
		raw_key += DP_HDCP_KEY_LEN;
	}

	DRM_DEBUG_DRIVER("Successfully ingested HDCP key\n");
	hdcp->key.valid = true;

out:
	mutex_unlock(&hdcp->key_lock);
	return ret;
}

static bool dp_hdcp_are_keys_valid(struct drm_connector *connector)
{
	struct dp_hdcp *hdcp = dp_display_connector_to_hdcp(connector);
	u32 val;

	val = dp_hdcp_read_ahb(hdcp, DP_HDCP_STATUS);
	return FIELD_GET(DP_HDCP_KEY_STATUS, val) == DP_HDCP_KEY_STATUS_VALID;
}

static int dp_hdcp_load_keys(struct drm_connector *connector)
{
	struct dp_hdcp *hdcp = dp_display_connector_to_hdcp(connector);
	int i, ret = 0;
	u64 an_seed = get_random_u64();

	mutex_lock(&hdcp->key_lock);

	if (!hdcp->key.valid) {
		ret = -ENOENT;
		goto out;
	}

	dp_hdcp_write_aux(hdcp, DP_HDCP_SW_LOWER_AKSV, hdcp->key.ksv.words[0]);
	dp_hdcp_write_aux(hdcp, DP_HDCP_SW_UPPER_AKSV, hdcp->key.ksv.words[1]);

	for (i = 0; i < DP_HDCP_NUM_KEYS; i++) {
		dp_hdcp_write_key(hdcp, DP_HDCP_KEY_LSB(i),
				 hdcp->key.keys[i].words[0]);
		dp_hdcp_write_key(hdcp, DP_HDCP_KEY_MSB(i),
				 hdcp->key.keys[i].words[1]);
	}

	dp_hdcp_write_key(hdcp, DP_HDCP_KEY_VALID, DP_HDCP_SW_KEY_VALID);

	dp_hdcp_write_link(hdcp, DP_HDCP_ENTROPY_CTRL0,
			      FIELD_GET(GENMASK(31,0), an_seed));
	dp_hdcp_write_link(hdcp, DP_HDCP_ENTROPY_CTRL1,
			      FIELD_GET(GENMASK_ULL(63,32), an_seed));

out:
	mutex_unlock(&hdcp->key_lock);
	return ret;
}

static int dp_hdcp_hdcp2_capable(struct drm_connector *connector, bool *capable)
{
	*capable = false;
	return 0;
}

static int dp_hdcp_hdcp1_read_an_aksv(struct drm_connector *connector,
				      u32 *an, u32 *aksv)
{
	struct dp_hdcp *hdcp = dp_display_connector_to_hdcp(connector);
	bool keys_valid;
	int ret;
	u32 val;

	dp_hdcp_write_ahb(hdcp, DP_HDCP_CTRL, 1);

	ret = read_poll_timeout(dp_hdcp_are_keys_valid, keys_valid, keys_valid,
				DP_KEYS_VALID_SLEEP_US,
				DP_KEYS_VALID_TIMEOUT_US, false, connector);
	if (ret) {
		drm_err(hdcp->dev, "HDCP keys invalid %d\n", ret);
		return ret;
	}

	/* Clear AInfo */
	dp_hdcp_write_aux(hdcp, DP_HDCP_RCVPORT_DATA4, 0);

	aksv[0] = dp_hdcp_read_aux(hdcp, DP_HDCP_RCVPORT_DATA3);
	aksv[1] = GENMASK(7, 0) & dp_hdcp_read_aux(hdcp, DP_HDCP_RCVPORT_DATA4);

	ret = read_poll_timeout(dp_hdcp_read_ahb, val,
		(val & DP_HDCP_AN_READY_MASK) == DP_HDCP_AN_READY_MASK,
		DP_AN_READY_SLEEP_US, DP_AN_READY_TIMEOUT_US,
		false, hdcp, DP_HDCP_STATUS);
	if (ret) {
		drm_err(hdcp->dev, "AN failed to become ready %x/%d\n", val,
			ret);
		return ret;
	}

	/*
	 * Get An from hardware, for unknown reasons we need to read the reg
	 * twice to get valid data.
	 */
	dp_hdcp_read_ahb(hdcp, DP_HDCP_RCVPORT_DATA5);
	an[0] = dp_hdcp_read_ahb(hdcp, DP_HDCP_RCVPORT_DATA5);

	udelay(DP_AN_READ_DELAY_US);

	dp_hdcp_read_ahb(hdcp, DP_HDCP_RCVPORT_DATA6);
	an[1] = dp_hdcp_read_ahb(hdcp, DP_HDCP_RCVPORT_DATA6);

	return 0;
}

static int dp_hdcp_hdcp1_store_receiver_info(struct drm_connector *connector,
					     u32 *ksv, u32 status, u8 bcaps,
					     bool is_repeater)
{
	struct dp_hdcp *hdcp = dp_display_connector_to_hdcp(connector);
	u32 val;

	dp_hdcp_write_tz_hlos(hdcp, HDCP_SEC_DP_TZ_HV_HLOS_HDCP_RCVPORT_DATA0,
			      ksv[0]);
	dp_hdcp_write_tz_hlos(hdcp, HDCP_SEC_DP_TZ_HV_HLOS_HDCP_RCVPORT_DATA1,
			      ksv[1]);

	val = FIELD_PREP(GENMASK(23, 8), status) |
	      FIELD_PREP(GENMASK(7, 0), bcaps);

	dp_hdcp_write_tz_hlos(hdcp, HDCP_SEC_DP_TZ_HV_HLOS_HDCP_RCVPORT_DATA12,
			      val);

	return 0;
}

static int dp_hdcp_hdcp1_enable_encryption(struct drm_connector *connector)
{
	return 0;
}

static int dp_hdcp_hdcp1_wait_for_r0(struct drm_connector *connector)
{
	struct dp_hdcp *hdcp = dp_display_connector_to_hdcp(connector);
	int ret;
	u32 val;

	ret = read_poll_timeout(dp_hdcp_read_ahb, val, (val & DP_HDCP_R0_READY),
				DP_R0_READY_SLEEP_US, DP_R0_READY_TIMEOUT_US,
				false, hdcp, DP_HDCP_STATUS);
	if (ret) {
		drm_err(hdcp->dev, "HDCP R0 not ready %x/%d\n", val, ret);
		return ret;
	}

	return 0;
}

static int dp_hdcp_hdcp1_match_ri(struct drm_connector *connector, u32 ri_prime)
{
	struct dp_hdcp *hdcp = dp_display_connector_to_hdcp(connector);
	int ret;
	u32 val;

	dp_hdcp_write_ahb(hdcp, DP_HDCP_RCVPORT_DATA2_0, ri_prime);

	ret = read_poll_timeout(dp_hdcp_read_ahb, val, (val & DP_HDCP_RI_MATCH),
				DP_RI_MATCH_SLEEP_US, DP_RI_MATCH_TIMEOUT_US,
				false, hdcp, DP_HDCP_STATUS);
	if (ret) {
		drm_err(hdcp->dev, "Failed to match Ri and Ri` (%08x) %08x/%d\n",
			ri_prime, val, ret);
		return ret;
	}
	return 0;
}

static int dp_hdcp_hdcp1_store_ksv_fifo(struct drm_connector *connector,
					u8 *ksv_fifo, u8 num_downstream,
					u8 *bstatus, u32 *vprime)
{
	struct dp_hdcp *hdcp = dp_display_connector_to_hdcp(connector);
	int num_bytes = num_downstream * DRM_HDCP_KSV_LEN;
	int ret, i;
	u32 val;

	/* Reset the SHA computation block */
	dp_hdcp_write_tz_hlos(hdcp, HDCP_SEC_DP_TZ_HV_HLOS_HDCP_SHA_CTRL,
			      DP_HDCP_SHA_CTRL_RESET);
	dp_hdcp_write_tz_hlos(hdcp, HDCP_SEC_DP_TZ_HV_HLOS_HDCP_SHA_CTRL, 0);

	/*
	 * KSV info gets written a byte at a time in the same order it was
	 * received. Every 64 bytes, we need to wait for the SHA_BLOCK_DONE
	 * bit to be set in SHA_CTRL.
	 */
	for (i = 0; i < num_bytes; i++) {
		val = FIELD_PREP(DP_HDCP_SHA_DATA_MASK, ksv_fifo[i]);

		if (i == (num_bytes - 1))
			val |= DP_HDCP_SHA_DATA_DONE;

		dp_hdcp_write_tz_hlos(hdcp,
				      HDCP_SEC_DP_TZ_HV_HLOS_HDCP_SHA_DATA,
				      val);

		if (((i + 1) % 64) != 0)
			continue;

		ret = read_poll_timeout(dp_hdcp_read_ahb, val,
					(val & DP_HDCP_SHA_DONE),
					DP_KSV_WRITTEN_SLEEP_US,
					DP_KSV_WRITTEN_TIMEOUT_US, false, hdcp,
					DP_HDCP_SHA_STATUS);
		if (ret) {
			drm_err(hdcp->dev, "SHA block incomplete %d\n", ret);
			return ret;
		}
	}

	ret = read_poll_timeout(dp_hdcp_read_ahb, val,
				(val & DP_HDCP_SHA_COMP_DONE),
				DP_SHA_COMPUTATION_SLEEP_US,
				DP_SHA_COMPUTATION_TIMEOUT_US,
				false, hdcp, DP_HDCP_SHA_STATUS);
	if (ret) {
		drm_err(hdcp->dev, "SHA computation incomplete %d\n", ret);
		return ret;
	}

	return 0;
}

static int dp_hdcp_hdcp1_disable(struct drm_connector *connector)
{
	struct dp_hdcp *hdcp = dp_display_connector_to_hdcp(connector);
	u32 val;

	val = dp_hdcp_read_ahb(hdcp, REG_DP_SW_RESET);
	dp_hdcp_write_ahb(hdcp, REG_DP_SW_RESET, val | DP_HDCP_SW_RESET);

	/* Disable encryption and disable the HDCP block */
	dp_hdcp_write_ahb(hdcp, DP_HDCP_CTRL, 0);

	dp_hdcp_write_ahb(hdcp, REG_DP_SW_RESET, val);

	return 0;
}

void dp_hdcp_commit(struct dp_hdcp *hdcp, struct drm_atomic_state *state)
{
	drm_hdcp_helper_atomic_commit(hdcp->helper_data, state, NULL);
}

static const struct drm_hdcp_helper_funcs dp_hdcp_funcs = {
	.are_keys_valid = dp_hdcp_are_keys_valid,
	.load_keys = dp_hdcp_load_keys,
	.hdcp2_capable = dp_hdcp_hdcp2_capable,
	.hdcp1_read_an_aksv = dp_hdcp_hdcp1_read_an_aksv,
	.hdcp1_store_receiver_info = dp_hdcp_hdcp1_store_receiver_info,
	.hdcp1_enable_encryption = dp_hdcp_hdcp1_enable_encryption,
	.hdcp1_wait_for_r0 = dp_hdcp_hdcp1_wait_for_r0,
	.hdcp1_match_ri = dp_hdcp_hdcp1_match_ri,
	.hdcp1_store_ksv_fifo = dp_hdcp_hdcp1_store_ksv_fifo,
	.hdcp1_disable = dp_hdcp_hdcp1_disable,
};

int dp_hdcp_attach(struct dp_hdcp *hdcp, struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_hdcp_helper_data *helper_data;
	int ret;

	/* HDCP is not configured for this device */
	if (!hdcp->parser->io.dp_controller.hdcp_key.base)
		return 0;

	helper_data = drm_hdcp_helper_initialize_dp(connector, hdcp->aux,
						    &dp_hdcp_funcs, false);
	if (IS_ERR_OR_NULL(helper_data))
		return PTR_ERR(helper_data);

	ret = drm_connector_attach_content_protection_property(connector, false);
	if (ret) {
		drm_hdcp_helper_destroy(helper_data);
		drm_err(dev, "Failed to attach content protection prop %d\n", ret);
		return ret;
	}

	hdcp->dev = connector->dev;
	hdcp->connector = connector;
	hdcp->helper_data = helper_data;

	return 0;
}

struct dp_hdcp *dp_hdcp_get(struct dp_parser *parser, struct drm_dp_aux *aux)
{
	struct device *dev = &parser->pdev->dev;
	struct dp_hdcp *hdcp;

	hdcp = devm_kzalloc(dev, sizeof(*hdcp), GFP_KERNEL);
	if (!hdcp)
		return ERR_PTR(-ENOMEM);

	hdcp->parser = parser;
	hdcp->aux = aux;

	mutex_init(&hdcp->key_lock);

	return hdcp;
}

void dp_hdcp_put(struct dp_hdcp *hdcp)
{
	if (hdcp)
		drm_hdcp_helper_destroy(hdcp->helper_data);
}
