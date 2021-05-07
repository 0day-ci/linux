// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include "cxl.h"

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
	struct cxl_walk_context ctx;

	if (!pci_root)
		return -ENXIO;

	/* TODO: fold in CEDT.CHBS retrieval */
	port = devm_cxl_add_port(dev, port, &root->dev, idx, ~0ULL);
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

	cxl_root = devm_cxl_add_root(dev, NULL, 0);
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
