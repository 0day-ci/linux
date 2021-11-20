// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/pci.h>
#include <cxl.h>
#include <pci.h>
#include "core.h"

/**
 * DOC: cxl core pci
 *
 * Compute Express Link protocols are layered on top of PCIe. CXL core provides
 * a set of helpers for CXL interactions which occur via PCIe.
 */

/**
 * find_parent_cxl_port() - Finds parent port through PCIe mechanisms
 * @pdev: PCIe USP or DSP to find an upstream port for
 *
 * Once all CXL ports are enumerated, there is no need to reference the PCIe
 * parallel universe as all downstream ports are contained in a linked list, and
 * all upstream ports are accessible via pointer. During the enumeration, it is
 * very convenient to be able to peak up one level in the hierarchy without
 * needing the established relationship between data structures so that the
 * parenting can be done as the ports/dports are created.
 *
 * A reference is kept to the found port.
 */
struct cxl_port *find_parent_cxl_port(struct pci_dev *pdev)
{
	struct device *parent_dev, *gparent_dev;

	/* Parent is either a downstream port, or root port */
	parent_dev = get_device(pdev->dev.parent);

	if (is_cxl_switch_usp(&pdev->dev)) {
		if (dev_WARN_ONCE(&pdev->dev,
				  pci_pcie_type(pdev) !=
						  PCI_EXP_TYPE_DOWNSTREAM &&
					  pci_pcie_type(pdev) !=
						  PCI_EXP_TYPE_ROOT_PORT,
				  "Parent not downstream\n"))
			goto err;

		/*
		 * Grandparent is either an upstream port or a platform device that has
		 * been added as a cxl_port already.
		 */
		gparent_dev = get_device(parent_dev->parent);
		put_device(parent_dev);

		return to_cxl_port(gparent_dev);
	} else if (is_cxl_switch_dsp(&pdev->dev)) {
		if (dev_WARN_ONCE(&pdev->dev,
				  pci_pcie_type(pdev) != PCI_EXP_TYPE_UPSTREAM,
				  "Parent not upstream"))
			goto err;
		return to_cxl_port(parent_dev);
	}

err:
	dev_WARN(&pdev->dev, "Invalid topology\n");
	put_device(parent_dev);
	return NULL;
}

/*
 * Unlike endpoints, switches don't discern CXL.mem capability. Simply finding
 * the DVSEC is sufficient.
 */
static bool is_cxl_switch(struct pci_dev *pdev)
{
	return pci_find_dvsec_capability(pdev, PCI_DVSEC_VENDOR_ID_CXL,
					 CXL_DVSEC_PORT_EXTENSIONS);
}

/**
 * is_cxl_switch_usp() - Is the device a CXL.mem enabled switch
 * @dev: Device to query for switch type
 *
 * If the device is a CXL.mem capable upstream switch port return true;
 * otherwise return false.
 */
bool is_cxl_switch_usp(struct device *dev)
{
	struct pci_dev *pdev;

	if (!dev_is_pci(dev))
		return false;

	pdev = to_pci_dev(dev);

	return pci_is_pcie(pdev) &&
	       pci_pcie_type(pdev) == PCI_EXP_TYPE_UPSTREAM &&
	       is_cxl_switch(pdev);
}
EXPORT_SYMBOL_NS_GPL(is_cxl_switch_usp, CXL);

/**
 * is_cxl_switch_dsp() - Is the device a CXL.mem enabled switch
 * @dev: Device to query for switch type
 *
 * If the device is a CXL.mem capable downstream switch port return true;
 * otherwise return false.
 */
bool is_cxl_switch_dsp(struct device *dev)
{
	struct pci_dev *pdev;

	if (!dev_is_pci(dev))
		return false;

	pdev = to_pci_dev(dev);

	return pci_is_pcie(pdev) &&
	       pci_pcie_type(pdev) == PCI_EXP_TYPE_DOWNSTREAM &&
	       is_cxl_switch(pdev);
}
EXPORT_SYMBOL_NS_GPL(is_cxl_switch_dsp, CXL);
