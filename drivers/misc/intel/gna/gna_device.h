/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2017-2021 Intel Corporation */

#ifndef __GNA_DEVICE_H__
#define __GNA_DEVICE_H__

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/idr.h>
#include <linux/pci.h>

#include "gna_hw.h"
#include "gna_mem.h"

struct gna_driver_private;
struct workqueue_struct;
union gna_parameter;
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

	/* list of opened files */
	struct list_head file_list;
	/* protects file_list */
	struct mutex flist_lock;

	struct pci_dev *pdev;
	/* pdev->dev */
	struct device *parent;

	int irq;
	/* hardware status set by interrupt handler */
	u32 hw_status;

	/* device related resources */
	void __iomem *bar0_base;
	struct gna_drv_info info;
	struct gna_hw_info hw_info;

	struct gna_mmu_object mmu;
	struct mutex mmu_lock;

	/* if true, then gna device is processing */
	bool dev_busy;
	struct wait_queue_head dev_busy_waitq;

	struct list_head request_list;
	/* protects request_list */
	struct mutex reqlist_lock;
	struct workqueue_struct *request_wq;
	atomic_t request_count;

	/* memory objects' store */
	struct idr memory_idr;
	/* lock protecting memory_idr */
	struct mutex memidr_lock;
};

extern const struct pci_device_id gna_pci_ids[];

int gna_probe(struct pci_dev *dev, const struct pci_device_id *id);

void gna_remove(struct pci_dev *dev);

int gna_getparam(struct gna_private *gna_priv, union gna_parameter *param);

#endif /* __GNA_DEVICE_H__ */
