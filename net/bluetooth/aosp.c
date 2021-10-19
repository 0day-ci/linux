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

	/* The bluetooth_quality_report_support is defined at version 0x0062.
	 * Refer to https://cs.android.com/android/platform/superproject/+/
	 *                  master:system/bt/gd/hci/controller.cc;l=477
	 */
	if (version_supported >= 0x0062 &&
	    rp->bluetooth_quality_report_support) {
		hdev->aosp_quality_report = true;
		bt_dev_info(hdev, "bluetooth quality report is supported");
	}

	kfree_skb(skb);
}

void aosp_do_close(struct hci_dev *hdev)
{
	if (!hdev->aosp_capable)
		return;

	bt_dev_dbg(hdev, "Cleanup of AOSP extension");
}

/* BQR command */
#define BQR_OPCODE			hci_opcode_pack(0x3f, 0x015e)

/* BQR report action */
#define REPORT_ACTION_ADD		0x00
#define REPORT_ACTION_DELETE		0x01
#define REPORT_ACTION_CLEAR		0x02

/* BQR event masks */
#define QUALITY_MONITORING		BIT(0)
#define APPRAOCHING_LSTO		BIT(1)
#define A2DP_AUDIO_CHOPPY		BIT(2)
#define SCO_VOICE_CHOPPY		BIT(3)

#define DEFAULT_BQR_EVENT_MASK	(QUALITY_MONITORING | APPRAOCHING_LSTO | \
				 A2DP_AUDIO_CHOPPY | SCO_VOICE_CHOPPY)

/* Reporting at milliseconds so as not to stress the controller too much.
 * Range: 0 ~ 65535 ms
 */
#define DEFALUT_REPORT_INTERVAL_MS	5000

struct aosp_bqr_cp {
	__u8	report_action;
	__u32	event_mask;
	__u16	min_report_interval;
} __packed;

static int enable_quality_report(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct aosp_bqr_cp cp;

	cp.report_action = REPORT_ACTION_ADD;
	cp.event_mask = DEFAULT_BQR_EVENT_MASK;
	cp.min_report_interval = DEFALUT_REPORT_INTERVAL_MS;

	skb = __hci_cmd_sync(hdev, BQR_OPCODE, sizeof(cp), &cp,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Enabling Android BQR failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	kfree_skb(skb);
	return 0;
}

static int disable_quality_report(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct aosp_bqr_cp cp = { 0 };

	cp.report_action = REPORT_ACTION_CLEAR;

	skb = __hci_cmd_sync(hdev, BQR_OPCODE, sizeof(cp), &cp,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Disabling Android BQR failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	kfree_skb(skb);
	return 0;
}

bool aosp_has_quality_report(struct hci_dev *hdev)
{
	return hdev->aosp_quality_report;
}

int aosp_set_quality_report(struct hci_dev *hdev, bool enable)
{
	if (!aosp_has_quality_report(hdev))
		return -EOPNOTSUPP;

	bt_dev_dbg(hdev, "quality report enable %d", enable);

	/* Enable or disable the quality report feature. */
	if (enable)
		return enable_quality_report(hdev);
	else
		return disable_quality_report(hdev);
}
