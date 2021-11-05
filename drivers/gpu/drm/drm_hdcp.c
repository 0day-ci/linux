// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Intel Corporation.
 *
 * Authors:
 * Ramalingam C <ramalingam.c@intel.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_hdcp.h>
#include <drm/drm_sysfs.h>
#include <drm/drm_print.h>
#include <drm/drm_device.h>
#include <drm/drm_property.h>
#include <drm/drm_mode_object.h>

#include "drm_internal.h"

static inline void drm_hdcp_print_ksv(const u8 *ksv)
{
	DRM_DEBUG("\t%#02x, %#02x, %#02x, %#02x, %#02x\n",
		  ksv[0], ksv[1], ksv[2], ksv[3], ksv[4]);
}

static u32 drm_hdcp_get_revoked_ksv_count(const u8 *buf, u32 vrls_length)
{
	u32 parsed_bytes = 0, ksv_count = 0, vrl_ksv_cnt, vrl_sz;

	while (parsed_bytes < vrls_length) {
		vrl_ksv_cnt = *buf;
		ksv_count += vrl_ksv_cnt;

		vrl_sz = (vrl_ksv_cnt * DRM_HDCP_KSV_LEN) + 1;
		buf += vrl_sz;
		parsed_bytes += vrl_sz;
	}

	/*
	 * When vrls are not valid, ksvs are not considered.
	 * Hence SRM will be discarded.
	 */
	if (parsed_bytes != vrls_length)
		ksv_count = 0;

	return ksv_count;
}

static u32 drm_hdcp_get_revoked_ksvs(const u8 *buf, u8 **revoked_ksv_list,
				     u32 vrls_length)
{
	u32 vrl_ksv_cnt, vrl_ksv_sz, vrl_idx = 0;
	u32 parsed_bytes = 0, ksv_count = 0;

	do {
		vrl_ksv_cnt = *buf;
		vrl_ksv_sz = vrl_ksv_cnt * DRM_HDCP_KSV_LEN;

		buf++;

		DRM_DEBUG("vrl: %d, Revoked KSVs: %d\n", vrl_idx++,
			  vrl_ksv_cnt);
		memcpy((*revoked_ksv_list) + (ksv_count * DRM_HDCP_KSV_LEN),
		       buf, vrl_ksv_sz);

		ksv_count += vrl_ksv_cnt;
		buf += vrl_ksv_sz;

		parsed_bytes += (vrl_ksv_sz + 1);
	} while (parsed_bytes < vrls_length);

	return ksv_count;
}

static inline u32 get_vrl_length(const u8 *buf)
{
	return drm_hdcp_be24_to_cpu(buf);
}

static int drm_hdcp_parse_hdcp1_srm(const u8 *buf, size_t count,
				    u8 **revoked_ksv_list, u32 *revoked_ksv_cnt)
{
	struct hdcp_srm_header *header;
	u32 vrl_length, ksv_count;

	if (count < (sizeof(struct hdcp_srm_header) +
	    DRM_HDCP_1_4_VRL_LENGTH_SIZE + DRM_HDCP_1_4_DCP_SIG_SIZE)) {
		DRM_ERROR("Invalid blob length\n");
		return -EINVAL;
	}

	header = (struct hdcp_srm_header *)buf;
	DRM_DEBUG("SRM ID: 0x%x, SRM Ver: 0x%x, SRM Gen No: 0x%x\n",
		  header->srm_id,
		  be16_to_cpu(header->srm_version), header->srm_gen_no);

	WARN_ON(header->reserved);

	buf = buf + sizeof(*header);
	vrl_length = get_vrl_length(buf);
	if (count < (sizeof(struct hdcp_srm_header) + vrl_length) ||
	    vrl_length < (DRM_HDCP_1_4_VRL_LENGTH_SIZE +
			  DRM_HDCP_1_4_DCP_SIG_SIZE)) {
		DRM_ERROR("Invalid blob length or vrl length\n");
		return -EINVAL;
	}

	/* Length of the all vrls combined */
	vrl_length -= (DRM_HDCP_1_4_VRL_LENGTH_SIZE +
		       DRM_HDCP_1_4_DCP_SIG_SIZE);

	if (!vrl_length) {
		DRM_ERROR("No vrl found\n");
		return -EINVAL;
	}

	buf += DRM_HDCP_1_4_VRL_LENGTH_SIZE;
	ksv_count = drm_hdcp_get_revoked_ksv_count(buf, vrl_length);
	if (!ksv_count) {
		DRM_DEBUG("Revoked KSV count is 0\n");
		return 0;
	}

	*revoked_ksv_list = kcalloc(ksv_count, DRM_HDCP_KSV_LEN, GFP_KERNEL);
	if (!*revoked_ksv_list) {
		DRM_ERROR("Out of Memory\n");
		return -ENOMEM;
	}

	if (drm_hdcp_get_revoked_ksvs(buf, revoked_ksv_list,
				      vrl_length) != ksv_count) {
		*revoked_ksv_cnt = 0;
		kfree(*revoked_ksv_list);
		return -EINVAL;
	}

	*revoked_ksv_cnt = ksv_count;
	return 0;
}

static int drm_hdcp_parse_hdcp2_srm(const u8 *buf, size_t count,
				    u8 **revoked_ksv_list, u32 *revoked_ksv_cnt)
{
	struct hdcp_srm_header *header;
	u32 vrl_length, ksv_count, ksv_sz;

	if (count < (sizeof(struct hdcp_srm_header) +
	    DRM_HDCP_2_VRL_LENGTH_SIZE + DRM_HDCP_2_DCP_SIG_SIZE)) {
		DRM_ERROR("Invalid blob length\n");
		return -EINVAL;
	}

	header = (struct hdcp_srm_header *)buf;
	DRM_DEBUG("SRM ID: 0x%x, SRM Ver: 0x%x, SRM Gen No: 0x%x\n",
		  header->srm_id & DRM_HDCP_SRM_ID_MASK,
		  be16_to_cpu(header->srm_version), header->srm_gen_no);

	if (header->reserved)
		return -EINVAL;

	buf = buf + sizeof(*header);
	vrl_length = get_vrl_length(buf);

	if (count < (sizeof(struct hdcp_srm_header) + vrl_length) ||
	    vrl_length < (DRM_HDCP_2_VRL_LENGTH_SIZE +
	    DRM_HDCP_2_DCP_SIG_SIZE)) {
		DRM_ERROR("Invalid blob length or vrl length\n");
		return -EINVAL;
	}

	/* Length of the all vrls combined */
	vrl_length -= (DRM_HDCP_2_VRL_LENGTH_SIZE +
		       DRM_HDCP_2_DCP_SIG_SIZE);

	if (!vrl_length) {
		DRM_ERROR("No vrl found\n");
		return -EINVAL;
	}

	buf += DRM_HDCP_2_VRL_LENGTH_SIZE;
	ksv_count = (*buf << 2) | DRM_HDCP_2_KSV_COUNT_2_LSBITS(*(buf + 1));
	if (!ksv_count) {
		DRM_DEBUG("Revoked KSV count is 0\n");
		return 0;
	}

	*revoked_ksv_list = kcalloc(ksv_count, DRM_HDCP_KSV_LEN, GFP_KERNEL);
	if (!*revoked_ksv_list) {
		DRM_ERROR("Out of Memory\n");
		return -ENOMEM;
	}

	ksv_sz = ksv_count * DRM_HDCP_KSV_LEN;
	buf += DRM_HDCP_2_NO_OF_DEV_PLUS_RESERVED_SZ;

	DRM_DEBUG("Revoked KSVs: %d\n", ksv_count);
	memcpy(*revoked_ksv_list, buf, ksv_sz);

	*revoked_ksv_cnt = ksv_count;
	return 0;
}

static inline bool is_srm_version_hdcp1(const u8 *buf)
{
	return *buf == (u8)(DRM_HDCP_1_4_SRM_ID << 4);
}

static inline bool is_srm_version_hdcp2(const u8 *buf)
{
	return *buf == (u8)(DRM_HDCP_2_SRM_ID << 4 | DRM_HDCP_2_INDICATOR);
}

static int drm_hdcp_srm_update(const u8 *buf, size_t count,
			       u8 **revoked_ksv_list, u32 *revoked_ksv_cnt)
{
	if (count < sizeof(struct hdcp_srm_header))
		return -EINVAL;

	if (is_srm_version_hdcp1(buf))
		return drm_hdcp_parse_hdcp1_srm(buf, count, revoked_ksv_list,
						revoked_ksv_cnt);
	else if (is_srm_version_hdcp2(buf))
		return drm_hdcp_parse_hdcp2_srm(buf, count, revoked_ksv_list,
						revoked_ksv_cnt);
	else
		return -EINVAL;
}

static int drm_hdcp_request_srm(struct drm_device *drm_dev,
				u8 **revoked_ksv_list, u32 *revoked_ksv_cnt)
{
	char fw_name[36] = "display_hdcp_srm.bin";
	const struct firmware *fw;
	int ret;

	ret = request_firmware_direct(&fw, (const char *)fw_name,
				      drm_dev->dev);
	if (ret < 0) {
		*revoked_ksv_cnt = 0;
		*revoked_ksv_list = NULL;
		ret = 0;
		goto exit;
	}

	if (fw->size && fw->data)
		ret = drm_hdcp_srm_update(fw->data, fw->size, revoked_ksv_list,
					  revoked_ksv_cnt);

exit:
	release_firmware(fw);
	return ret;
}

/**
 * drm_hdcp_check_ksvs_revoked - Check the revoked status of the IDs
 *
 * @drm_dev: drm_device for which HDCP revocation check is requested
 * @ksvs: List of KSVs (HDCP receiver IDs)
 * @ksv_count: KSV count passed in through @ksvs
 *
 * This function reads the HDCP System renewability Message(SRM Table)
 * from userspace as a firmware and parses it for the revoked HDCP
 * KSVs(Receiver IDs) detected by DCP LLC. Once the revoked KSVs are known,
 * revoked state of the KSVs in the list passed in by display drivers are
 * decided and response is sent.
 *
 * SRM should be presented in the name of "display_hdcp_srm.bin".
 *
 * Format of the SRM table, that userspace needs to write into the binary file,
 * is defined at:
 * 1. Renewability chapter on 55th page of HDCP 1.4 specification
 * https://www.digital-cp.com/sites/default/files/specifications/HDCP%20Specification%20Rev1_4_Secure.pdf
 * 2. Renewability chapter on 63rd page of HDCP 2.2 specification
 * https://www.digital-cp.com/sites/default/files/specifications/HDCP%20on%20HDMI%20Specification%20Rev2_2_Final1.pdf
 *
 * Returns:
 * Count of the revoked KSVs or -ve error number in case of the failure.
 */
int drm_hdcp_check_ksvs_revoked(struct drm_device *drm_dev, u8 *ksvs,
				u32 ksv_count)
{
	u32 revoked_ksv_cnt = 0, i, j;
	u8 *revoked_ksv_list = NULL;
	int ret = 0;

	ret = drm_hdcp_request_srm(drm_dev, &revoked_ksv_list,
				   &revoked_ksv_cnt);
	if (ret)
		return ret;

	/* revoked_ksv_cnt will be zero when above function failed */
	for (i = 0; i < revoked_ksv_cnt; i++)
		for  (j = 0; j < ksv_count; j++)
			if (!memcmp(&ksvs[j * DRM_HDCP_KSV_LEN],
				    &revoked_ksv_list[i * DRM_HDCP_KSV_LEN],
				    DRM_HDCP_KSV_LEN)) {
				DRM_DEBUG("Revoked KSV is ");
				drm_hdcp_print_ksv(&ksvs[j * DRM_HDCP_KSV_LEN]);
				ret++;
			}

	kfree(revoked_ksv_list);
	return ret;
}
EXPORT_SYMBOL_GPL(drm_hdcp_check_ksvs_revoked);

static struct drm_prop_enum_list drm_cp_enum_list[] = {
	{ DRM_MODE_CONTENT_PROTECTION_UNDESIRED, "Undesired" },
	{ DRM_MODE_CONTENT_PROTECTION_DESIRED, "Desired" },
	{ DRM_MODE_CONTENT_PROTECTION_ENABLED, "Enabled" },
};
DRM_ENUM_NAME_FN(drm_get_content_protection_name, drm_cp_enum_list)

static struct drm_prop_enum_list drm_hdcp_content_type_enum_list[] = {
	{ DRM_MODE_HDCP_CONTENT_TYPE0, "HDCP Type0" },
	{ DRM_MODE_HDCP_CONTENT_TYPE1, "HDCP Type1" },
};
DRM_ENUM_NAME_FN(drm_get_hdcp_content_type_name,
		 drm_hdcp_content_type_enum_list)

/**
 * drm_connector_attach_content_protection_property - attach content protection
 * property
 *
 * @connector: connector to attach CP property on.
 * @hdcp_content_type: is HDCP Content Type property needed for connector
 *
 * This is used to add support for content protection on select connectors.
 * Content Protection is intentionally vague to allow for different underlying
 * technologies, however it is most implemented by HDCP.
 *
 * When hdcp_content_type is true enum property called HDCP Content Type is
 * created (if it is not already) and attached to the connector.
 *
 * This property is used for sending the protected content's stream type
 * from userspace to kernel on selected connectors. Protected content provider
 * will decide their type of their content and declare the same to kernel.
 *
 * Content type will be used during the HDCP 2.2 authentication.
 * Content type will be set to &drm_connector_state.hdcp_content_type.
 *
 * The content protection will be set to &drm_connector_state.content_protection
 *
 * When kernel triggered content protection state change like DESIRED->ENABLED
 * and ENABLED->DESIRED, will use drm_hdcp_update_content_protection() to update
 * the content protection state of a connector.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_connector_attach_content_protection_property(
		struct drm_connector *connector, bool hdcp_content_type)
{
	struct drm_device *dev = connector->dev;
	struct drm_property *prop =
			dev->mode_config.content_protection_property;

	if (!prop)
		prop = drm_property_create_enum(dev, 0, "Content Protection",
						drm_cp_enum_list,
						ARRAY_SIZE(drm_cp_enum_list));
	if (!prop)
		return -ENOMEM;

	drm_object_attach_property(&connector->base, prop,
				   DRM_MODE_CONTENT_PROTECTION_UNDESIRED);
	dev->mode_config.content_protection_property = prop;

	if (!hdcp_content_type)
		return 0;

	prop = dev->mode_config.hdcp_content_type_property;
	if (!prop)
		prop = drm_property_create_enum(dev, 0, "HDCP Content Type",
					drm_hdcp_content_type_enum_list,
					ARRAY_SIZE(
					drm_hdcp_content_type_enum_list));
	if (!prop)
		return -ENOMEM;

	drm_object_attach_property(&connector->base, prop,
				   DRM_MODE_HDCP_CONTENT_TYPE0);
	dev->mode_config.hdcp_content_type_property = prop;

	return 0;
}
EXPORT_SYMBOL(drm_connector_attach_content_protection_property);

/**
 * drm_hdcp_update_content_protection - Updates the content protection state
 * of a connector
 *
 * @connector: drm_connector on which content protection state needs an update
 * @val: New state of the content protection property
 *
 * This function can be used by display drivers, to update the kernel triggered
 * content protection state changes of a drm_connector such as DESIRED->ENABLED
 * and ENABLED->DESIRED. No uevent for DESIRED->UNDESIRED or ENABLED->UNDESIRED,
 * as userspace is triggering such state change and kernel performs it without
 * fail.This function update the new state of the property into the connector's
 * state and generate an uevent to notify the userspace.
 */
void drm_hdcp_update_content_protection(struct drm_connector *connector,
					u64 val)
{
	struct drm_device *dev = connector->dev;
	struct drm_connector_state *state = connector->state;

	WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));
	if (state->content_protection == val)
		return;

	state->content_protection = val;
	drm_sysfs_connector_status_event(connector,
				 dev->mode_config.content_protection_property);
}
EXPORT_SYMBOL(drm_hdcp_update_content_protection);

/**
 * drm_hdcp_atomic_check - Helper for drivers to call during connector->atomic_check
 *
 * @state: pointer to the atomic state being checked
 * @connector: drm_connector on which content protection state needs an update
 *
 * This function can be used by display drivers to perform an atomic check on the
 * hdcp state elements. If hdcp state has changed in a manner which requires the
 * driver to enable or disable content protection, this function will return
 * true.
 *
 * Returns:
 * true if the driver must enable/disable hdcp, false otherwise
 */
bool drm_hdcp_atomic_check(struct drm_connector *connector,
			   struct drm_atomic_state *state)
{
	struct drm_connector_state *new_conn_state, *old_conn_state;
	struct drm_crtc_state *new_crtc_state;
	u64 old_hdcp, new_hdcp;

	old_conn_state = drm_atomic_get_old_connector_state(state, connector);
	old_hdcp = old_conn_state->content_protection;

	new_conn_state = drm_atomic_get_new_connector_state(state, connector);
	new_hdcp = new_conn_state->content_protection;

	if (!new_conn_state->crtc) {
		/*
		 * If the connector is being disabled with CP enabled, mark it
		 * desired so it's re-enabled when the connector is brought back
		 */
		if (old_hdcp == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
			new_conn_state->content_protection =
				DRM_MODE_CONTENT_PROTECTION_DESIRED;
			return true;
		}
		return false;
	}

	new_crtc_state = drm_atomic_get_new_crtc_state(state,
						       new_conn_state->crtc);
	/*
	* Fix the HDCP uapi content protection state in case of modeset.
	* FIXME: As per HDCP content protection property uapi doc, an uevent()
	* need to be sent if there is transition from ENABLED->DESIRED.
	*/
	if (drm_atomic_crtc_needs_modeset(new_crtc_state) &&
	    (old_hdcp == DRM_MODE_CONTENT_PROTECTION_ENABLED &&
	     new_hdcp != DRM_MODE_CONTENT_PROTECTION_UNDESIRED)) {
		new_conn_state->content_protection =
			DRM_MODE_CONTENT_PROTECTION_DESIRED;
		return true;
	}

	/*
	 * Coming back from disable or changing CRTC with DESIRED state requires
	 * that the driver try CP enable.
	 */
	if (new_hdcp == DRM_MODE_CONTENT_PROTECTION_DESIRED &&
	    new_conn_state->crtc != old_conn_state->crtc)
		return true;

	/*
	 * Content type changes require an HDCP disable/enable cycle.
	 */
	if (new_conn_state->hdcp_content_type != old_conn_state->hdcp_content_type) {
		new_conn_state->content_protection =
			DRM_MODE_CONTENT_PROTECTION_DESIRED;
		return true;
	}

	/*
	 * Ignore meaningless state changes:
	 *  - HDCP was activated since the last commit
	 *  - Attempting to set to desired while already enabled
	 */
	if ((old_hdcp == DRM_MODE_CONTENT_PROTECTION_DESIRED &&
	     new_hdcp == DRM_MODE_CONTENT_PROTECTION_ENABLED) ||
	    (old_hdcp == DRM_MODE_CONTENT_PROTECTION_ENABLED &&
	     new_hdcp == DRM_MODE_CONTENT_PROTECTION_DESIRED)) {
		new_conn_state->content_protection =
			DRM_MODE_CONTENT_PROTECTION_ENABLED;
		return false;
	}

	/* Finally, if state changes, we need action */
	return old_hdcp != new_hdcp;
}
EXPORT_SYMBOL(drm_hdcp_atomic_check);

struct drm_hdcp_helper_data {
	struct mutex mutex;
	struct mutex *driver_mutex;

	struct drm_connector *connector;
	const struct drm_hdcp_helper_funcs *funcs;

	u64 value;
	unsigned int enabled_type;

	struct delayed_work check_work;
	struct work_struct prop_work;

	struct drm_dp_aux *aux;
	const struct drm_hdcp_hdcp1_receiver_reg_lut *hdcp1_lut;
};

struct drm_hdcp_hdcp1_receiver_reg_lut {
	unsigned int bksv;
	unsigned int ri;
	unsigned int aksv;
	unsigned int an;
	unsigned int ainfo;
	unsigned int v[5];
	unsigned int bcaps;
	unsigned int bcaps_mask_repeater_present;
	unsigned int bstatus;
};

static const struct drm_hdcp_hdcp1_receiver_reg_lut drm_hdcp_hdcp1_ddc_lut = {
	.bksv = DRM_HDCP_DDC_BKSV,
	.ri = DRM_HDCP_DDC_RI_PRIME,
	.aksv = DRM_HDCP_DDC_AKSV,
	.an = DRM_HDCP_DDC_AN,
	.ainfo = DRM_HDCP_DDC_AINFO,
	.v = { DRM_HDCP_DDC_V_PRIME(0), DRM_HDCP_DDC_V_PRIME(1),
	       DRM_HDCP_DDC_V_PRIME(2), DRM_HDCP_DDC_V_PRIME(3),
	       DRM_HDCP_DDC_V_PRIME(4) },
	.bcaps = DRM_HDCP_DDC_BCAPS,
	.bcaps_mask_repeater_present = DRM_HDCP_DDC_BCAPS_REPEATER_PRESENT,
	.bstatus = DRM_HDCP_DDC_BSTATUS,
};

static const struct drm_hdcp_hdcp1_receiver_reg_lut drm_hdcp_hdcp1_dpcd_lut = {
	.bksv = DP_AUX_HDCP_BKSV,
	.ri = DP_AUX_HDCP_RI_PRIME,
	.aksv = DP_AUX_HDCP_AKSV,
	.an = DP_AUX_HDCP_AN,
	.ainfo = DP_AUX_HDCP_AINFO,
	.v = { DP_AUX_HDCP_V_PRIME(0), DP_AUX_HDCP_V_PRIME(1),
	       DP_AUX_HDCP_V_PRIME(2), DP_AUX_HDCP_V_PRIME(3),
	       DP_AUX_HDCP_V_PRIME(4) },
	.bcaps = DP_AUX_HDCP_BCAPS,
	.bcaps_mask_repeater_present = DP_BCAPS_REPEATER_PRESENT,

	/*
	 * For some reason the HDMI and DP HDCP specs call this register
	 * definition by different names. In the HDMI spec, it's called BSTATUS,
	 * but in DP it's called BINFO.
	 */
	.bstatus = DP_AUX_HDCP_BINFO,
};

static int drm_hdcp_remote_ddc_read(struct i2c_adapter *i2c,
				    unsigned int offset, u8 *value, size_t len)
{
	int ret;
	u8 start = offset & 0xff;
	struct i2c_msg msgs[] = {
		{
			.addr = DRM_HDCP_DDC_ADDR,
			.flags = 0,
			.len = 1,
			.buf = &start,
		},
		{
			.addr = DRM_HDCP_DDC_ADDR,
			.flags = I2C_M_RD,
			.len = len,
			.buf = value
		}
	};
	ret = i2c_transfer(i2c, msgs, ARRAY_SIZE(msgs));
	if (ret == ARRAY_SIZE(msgs))
		return 0;
	return ret >= 0 ? -EIO : ret;
}

static int drm_hdcp_remote_dpcd_read(struct drm_dp_aux *aux,
				     unsigned int offset, u8 *value,
				     size_t len)
{
	ssize_t ret;

	ret = drm_dp_dpcd_read(aux, offset, value, len);
	if (ret != len) {
		if (ret >= 0)
			return -EIO;
		return ret;
	}

	return 0;
}

static int drm_hdcp_remote_read(struct drm_hdcp_helper_data *data,
				unsigned int offset, u8 *value, u8 len)
{
	if (data->aux)
		return drm_hdcp_remote_dpcd_read(data->aux, offset, value, len);
	else
		return drm_hdcp_remote_ddc_read(data->connector->ddc, offset, value, len);
}

static int drm_hdcp_remote_ddc_write(struct i2c_adapter *i2c,
				     unsigned int offset, u8 *buffer,
				     size_t size)
{
	int ret;
	u8 *write_buf;
	struct i2c_msg msg;

	write_buf = kzalloc(size + 1, GFP_KERNEL);
	if (!write_buf)
		return -ENOMEM;

	write_buf[0] = offset & 0xff;
	memcpy(&write_buf[1], buffer, size);

	msg.addr = DRM_HDCP_DDC_ADDR;
	msg.flags = 0,
	msg.len = size + 1,
	msg.buf = write_buf;

	ret = i2c_transfer(i2c, &msg, 1);
	if (ret == 1)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

	kfree(write_buf);
	return ret;
}

static int drm_hdcp_remote_dpcd_write(struct drm_dp_aux *aux,
				     unsigned int offset, u8 *value,
				     size_t len)
{
	ssize_t ret;

	ret = drm_dp_dpcd_write(aux, offset, value, len);
	if (ret != len) {
		if (ret >= 0)
			return -EIO;
		return ret;
	}

	return 0;
}

static int drm_hdcp_remote_write(struct drm_hdcp_helper_data *data,
				 unsigned int offset, u8 *value, u8 len)
{
	if (data->aux)
		return drm_hdcp_remote_dpcd_write(data->aux, offset, value, len);
	else
		return drm_hdcp_remote_ddc_write(data->connector->ddc, offset,
						 value, len);
}

static bool drm_hdcp_is_ksv_valid(struct drm_hdcp_ksv *ksv)
{
	/* Valid Ksv has 20 0's and 20 1's */
	return hweight32(ksv->words[0]) + hweight32(ksv->words[1]) == 20;
}

static int drm_hdcp_read_valid_bksv(struct drm_hdcp_helper_data *data,
				    struct drm_hdcp_ksv *bksv)
{
	int ret, i, tries = 2;

	/* HDCP spec states that we must retry the bksv if it is invalid */
	for (i = 0; i < tries; i++) {
		ret = drm_hdcp_remote_read(data, data->hdcp1_lut->bksv,
					   bksv->bytes, DRM_HDCP_KSV_LEN);
		if (ret)
			return ret;

		if (drm_hdcp_is_ksv_valid(bksv))
			break;
	}
	if (i == tries) {
		drm_dbg_kms(data->connector->dev, "Bksv is invalid %*ph\n",
			    DRM_HDCP_KSV_LEN, bksv->bytes);
		return -ENODEV;
	}

	return 0;
}

/**
 * drm_hdcp_helper_hdcp1_capable - Checks if the sink is capable of HDCP 1.x.
 *
 * @data: pointer to the HDCP helper data.
 * @capable: pointer to a bool which will contain true if the sink is capable.
 *
 * Returns:
 * -errno if the transacation between source and sink fails.
 */
int drm_hdcp_helper_hdcp1_capable(struct drm_hdcp_helper_data *data,
				  bool *capable)
{
	/*
	 * DisplayPort has a dedicated bit for this in DPCD whereas HDMI spec
	 * states that transmitters should use bksv to determine capability.
	 */
	if (data->aux) {
		int ret;
		u8 bcaps;

		ret = drm_hdcp_remote_read(data, data->hdcp1_lut->bcaps,
					   &bcaps, 1);
		*capable = !ret && (bcaps & DP_BCAPS_HDCP_CAPABLE);
	} else {
		struct drm_hdcp_ksv bksv;

		*capable = drm_hdcp_read_valid_bksv(data, &bksv) == 0;
	}

	return 0;
}
EXPORT_SYMBOL(drm_hdcp_helper_hdcp1_capable);

static void drm_hdcp_update_value(struct drm_hdcp_helper_data *data,
				  u64 value, bool update_property)
{
	WARN_ON(!mutex_is_locked(&data->mutex));

	data->value = value;
	if (update_property) {
		drm_connector_get(data->connector);
		schedule_work(&data->prop_work);
	}
}

static int
drm_hdcp_helper_hdcp1_ksv_fifo_ready(struct drm_hdcp_helper_data *data)
{
	int ret;
	u8 val, mask;

	/* KSV FIFO ready bit is stored in different locations on DP v. HDMI */
	if (data->aux) {
		ret = drm_hdcp_remote_dpcd_read(data->aux, DP_AUX_HDCP_BSTATUS,
						&val, 1);
		mask = DP_BSTATUS_READY;
	} else {
		ret = drm_hdcp_remote_ddc_read(data->connector->ddc,
					       DRM_HDCP_DDC_BCAPS, &val, 1);
		mask = DRM_HDCP_DDC_BCAPS_KSV_FIFO_READY;
	}
	if (ret)
		return ret;
	if (val & mask)
		return 0;

	return -EAGAIN;
}

static int
drm_hdcp_helper_hdcp1_read_ksv_fifo(struct drm_hdcp_helper_data *data, u8 *fifo,
				    u8 num_downstream)
{
	struct drm_device *dev = data->connector->dev;
	int ret, i;

	/* Over HDMI, read the whole thing at once */
	if (data->connector->ddc) {
		ret = drm_hdcp_remote_ddc_read(data->connector->ddc,
					       DRM_HDCP_DDC_KSV_FIFO, fifo,
					       num_downstream * DRM_HDCP_KSV_LEN);
		if (ret)
			drm_err(dev, "DDC ksv fifo read failed (%d)\n", ret);
		return ret;
	}

	/* Over DP, read via 15 byte window (3 entries @ 5 bytes each) */
	for (i = 0; i < num_downstream; i += 3) {
		size_t len = min(num_downstream - i, 3) * DRM_HDCP_KSV_LEN;
		ret = drm_hdcp_remote_dpcd_read(data->aux, DP_AUX_HDCP_KSV_FIFO,
						fifo + i * DRM_HDCP_KSV_LEN,
						len);
		if (ret) {
			drm_err(dev, "Read ksv[%d] from DP/AUX failed (%d)\n",
				i, ret);
			return ret;
		}
	}

	return 0;
}

static int drm_hdcp_helper_hdcp1_read_v_prime(struct drm_hdcp_helper_data *data,
					      u32 *v_prime)
{
	struct drm_device *dev = data->connector->dev;
	int ret, i;

	for (i = 0; i < DRM_HDCP_V_PRIME_NUM_PARTS; i++) {
		ret = drm_hdcp_remote_read(data, data->hdcp1_lut->v[i],
					   (u8 *)&v_prime[i],
					   DRM_HDCP_V_PRIME_PART_LEN);
		if (ret) {
			drm_dbg_kms(dev, "Read v'[%d] from failed (%d)\n", i, ret);
			return ret >= 0 ? -EIO : ret;
		}
	}
	return 0;
}

static int
drm_hdcp_helper_hdcp1_authenticate_downstream(struct drm_hdcp_helper_data *data)
{
	struct drm_connector *connector = data->connector;
	struct drm_device *dev = connector->dev;
	u32 v_prime[DRM_HDCP_V_PRIME_NUM_PARTS];
	u8 bstatus[DRM_HDCP_BSTATUS_LEN];
	u8 num_downstream, *ksv_fifo;
	int ret, i, tries = 3;

	ret = read_poll_timeout(drm_hdcp_helper_hdcp1_ksv_fifo_ready, ret, !ret,
				10 * 1000, 5 * 1000 * 1000, false, data);
	if (ret) {
		drm_err(dev, "Failed to poll ksv ready, %d\n", ret);
		return ret;
	}

	ret = drm_hdcp_remote_read(data, data->hdcp1_lut->bstatus,
				   bstatus, DRM_HDCP_BSTATUS_LEN);
	if (ret)
		return ret;

	/*
	 * When repeater reports 0 device count, HDCP1.4 spec allows disabling
	 * the HDCP encryption. That implies that repeater can't have its own
	 * display. As there is no consumption of encrypted content in the
	 * repeater with 0 downstream devices, we are failing the
	 * authentication.
	 */
	num_downstream = DRM_HDCP_NUM_DOWNSTREAM(bstatus[0]);
	if (num_downstream == 0) {
		drm_err(dev, "Repeater with zero downstream devices, %*ph\n",
			DRM_HDCP_BSTATUS_LEN, bstatus);
		return -EINVAL;
	}

	ksv_fifo = kcalloc(DRM_HDCP_KSV_LEN, num_downstream, GFP_KERNEL);
	if (!ksv_fifo)
		return -ENOMEM;

	ret = drm_hdcp_helper_hdcp1_read_ksv_fifo(data, ksv_fifo,
						  num_downstream);
	if (ret) {
		drm_err(dev, "Failed to read ksv fifo, %d/%d\n", num_downstream,
			ret);
		goto out;
	}

	if (drm_hdcp_check_ksvs_revoked(dev, ksv_fifo, num_downstream)) {
		drm_err(dev, "Revoked Ksv(s) in ksv_fifo\n");
		ret = -EPERM;
		goto out;
	}

	/*
	 * When V prime mismatches, DP Spec mandates re-read of
	 * V prime atleast twice.
	 */
	for (i = 0; i < tries; i++) {
		ret = drm_hdcp_helper_hdcp1_read_v_prime(data, v_prime);
		if (ret)
			continue;

		ret = data->funcs->hdcp1_store_ksv_fifo(connector, ksv_fifo,
							num_downstream,
							bstatus, v_prime);
		if (!ret)
			break;
	}
	if (ret)
		drm_err(dev, "Could not validate KSV FIFO with V' %d\n", ret);

out:
	if (!ret)
		drm_dbg_kms(dev, "HDCP is enabled (%d downstream devices)\n",
			    num_downstream);

	kfree(ksv_fifo);
	return ret;
}

static int drm_hdcp_helper_hdcp1_validate_ri(struct drm_hdcp_helper_data *data)
{
	union {
		u32 word;
		u8 bytes[DRM_HDCP_RI_LEN];
	} ri_prime = { .word = 0 };
	struct drm_connector *connector = data->connector;
	struct drm_device *dev = connector->dev;
	int ret;

	ret = drm_hdcp_remote_read(data, data->hdcp1_lut->ri, ri_prime.bytes,
				   DRM_HDCP_RI_LEN);
	if (ret) {
		drm_err(dev, "Failed to read R0' %d\n", ret);
		return ret;
	}

	return data->funcs->hdcp1_match_ri(connector, ri_prime.word);
}

static int drm_hdcp_helper_hdcp1_authenticate(struct drm_hdcp_helper_data *data)
{
	union {
		u32 word;
		u8 bytes[DRM_HDCP_BSTATUS_LEN];
	} bstatus;
	const struct drm_hdcp_helper_funcs *funcs = data->funcs;
	struct drm_connector *connector = data->connector;
	struct drm_device *dev = connector->dev;
	unsigned long r0_prime_timeout, r0_prime_remaining_us = 0, tmp_jiffies;
	struct drm_hdcp_ksv aksv;
	struct drm_hdcp_ksv bksv;
	struct drm_hdcp_an an;
	bool repeater_present;
	int ret, i, tries = 3;
	u8 bcaps;

	if (funcs->hdcp1_read_an_aksv) {
		ret = funcs->hdcp1_read_an_aksv(connector, an.words, aksv.words);
		if (ret) {
			drm_err(dev, "Failed to read An/Aksv values, %d\n", ret);
			return ret;
		}

		ret = drm_hdcp_remote_write(data, data->hdcp1_lut->an, an.bytes,
					DRM_HDCP_AN_LEN);
		if (ret) {
			drm_err(dev, "Failed to write An to receiver, %d\n", ret);
			return ret;
		}

		ret = drm_hdcp_remote_write(data, data->hdcp1_lut->aksv, aksv.bytes,
					DRM_HDCP_KSV_LEN);
		if (ret) {
			drm_err(dev, "Failed to write Aksv to receiver, %d\n", ret);
			return ret;
		}
	} else {
		ret = funcs->hdcp1_send_an_aksv(connector);
		if (ret) {
			drm_err(dev, "Failed to read An/Aksv values, %d\n", ret);
			return ret;
		}
	}

	/*
	 * Timeout for R0' to become available. The spec says 100ms from Aksv,
	 * but some monitors can take longer than this. We'll set the timeout at
	 * 300ms just to be sure.
	 */
	r0_prime_timeout = jiffies + msecs_to_jiffies(300);

	memset(&bksv, 0, sizeof(bksv));

	ret = drm_hdcp_read_valid_bksv(data, &bksv);
	if (ret < 0)
		return ret;

	if (drm_hdcp_check_ksvs_revoked(dev, bksv.bytes, 1)) {
		drm_err(dev, "BKSV is revoked\n");
		return -EPERM;
	}

	ret = drm_hdcp_remote_read(data, data->hdcp1_lut->bcaps, &bcaps, 1);
	if (ret)
		return ret;

	memset(&bstatus, 0, sizeof(bstatus));

	ret = drm_hdcp_remote_read(data, data->hdcp1_lut->bstatus,
				   bstatus.bytes, DRM_HDCP_BSTATUS_LEN);
	if (ret)
		return ret;

	if (DRM_HDCP_MAX_DEVICE_EXCEEDED(bstatus.bytes[0]) ||
	    DRM_HDCP_MAX_CASCADE_EXCEEDED(bstatus.bytes[1])) {
		drm_err(dev, "Max Topology Limit Exceeded, bstatus=%*ph\n",
			DRM_HDCP_BSTATUS_LEN, bstatus.bytes);
		return -EPERM;
	}

	repeater_present = bcaps & data->hdcp1_lut->bcaps_mask_repeater_present;

	ret = funcs->hdcp1_store_receiver_info(connector, bksv.words,
					       bstatus.word, bcaps,
					       repeater_present);
	if (ret) {
		drm_err(dev, "Failed to store bksv, %d\n", ret);
		return ret;
	}

	ret = funcs->hdcp1_enable_encryption(connector);
	if (ret)
		return ret;

	ret = funcs->hdcp1_wait_for_r0(connector);
	if (ret)
		return ret;

	tmp_jiffies = jiffies;
	if (time_before(tmp_jiffies, r0_prime_timeout))
		r0_prime_remaining_us = jiffies_to_usecs(r0_prime_timeout - tmp_jiffies);

	/*
	 * Wait for R0' to become available.
	 *
	 * On DP, there's an R0_READY bit available but no such bit
	 * exists on HDMI. So poll the ready bit for DP and just wait the
	 * remainder of the 300 ms timeout for HDMI.
	 */
	if (data->aux) {
		u8 val;
		ret = read_poll_timeout(drm_hdcp_remote_dpcd_read, ret,
					!ret && (val & DP_BSTATUS_R0_PRIME_READY),
					1000, r0_prime_remaining_us, false,
					data->aux, DP_AUX_HDCP_BSTATUS, &val, 1);
		if (ret) {
			drm_err(dev, "R0' did not become ready %d\n", ret);
			return ret;
		}
	} else {
		usleep_range(r0_prime_remaining_us,
			     r0_prime_remaining_us + 1000);
	}

	/*
	 * DP HDCP Spec mandates the two more reattempt to read R0, incase
	 * of R0 mismatch.
	 */
	for (i = 0; i < tries; i++) {
		ret = drm_hdcp_helper_hdcp1_validate_ri(data);
		if (!ret)
			break;
	}
	if (ret) {
		drm_err(dev, "Failed to match R0/R0', aborting HDCP %d\n", ret);
		return ret;
	}

	if (repeater_present)
		return drm_hdcp_helper_hdcp1_authenticate_downstream(data);

	drm_dbg_kms(dev, "HDCP is enabled (no repeater present)\n");
	return 0;
}

static int drm_hdcp_helper_hdcp1_enable(struct drm_hdcp_helper_data *data)
{
	struct drm_connector *connector = data->connector;
	struct drm_device *dev = connector->dev;
	int i, ret, tries = 3;

	drm_dbg_kms(dev, "[%s:%d] HDCP is being enabled...\n", connector->name,
		    connector->base.id);

	/* Incase of authentication failures, HDCP spec expects reauth. */
	for (i = 0; i < tries; i++) {
		ret = drm_hdcp_helper_hdcp1_authenticate(data);
		if (!ret)
			return 0;

		drm_dbg_kms(dev, "HDCP Auth failure (%d)\n", ret);

		/* Ensuring HDCP encryption and signalling are stopped. */
		data->funcs->hdcp1_disable(data->connector);
	}

	drm_err(dev, "HDCP authentication failed (%d tries/%d)\n", tries, ret);
	return ret;
}

static inline
void drm_hdcp_helper_driver_lock(struct drm_hdcp_helper_data *data)
{
	if (data->driver_mutex)
		mutex_lock(data->driver_mutex);
}

static inline
void drm_hdcp_helper_driver_unlock(struct drm_hdcp_helper_data *data)
{
	if (data->driver_mutex)
		mutex_unlock(data->driver_mutex);
}

static int drm_hdcp_helper_enable_hdcp(struct drm_hdcp_helper_data *data,
				       struct drm_atomic_state *state,
				       struct mutex *driver_mutex)
{
	struct drm_connector *connector = data->connector;
	struct drm_connector_state *conn_state;
	struct drm_device *dev = connector->dev;
	unsigned long check_link_interval = DRM_HDCP2_CHECK_PERIOD_MS;
	bool capable;
	int ret = 0;

	conn_state = drm_atomic_get_new_connector_state(state, connector);

	mutex_lock(&data->mutex);

	if (data->value == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
		drm_hdcp_update_value(data, DRM_MODE_CONTENT_PROTECTION_ENABLED,
				      true);
		goto out_data_mutex;
	}

	drm_WARN_ON(dev, data->driver_mutex != NULL);
	data->driver_mutex = driver_mutex;

	drm_hdcp_helper_driver_lock(data);

	if (data->funcs->setup) {
		ret = data->funcs->setup(connector, state);
		if (ret) {
			drm_err(dev, "Failed to setup HDCP %d\n", ret);
			goto out;
		}
	}

	if (!data->funcs->are_keys_valid ||
	    !data->funcs->are_keys_valid(connector)) {
		if (data->funcs->load_keys) {
			ret = data->funcs->load_keys(connector);
			if (ret) {
				drm_err(dev, "Failed to load HDCP keys %d\n", ret);
				goto out;
			}
		}
	}

	/*
	 * Considering that HDCP2.2 is more secure than HDCP1.4, If the setup
	 * is capable of HDCP2.2, it is preferred to use HDCP2.2.
	 */
	ret = data->funcs->hdcp2_capable(connector, &capable);
	if (ret) {
		drm_err(dev, "HDCP 2.x capability check failed %d\n", ret);
		goto out;
	}
	if (capable) {
		data->enabled_type = DRM_MODE_HDCP_CONTENT_TYPE1;
		ret = data->funcs->hdcp2_enable(connector);
		if (!ret) {
			check_link_interval = DRM_HDCP2_CHECK_PERIOD_MS;
			goto out;
		}
	}

	/*
	 * When HDCP2.2 fails and Content Type is not Type1, HDCP1.4 will
	 * be attempted.
	 */
	ret = drm_hdcp_helper_hdcp1_capable(data, &capable);
	if (ret) {
		drm_err(dev, "HDCP 1.x capability check failed %d\n", ret);
		goto out;
	}
	if (capable && conn_state->content_type != DRM_MODE_HDCP_CONTENT_TYPE1) {
		data->enabled_type = DRM_MODE_HDCP_CONTENT_TYPE0;
		ret = drm_hdcp_helper_hdcp1_enable(data);
		if (!ret)
			check_link_interval = DRM_HDCP_CHECK_PERIOD_MS;
	}

out:
	if (!ret) {
		schedule_delayed_work(&data->check_work, check_link_interval);
		drm_hdcp_update_value(data, DRM_MODE_CONTENT_PROTECTION_ENABLED,
				      true);
	}

	drm_hdcp_helper_driver_unlock(data);
	if (ret)
		data->driver_mutex = NULL;

out_data_mutex:
	mutex_unlock(&data->mutex);
	return ret;
}

static int drm_hdcp_helper_disable_hdcp(struct drm_hdcp_helper_data *data)
{
	int ret = 0;

	mutex_lock(&data->mutex);
	drm_hdcp_helper_driver_lock(data);

	if (data->value == DRM_MODE_CONTENT_PROTECTION_UNDESIRED)
		goto out;

	drm_dbg_kms(data->connector->dev, "[%s:%d] HDCP is being disabled...\n",
		    data->connector->name, data->connector->base.id);

	drm_hdcp_update_value(data, DRM_MODE_CONTENT_PROTECTION_UNDESIRED, true);

	if (data->enabled_type == DRM_MODE_HDCP_CONTENT_TYPE1)
		ret = data->funcs->hdcp2_disable(data->connector);
	else
		ret = data->funcs->hdcp1_disable(data->connector);

	drm_dbg_kms(data->connector->dev, "HDCP is disabled\n");

out:
	drm_hdcp_helper_driver_unlock(data);
	data->driver_mutex = NULL;
	mutex_unlock(&data->mutex);
	cancel_delayed_work_sync(&data->check_work);
	return ret;
}

/**
 * drm_hdcp_helper_atomic_commit - Helper for drivers to call during commit to
 * enable/disable HDCP
 *
 * @data: pointer to the @drm_hdcp_helper_data for the connector
 * @state: pointer to the atomic state being committed
 * @driver_mutex: driver-provided lock to be used while interacting with the driver
 *
 * This function can be used by display drivers to determine when HDCP should be
 * enabled or disabled based on the connector state. It should be called during
 * steady-state commits as well as connector enable/disable. The function will
 * handle the HDCP authentication/encryption logic, calling back into the driver
 * when source operations are necessary.
 *
 * @driver_mutex will be retained and used for the duration of the HDCP session
 * since it will be needed for link checks and retries. This mutex is useful if
 * the driver has shared resources across connectors which must be serialized.
 * For example, driver_mutex can be used for MST connectors sharing a common
 * encoder which should not be accessed/changed concurrently. When the
 * connector's session is torn down, the mutex will be forgotten by the helper
 * for this connector until the next session.
 */
void drm_hdcp_helper_atomic_commit(struct drm_hdcp_helper_data *data,
				   struct drm_atomic_state *state,
				   struct mutex *driver_mutex)
{
	struct drm_connector *connector = data->connector;
	struct drm_connector_state *conn_state;
	bool type_changed;

	conn_state = drm_atomic_get_new_connector_state(state, connector);

	type_changed = conn_state->hdcp_content_type != data->enabled_type;

	if (conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		drm_hdcp_helper_disable_hdcp(data);
		return;
	}

	if (!conn_state->crtc) {
		drm_hdcp_helper_disable_hdcp(data);

		/* Restore property to DESIRED so it's retried later */
		if (conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
			mutex_lock(&data->mutex);
			drm_hdcp_update_value(data, DRM_MODE_CONTENT_PROTECTION_DESIRED,
					true);
			mutex_unlock(&data->mutex);
		}
		return;
	}

	/* Already enabled */
	if (conn_state->content_protection == DRM_MODE_CONTENT_PROTECTION_ENABLED)
		return;

	/* Disable and re-enable HDCP on content type change */
	if (type_changed)
		drm_hdcp_helper_disable_hdcp(data);

	drm_hdcp_helper_enable_hdcp(data, state, driver_mutex);
}
EXPORT_SYMBOL(drm_hdcp_helper_atomic_commit);

static void drm_hdcp_helper_prop_work(struct work_struct *work)
{
	struct drm_hdcp_helper_data *data = container_of(work,
							 struct drm_hdcp_helper_data,
							 prop_work);
	struct drm_connector *connector = data->connector;
	struct drm_device *dev = connector->dev;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	mutex_lock(&data->mutex);

	/*
	 * This worker is only used to flip between ENABLED/DESIRED. Either of
	 * those to UNDESIRED is handled by core. If value == UNDESIRED,
	 * we're running just after hdcp has been disabled, so just exit
	 */
	if (data->value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED)
		drm_hdcp_update_content_protection(connector, data->value);

	mutex_unlock(&data->mutex);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
}

static int drm_hdcp_hdcp1_check_link(struct drm_hdcp_helper_data *data)
{
	struct drm_connector *connector = data->connector;
	struct drm_device *dev = connector->dev;
	int ret;

	if (data->funcs->hdcp1_check_link) {
		ret = data->funcs->hdcp1_check_link(connector);
		if (ret)
			goto retry;
	}

	/* The link is checked differently for DP and HDMI */
	if (data->aux) {
		u8 bstatus;
		ret = drm_hdcp_remote_dpcd_read(data->aux, DP_AUX_HDCP_BSTATUS,
						&bstatus, 1);
		if (ret) {
			drm_err(dev, "Failed to read dpcd bstatus, %d\n", ret);
			return ret;
		}
		if (bstatus & (DP_BSTATUS_LINK_FAILURE | DP_BSTATUS_REAUTH_REQ))
			ret = -EINVAL;
	} else {
		ret = drm_hdcp_helper_hdcp1_validate_ri(data);
		if (ret)
			drm_err(dev,"Ri' mismatch, check failed (%d)\n", ret);
	}
	if (!ret)
		return 0;

retry:
	drm_err(dev, "[%s:%d] HDCP link failed, retrying authentication\n",
		connector->name, connector->base.id);

	ret = data->funcs->hdcp1_disable(connector);
	if (ret) {
		drm_err(dev, "Failed to disable hdcp (%d)\n", ret);
		drm_hdcp_update_value(data, DRM_MODE_CONTENT_PROTECTION_DESIRED,
				      true);
		return ret;
	}

	ret = drm_hdcp_helper_hdcp1_enable(data);
	if (ret) {
		drm_err(dev, "Failed to enable hdcp (%d)\n", ret);
		drm_hdcp_update_value(data, DRM_MODE_CONTENT_PROTECTION_DESIRED,
				      true);
		return ret;
	}

	return 0;
}

static int drm_hdcp_hdcp2_check_link(struct drm_hdcp_helper_data *data)
{
	struct drm_connector *connector = data->connector;
	struct drm_device *dev = connector->dev;
	int ret;

	ret = data->funcs->hdcp2_check_link(connector);
	if (!ret)
		return 0;

	drm_err(dev, "[%s:%d] HDCP2 link failed, retrying authentication\n",
		connector->name, connector->base.id);

	ret = data->funcs->hdcp2_disable(connector);
	if (ret) {
		drm_err(dev, "Failed to disable hdcp2 (%d)\n", ret);
		drm_hdcp_update_value(data, DRM_MODE_CONTENT_PROTECTION_DESIRED,
				      true);
		return ret;
	}

	ret = data->funcs->hdcp2_enable(connector);
	if (ret) {
		drm_err(dev, "Failed to enable hdcp2 (%d)\n", ret);
		drm_hdcp_update_value(data, DRM_MODE_CONTENT_PROTECTION_DESIRED,
				      true);
		return ret;
	}

	return 0;
}

static void drm_hdcp_helper_check_work(struct work_struct *work)
{
	struct drm_hdcp_helper_data *data = container_of(to_delayed_work(work),
							 struct drm_hdcp_helper_data,
							 check_work);
	unsigned long check_link_interval;

	mutex_lock(&data->mutex);
	if (data->value != DRM_MODE_CONTENT_PROTECTION_ENABLED)
		goto out_data_mutex;

	drm_hdcp_helper_driver_lock(data);

	if (data->enabled_type == DRM_MODE_HDCP_CONTENT_TYPE1) {
		if (drm_hdcp_hdcp2_check_link(data))
			goto out;
		check_link_interval = DRM_HDCP2_CHECK_PERIOD_MS;
	} else {
		if (drm_hdcp_hdcp1_check_link(data))
			goto out;
		check_link_interval = DRM_HDCP_CHECK_PERIOD_MS;
	}
	schedule_delayed_work(&data->check_work, check_link_interval);

out:
	drm_hdcp_helper_driver_unlock(data);
out_data_mutex:
	mutex_unlock(&data->mutex);
}

/**
 * drm_hdcp_helper_schedule_hdcp_check - Schedule a check link cycle.
 *
 * @data: Pointer to the HDCP helper data.
 *
 * This function will kick off a check link cycle on behalf of the caller. This
 * can be used by DP short hpd interrupt handlers, where the driver must poke
 * the helper to check the link is still valid.
 */
void drm_hdcp_helper_schedule_hdcp_check(struct drm_hdcp_helper_data *data)
{
	schedule_delayed_work(&data->check_work, 0);
}
EXPORT_SYMBOL(drm_hdcp_helper_schedule_hdcp_check);

static struct drm_hdcp_helper_data *
drm_hdcp_helper_initialize(struct drm_connector *connector,
			   const struct drm_hdcp_helper_funcs *funcs,
			   bool attach_content_type_property)
{
	struct drm_hdcp_helper_data *out;
	int ret;

	out = kzalloc(sizeof(*out), GFP_KERNEL);
	if (!out)
		return ERR_PTR(-ENOMEM);

	out->connector = connector;
	out->funcs = funcs;

	mutex_init(&out->mutex);
	out->value = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;

	INIT_DELAYED_WORK(&out->check_work, drm_hdcp_helper_check_work);
	INIT_WORK(&out->prop_work, drm_hdcp_helper_prop_work);

	ret = drm_connector_attach_content_protection_property(connector,
			attach_content_type_property);
	if (ret) {
		drm_hdcp_helper_destroy(out);
		return ERR_PTR(ret);
	}

	return out;
}

/**
 * drm_hdcp_helper_initialize_dp - Initializes the HDCP helpers for a
 * DisplayPort connector
 *
 * @connector: pointer to the DisplayPort connector.
 * @funcs: pointer to the vtable of HDCP helper funcs for this connector.
 * @attach_content_type_property: True if the content_type property should be
 * attached.
 *
 * This function intializes the HDCP helper for the given DisplayPort connector.
 * This involves creating the Content Protection property as well as the Content
 * Type property (if desired). Upon success, it will return a pointer to the
 * HDCP helper data. Ownership of the underlaying memory is transfered to the
 * caller and should be freed using drm_hdcp_helper_destroy().
 *
 * Returns:
 * Pointer to newly created HDCP helper data. PTR_ERR on failure.
 */
struct drm_hdcp_helper_data *
drm_hdcp_helper_initialize_dp(struct drm_connector *connector,
			      struct drm_dp_aux *aux,
			      const struct drm_hdcp_helper_funcs *funcs,
			      bool attach_content_type_property)
{
	struct drm_hdcp_helper_data *out;

	out = drm_hdcp_helper_initialize(connector, funcs,
					 attach_content_type_property);
	if (IS_ERR(out))
		return out;

	out->aux = aux;
	out->hdcp1_lut = &drm_hdcp_hdcp1_dpcd_lut;

	return out;
}
EXPORT_SYMBOL(drm_hdcp_helper_initialize_dp);

/**
 * drm_hdcp_helper_initialize_hdmi - Initializes the HDCP helpers for an HDMI
 * connector
 *
 * @connector: pointer to the HDMI connector.
 * @funcs: pointer to the vtable of HDCP helper funcs for this connector.
 * @attach_content_type_property: True if the content_type property should be
 * attached.
 *
 * This function intializes the HDCP helper for the given HDMI connector. This
 * involves creating the Content Protection property as well as the Content Type
 * property (if desired). Upon success, it will return a pointer to the HDCP
 * helper data. Ownership of the underlaying memory is transfered to the caller
 * and should be freed using drm_hdcp_helper_destroy().
 *
 * Returns:
 * Pointer to newly created HDCP helper data. PTR_ERR on failure.
 */
struct drm_hdcp_helper_data *
drm_hdcp_helper_initialize_hdmi(struct drm_connector *connector,
				const struct drm_hdcp_helper_funcs *funcs,
				bool attach_content_type_property)
{
	struct drm_hdcp_helper_data *out;

	out = drm_hdcp_helper_initialize(connector, funcs,
					 attach_content_type_property);
	if (IS_ERR(out))
		return out;

	out->hdcp1_lut = &drm_hdcp_hdcp1_ddc_lut;

	return out;
}
EXPORT_SYMBOL(drm_hdcp_helper_initialize_hdmi);

/**
 * drm_hdcp_helper_destroy - Destroys the given HDCP helper data.
 *
 * @data: Pointer to the HDCP helper data.
 *
 * This function cleans up and destroys the HDCP helper data created by
 * drm_hdcp_helper_initialize_dp() or drm_hdcp_helper_initialize_hdmi().
 */
void drm_hdcp_helper_destroy(struct drm_hdcp_helper_data *data)
{
	struct drm_connector *connector;

	if (!data)
		return;

	connector = data->connector;

	/*
	 * If the connector is registered, it's possible userspace could kick
	 * off another HDCP enable, which would re-spawn the workers.
	 */
	drm_WARN_ON(connector->dev,
		    connector->registration_state == DRM_CONNECTOR_REGISTERED);

	/*
	 * Now that the connector is not registered, check_work won't be run,
	 * but cancel any outstanding instances of it
	 */
	cancel_delayed_work_sync(&data->check_work);

	/*
	 * We don't cancel prop_work in the same way as check_work since it
	 * requires connection_mutex which could be held while calling this
	 * function. Instead, we rely on the connector references grabbed before
	 * scheduling prop_work to ensure the connector is alive when prop_work
	 * is run. So if we're in the destroy path (which is where this
	 * function should be called), we're "guaranteed" that prop_work is not
	 * active (tl;dr This Should Never Happen).
	 */
	drm_WARN_ON(connector->dev, work_pending(&data->prop_work));

	kfree(data);
}
EXPORT_SYMBOL(drm_hdcp_helper_destroy);
