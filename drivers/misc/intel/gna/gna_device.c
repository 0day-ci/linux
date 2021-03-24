// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2021 Intel Corporation

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <uapi/misc/intel/gna.h>

#include "gna_device.h"
#include "gna_driver.h"
#include "gna_hw.h"
#include "gna_ioctl.h"
#include "gna_request.h"

#define GNA_DEV_HWID_CNL	0x5A11
#define GNA_DEV_HWID_EHL	0x4511
#define GNA_DEV_HWID_GLK	0x3190
#define GNA_DEV_HWID_ICL	0x8A11
#define GNA_DEV_HWID_JSL	0x4E11
#define GNA_DEV_HWID_TGL	0x9A11

#define GNA_BAR0		0

#define GNA_FEATURES \
	.max_hw_mem = 256 * 1024 * 1024, \
	.num_pagetables = 64, \
	.num_page_entries = PAGE_SIZE / sizeof(u32), \
	/* desc_info all in bytes */ \
	.desc_info = { \
		.rsvd_size = 256, \
		.cfg_size = 256, \
		.desc_size = 784, \
		.mmu_info = { \
			.vamax_size = 4, \
			.rsvd_size = 12, \
			.pd_size = 4 * 64, \
		}, \
	}

#define GNA_GEN1_FEATURES \
	GNA_FEATURES, \
	.max_layer_count = 1024

#define GNA_GEN2_FEATURES \
	GNA_FEATURES, \
	.max_layer_count = 4096

static const struct gna_drv_info cnl_drv_info = {
	.hwid = GNA_DEV_HWID_CNL,
	GNA_GEN1_FEATURES
};

static const struct gna_drv_info glk_drv_info = {
	.hwid = GNA_DEV_HWID_GLK,
	GNA_GEN1_FEATURES
};

static const struct gna_drv_info ehl_drv_info = {
	.hwid = GNA_DEV_HWID_EHL,
	GNA_GEN1_FEATURES
};

static const struct gna_drv_info icl_drv_info = {
	.hwid = GNA_DEV_HWID_ICL,
	GNA_GEN1_FEATURES
};

static const struct gna_drv_info jsl_drv_info = {
	.hwid = GNA_DEV_HWID_JSL,
	GNA_GEN2_FEATURES
};

static const struct gna_drv_info tgl_drv_info = {
	.hwid = GNA_DEV_HWID_TGL,
	GNA_GEN2_FEATURES
};

#define INTEL_GNA_DEVICE(hwid, info) \
	{ PCI_VDEVICE(INTEL, hwid), (kernel_ulong_t)(info) }

const struct pci_device_id gna_pci_ids[] = {
	INTEL_GNA_DEVICE(GNA_DEV_HWID_CNL, &cnl_drv_info),
	INTEL_GNA_DEVICE(GNA_DEV_HWID_EHL, &ehl_drv_info),
	INTEL_GNA_DEVICE(GNA_DEV_HWID_GLK, &glk_drv_info),
	INTEL_GNA_DEVICE(GNA_DEV_HWID_ICL, &icl_drv_info),
	INTEL_GNA_DEVICE(GNA_DEV_HWID_JSL, &jsl_drv_info),
	INTEL_GNA_DEVICE(GNA_DEV_HWID_TGL, &tgl_drv_info),
	{ }
};

MODULE_DEVICE_TABLE(pci, gna_pci_ids);

static int gna_open(struct inode *inode, struct file *f)
{
	struct gna_file_private *file_priv;
	struct gna_private *gna_priv;

	gna_priv = container_of(f->private_data, struct gna_private, misc);

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file_priv->fd = f;
	file_priv->gna_priv = gna_priv;

	mutex_init(&file_priv->memlist_lock);
	INIT_LIST_HEAD(&file_priv->memory_list);

	mutex_lock(&gna_priv->flist_lock);
	list_add_tail(&file_priv->flist, &gna_priv->file_list);
	mutex_unlock(&gna_priv->flist_lock);

	f->private_data = file_priv;

	return 0;
}

static int gna_release(struct inode *inode, struct file *f)
{
	struct gna_file_private *iter_file, *temp_file;
	struct gna_memory_object *iter_mo, *temp_mo;
	struct gna_file_private *file_priv;
	struct gna_private *gna_priv;

	/* free all memory objects created by that file */
	file_priv = (struct gna_file_private *)f->private_data;
	gna_priv = file_priv->gna_priv;

	mutex_lock(&file_priv->memlist_lock);
	list_for_each_entry_safe(iter_mo, temp_mo, &file_priv->memory_list, file_mem_list) {
		queue_work(gna_priv->request_wq, &iter_mo->work);
		wait_event(iter_mo->waitq, true);
		gna_memory_free(gna_priv, iter_mo);
	}
	mutex_unlock(&file_priv->memlist_lock);

	gna_delete_file_requests(f, gna_priv);

	mutex_lock(&gna_priv->flist_lock);
	list_for_each_entry_safe(iter_file, temp_file, &gna_priv->file_list, flist) {
		if (iter_file->fd == f) {
			list_del(&iter_file->flist);
			f->private_data = NULL;
			kfree(iter_file);
			break;
		}
	}
	mutex_unlock(&gna_priv->flist_lock);

	return 0;
}

static const struct file_operations gna_file_ops = {
	.owner		=	THIS_MODULE,
	.open		=	gna_open,
	.release	=	gna_release,
	.unlocked_ioctl =	gna_ioctl,
};

static void gna_dev_release(struct gna_private *gna_priv)
{
	misc_deregister(&gna_priv->misc);
	kfree(gna_priv->misc.name);
	gna_priv->misc.name = NULL;
}

static int gna_dev_create(struct gna_private *gna_priv, char *gna_name)
{
	struct pci_dev *pcidev;
	int ret;

	pcidev = gna_priv->pdev;

	gna_priv->misc.minor = MISC_DYNAMIC_MINOR;
	gna_priv->misc.name = kasprintf(GFP_KERNEL, "%s", gna_name);
	gna_priv->misc.fops = &gna_file_ops;
	gna_priv->misc.parent = &pcidev->dev;
	gna_priv->misc.mode = 0666;

	dev_dbg(&pcidev->dev, "registering device: %s\n",
		gna_priv->misc.name);

	ret = misc_register(&gna_priv->misc);
	if (ret) {
		dev_err(&pcidev->dev, "misc_register %s failed: %d\n",
			gna_name, ret);
		misc_deregister(&gna_priv->misc);
		kfree(gna_priv->misc.name);
		gna_priv->misc.name = NULL;
	}

	return ret;
}


static int gna_dev_init(struct gna_private *gna_priv, struct pci_dev *pcidev,
			const struct pci_device_id *pci_id)
{
	// strlen(GNA_DV_NAME) + max minor number.
	char gna_name[sizeof(GNA_DV_NAME) + sizeof("255") + 1];
	u32 bld_reg;
	int ret;

	pci_set_drvdata(pcidev, gna_priv);

	gna_priv->parent = &pcidev->dev;
	gna_priv->pdev = pcidev;
	gna_priv->info = *(struct gna_drv_info *)pci_id->driver_data;
	gna_priv->drv_priv = &gna_drv_priv;

	gna_priv->index = atomic_inc_return(&gna_priv->drv_priv->dev_last_idx);

	bld_reg = gna_reg_read(gna_priv->bar0_base, GNA_MMIO_IBUFFS);
	gna_priv->hw_info.in_buf_s = bld_reg & GENMASK(7, 0);

	if (gna_mmu_alloc(gna_priv)) {
		dev_err(&pcidev->dev, "mmu allocation failed\n");
		ret = -EFAULT;
		goto err_pci_drvdata_unset;

	}
	dev_dbg(&pcidev->dev, "maximum memory size %llu num pd %d\n",
		gna_priv->info.max_hw_mem, gna_priv->info.num_pagetables);
	dev_dbg(&pcidev->dev, "desc rsvd size %d mmu vamax size %d\n",
		gna_priv->info.desc_info.rsvd_size,
		gna_priv->info.desc_info.mmu_info.vamax_size);

	mutex_init(&gna_priv->mmu_lock);

	idr_init(&gna_priv->memory_idr);
	mutex_init(&gna_priv->memidr_lock);

	mutex_init(&gna_priv->flist_lock);
	INIT_LIST_HEAD(&gna_priv->file_list);

	atomic_set(&gna_priv->request_count, 0);

	mutex_init(&gna_priv->reqlist_lock);
	INIT_LIST_HEAD(&gna_priv->request_list);

	init_waitqueue_head(&gna_priv->dev_busy_waitq);

	snprintf(gna_name, sizeof(gna_name), "%s%d", GNA_DV_NAME, gna_priv->index);

	gna_priv->request_wq = create_singlethread_workqueue(gna_name);
	if (!gna_priv->request_wq) {
		dev_err(&pcidev->dev, "could not create %s workqueue\n", gna_name);
		ret = -EFAULT;
		goto err_pci_drvdata_unset;
	}

	ret = gna_dev_create(gna_priv, gna_name);
	if (ret) {
		dev_err(&pcidev->dev, "could not create %s device\n", GNA_DV_NAME);
		goto err_del_wq;
	}

	return 0;

err_del_wq:
	destroy_workqueue(gna_priv->request_wq);

err_pci_drvdata_unset:
	pci_set_drvdata(pcidev, NULL);

	return ret;
}

static void gna_dev_deinit(struct gna_private *gna_priv)
{
	gna_dev_release(gna_priv);

	flush_workqueue(gna_priv->request_wq);
	destroy_workqueue(gna_priv->request_wq);

	idr_destroy(&gna_priv->memory_idr);
	gna_mmu_free(gna_priv);
}

static irqreturn_t gna_interrupt(int irq, void *priv)
{
	struct gna_private *gna_priv;

	gna_priv = (struct gna_private *)priv;
	gna_priv->dev_busy = false;
	wake_up(&gna_priv->dev_busy_waitq);
	return IRQ_HANDLED;
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

	ret = pci_alloc_irq_vectors(pcidev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;

	gna_priv->irq = pci_irq_vector(pcidev, 0);
	if (unlikely(gna_priv->irq < 0)) {
		dev_err(&pcidev->dev, "could not obtain irq number\n");
		ret = -EIO;
		goto err_free_irq_vector;
	}

	ret = request_irq(gna_priv->irq, gna_interrupt,
			IRQF_SHARED, GNA_DV_NAME, gna_priv);

	if (ret) {
		dev_err(&pcidev->dev, "could not register for interrupt\n");
		goto err_free_irq_vector;
	}

	dev_dbg(&pcidev->dev, "irq num %d\n", gna_priv->irq);

	ret = gna_dev_init(gna_priv, pcidev, pci_id);
	if (ret) {
		dev_err(&pcidev->dev, "could not initialize %s device\n", GNA_DV_NAME);
		goto err_free_irq;
	}


	return 0;

err_free_irq:
	free_irq(gna_priv->irq, gna_priv);
err_free_irq_vector:
	pci_free_irq_vectors(pcidev);

	return ret;
}

void gna_remove(struct pci_dev *pcidev)
{
	struct gna_private *gna_priv;

	gna_priv = pci_get_drvdata(pcidev);

	free_irq(gna_priv->irq, gna_priv);

	gna_dev_deinit(gna_priv);

	pci_free_irq_vectors(pcidev);
}

static u32 gna_device_type_by_hwid(u32 hwid)
{
	switch (hwid) {
	case GNA_DEV_HWID_CNL:
		return GNA_DEV_TYPE_0_9;
	case GNA_DEV_HWID_GLK:
	case GNA_DEV_HWID_EHL:
	case GNA_DEV_HWID_ICL:
		return GNA_DEV_TYPE_1_0;
	case GNA_DEV_HWID_JSL:
	case GNA_DEV_HWID_TGL:
		return GNA_DEV_TYPE_2_0;
	default:
		return 0;
	}
}

int gna_getparam(struct gna_private *gna_priv, union gna_parameter *param)
{
	switch (param->in.id) {
	case GNA_PARAM_DEVICE_ID:
		param->out.value = gna_priv->info.hwid;
		break;
	case GNA_PARAM_RECOVERY_TIMEOUT:
		param->out.value = jiffies_to_msecs(gna_priv->drv_priv->recovery_timeout_jiffies) / 1000;
		break;
	case GNA_PARAM_INPUT_BUFFER_S:
		param->out.value = gna_priv->hw_info.in_buf_s;
		break;
	case GNA_PARAM_DEVICE_TYPE:
		param->out.value = gna_device_type_by_hwid(gna_priv->info.hwid);
		break;
	default:
		dev_err(gna_priv->misc.this_device, "unknown parameter id %llu\n", param->in.id);
		return -EINVAL;
	}

	return 0;
}
