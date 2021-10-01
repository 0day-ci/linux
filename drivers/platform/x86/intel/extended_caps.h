/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _EXTENDED_CAPS_H
#define _EXTENDED_CAPS_H

#include <linux/auxiliary_bus.h>

struct extended_caps_header {
	u8	rev;
	u16	length;
	u16	id;
	u8	num_entries;
	u8	entry_size;
	u8	tbir;
	u32	offset;
};

enum extended_caps_quirks {
	/* Watcher capability not supported */
	EXT_CAPS_QUIRK_NO_WATCHER	= BIT(0),

	/* Crashlog capability not supported */
	EXT_CAPS_QUIRK_NO_CRASHLOG	= BIT(1),

	/* Use shift instead of mask to read discovery table offset */
	EXT_CAPS_QUIRK_TABLE_SHIFT	= BIT(2),

	/* DVSEC not present (provided in driver data) */
	EXT_CAPS_QUIRK_NO_DVSEC		= BIT(3),
};

struct intel_extended_cap_device {
	struct auxiliary_device aux_dev;
	struct extended_caps_header *header;
	struct pci_dev *pcidev;
	struct resource *resource;
	unsigned long quirks;
	int num_resources;
};

struct resource *intel_ext_cap_get_resource(struct intel_extended_cap_device *intel_cap_dev,
					    unsigned int num);
#endif
