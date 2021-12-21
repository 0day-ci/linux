// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#include <linux/aer.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/uaccess.h>

#include "dlb_main.h"

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel(R) Dynamic Load Balancer (DLB) Driver");

static struct class *dlb_class;
static struct cdev dlb_cdev;
static dev_t dlb_devt;
static DEFINE_IDR(dlb_ids);
static DEFINE_MUTEX(dlb_ids_lock);

static int dlb_reset_device(struct pci_dev *pdev)
{
	int ret;

	ret = pci_save_state(pdev);
	if (ret)
		return ret;

	ret = __pci_reset_function_locked(pdev);
	if (ret)
		return ret;

	pci_restore_state(pdev);

	return 0;
}

static int dlb_device_create(struct dlb *dlb, struct pci_dev *pdev)
{
	/*
	 * Create a new device in order to create a /dev/dlb node. This device
	 * is a child of the DLB PCI device.
	 */
	dlb->dev_number = MKDEV(MAJOR(dlb_devt), dlb->id);
	dlb->dev = device_create(dlb_class, &pdev->dev, dlb->dev_number, dlb,
				 "dlb%d", dlb->id);
	if (IS_ERR(dlb->dev)) {
		dev_err(dlb->dev, "device_create() returned %ld\n",
			PTR_ERR(dlb->dev));

		return PTR_ERR(dlb->dev);
	}

	return 0;
}

/********************************/
/****** Char dev callbacks ******/
/********************************/

static const struct file_operations dlb_fops = {
	.owner   = THIS_MODULE,
};

/**********************************/
/****** PCI driver callbacks ******/
/**********************************/

static int dlb_probe(struct pci_dev *pdev, const struct pci_device_id *pdev_id)
{
	struct dlb *dlb;
	int ret;

	dlb = devm_kzalloc(&pdev->dev, sizeof(*dlb), GFP_KERNEL);
	if (!dlb)
		return -ENOMEM;

	pci_set_drvdata(pdev, dlb);

	dlb->pdev = pdev;

	mutex_lock(&dlb_ids_lock);
	dlb->id = idr_alloc(&dlb_ids, (void *)dlb, 0, DLB_MAX_NUM_DEVICES - 1,
			    GFP_KERNEL);
	mutex_unlock(&dlb_ids_lock);

	if (dlb->id < 0) {
		dev_err(&pdev->dev, "device ID allocation failed\n");

		ret = dlb->id;
		goto alloc_id_fail;
	}

	ret = pcim_enable_device(pdev);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to enable: %d\n", ret);

		goto pci_enable_device_fail;
	}

	ret = pcim_iomap_regions(pdev,
				 (1U << DLB_CSR_BAR) | (1U << DLB_FUNC_BAR),
				 "dlb");
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to map: %d\n", ret);

		goto pci_enable_device_fail;
	}

	pci_set_master(pdev);

	ret = pci_enable_pcie_error_reporting(pdev);
	if (ret != 0)
		dev_info(&pdev->dev, "AER is not supported\n");

	ret = dlb_pf_map_pci_bar_space(dlb, pdev);
	if (ret)
		goto map_pci_bar_fail;

	ret = dlb_device_create(dlb, pdev);
	if (ret)
		goto map_pci_bar_fail;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		goto dma_set_mask_fail;

	/*
	 * PM enable must be done before any other MMIO accesses, and this
	 * setting is persistent across device reset.
	 */
	dlb_pf_enable_pm(dlb);

	ret = dlb_pf_wait_for_device_ready(dlb, pdev);
	if (ret)
		goto wait_for_device_ready_fail;

	ret = dlb_reset_device(pdev);
	if (ret)
		goto dlb_reset_fail;

	ret = dlb_resource_init(&dlb->hw);
	if (ret)
		goto resource_init_fail;

	ret = dlb_pf_init_driver_state(dlb);
	if (ret)
		goto init_driver_state_fail;

	return 0;

init_driver_state_fail:
	dlb_resource_free(&dlb->hw);
resource_init_fail:
dlb_reset_fail:
wait_for_device_ready_fail:
dma_set_mask_fail:
	device_destroy(dlb_class, dlb->dev_number);
map_pci_bar_fail:
	pci_disable_pcie_error_reporting(pdev);
pci_enable_device_fail:
	mutex_lock(&dlb_ids_lock);
	idr_remove(&dlb_ids, dlb->id);
	mutex_unlock(&dlb_ids_lock);
alloc_id_fail:
	return ret;
}

static void dlb_remove(struct pci_dev *pdev)
{
	struct dlb *dlb = pci_get_drvdata(pdev);

	dlb_resource_free(&dlb->hw);

	device_destroy(dlb_class, dlb->dev_number);

	pci_disable_pcie_error_reporting(pdev);

	mutex_lock(&dlb_ids_lock);
	idr_remove(&dlb_ids, dlb->id);
	mutex_unlock(&dlb_ids_lock);
}

static struct pci_device_id dlb_id_table[] = {
	{ PCI_DEVICE_DATA(INTEL, DLB_PF, DLB_PF) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, dlb_id_table);

static struct pci_driver dlb_pci_driver = {
	.name		 = "dlb",
	.id_table	 = dlb_id_table,
	.probe		 = dlb_probe,
	.remove		 = dlb_remove,
};

static int __init dlb_init_module(void)
{
	int dlb_major;
	int err;

	dlb_class = class_create(THIS_MODULE, "dlb");

	if (IS_ERR(dlb_class)) {
		pr_err("dlb: class_create() returned %ld\n",
		       PTR_ERR(dlb_class));

		return PTR_ERR(dlb_class);
	}

	err = alloc_chrdev_region(&dlb_devt, 0, DLB_MAX_NUM_DEVICES, "dlb");

	if (err < 0) {
		pr_err("dlb: alloc_chrdev_region() returned %d\n", err);

		goto alloc_chrdev_fail;
	}

	dlb_major = MAJOR(dlb_devt);
	cdev_init(&dlb_cdev, &dlb_fops);
	err = cdev_add(&dlb_cdev, MKDEV(dlb_major, 0), DLB_MAX_NUM_DEVICES);
	if (err)
		goto cdev_add_fail;

	err = pci_register_driver(&dlb_pci_driver);
	if (err < 0) {
		pr_err("dlb: pci_register_driver() returned %d\n", err);

		goto pci_register_fail;
	}

	return 0;

pci_register_fail:
	cdev_del(&dlb_cdev);
cdev_add_fail:
	unregister_chrdev_region(dlb_devt, DLB_MAX_NUM_DEVICES);
alloc_chrdev_fail:
	class_destroy(dlb_class);

	return err;
}

static void __exit dlb_exit_module(void)
{
	pci_unregister_driver(&dlb_pci_driver);

	cdev_del(&dlb_cdev);

	unregister_chrdev_region(dlb_devt, DLB_MAX_NUM_DEVICES);

	class_destroy(dlb_class);
}

module_init(dlb_init_module);
module_exit(dlb_exit_module);
