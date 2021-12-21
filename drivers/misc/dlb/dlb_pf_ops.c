// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#include "dlb_main.h"

/********************************/
/****** PCI BAR management ******/
/********************************/

int dlb_pf_map_pci_bar_space(struct dlb *dlb, struct pci_dev *pdev)
{
	dlb->hw.func_kva = pcim_iomap_table(pdev)[DLB_FUNC_BAR];
	dlb->hw.func_phys_addr = pci_resource_start(pdev, DLB_FUNC_BAR);

	if (!dlb->hw.func_kva) {
		dev_err(&pdev->dev, "Cannot iomap BAR 0 (size %llu)\n",
			pci_resource_len(pdev, 0));

		return -EIO;
	}

	dlb->hw.csr_kva = pcim_iomap_table(pdev)[DLB_CSR_BAR];
	dlb->hw.csr_phys_addr = pci_resource_start(pdev, DLB_CSR_BAR);

	if (!dlb->hw.csr_kva) {
		dev_err(&pdev->dev, "Cannot iomap BAR 2 (size %llu)\n",
			pci_resource_len(pdev, 2));

		return -EIO;
	}

	return 0;
}
