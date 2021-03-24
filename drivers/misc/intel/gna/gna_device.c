// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2021 Intel Corporation

#include <linux/module.h>
#include <linux/pci.h>

#include "gna_device.h"
#include "gna_driver.h"

#define GNA_BAR0		0

static void gna_dev_init(struct gna_private *gna_priv, struct pci_dev *pcidev,
			const struct pci_device_id *pci_id)
{
	u32 bld_reg;

	pci_set_drvdata(pcidev, gna_priv);

	gna_priv->parent = &pcidev->dev;
	gna_priv->pdev = pcidev;
	gna_priv->info = *(struct gna_drv_info *)pci_id->driver_data;
	gna_priv->drv_priv = &gna_drv_priv;

	bld_reg = gna_reg_read(gna_priv->bar0_base, GNA_MMIO_IBUFFS);
	gna_priv->hw_info.in_buf_s = bld_reg & GENMASK(7, 0);
}

int gna_probe(struct pci_dev *pcidev, const struct pci_device_id *pci_id)
{
	struct gna_private *gna_priv;
	void __iomem *const *iomap;
	unsigned long phys_len;
	phys_addr_t phys;
	int ret;

	ret = pcim_enable_device(pcidev);
	if (ret) {
		dev_err(&pcidev->dev, "pci device can't be enabled\n");
		return ret;
	}

	ret = pcim_iomap_regions(pcidev, 1 << GNA_BAR0, GNA_DV_NAME);
	if (ret) {
		dev_err(&pcidev->dev, "cannot iomap regions\n");
		return ret;
	}

	phys = pci_resource_start(pcidev, GNA_BAR0);
	phys_len = pci_resource_len(pcidev, GNA_BAR0);

	dev_info(&pcidev->dev, "physical base address %pap, %lu bytes\n",
		&phys, phys_len);

	iomap = pcim_iomap_table(pcidev);
	if (!iomap) {
		dev_err(&pcidev->dev, "failed to iomap table\n");
		return -ENODEV;
	}

	gna_priv = devm_kzalloc(&pcidev->dev, sizeof(*gna_priv), GFP_KERNEL);
	if (!gna_priv)
		return -ENOMEM;

	gna_priv->bar0_base = iomap[GNA_BAR0];

	dev_dbg(&pcidev->dev, "bar0 memory address: %p\n", gna_priv->bar0_base);

	ret = dma_set_mask(&pcidev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pcidev->dev, "pci_set_dma_mask returned error %d\n", ret);
		return ret;
	}

	pci_set_master(pcidev);

	gna_dev_init(gna_priv, pcidev, pci_id);

	return 0;
}
