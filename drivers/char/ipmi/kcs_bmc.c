// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 * Copyright (c) 2021, IBM Corp.
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include "kcs_bmc.h"

/* Implement both the device and client interfaces here */
#include "kcs_bmc_device.h"
#include "kcs_bmc_client.h"

/* Record probed devices and cdevs */
static DEFINE_MUTEX(kcs_bmc_lock);
static LIST_HEAD(kcs_bmc_devices);
static LIST_HEAD(kcs_bmc_cdevs);

/* Consumer data access */

u8 kcs_bmc_read_data(struct kcs_bmc_device *kcs_bmc)
{
	return kcs_bmc->ops->io_inputb(kcs_bmc, kcs_bmc->ioreg.idr);
}
EXPORT_SYMBOL(kcs_bmc_read_data);

void kcs_bmc_write_data(struct kcs_bmc_device *kcs_bmc, u8 data)
{
	kcs_bmc->ops->io_outputb(kcs_bmc, kcs_bmc->ioreg.odr, data);
}
EXPORT_SYMBOL(kcs_bmc_write_data);

u8 kcs_bmc_read_status(struct kcs_bmc_device *kcs_bmc)
{
	return kcs_bmc->ops->io_inputb(kcs_bmc, kcs_bmc->ioreg.str);
}
EXPORT_SYMBOL(kcs_bmc_read_status);

void kcs_bmc_write_status(struct kcs_bmc_device *kcs_bmc, u8 data)
{
	kcs_bmc->ops->io_outputb(kcs_bmc, kcs_bmc->ioreg.str, data);
}
EXPORT_SYMBOL(kcs_bmc_write_status);

void kcs_bmc_update_status(struct kcs_bmc_device *kcs_bmc, u8 mask, u8 val)
{
	kcs_bmc->ops->io_updateb(kcs_bmc, kcs_bmc->ioreg.str, mask, val);
}
EXPORT_SYMBOL(kcs_bmc_update_status);

int kcs_bmc_handle_event(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_client *client;
	int rc;

	spin_lock(&kcs_bmc->lock);
	client = kcs_bmc->client;
	if (client) {
		rc = client->ops->event(client);
	} else {
		u8 status;

		status = kcs_bmc_read_status(kcs_bmc);
		if (status & KCS_BMC_STR_IBF) {
			/* Ack the event by reading the data */
			kcs_bmc_read_data(kcs_bmc);
			rc = KCS_BMC_EVENT_HANDLED;
		} else {
			rc = KCS_BMC_EVENT_NONE;
		}
	}
	spin_unlock(&kcs_bmc->lock);

	return rc;
}
EXPORT_SYMBOL(kcs_bmc_handle_event);

int kcs_bmc_enable_device(struct kcs_bmc_device *kcs_bmc, struct kcs_bmc_client *client)
{
	int rc;

	spin_lock_irq(&kcs_bmc->lock);
	if (kcs_bmc->client) {
		rc = -EBUSY;
	} else {
		kcs_bmc->client = client;
		rc = 0;
	}
	spin_unlock_irq(&kcs_bmc->lock);

	return rc;
}
EXPORT_SYMBOL(kcs_bmc_enable_device);

void kcs_bmc_disable_device(struct kcs_bmc_device *kcs_bmc, struct kcs_bmc_client *client)
{
	spin_lock_irq(&kcs_bmc->lock);
	if (client == kcs_bmc->client)
		kcs_bmc->client = NULL;
	spin_unlock_irq(&kcs_bmc->lock);
}
EXPORT_SYMBOL(kcs_bmc_disable_device);

int kcs_bmc_add_device(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_cdev *cdev;
	int rc;

	spin_lock_init(&kcs_bmc->lock);
	kcs_bmc->client = NULL;

	mutex_lock(&kcs_bmc_lock);
	list_add(&kcs_bmc->entry, &kcs_bmc_devices);
	list_for_each_entry(cdev, &kcs_bmc_cdevs, entry) {
		rc = cdev->ops->add_device(kcs_bmc);
		if (rc)
			dev_err(kcs_bmc->dev, "Failed to add chardev for KCS channel %d: %d",
				kcs_bmc->channel, rc);
	}
	mutex_unlock(&kcs_bmc_lock);

	return 0;
}
EXPORT_SYMBOL(kcs_bmc_add_device);

int kcs_bmc_remove_device(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_cdev *cdev;
	int rc;

	mutex_lock(&kcs_bmc_lock);
	list_del(&kcs_bmc->entry);
	list_for_each_entry(cdev, &kcs_bmc_cdevs, entry) {
		rc = cdev->ops->remove_device(kcs_bmc);
		if (rc)
			dev_err(kcs_bmc->dev, "Failed to remove chardev for KCS channel %d: %d",
				kcs_bmc->channel, rc);
	}
	mutex_unlock(&kcs_bmc_lock);

	return 0;
}
EXPORT_SYMBOL(kcs_bmc_remove_device);

int kcs_bmc_register_cdev(struct kcs_bmc_cdev *cdev)
{
	struct kcs_bmc_device *kcs_bmc;
	int rc;

	mutex_lock(&kcs_bmc_lock);
	list_add(&cdev->entry, &kcs_bmc_cdevs);
	list_for_each_entry(kcs_bmc, &kcs_bmc_devices, entry) {
		rc = cdev->ops->add_device(kcs_bmc);
		if (rc)
			dev_err(kcs_bmc->dev, "Failed to add chardev for KCS channel %d: %d",
				kcs_bmc->channel, rc);
	}
	mutex_unlock(&kcs_bmc_lock);

	return 0;
}
EXPORT_SYMBOL(kcs_bmc_register_cdev);

int kcs_bmc_unregister_cdev(struct kcs_bmc_cdev *cdev)
{
	struct kcs_bmc_device *kcs_bmc;
	int rc;

	mutex_lock(&kcs_bmc_lock);
	list_del(&cdev->entry);
	list_for_each_entry(kcs_bmc, &kcs_bmc_devices, entry) {
		rc = cdev->ops->remove_device(kcs_bmc);
		if (rc)
			dev_err(kcs_bmc->dev, "Failed to add chardev for KCS channel %d: %d",
				kcs_bmc->channel, rc);
	}
	mutex_unlock(&kcs_bmc_lock);

	return rc;
}
EXPORT_SYMBOL(kcs_bmc_unregister_cdev);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Haiyue Wang <haiyue.wang@linux.intel.com>");
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_DESCRIPTION("KCS BMC to handle the IPMI request from system software");
