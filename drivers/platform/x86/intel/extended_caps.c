// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Extended Capabilities auxiliary bus driver
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: David E. Box <david.e.box@linux.intel.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/idr.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "extended_caps.h"

/* Intel DVSEC capability vendor space offsets */
#define INTEL_DVSEC_ENTRIES		0xA
#define INTEL_DVSEC_SIZE		0xB
#define INTEL_DVSEC_TABLE		0xC
#define INTEL_DVSEC_TABLE_BAR(x)	((x) & GENMASK(2, 0))
#define INTEL_DVSEC_TABLE_OFFSET(x)	((x) & GENMASK(31, 3))
#define INTEL_DVSEC_ENTRY_SIZE		4

/* EXT_CAPS capabilities */
#define EXTENDED_CAP_ID_TELEMETRY	2
#define EXTENDED_CAP_ID_WATCHER		3
#define EXTENDED_CAP_ID_CRASHLOG	4
#define EXTENDED_CAP_ID_SDSI		65

static DEFINE_IDA(extended_caps_ida);

static int extended_caps_allow_list[] = {
	EXTENDED_CAP_ID_TELEMETRY,
	EXTENDED_CAP_ID_WATCHER,
	EXTENDED_CAP_ID_CRASHLOG,
	EXTENDED_CAP_ID_SDSI,
};

struct extended_caps_platform_info {
	struct extended_caps_header **capabilities;
	unsigned long quirks;
};

static const struct extended_caps_platform_info tgl_info = {
	.quirks = EXT_CAPS_QUIRK_NO_WATCHER | EXT_CAPS_QUIRK_NO_CRASHLOG |
		  EXT_CAPS_QUIRK_TABLE_SHIFT,
};

/* DG1 Platform with DVSEC quirk*/
static struct extended_caps_header dg1_telemetry = {
	.length = 0x10,
	.id = 2,
	.num_entries = 1,
	.entry_size = 3,
	.tbir = 0,
	.offset = 0x466000,
};

static struct extended_caps_header *dg1_capabilities[] = {
	&dg1_telemetry,
	NULL
};

static const struct extended_caps_platform_info dg1_info = {
	.capabilities = dg1_capabilities,
	.quirks = EXT_CAPS_QUIRK_NO_DVSEC,
};

static bool extended_caps_allowed(u16 id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(extended_caps_allow_list); i++)
		if (extended_caps_allow_list[i] == id)
			return true;

	return false;
}

static bool extended_caps_disabled(u16 id, unsigned long quirks)
{
	switch (id) {
	case EXTENDED_CAP_ID_WATCHER:
		return !!(quirks & EXT_CAPS_QUIRK_NO_WATCHER);

	case EXTENDED_CAP_ID_CRASHLOG:
		return !!(quirks & EXT_CAPS_QUIRK_NO_CRASHLOG);

	default:
		return false;
	}
}

struct resource *intel_ext_cap_get_resource(struct intel_extended_cap_device *intel_cap_dev,
					    unsigned int num)
{
	u32 i;

	for (i = 0; i < intel_cap_dev->num_resources; i++) {
		struct resource *r = &intel_cap_dev->resource[i];

		if (num-- == 0)
			return r;
	}
	return NULL;
}
EXPORT_SYMBOL_NS(intel_ext_cap_get_resource, INTEL_EXT_CAPS);

static void extended_caps_remove_aux(void *data)
{
	auxiliary_device_delete(data);
	auxiliary_device_uninit(data);
}

static void extended_caps_dev_release(struct device *dev)
{
	struct intel_extended_cap_device *intel_cap_dev =
			container_of(dev, struct intel_extended_cap_device, aux_dev.dev);

	ida_free(&extended_caps_ida, intel_cap_dev->aux_dev.id);
	kfree(intel_cap_dev->resource);
	kfree(intel_cap_dev);
}

static int extended_caps_add_dev(struct pci_dev *pdev, struct extended_caps_header *header,
				 unsigned long quirks)
{
	struct intel_extended_cap_device *intel_cap_dev;
	struct auxiliary_device *aux_dev;
	struct resource *res, *tmp;
	int count = header->num_entries;
	int size = header->entry_size;
	int id = header->id;
	char id_str[8];
	int ret, i;

	if (!extended_caps_allowed(id))
		return -EINVAL;

	if (extended_caps_disabled(id, quirks))
		return -EINVAL;

	if (!header->num_entries) {
		dev_err(&pdev->dev, "Invalid 0 entry count for header id %d\n", id);
		return -EINVAL;
	}

	if (!header->entry_size) {
		dev_err(&pdev->dev, "Invalid 0 entry size for headerid %d\n", id);
		return -EINVAL;
	}

	intel_cap_dev = kzalloc(sizeof(*intel_cap_dev), GFP_KERNEL);
	if (!intel_cap_dev)
		return -ENOMEM;

	res = kcalloc(count, sizeof(*res), GFP_KERNEL);
	if (!res) {
		kfree(intel_cap_dev);
		return -ENOMEM;
	}

	if (quirks & EXT_CAPS_QUIRK_TABLE_SHIFT)
		header->offset >>= 3;

	/*
	 * The DVSEC/VSEC contains the starting offset and count for a block of
	 * discovery tables. Create a resource list of these tables to the
	 * auxiliary device driver.
	 */
	for (i = 0, tmp = res; i < count; i++, tmp++) {
		tmp->start = pdev->resource[header->tbir].start +
			     header->offset + i * (size * sizeof(u32));
		tmp->end = tmp->start + (size * sizeof(u32)) - 1;
		tmp->flags = IORESOURCE_MEM;
	}

	intel_cap_dev->header = header;
	intel_cap_dev->pcidev = pdev;
	intel_cap_dev->resource = res;
	intel_cap_dev->num_resources = count;
	intel_cap_dev->quirks = quirks;

	snprintf(id_str, sizeof(id_str), "%d", id);

	aux_dev = &intel_cap_dev->aux_dev;
	aux_dev->name = id_str;
	aux_dev->dev.parent = &pdev->dev;
	aux_dev->dev.release = extended_caps_dev_release;

	ret = ida_alloc(&extended_caps_ida, GFP_KERNEL);
	if (ret < 0) {
		kfree(res);
		kfree(intel_cap_dev);
		return ret;
	}
	aux_dev->id = ret;

	ret = auxiliary_device_init(aux_dev);
	if (ret < 0) {
		ida_free(&extended_caps_ida, aux_dev->id);
		kfree(res);
		kfree(intel_cap_dev);
		return ret;
	}

	ret = auxiliary_device_add(aux_dev);
	if (ret) {
		auxiliary_device_uninit(aux_dev);
		ida_free(&extended_caps_ida, aux_dev->id);
		kfree(res);
		kfree(intel_cap_dev);
		return ret;
	}

	return devm_add_action_or_reset(&pdev->dev, extended_caps_remove_aux, aux_dev);
}

static bool extended_caps_walk_header(struct pci_dev *pdev, unsigned long quirks,
				      struct extended_caps_header **header)
{
	bool have_devices = false;
	int ret;

	while (*header) {
		ret = extended_caps_add_dev(pdev, *header, quirks);
		if (ret)
			dev_warn(&pdev->dev,
				 "Failed to add device for DVSEC id %d\n",
				 (*header)->id);
		else
			have_devices = true;

		++header;
	}

	return have_devices;
}

static bool extended_caps_walk_dvsec(struct pci_dev *pdev, unsigned long quirks)
{
	bool have_devices = false;
	int pos = 0;

	do {
		struct extended_caps_header header;
		u32 table, hdr;
		u16 vid;
		int ret;

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

		ret = extended_caps_add_dev(pdev, &header, quirks);
		if (ret)
			continue;

		have_devices = true;
	} while (true);

	return have_devices;
}

static bool extended_caps_walk_vsec(struct pci_dev *pdev, unsigned long quirks)
{
	bool have_devices = false;
	int pos = 0;

	do {
		struct extended_caps_header header;
		u32 table, hdr;
		int ret;

		pos = pci_find_next_ext_capability(pdev, pos, PCI_EXT_CAP_ID_VNDR);
		if (!pos)
			break;

		pci_read_config_dword(pdev, pos + PCI_VNDR_HEADER, &hdr);

		/* Support only revision 1 */
		header.rev = PCI_VNDR_HEADER_REV(hdr);
		if (header.rev != 1) {
			dev_warn(&pdev->dev, "Unsupported VSEC revision %d\n",
				 header.rev);
			continue;
		}

		header.id = PCI_VNDR_HEADER_ID(hdr);
		header.length = PCI_VNDR_HEADER_LEN(hdr);

		/* entry, size, and table offset are the same as DVSEC */
		pci_read_config_byte(pdev, pos + INTEL_DVSEC_ENTRIES,
				     &header.num_entries);
		pci_read_config_byte(pdev, pos + INTEL_DVSEC_SIZE,
				     &header.entry_size);
		pci_read_config_dword(pdev, pos + INTEL_DVSEC_TABLE,
				      &table);

		header.tbir = INTEL_DVSEC_TABLE_BAR(table);
		header.offset = INTEL_DVSEC_TABLE_OFFSET(table);

		ret = extended_caps_add_dev(pdev, &header, quirks);
		if (ret)
			continue;

		have_devices = true;
	} while (true);

	return have_devices;
}

static int extended_caps_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct extended_caps_platform_info *info;
	bool have_devices = false;
	unsigned long quirks = 0;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	info = (struct extended_caps_platform_info *)id->driver_data;
	if (info)
		quirks = info->quirks;

	have_devices |= extended_caps_walk_dvsec(pdev, quirks);
	have_devices |= extended_caps_walk_vsec(pdev, quirks);

	if (info && (info->quirks & EXT_CAPS_QUIRK_NO_DVSEC))
		have_devices |= extended_caps_walk_header(pdev, quirks, info->capabilities);

	if (!have_devices)
		return -ENODEV;

	return 0;
}

static void extended_caps_pci_remove(struct pci_dev *pdev)
{
}

#define PCI_DEVICE_ID_INTEL_EXT_CAPS_ADL	0x467d
#define PCI_DEVICE_ID_INTEL_EXT_CAPS_DG1	0x490e
#define PCI_DEVICE_ID_INTEL_EXT_CAPS_OOBMSM	0x09a7
#define PCI_DEVICE_ID_INTEL_EXT_CAPS_TGL	0x9a0d
static const struct pci_device_id extended_caps_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, EXT_CAPS_ADL, &tgl_info) },
	{ PCI_DEVICE_DATA(INTEL, EXT_CAPS_DG1, &dg1_info) },
	{ PCI_DEVICE_DATA(INTEL, EXT_CAPS_OOBMSM, NULL) },
	{ PCI_DEVICE_DATA(INTEL, EXT_CAPS_TGL, &tgl_info) },
	{ }
};
MODULE_DEVICE_TABLE(pci, extended_caps_pci_ids);

static struct pci_driver extended_caps_pci_driver = {
	.name = "intel_extended_caps",
	.id_table = extended_caps_pci_ids,
	.probe = extended_caps_pci_probe,
	.remove = extended_caps_pci_remove,
};
module_pci_driver(extended_caps_pci_driver);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel Extended Capabilities auxiliary bus driver");
MODULE_LICENSE("GPL v2");
