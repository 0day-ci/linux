// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include "dlb_main.h"
#include "dlb_regs.h"

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

/*******************************/
/****** Driver management ******/
/*******************************/

int dlb_pf_init_driver_state(struct dlb *dlb)
{
	mutex_init(&dlb->resource_mutex);

	/*
	 * Allow PF runtime power-management (forbidden by default by the PCI
	 * layer during scan). The driver puts the device into D3hot while
	 * there are no scheduling domains to service.
	 */
	pm_runtime_allow(&dlb->pdev->dev);

	return 0;
}

void dlb_pf_enable_pm(struct dlb *dlb)
{
	/*
	 * Clear the power-management-disable register to power on the bulk of
	 * the device's hardware.
	 */
	dlb_clr_pmcsr_disable(&dlb->hw);
}

#define DLB_READY_RETRY_LIMIT 1000
int dlb_pf_wait_for_device_ready(struct dlb *dlb, struct pci_dev *pdev)
{
	u32 retries = DLB_READY_RETRY_LIMIT;

	/* Allow at least 1s for the device to become active after power-on */
	do {
		u32 idle, pm_st, addr;

		addr = CM_CFG_PM_STATUS;

		pm_st = DLB_CSR_RD(&dlb->hw, addr);

		addr = CM_CFG_DIAGNOSTIC_IDLE_STATUS;

		idle = DLB_CSR_RD(&dlb->hw, addr);

		if (FIELD_GET(CM_CFG_PM_STATUS_PMSM, pm_st) == 1 &&
		    FIELD_GET(CM_CFG_DIAGNOSTIC_IDLE_STATUS_DLB_FUNC_IDLE, idle)
		    == 1)
			break;

		usleep_range(1000, 2000);
	} while (--retries);

	if (!retries) {
		dev_err(&pdev->dev, "Device idle test failed\n");
		return -EIO;
	}

	return 0;
}

void dlb_pf_init_hardware(struct dlb *dlb)
{
	/* Use sparse mode as default */
	dlb_hw_enable_sparse_ldb_cq_mode(&dlb->hw);
	dlb_hw_enable_sparse_dir_cq_mode(&dlb->hw);
}
