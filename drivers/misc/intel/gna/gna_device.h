/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2017-2021 Intel Corporation */

#ifndef __GNA_DEVICE_H__
#define __GNA_DEVICE_H__

#include <linux/types.h>

#include "gna_hw.h"

struct gna_driver_private;
struct pci_device_id;
struct pci_dev;
struct device;

struct gna_drv_info {
	u32 hwid;
	u32 num_pagetables;
	u32 num_page_entries;
	u32 max_layer_count;
	u64 max_hw_mem;

	struct gna_desc_info desc_info;
};

struct gna_hw_info {
	u8 in_buf_s;
};

struct gna_private {
	struct gna_driver_private *drv_priv;

	struct pci_dev *pdev;
	/* pdev->dev */
	struct device *parent;

	/* device related resources */
	void __iomem *bar0_base;
	struct gna_drv_info info;
	struct gna_hw_info hw_info;
};

int gna_probe(struct pci_dev *dev, const struct pci_device_id *id);

#endif /* __GNA_DEVICE_H__ */
