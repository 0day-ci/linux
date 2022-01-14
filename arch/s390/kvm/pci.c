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
#include <asm/pci_io.h>
#include <asm/pci_dma.h>
#include <asm/sclp.h>
#include "pci.h"
#include "kvm-s390.h"

struct zpci_aift *aift;

#define shadow_ioat_init zdev->kzdev->ioat.head[0]

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

/* Modify PCI: Register floating adapter interruption forwarding */
static int kvm_zpci_set_airq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_REG_INT);
	struct zpci_fib fib = {0};
	u8 status;

	fib.fmt0.isc = zdev->kzdev->fib.fmt0.isc;
	fib.fmt0.sum = 1;       /* enable summary notifications */
	fib.fmt0.noi = airq_iv_end(zdev->aibv);
	fib.fmt0.aibv = virt_to_phys(zdev->aibv->vector);
	fib.fmt0.aibvo = 0;
	fib.fmt0.aisb = virt_to_phys(aift->sbv->vector + (zdev->aisb / 64) * 8);
	fib.fmt0.aisbo = zdev->aisb & 63;
	fib.gd = zdev->gd;

	return zpci_mod_fc(req, &fib, &status) ? -EIO : 0;
}

/* Modify PCI: Unregister floating adapter interruption forwarding */
static int kvm_zpci_clear_airq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_DEREG_INT);
	struct zpci_fib fib = {0};
	u8 cc, status;

	fib.gd = zdev->gd;

	cc = zpci_mod_fc(req, &fib, &status);
	if (cc == 3 || (cc == 1 && status == 24))
		/* Function already gone or IRQs already deregistered. */
		cc = 0;

	return cc ? -EIO : 0;
}

int kvm_s390_pci_aif_probe(struct zpci_dev *zdev)
{
	/* Must have appropriate hardware facilities */
	if (!(sclp.has_aeni && test_facility(71)))
		return -EINVAL;

	/* Must have a KVM association registered */
	if (!zdev->kzdev || !zdev->kzdev->kvm)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_aif_probe);

int kvm_s390_pci_aif_enable(struct zpci_dev *zdev, struct zpci_fib *fib,
			    bool assist)
{
	struct page *aibv_page, *aisb_page = NULL;
	unsigned int msi_vecs, idx;
	struct zpci_gaite *gaite;
	unsigned long bit;
	struct kvm *kvm;
	phys_addr_t gaddr;
	int rc = 0;

	/*
	 * Interrupt forwarding is only applicable if the device is already
	 * enabled for interpretation
	 */
	if (zdev->gd == 0)
		return -EINVAL;

	kvm = zdev->kzdev->kvm;
	msi_vecs = min_t(unsigned int, fib->fmt0.noi, zdev->max_msi);

	/* Replace AIBV address */
	idx = srcu_read_lock(&kvm->srcu);
	aibv_page = gfn_to_page(kvm, gpa_to_gfn((gpa_t)fib->fmt0.aibv));
	srcu_read_unlock(&kvm->srcu, idx);
	if (is_error_page(aibv_page)) {
		rc = -EIO;
		goto out;
	}
	gaddr = page_to_phys(aibv_page) + (fib->fmt0.aibv & ~PAGE_MASK);
	fib->fmt0.aibv = gaddr;

	/* Pin the guest AISB if one was specified */
	if (fib->fmt0.sum == 1) {
		idx = srcu_read_lock(&kvm->srcu);
		aisb_page = gfn_to_page(kvm, gpa_to_gfn((gpa_t)fib->fmt0.aisb));
		srcu_read_unlock(&kvm->srcu, idx);
		if (is_error_page(aisb_page)) {
			rc = -EIO;
			goto unpin1;
		}
	}

	/* AISB must be allocated before we can fill in GAITE */
	mutex_lock(&aift->lock);
	bit = airq_iv_alloc_bit(aift->sbv);
	if (bit == -1UL)
		goto unpin2;
	zdev->aisb = bit;
	zdev->aibv = airq_iv_create(msi_vecs, AIRQ_IV_DATA |
					      AIRQ_IV_BITLOCK |
					      AIRQ_IV_GUESTVEC,
				    (unsigned long *)fib->fmt0.aibv);

	spin_lock_irq(&aift->gait_lock);
	gaite = (struct zpci_gaite *)aift->gait + (zdev->aisb *
						   sizeof(struct zpci_gaite));

	/* If assist not requested, host will get all alerts */
	if (assist)
		gaite->gisa = (u32)(u64)&kvm->arch.sie_page2->gisa;
	else
		gaite->gisa = 0;

	gaite->gisc = fib->fmt0.isc;
	gaite->count++;
	gaite->aisbo = fib->fmt0.aisbo;
	gaite->aisb = virt_to_phys(page_address(aisb_page) + (fib->fmt0.aisb &
							      ~PAGE_MASK));
	aift->kzdev[zdev->aisb] = zdev->kzdev;
	spin_unlock_irq(&aift->gait_lock);

	/* Update guest FIB for re-issue */
	fib->fmt0.aisbo = zdev->aisb & 63;
	fib->fmt0.aisb = virt_to_phys(aift->sbv->vector + (zdev->aisb / 64) * 8);
	fib->fmt0.isc = kvm_s390_gisc_register(kvm, gaite->gisc);

	/* Save some guest fib values in the host for later use */
	zdev->kzdev->fib.fmt0.isc = fib->fmt0.isc;
	zdev->kzdev->fib.fmt0.aibv = fib->fmt0.aibv;
	mutex_unlock(&aift->lock);

	/* Issue the clp to setup the irq now */
	rc = kvm_zpci_set_airq(zdev);
	return rc;

unpin2:
	mutex_unlock(&aift->lock);
	if (fib->fmt0.sum == 1) {
		gaddr = page_to_phys(aisb_page);
		kvm_release_pfn_dirty(gaddr >> PAGE_SHIFT);
	}
unpin1:
	kvm_release_pfn_dirty(fib->fmt0.aibv >> PAGE_SHIFT);
out:
	return rc;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_aif_enable);

int kvm_s390_pci_aif_disable(struct zpci_dev *zdev)
{
	struct kvm_zdev *kzdev = zdev->kzdev;
	struct zpci_gaite *gaite;
	int rc;
	u8 isc;

	if (zdev->gd == 0)
		return -EINVAL;

	/* Even if the clear fails due to an error, clear the GAITE */
	rc = kvm_zpci_clear_airq(zdev);

	mutex_lock(&aift->lock);
	if (zdev->kzdev->fib.fmt0.aibv == 0)
		goto out;
	spin_lock_irq(&aift->gait_lock);
	gaite = (struct zpci_gaite *)aift->gait + (zdev->aisb *
						   sizeof(struct zpci_gaite));
	isc = gaite->gisc;
	gaite->count--;
	if (gaite->count == 0) {
		/* Release guest AIBV and AISB */
		kvm_release_pfn_dirty(kzdev->fib.fmt0.aibv >> PAGE_SHIFT);
		if (gaite->aisb != 0)
			kvm_release_pfn_dirty(gaite->aisb >> PAGE_SHIFT);
		/* Clear the GAIT entry */
		gaite->aisb = 0;
		gaite->gisc = 0;
		gaite->aisbo = 0;
		gaite->gisa = 0;
		aift->kzdev[zdev->aisb] = 0;
		/* Clear zdev info */
		airq_iv_free_bit(aift->sbv, zdev->aisb);
		airq_iv_release(zdev->aibv);
		zdev->aisb = 0;
		zdev->aibv = NULL;
	}
	spin_unlock_irq(&aift->gait_lock);
	kvm_s390_gisc_unregister(kzdev->kvm, isc);
	kzdev->fib.fmt0.isc = 0;
	kzdev->fib.fmt0.aibv = 0;
out:
	mutex_unlock(&aift->lock);

	return rc;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_aif_disable);

int kvm_s390_pci_ioat_probe(struct zpci_dev *zdev)
{
	/* Must have a KVM association registered */
	if (!zdev->kzdev || !zdev->kzdev->kvm)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_ioat_probe);

int kvm_s390_pci_ioat_enable(struct zpci_dev *zdev, u64 iota)
{
	gpa_t gpa = (gpa_t)(iota & ZPCI_RTE_ADDR_MASK);
	struct kvm_zdev_ioat *ioat;
	struct page *page;
	struct kvm *kvm;
	unsigned int idx;
	void *iaddr;
	int i, rc = 0;

	if (shadow_ioat_init)
		return -EINVAL;

	/* Ensure supported type specified */
	if ((iota & ZPCI_IOTA_RTTO_FLAG) != ZPCI_IOTA_RTTO_FLAG)
		return -EINVAL;

	kvm = zdev->kzdev->kvm;
	ioat = &zdev->kzdev->ioat;
	mutex_lock(&ioat->lock);
	idx = srcu_read_lock(&kvm->srcu);
	for (i = 0; i < ZPCI_TABLE_PAGES; i++) {
		page = gfn_to_page(kvm, gpa_to_gfn(gpa));
		if (is_error_page(page)) {
			srcu_read_unlock(&kvm->srcu, idx);
			rc = -EIO;
			goto out;
		}
		iaddr = page_to_virt(page) + (gpa & ~PAGE_MASK);
		ioat->head[i] = (unsigned long *)iaddr;
		gpa += PAGE_SIZE;
	}
	srcu_read_unlock(&kvm->srcu, idx);

	zdev->kzdev->ioat.seg = kcalloc(ZPCI_TABLE_ENTRIES_PAGES,
					sizeof(unsigned long *), GFP_KERNEL);
	if (!zdev->kzdev->ioat.seg)
		goto unpin;
	zdev->kzdev->ioat.pt = kcalloc(ZPCI_TABLE_ENTRIES,
				       sizeof(unsigned long **), GFP_KERNEL);
	if (!zdev->kzdev->ioat.pt)
		goto free_seg;

out:
	mutex_unlock(&ioat->lock);
	return rc;

free_seg:
	kfree(zdev->kzdev->ioat.seg);
unpin:
	for (i = 0; i < ZPCI_TABLE_PAGES; i++) {
		kvm_release_pfn_dirty((u64)ioat->head[i] >> PAGE_SHIFT);
		ioat->head[i] = 0;
	}
	mutex_unlock(&ioat->lock);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_ioat_enable);

static void free_pt_entry(struct kvm_zdev_ioat *ioat, int st, int pt)
{
	if (!ioat->pt[st][pt])
		return;

	kvm_release_pfn_dirty((u64)ioat->pt[st][pt]);
}

static void free_seg_entry(struct kvm_zdev_ioat *ioat, int entry)
{
	int i, st, count = 0;

	for (i = 0; i < ZPCI_TABLE_PAGES; i++) {
		if (ioat->seg[entry + i]) {
			kvm_release_pfn_dirty((u64)ioat->seg[entry + i]);
			count++;
		}
	}

	if (count == 0)
		return;

	st = entry / ZPCI_TABLE_PAGES;
	for (i = 0; i < ZPCI_TABLE_ENTRIES; i++)
		free_pt_entry(ioat, st, i);
	kfree(ioat->pt[st]);
}

int kvm_s390_pci_ioat_disable(struct zpci_dev *zdev)
{
	struct kvm_zdev_ioat *ioat;
	int i;

	if (!shadow_ioat_init)
		return -EINVAL;

	ioat = &zdev->kzdev->ioat;
	mutex_lock(&ioat->lock);
	for (i = 0; i < ZPCI_TABLE_PAGES; i++) {
		kvm_release_pfn_dirty((u64)ioat->head[i] >> PAGE_SHIFT);
		ioat->head[i] = 0;
	}

	for (i = 0; i < ZPCI_TABLE_ENTRIES_PAGES; i += ZPCI_TABLE_PAGES)
		free_seg_entry(ioat, i);

	kfree(ioat->seg);
	kfree(ioat->pt);
	mutex_unlock(&ioat->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_ioat_disable);

u8 kvm_s390_pci_get_dtsm(struct zpci_dev *zdev)
{
	return (zdev->dtsm & KVM_S390_PCI_DTSM_MASK);
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_get_dtsm);

int kvm_s390_pci_interp_probe(struct zpci_dev *zdev)
{
	/* Must have appropriate hardware facilities */
	if (!(sclp.has_zpci_lsi && test_facility(69)))
		return -EINVAL;

	/* Must have a KVM association registered */
	if (!zdev->kzdev || !zdev->kzdev->kvm)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_interp_probe);

int kvm_s390_pci_interp_enable(struct zpci_dev *zdev)
{
	u32 gd;
	int rc;

	if (!zdev->kzdev || !zdev->kzdev->kvm)
		return -EINVAL;

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
				virt_to_phys(zdev->dma_table));
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

	/* Forwarding must be turned off before interpretation */
	if (zdev->kzdev->fib.fmt0.aibv != 0)
		kvm_s390_pci_aif_disable(zdev);

	/* If we are using the IOAT assist, disable it now */
	if (zdev->kzdev->ioat.head[0])
		kvm_s390_pci_ioat_disable(zdev);

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
				virt_to_phys(zdev->dma_table));

	return rc;
}
EXPORT_SYMBOL_GPL(kvm_s390_pci_interp_disable);

int kvm_s390_pci_dev_open(struct zpci_dev *zdev)
{
	struct kvm_zdev *kzdev;

	kzdev = kzalloc(sizeof(struct kvm_zdev), GFP_KERNEL);
	if (!kzdev)
		return -ENOMEM;

	mutex_init(&kzdev->ioat.lock);

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
	mutex_destroy(&kzdev->ioat.lock);
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
