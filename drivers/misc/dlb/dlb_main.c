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
static dev_t dlb_devt;
static DEFINE_IDR(dlb_ids);
static DEFINE_MUTEX(dlb_ids_lock);

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

	return 0;

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

	err = pci_register_driver(&dlb_pci_driver);
	if (err < 0) {
		pr_err("dlb: pci_register_driver() returned %d\n", err);

		goto pci_register_fail;
	}

	return 0;

pci_register_fail:
	unregister_chrdev_region(dlb_devt, DLB_MAX_NUM_DEVICES);
alloc_chrdev_fail:
	class_destroy(dlb_class);

	return err;
}

static void __exit dlb_exit_module(void)
{
	pci_unregister_driver(&dlb_pci_driver);

	unregister_chrdev_region(dlb_devt, DLB_MAX_NUM_DEVICES);

	class_destroy(dlb_class);
}

module_init(dlb_init_module);
module_exit(dlb_exit_module);
