// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Extended Capabilities module
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: David E. Box <david.e.box@linux.intel.com>
 */

#include <linux/bits.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include "intel_extended_caps.h"

/* Intel DVSEC capability vendor space offsets */
#define INTEL_DVSEC_ENTRIES		0xA
#define INTEL_DVSEC_SIZE		0xB
#define INTEL_DVSEC_TABLE		0xC
#define INTEL_DVSEC_TABLE_BAR(x)	((x) & GENMASK(2, 0))
#define INTEL_DVSEC_TABLE_OFFSET(x)	((x) & GENMASK(31, 3))

/* Intel Extended Features */
#define INTEL_EXT_CAP_ID_TELEMETRY	2
#define INTEL_EXT_CAP_ID_WATCHER	3
#define INTEL_EXT_CAP_ID_CRASHLOG	4

#define INTEL_EXT_CAP_PREFIX		"intel_extnd_cap"
#define FEATURE_ID_NAME_LENGTH		25

static int intel_ext_cap_allow_list[] = {
	INTEL_EXT_CAP_ID_TELEMETRY,
	INTEL_EXT_CAP_ID_WATCHER,
	INTEL_EXT_CAP_ID_CRASHLOG,
};

static bool intel_ext_cap_allowed(u16 id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(intel_ext_cap_allow_list); i++)
		if (intel_ext_cap_allow_list[i] == id)
			return true;

	return false;
}

static bool intel_ext_cap_disabled(u16 id, unsigned long quirks)
{
	switch (id) {
	case INTEL_EXT_CAP_ID_WATCHER:
		return !!(quirks & EXT_CAP_QUIRK_NO_WATCHER);

	case INTEL_EXT_CAP_ID_CRASHLOG:
		return !!(quirks & EXT_CAP_QUIRK_NO_CRASHLOG);

	default:
		return false;
	}
}

static int intel_ext_cap_add_dev(struct pci_dev *pdev, struct intel_ext_cap_header *header,
				 unsigned long quirks)
{
	struct device *dev = &pdev->dev;
	struct resource *res, *tmp;
	struct mfd_cell *cell;
	char feature_id_name[FEATURE_ID_NAME_LENGTH];
	int count = header->num_entries;
	int size = header->entry_size;
	int id = header->id;
	int i;

	if (!intel_ext_cap_allowed(id))
		return -EINVAL;

	if (intel_ext_cap_disabled(id, quirks))
		return -EINVAL;

	snprintf(feature_id_name, sizeof(feature_id_name), "%s_%d", INTEL_EXT_CAP_PREFIX, id);

	if (!header->num_entries) {
		dev_err(dev, "Invalid 0 entry count for %s header\n", feature_id_name);
		return -EINVAL;
	}

	if (!header->entry_size) {
		dev_err(dev, "Invalid 0 entry size for %s header\n", feature_id_name);
		return -EINVAL;
	}

	cell = devm_kzalloc(dev, sizeof(*cell), GFP_KERNEL);
	if (!cell)
		return -ENOMEM;

	res = devm_kcalloc(dev, count, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	if (quirks & EXT_CAP_QUIRK_TABLE_SHIFT)
		header->offset >>= 3;

	/*
	 * The DVSEC contains the starting offset and count for a block of
	 * discovery tables, each providing access to monitoring facilities for
	 * a section of the device. Create a resource list of these tables to
	 * provide to the driver.
	 */
	for (i = 0, tmp = res; i < count; i++, tmp++) {
		tmp->start = pdev->resource[header->tbir].start +
			     header->offset + i * (size * sizeof(u32));
		tmp->end = tmp->start + (size * sizeof(u32)) - 1;
		tmp->flags = IORESOURCE_MEM;
	}

	cell->resources = res;
	cell->num_resources = count;
	cell->name = feature_id_name;

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, cell, 1, NULL, 0, NULL);
}

int intel_ext_cap_probe(struct pci_dev *pdev, struct intel_ext_cap_platform_info *info)
{
	unsigned long quirks = 0;
	bool found_devices = false;
	int ret, pos;

	if (info)
		quirks = info->quirks;

	if (info && (info->quirks & EXT_CAP_QUIRK_NO_DVSEC)) {
		struct intel_ext_cap_header **header;

		header = info->capabilities;
		while (*header) {
			ret = intel_ext_cap_add_dev(pdev, *header, quirks);
			if (ret)
				dev_warn(&pdev->dev,
					 "Failed to add device for DVSEC id %d\n",
					 (*header)->id);
			else
				found_devices = true;

			header++;
		}
	} else {
		/* Find DVSEC features */
		pos = 0;
		do {
			struct intel_ext_cap_header header;
			u32 table, hdr;
			u16 vid;

			pos = pci_find_next_ext_capability(pdev, pos, PCI_EXT_CAP_ID_DVSEC);
			if (!pos)
				break;

			pci_read_config_dword(pdev, pos + PCI_DVSEC_HEADER1, &hdr);
			vid = PCI_DVSEC_HEADER1_VID(hdr);
			if (vid != PCI_VENDOR_ID_INTEL)
				continue;

			/* Support only revision 1 */
			header.rev = PCI_DVSEC_HEADER1_REV(hdr);
			if (header.rev != 1) {
				dev_warn(&pdev->dev, "Unsupported DVSEC revision %d\n",
					 header.rev);
				continue;
			}

			header.length = PCI_DVSEC_HEADER1_LEN(hdr);

			pci_read_config_byte(pdev, pos + INTEL_DVSEC_ENTRIES,
					     &header.num_entries);
			pci_read_config_byte(pdev, pos + INTEL_DVSEC_SIZE,
					     &header.entry_size);
			pci_read_config_dword(pdev, pos + INTEL_DVSEC_TABLE,
					      &table);

			header.tbir = INTEL_DVSEC_TABLE_BAR(table);
			header.offset = INTEL_DVSEC_TABLE_OFFSET(table);

			pci_read_config_dword(pdev, pos + PCI_DVSEC_HEADER2, &hdr);
			header.id = PCI_DVSEC_HEADER2_ID(hdr);

			ret = intel_ext_cap_add_dev(pdev, &header, quirks);
			if (ret)
				continue;

			found_devices = true;
		} while (true);
	}

	if (!found_devices)
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL_GPL(intel_ext_cap_probe);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel Extended Capability Core driver");
MODULE_LICENSE("GPL v2");
