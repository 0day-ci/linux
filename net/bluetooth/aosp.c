// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Intel Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "aosp.h"

#define AOSP_OP_LE_GET_VENDOR_CAPABILITIES	0x153
struct aosp_rp_le_get_vendor_capabilities {
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
	__u8	le_address_generation_offloading_support;
	__u32	a2dp_source_offload_capability_mask;
	__u8	bluetooth_quality_report_support;
	__u32	dynamic_audio_buffer_support;
} __packed;

void aosp_do_open(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct aosp_rp_le_get_vendor_capabilities *rp;
	u16 opcode;
	u16 version_supported;

	if (!hdev->aosp_capable)
		return;

	bt_dev_dbg(hdev, "Initialize AOSP extension");

	/* LE Get Vendor Capabilities Command */
	opcode = hci_opcode_pack(0x3f, AOSP_OP_LE_GET_VENDOR_CAPABILITIES);
	skb = __hci_cmd_sync(hdev, opcode, 0, NULL, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_warn(hdev, "AOSP get vendor capabilities (%ld)",
			    PTR_ERR(skb));
		return;
	}

	bt_dev_info(hdev, "aosp le vendor capabilities length %d", skb->len);

	rp = (struct aosp_rp_le_get_vendor_capabilities *)skb->data;

	if (rp->status) {
		bt_dev_err(hdev, "AOSP LE Get Vendor Capabilities status %d",
			   rp->status);
		return;
	}

	version_supported = le16_to_cpu(rp->version_supported);
	bt_dev_info(hdev, "AOSP version 0x%4.4x", version_supported);

	kfree_skb(skb);
}

void aosp_do_close(struct hci_dev *hdev)
{
	if (!hdev->aosp_capable)
		return;

	bt_dev_dbg(hdev, "Cleanup of AOSP extension");
}
