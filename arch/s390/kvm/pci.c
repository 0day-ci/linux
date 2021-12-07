// SPDX-License-Identifier: GPL-2.0
/*
 * s390 kvm PCI passthrough support
 *
 * Copyright IBM Corp. 2021
 *
 *    Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 */

#include <linux/kvm_host.h>
#include <linux/pci.h>
#include <asm/kvm_pci.h>
#include <asm/sclp.h>
#include "pci.h"
#include "kvm-s390.h"

static struct zpci_aift aift;

static inline int __set_irq_noiib(u16 ctl, u8 isc)
{
	union zpci_sic_iib iib = {{0}};

	return zpci_set_irq_ctrl(ctl, isc, &iib);
}

struct zpci_aift *kvm_s390_pci_get_aift(void)
{
	return &aift;
}

/* Caller must hold the aift lock before calling this function */
void kvm_s390_pci_aen_exit(void)
{
	struct zpci_gaite *gait;
	unsigned long flags;
	struct airq_iv *sbv;
	struct kvm_zdev **gait_kzdev;
	int size;

	/* Clear the GAIT and forwarding summary vector */
	__set_irq_noiib(SIC_SET_AENI_CONTROLS, 0);

	spin_lock_irqsave(&aift.gait_lock, flags);
	gait = aift.gait;
	sbv = aift.sbv;
	gait_kzdev = aift.kzdev;
	aift.gait = 0;
	aift.sbv = 0;
	aift.kzdev = 0;
	spin_unlock_irqrestore(&aift.gait_lock, flags);

	if (sbv)
		airq_iv_release(sbv);
	size = get_order(PAGE_ALIGN(ZPCI_NR_DEVICES *
				    sizeof(struct zpci_gaite)));
	free_pages((unsigned long)gait, size);
	kfree(gait_kzdev);
}

int kvm_s390_pci_aen_init(u8 nisc)
{
	union zpci_sic_iib iib = {{0}};
	struct page *page;
	int rc = 0, size;

	/* If already enabled for AEN, bail out now */
	if (aift.gait || aift.sbv)
		return -EPERM;

	mutex_lock(&aift.lock);
	aift.kzdev = kcalloc(ZPCI_NR_DEVICES, sizeof(struct kvm_zdev),
			     GFP_KERNEL);
	if (!aift.kzdev) {
		rc = -ENOMEM;
		goto unlock;
	}
	aift.sbv = airq_iv_create(ZPCI_NR_DEVICES, AIRQ_IV_ALLOC, 0);
	if (!aift.sbv) {
		rc = -ENOMEM;
		goto free_zdev;
	}
	size = get_order(PAGE_ALIGN(ZPCI_NR_DEVICES *
				    sizeof(struct zpci_gaite)));
	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, size);
	if (!page) {
		rc = -ENOMEM;
		goto free_sbv;
	}
	aift.gait = (struct zpci_gaite *)page_to_phys(page);

	iib.aipb.faisb = (u64)aift.sbv->vector;
	iib.aipb.gait = (u64)aift.gait;
	iib.aipb.afi = nisc;
	iib.aipb.faal = ZPCI_NR_DEVICES;

	/* Setup Adapter Event Notification Interpretation */
	if (zpci_set_irq_ctrl(SIC_SET_AENI_CONTROLS, 0, &iib)) {
		rc = -EIO;
		goto free_gait;
	}

	/* Enable floating IRQs */
	if (__set_irq_noiib(SIC_IRQ_MODE_SINGLE, nisc)) {
		rc = -EIO;
		kvm_s390_pci_aen_exit();
	}

	goto unlock;

free_gait:
	size = get_order(PAGE_ALIGN(ZPCI_NR_DEVICES *
				    sizeof(struct zpci_gaite)));
	free_pages((unsigned long)aift.gait, size);
free_sbv:
	airq_iv_release(aift.sbv);
free_zdev:
	kfree(aift.kzdev);
unlock:
	mutex_unlock(&aift.lock);
	return rc;
}

int kvm_s390_pci_interp_probe(struct zpci_dev *zdev)
{
	if (!(sclp.has_zpci_interp && test_facility(69)))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_interp_probe);

int kvm_s390_pci_interp_enable(struct zpci_dev *zdev)
{
	u32 gd;
	int rc;

	/*
	 * If this is the first request to use an interpreted device, make the
	 * necessary vcpu changes
	 */
	if (!zdev->kzdev->kvm->arch.use_zpci_interp)
		kvm_s390_vcpu_pci_enable_interp(zdev->kzdev->kvm);

	/*
	 * In the event of a system reset in userspace, the GISA designation
	 * may still be assigned because the device is still enabled.
	 * Verify it's the same guest before proceeding.
	 */
	gd = (u32)(u64)&zdev->kzdev->kvm->arch.sie_page2->gisa;
	if (zdev->gd != 0 && zdev->gd != gd)
		return -EPERM;

	if (zdev_enabled(zdev)) {
		zdev->gd = 0;
		rc = zpci_disable_device(zdev);
		if (rc)
			return rc;
	}

	/*
	 * Store information about the identity of the kvm guest allowed to
	 * access this device via interpretation to be used by host CLP
	 */
	zdev->gd = gd;

	rc = zpci_enable_device(zdev);
	if (rc)
		goto err;

	/* Re-register the IOMMU that was already created */
	rc = zpci_register_ioat(zdev, 0, zdev->start_dma, zdev->end_dma,
				(u64)zdev->dma_table);
	if (rc)
		goto err;

	return rc;

err:
	zdev->gd = 0;
	return rc;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_interp_enable);

int kvm_s390_pci_interp_disable(struct zpci_dev *zdev)
{
	int rc;

	if (zdev->gd == 0)
		return -EINVAL;

	/* Remove the host CLP guest designation */
	zdev->gd = 0;

	if (zdev_enabled(zdev)) {
		rc = zpci_disable_device(zdev);
		if (rc)
			return rc;
	}

	rc = zpci_enable_device(zdev);
	if (rc)
		return rc;

	/* Re-register the IOMMU that was already created */
	rc = zpci_register_ioat(zdev, 0, zdev->start_dma, zdev->end_dma,
				(u64)zdev->dma_table);

	return rc;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_interp_disable);

int kvm_s390_pci_dev_open(struct zpci_dev *zdev)
{
	struct kvm_zdev *kzdev;

	if (zdev == NULL)
		return -ENODEV;

	kzdev = kzalloc(sizeof(struct kvm_zdev), GFP_KERNEL);
	if (!kzdev)
		return -ENOMEM;

	kzdev->zdev = zdev;
	zdev->kzdev = kzdev;

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_dev_open);

void kvm_s390_pci_dev_release(struct zpci_dev *zdev)
{
	struct kvm_zdev *kzdev;

	if (!zdev || !zdev->kzdev)
		return;

	kzdev = zdev->kzdev;
	WARN_ON(kzdev->zdev != zdev);
	zdev->kzdev = 0;
	kfree(kzdev);

}
EXPORT_SYMBOL_GPL(kvm_s390_pci_dev_release);

int kvm_s390_pci_attach_kvm(struct zpci_dev *zdev, struct kvm *kvm)
{
	struct kvm_zdev *kzdev = zdev->kzdev;

	if (!kzdev)
		return -ENODEV;

	kzdev->kvm = kvm;
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_attach_kvm);

void kvm_s390_pci_init(void)
{
	spin_lock_init(&aift.gait_lock);
	mutex_init(&aift.lock);
}
