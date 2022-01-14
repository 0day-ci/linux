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
#include <asm/pci.h>
#include <asm/pci_insn.h>
#include "pci.h"

struct zpci_aift *aift;

static inline int __set_irq_noiib(u16 ctl, u8 isc)
{
	union zpci_sic_iib iib = {{0}};

	return zpci_set_irq_ctrl(ctl, isc, &iib);
}

/* Caller must hold the aift lock before calling this function */
void kvm_s390_pci_aen_exit(void)
{
	unsigned long flags;
	struct kvm_zdev **gait_kzdev;

	/*
	 * Contents of the aipb remain registered for the life of the host
	 * kernel, the information preserved in zpci_aipb and zpci_aif_sbv
	 * in case we insert the KVM module again later.  Clear the AIFT
	 * information and free anything not registered with underlying
	 * firmware.
	 */
	spin_lock_irqsave(&aift->gait_lock, flags);
	gait_kzdev = aift->kzdev;
	aift->gait = 0;
	aift->sbv = 0;
	aift->kzdev = 0;
	spin_unlock_irqrestore(&aift->gait_lock, flags);

	kfree(gait_kzdev);
}

int kvm_s390_pci_aen_init(u8 nisc)
{
	struct page *page;
	int rc = 0, size;
	bool first = false;

	/* If already enabled for AEN, bail out now */
	if (aift->gait || aift->sbv)
		return -EPERM;

	mutex_lock(&aift->lock);
	aift->kzdev = kcalloc(ZPCI_NR_DEVICES, sizeof(struct kvm_zdev),
			      GFP_KERNEL);
	if (!aift->kzdev) {
		rc = -ENOMEM;
		goto unlock;
	}

	if (!zpci_aipb) {
		zpci_aipb = kzalloc(sizeof(union zpci_sic_iib), GFP_KERNEL);
		if (!zpci_aipb) {
			rc = -ENOMEM;
			goto free_zdev;
		}
		first = true;
		aift->sbv = airq_iv_create(ZPCI_NR_DEVICES, AIRQ_IV_ALLOC, 0);
		if (!aift->sbv) {
			rc = -ENOMEM;
			goto free_aipb;
		}
		zpci_aif_sbv = aift->sbv;
		size = get_order(PAGE_ALIGN(ZPCI_NR_DEVICES *
					    sizeof(struct zpci_gaite)));
		page = alloc_pages(GFP_KERNEL | __GFP_ZERO, size);
		if (!page) {
			rc = -ENOMEM;
			goto free_sbv;
		}
		aift->gait = (struct zpci_gaite *)page_to_phys(page);

		zpci_aipb->aipb.faisb = virt_to_phys(aift->sbv->vector);
		zpci_aipb->aipb.gait = virt_to_phys(aift->gait);
		zpci_aipb->aipb.afi = nisc;
		zpci_aipb->aipb.faal = ZPCI_NR_DEVICES;

		/* Setup Adapter Event Notification Interpretation */
		if (zpci_set_irq_ctrl(SIC_SET_AENI_CONTROLS, 0, zpci_aipb)) {
			rc = -EIO;
			goto free_gait;
		}
	} else {
		/*
		 * AEN registration can only happen once per system boot.  If
		 * an aipb already exists then AEN was already registered and
		 * we can re-use the aipb contents.  This can only happen if
		 * the KVM module was removed and re-inserted.
		 */
		if (zpci_aipb->aipb.afi != nisc ||
		    zpci_aipb->aipb.faal != ZPCI_NR_DEVICES) {
			rc = -EINVAL;
			goto free_zdev;
		}
		aift->sbv = zpci_aif_sbv;
		aift->gait = (struct zpci_gaite *)zpci_aipb->aipb.gait;
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
	free_pages((unsigned long)aift->gait, size);
free_sbv:
	if (first) {
		/* If AEN setup failed, only free a newly-allocated sbv */
		airq_iv_release(aift->sbv);
		zpci_aif_sbv = 0;
	}
free_aipb:
	if (first) {
		/* If AEN setup failed, only free a newly-allocated aipb */
		kfree(zpci_aipb);
		zpci_aipb = 0;
	}
free_zdev:
	kfree(aift->kzdev);
unlock:
	mutex_unlock(&aift->lock);
	return rc;
}

int kvm_s390_pci_dev_open(struct zpci_dev *zdev)
{
	struct kvm_zdev *kzdev;

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

	kzdev = zdev->kzdev;
	WARN_ON(kzdev->zdev != zdev);
	zdev->kzdev = 0;
	kfree(kzdev);
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_dev_release);

void kvm_s390_pci_attach_kvm(struct zpci_dev *zdev, struct kvm *kvm)
{
	struct kvm_zdev *kzdev = zdev->kzdev;

	kzdev->kvm = kvm;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_attach_kvm);

int kvm_s390_pci_init(void)
{
	aift = kzalloc(sizeof(struct zpci_aift), GFP_KERNEL);
	if (!aift)
		return -ENOMEM;

	spin_lock_init(&aift->gait_lock);
	mutex_init(&aift->lock);

	return 0;
}
