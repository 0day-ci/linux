// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/range.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include "cxl.h"

/*
 * TODO: Replace all of the below module parameters with ACPI CXL
 * resource descriptions once ACPICA makes them available.
 */
static unsigned long chbcr[4];
module_param_named(chbcr0, chbcr[0], ulong, 0400);
module_param_named(chbcr1, chbcr[1], ulong, 0400);
module_param_named(chbcr2, chbcr[2], ulong, 0400);
module_param_named(chbcr3, chbcr[3], ulong, 0400);

/* TODO: cross-bridge interleave */
static struct cxl_address_space cxl_space[] = {
	[0] = { .range = { 0, -1 }, .targets = 0x1, },
	[1] = { .range = { 0, -1 }, .targets = 0x1, },
	[2] = { .range = { 0, -1 }, .targets = 0x1, },
	[3] = { .range = { 0, -1 }, .targets = 0x1, },
};

static int set_range(const char *val, const struct kernel_param *kp)
{
	unsigned long long size, base;
	struct cxl_address_space *space;
	unsigned long flags;
	char *p;
	int rc;

	size = memparse(val, &p);
	if (*p != '@')
		return -EINVAL;

	base = memparse(p + 1, &p);
	if (*p != ':')
		return -EINVAL;

	rc = kstrtoul(p + 1, 0, &flags);
	if (rc)
		return rc;
	if (!flags || flags > CXL_ADDRSPACE_MASK)
		return rc;

	space = kp->arg;
	*space = (struct cxl_address_space) {
		.range = {
			.start = base,
			.end = base + size - 1,
		},
		.flags = flags,
	};

	return 0;
}

static int get_range(char *buf, const struct kernel_param *kp)
{
	struct cxl_address_space *space = kp->arg;

	if (!range_len(&space->range))
		return -EINVAL;

	return sysfs_emit(buf, "%#llx@%#llx :%s%s%s%s\n",
			  (unsigned long long)range_len(&space->range),
			  (unsigned long long)space->range.start,
			  space->flags & CXL_ADDRSPACE_RAM ? " ram" : "",
			  space->flags & CXL_ADDRSPACE_PMEM ? " pmem" : "",
			  space->flags & CXL_ADDRSPACE_TYPE2 ? " type2" : "",
			  space->flags & CXL_ADDRSPACE_TYPE3 ? " type3" : "");
}

module_param_call(range0, set_range, get_range, &cxl_space[0], 0400);
module_param_call(range1, set_range, get_range, &cxl_space[1], 0400);
module_param_call(range2, set_range, get_range, &cxl_space[2], 0400);
module_param_call(range3, set_range, get_range, &cxl_space[3], 0400);

static int match_ACPI0016(struct device *dev, const void *host)
{
	struct acpi_device *adev = to_acpi_device(dev);
	const char *hid = acpi_device_hid(adev);

	return strcmp(hid, "ACPI0016") == 0;
}

struct cxl_walk_context {
	struct device *dev;
	struct pci_bus *root;
	struct cxl_port *port;
	int error;
	int count;
};

static int match_add_root_ports(struct pci_dev *pdev, void *data)
{
	struct cxl_walk_context *ctx = data;
	struct pci_bus *root_bus = ctx->root;
	struct cxl_port *port = ctx->port;
	int type = pci_pcie_type(pdev);
	struct device *dev = ctx->dev;
	resource_size_t cxl_regs_phys;
	int target_id = ctx->count;

	if (pdev->bus != root_bus)
		return 0;
	if (!pci_is_pcie(pdev))
		return 0;
	if (type != PCI_EXP_TYPE_ROOT_PORT)
		return 0;

	ctx->count++;

	/* TODO walk DVSEC to find component register base */
	cxl_regs_phys = -1;

	port = devm_cxl_add_port(dev, port, &pdev->dev, target_id,
				 cxl_regs_phys);
	if (IS_ERR(port)) {
		ctx->error = PTR_ERR(port);
		return ctx->error;
	}

	dev_dbg(dev, "%s: register: %s\n", dev_name(&pdev->dev),
		dev_name(&port->dev));

	return 0;
}

/*
 * A host bridge may contain one or more root ports.  Register each port
 * as a child of the cxl_root.
 */
static int cxl_acpi_register_ports(struct device *dev, struct acpi_device *root,
				   struct cxl_port *port, int idx)
{
	struct acpi_pci_root *pci_root = acpi_pci_find_root(root->handle);
	resource_size_t chbcr_base = ~0ULL;
	struct cxl_walk_context ctx;

	if (!pci_root)
		return -ENXIO;

	/* TODO: fold in CEDT.CHBS retrieval */
	if (idx < ARRAY_SIZE(chbcr))
		chbcr_base = chbcr[idx];
	port = devm_cxl_add_port(dev, port, &root->dev, idx, chbcr_base);
	if (IS_ERR(port))
		return PTR_ERR(port);
	dev_dbg(dev, "%s: register: %s\n", dev_name(&root->dev),
		dev_name(&port->dev));

	ctx = (struct cxl_walk_context) {
		.dev = dev,
		.root = pci_root->bus,
		.port = port,
	};
	pci_walk_bus(pci_root->bus, match_add_root_ports, &ctx);

	if (ctx.count == 0)
		return -ENODEV;
	return ctx.error;
}

static int cxl_acpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct device *bridge = NULL;
	struct cxl_root *cxl_root;
	int rc, i = 0;

	cxl_root = devm_cxl_add_root(dev, cxl_space, ARRAY_SIZE(cxl_space));
	if (IS_ERR(cxl_root))
		return PTR_ERR(cxl_root);
	dev_dbg(dev, "register: %s\n", dev_name(&cxl_root->port.dev));

	while (true) {
		bridge = bus_find_device(adev->dev.bus, bridge, dev,
					 match_ACPI0016);
		if (!bridge)
			break;

		rc = cxl_acpi_register_ports(dev, to_acpi_device(bridge),
					     &cxl_root->port, i++);
		if (rc)
			return rc;
	}

	return 0;
}

static const struct acpi_device_id cxl_acpi_ids[] = {
	{ "ACPI0017", 0 },
	{ "", 0 },
};
MODULE_DEVICE_TABLE(acpi, cxl_acpi_ids);

static struct platform_driver cxl_acpi_driver = {
	.probe = cxl_acpi_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.acpi_match_table = cxl_acpi_ids,
	},
};

module_platform_driver(cxl_acpi_driver);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
