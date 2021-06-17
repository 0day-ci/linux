/* SPDX-License-Identifier: GPL-2.0 */
#ifndef INTEL_EXTENDED_CAPS_H
#define INTEL_EXTENDED_CAPS_H

/* Intel Extended Features */
#define INTEL_EXT_CAP_ID_TELEMETRY	2
#define INTEL_EXT_CAP_ID_WATCHER	3
#define INTEL_EXT_CAP_ID_CRASHLOG	4

struct intel_ext_cap_header {
	u8	rev;
	u16	length;
	u16	id;
	u8	num_entries;
	u8	entry_size;
	u8	tbir;
	u32	offset;
};

enum intel_ext_cap_quirks {
	/* Watcher capability not supported */
	EXT_CAP_QUIRK_NO_WATCHER	= BIT(0),

	/* Crashlog capability not supported */
	EXT_CAP_QUIRK_NO_CRASHLOG	= BIT(1),

	/* Use shift instead of mask to read discovery table offset */
	EXT_CAP_QUIRK_TABLE_SHIFT	= BIT(2),

	/* DVSEC not present (provided in driver data) */
	EXT_CAP_QUIRK_NO_DVSEC	= BIT(3),
};

struct intel_ext_cap_platform_info {
	unsigned long quirks;
	struct intel_ext_cap_header **capabilities;
};

int intel_ext_cap_probe(struct pci_dev *pdev, struct intel_ext_cap_platform_info *info);
#endif
