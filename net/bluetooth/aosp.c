// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Intel Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "aosp.h"

/* Command complete parameters of LE_Get_Vendor_Capabilities_Command
 * The parameters grow over time. The first version that declares the
 * version_supported field is v0.95. Refer to
 * https://cs.android.com/android/platform/superproject/+/master:system/
 *         bt/gd/hci/controller.cc;l=452?q=le_get_vendor_capabilities_handler
 */

/* the base capabilities struct with the version_supported field */
struct aosp_rp_le_get_vendor_capa_v95 {
	__u8	status;
	__u8	max_advt_instances;
	__u8	offloaded_resolution_of_private_address;
	__u16	total_scan_results_storage;
	__u8	max_irk_list_sz;
	__u8	filtering_support;
	__u8	max_filter;
	__u8	activity_energy_info_support;
	__u16	version_supported;
	__u16	total_num_of_advt_tracked;
	__u8	extended_scan_support;
	__u8	debug_logging_supported;
} __packed;

struct aosp_rp_le_get_vendor_capa_v96 {
	struct aosp_rp_le_get_vendor_capa_v95 v95;
	/* v96 */
	__u8	le_address_generation_offloading_support;
} __packed;

struct aosp_rp_le_get_vendor_capa_v98 {
	struct aosp_rp_le_get_vendor_capa_v96 v96;
	/* v98 */
	__u32	a2dp_source_offload_capability_mask;
	__u8	bluetooth_quality_report_support;
} __packed;

struct aosp_rp_le_get_vendor_capa_v100 {
	struct aosp_rp_le_get_vendor_capa_v98 v98;
	/* v100 */
	__u32	dynamic_audio_buffer_support;
} __packed;

void aosp_do_open(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct aosp_rp_le_get_vendor_capa_v95 *base_rp;
	u16 version_supported;

	if (!hdev->aosp_capable)
		return;

	bt_dev_dbg(hdev, "Initialize AOSP extension");

	/* LE Get Vendor Capabilities Command */
	skb = __hci_cmd_sync(hdev, hci_opcode_pack(0x3f, 0x153), 0, NULL,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_warn(hdev, "AOSP get vendor capabilities (%ld)",
			    PTR_ERR(skb));
		return;
	}

	bt_dev_dbg(hdev, "aosp le vendor capabilities length %d", skb->len);

	base_rp = (struct aosp_rp_le_get_vendor_capa_v95 *)skb->data;

	if (base_rp->status) {
		bt_dev_err(hdev, "AOSP LE Get Vendor Capabilities status %d",
			   base_rp->status);
		goto done;
	}

	version_supported = le16_to_cpu(base_rp->version_supported);
	bt_dev_info(hdev, "AOSP version %u", version_supported);

	/* Do not support very old versions. */
	if (version_supported < 95) {
		bt_dev_err(hdev, "capabilities version %u too old",
			   version_supported);
		goto done;
	}

	if (version_supported >= 95) {
		struct aosp_rp_le_get_vendor_capa_v95 *rp;

		rp = (struct aosp_rp_le_get_vendor_capa_v95 *)skb->data;
		if (skb->len < sizeof(*rp))
			goto length_error;
	}

	if (version_supported >= 96) {
		struct aosp_rp_le_get_vendor_capa_v96 *rp;

		rp = (struct aosp_rp_le_get_vendor_capa_v96 *)skb->data;
		if (skb->len < sizeof(*rp))
			goto length_error;
	}

	if (version_supported >= 98) {
		struct aosp_rp_le_get_vendor_capa_v98 *rp;

		rp = (struct aosp_rp_le_get_vendor_capa_v98 *)skb->data;
		if (skb->len < sizeof(*rp))
			goto length_error;

		/* The bluetooth_quality_report_support is defined at version v0.98.
		 * Refer to https://cs.android.com/android/platform/superproject/+/
		 *                  master:system/bt/gd/hci/controller.cc;l=477
		 */
		if (rp->bluetooth_quality_report_support) {
			hdev->aosp_quality_report = true;
			bt_dev_info(hdev, "bluetooth quality report is supported");
		}
	}

	if (version_supported >= 100) {
		struct aosp_rp_le_get_vendor_capa_v100 *rp;

		rp = (struct aosp_rp_le_get_vendor_capa_v100 *)skb->data;
		if (skb->len < sizeof(*rp))
			goto length_error;
	}

	goto done;

length_error:
	bt_dev_err(hdev, "AOSP capabilities length %d too short", skb->len);

done:
	kfree_skb(skb);
}

void aosp_do_close(struct hci_dev *hdev)
{
	if (!hdev->aosp_capable)
		return;

	bt_dev_dbg(hdev, "Cleanup of AOSP extension");
}
