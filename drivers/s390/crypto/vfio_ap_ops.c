// SPDX-License-Identifier: GPL-2.0+
/*
 * Adjunct processor matrix VFIO device driver callbacks.
 *
 * Copyright IBM Corp. 2018
 *
 * Author(s): Tony Krowiak <akrowiak@linux.ibm.com>
 *	      Halil Pasic <pasic@linux.ibm.com>
 *	      Pierre Morel <pmorel@linux.ibm.com>
 */
#include <linux/string.h>
#include <linux/vfio.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/ctype.h>
#include <linux/bitops.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <asm/kvm.h>
#include <asm/zcrypt.h>

#include "vfio_ap_private.h"

#define VFIO_AP_MDEV_TYPE_HWVIRT "passthrough"
#define VFIO_AP_MDEV_NAME_HWVIRT "VFIO AP Passthrough Device"

static int vfio_ap_mdev_reset_queues(struct mdev_device *mdev);
static struct vfio_ap_queue *vfio_ap_find_queue(int apqn);

static struct vfio_ap_queue *
vfio_ap_mdev_get_queue(struct ap_matrix_mdev *matrix_mdev, unsigned long apqn)
{
	struct vfio_ap_queue *q;

	hash_for_each_possible(matrix_mdev->qtable, q, mdev_qnode, apqn) {
		if (q && q->apqn == apqn)
			return q;
	}

	return NULL;
}

/**
 * vfio_ap_wait_for_irqclear
 * @apqn: The AP Queue number
 *
 * Checks the IRQ bit for the status of this APQN using ap_tapq.
 * Returns if the ap_tapq function succeeded and the bit is clear.
 * Returns if ap_tapq function failed with invalid, deconfigured or
 * checkstopped AP.
 * Otherwise retries up to 5 times after waiting 20ms.
 *
 */
static void vfio_ap_wait_for_irqclear(int apqn)
{
	struct ap_queue_status status;
	int retry = 5;

	do {
		status = ap_tapq(apqn, NULL);
		switch (status.response_code) {
		case AP_RESPONSE_NORMAL:
		case AP_RESPONSE_RESET_IN_PROGRESS:
			if (!status.irq_enabled)
				return;
			fallthrough;
		case AP_RESPONSE_BUSY:
			msleep(20);
			break;
		case AP_RESPONSE_Q_NOT_AVAIL:
		case AP_RESPONSE_DECONFIGURED:
		case AP_RESPONSE_CHECKSTOPPED:
		default:
			WARN_ONCE(1, "%s: tapq rc %02x: %04x\n", __func__,
				  status.response_code, apqn);
			return;
		}
	} while (--retry);

	WARN_ONCE(1, "%s: tapq rc %02x: %04x could not clear IR bit\n",
		  __func__, status.response_code, apqn);
}

/**
 * vfio_ap_free_aqic_resources
 * @q: The vfio_ap_queue
 *
 * Unregisters the ISC in the GIB when the saved ISC not invalid.
 * Unpin the guest's page holding the NIB when it exist.
 * Reset the saved_pfn and saved_isc to invalid values.
 *
 */
static void vfio_ap_free_aqic_resources(struct vfio_ap_queue *q)
{
	if (!q)
		return;
	if (q->saved_isc != VFIO_AP_ISC_INVALID &&
	    !WARN_ON(!(q->matrix_mdev && q->matrix_mdev->kvm))) {
		kvm_s390_gisc_unregister(q->matrix_mdev->kvm, q->saved_isc);
		q->saved_isc = VFIO_AP_ISC_INVALID;
	}
	if (q->saved_pfn && !WARN_ON(!q->matrix_mdev)) {
		vfio_unpin_pages(mdev_dev(q->matrix_mdev->mdev),
				 &q->saved_pfn, 1);
		q->saved_pfn = 0;
	}
}

/**
 * vfio_ap_irq_disable
 * @q: The vfio_ap_queue
 *
 * Uses ap_aqic to disable the interruption and in case of success, reset
 * in progress or IRQ disable command already proceeded: calls
 * vfio_ap_wait_for_irqclear() to check for the IRQ bit to be clear
 * and calls vfio_ap_free_aqic_resources() to free the resources associated
 * with the AP interrupt handling.
 *
 * In the case the AP is busy, or a reset is in progress,
 * retries after 20ms, up to 5 times.
 *
 * Returns if ap_aqic function failed with invalid, deconfigured or
 * checkstopped AP.
 */
static struct ap_queue_status vfio_ap_irq_disable(struct vfio_ap_queue *q)
{
	struct ap_qirq_ctrl aqic_gisa = {};
	struct ap_queue_status status;
	int retries = 5;

	do {
		status = ap_aqic(q->apqn, aqic_gisa, NULL);
		switch (status.response_code) {
		case AP_RESPONSE_OTHERWISE_CHANGED:
		case AP_RESPONSE_NORMAL:
			vfio_ap_wait_for_irqclear(q->apqn);
			goto end_free;
		case AP_RESPONSE_RESET_IN_PROGRESS:
		case AP_RESPONSE_BUSY:
			msleep(20);
			break;
		case AP_RESPONSE_Q_NOT_AVAIL:
		case AP_RESPONSE_DECONFIGURED:
		case AP_RESPONSE_CHECKSTOPPED:
		case AP_RESPONSE_INVALID_ADDRESS:
		default:
			/* All cases in default means AP not operational */
			WARN_ONCE(1, "%s: ap_aqic status %d\n", __func__,
				  status.response_code);
			goto end_free;
		}
	} while (retries--);

	WARN_ONCE(1, "%s: ap_aqic status %d\n", __func__,
		  status.response_code);
end_free:
	vfio_ap_free_aqic_resources(q);
	return status;
}

/**
 * vfio_ap_setirq: Enable Interruption for a APQN
 *
 * @dev: the device associated with the ap_queue
 * @q:	 the vfio_ap_queue holding AQIC parameters
 *
 * Pin the NIB saved in *q
 * Register the guest ISC to GIB interface and retrieve the
 * host ISC to issue the host side PQAP/AQIC
 *
 * Response.status may be set to AP_RESPONSE_INVALID_ADDRESS in case the
 * vfio_pin_pages failed.
 *
 * Otherwise return the ap_queue_status returned by the ap_aqic(),
 * all retry handling will be done by the guest.
 */
static struct ap_queue_status vfio_ap_irq_enable(struct vfio_ap_queue *q,
						 int isc,
						 unsigned long nib)
{
	struct ap_qirq_ctrl aqic_gisa = {};
	struct ap_queue_status status = {};
	struct kvm_s390_gisa *gisa;
	struct kvm *kvm;
	unsigned long h_nib, g_pfn, h_pfn;
	int ret;

	g_pfn = nib >> PAGE_SHIFT;
	ret = vfio_pin_pages(mdev_dev(q->matrix_mdev->mdev), &g_pfn, 1,
			     IOMMU_READ | IOMMU_WRITE, &h_pfn);
	switch (ret) {
	case 1:
		break;
	default:
		status.response_code = AP_RESPONSE_INVALID_ADDRESS;
		return status;
	}

	kvm = q->matrix_mdev->kvm;
	gisa = kvm->arch.gisa_int.origin;

	h_nib = (h_pfn << PAGE_SHIFT) | (nib & ~PAGE_MASK);
	aqic_gisa.gisc = isc;
	aqic_gisa.isc = kvm_s390_gisc_register(kvm, isc);
	aqic_gisa.ir = 1;
	aqic_gisa.gisa = (uint64_t)gisa >> 4;

	status = ap_aqic(q->apqn, aqic_gisa, (void *)h_nib);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		/* See if we did clear older IRQ configuration */
		vfio_ap_free_aqic_resources(q);
		q->saved_pfn = g_pfn;
		q->saved_isc = isc;
		break;
	case AP_RESPONSE_OTHERWISE_CHANGED:
		/* We could not modify IRQ setings: clear new configuration */
		vfio_unpin_pages(mdev_dev(q->matrix_mdev->mdev), &g_pfn, 1);
		kvm_s390_gisc_unregister(kvm, isc);
		break;
	default:
		pr_warn("%s: apqn %04x: response: %02x\n", __func__, q->apqn,
			status.response_code);
		vfio_ap_irq_disable(q);
		break;
	}

	return status;
}

/**
 * handle_pqap: PQAP instruction callback
 *
 * @vcpu: The vcpu on which we received the PQAP instruction
 *
 * Get the general register contents to initialize internal variables.
 * REG[0]: APQN
 * REG[1]: IR and ISC
 * REG[2]: NIB
 *
 * Response.status may be set to following Response Code:
 * - AP_RESPONSE_Q_NOT_AVAIL: if the queue is not available
 * - AP_RESPONSE_DECONFIGURED: if the queue is not configured
 * - AP_RESPONSE_NORMAL (0) : in case of successs
 *   Check vfio_ap_setirq() and vfio_ap_clrirq() for other possible RC.
 * We take the matrix_dev lock to ensure serialization on queues and
 * mediated device access.
 *
 * Return 0 if we could handle the request inside KVM.
 * otherwise, returns -EOPNOTSUPP to let QEMU handle the fault.
 */
static int handle_pqap(struct kvm_vcpu *vcpu)
{
	uint64_t status;
	uint16_t apqn;
	struct vfio_ap_queue *q;
	struct ap_queue_status qstatus = {
			       .response_code = AP_RESPONSE_Q_NOT_AVAIL, };
	struct ap_matrix_mdev *matrix_mdev;

	/* If we do not use the AIV facility just go to userland */
	if (!(vcpu->arch.sie_block->eca & ECA_AIV))
		return -EOPNOTSUPP;

	apqn = vcpu->run->s.regs.gprs[0] & 0xffff;
	mutex_lock(&matrix_dev->lock);

	if (!vcpu->kvm->arch.crypto.pqap_hook)
		goto out_unlock;
	matrix_mdev = container_of(vcpu->kvm->arch.crypto.pqap_hook,
				   struct ap_matrix_mdev, pqap_hook);

	/*
	 * If the KVM pointer is in the process of being set, wait until the
	 * process has completed.
	 */
	wait_event_cmd(matrix_mdev->wait_for_kvm,
		       !matrix_mdev->kvm_busy,
		       mutex_unlock(&matrix_dev->lock),
		       mutex_lock(&matrix_dev->lock));

	/* If the there is no guest using the mdev, there is nothing to do */
	if (!matrix_mdev->kvm)
		goto out_unlock;

	q = vfio_ap_mdev_get_queue(matrix_mdev, apqn);
	if (!q)
		goto out_unlock;

	status = vcpu->run->s.regs.gprs[1];

	/* If IR bit(16) is set we enable the interrupt */
	if ((status >> (63 - 16)) & 0x01)
		qstatus = vfio_ap_irq_enable(q, status & 0x07,
					     vcpu->run->s.regs.gprs[2]);
	else
		qstatus = vfio_ap_irq_disable(q);

out_unlock:
	memcpy(&vcpu->run->s.regs.gprs[1], &qstatus, sizeof(qstatus));
	vcpu->run->s.regs.gprs[1] >>= 32;
	mutex_unlock(&matrix_dev->lock);
	return 0;
}

static void vfio_ap_matrix_init(struct ap_config_info *info,
				struct ap_matrix *matrix)
{
	matrix->apm_max = info->apxa ? info->Na : 63;
	matrix->aqm_max = info->apxa ? info->Nd : 15;
	matrix->adm_max = info->apxa ? info->Nd : 15;
}

static bool vfio_ap_mdev_has_crycb(struct ap_matrix_mdev *matrix_mdev)
{
	return (matrix_mdev->kvm && matrix_mdev->kvm->arch.crypto.crycbd);
}

static void vfio_ap_mdev_commit_apcb(struct ap_matrix_mdev *matrix_mdev)
{
	if (vfio_ap_mdev_has_crycb(matrix_mdev))
		kvm_arch_crypto_set_masks(matrix_mdev->kvm,
					  matrix_mdev->shadow_apcb.apm,
					  matrix_mdev->shadow_apcb.aqm,
					  matrix_mdev->shadow_apcb.adm);
}

/*
 * vfio_ap_mdev_filter_apcb
 *
 * @matrix_mdev: the mdev whose AP configuration is to be filtered.
 * @shadow_apcb: the APCB to use to store the guest's AP configuration after
 *		 filtering takes place.
 */
static void vfio_ap_mdev_filter_apcb(struct ap_matrix_mdev *matrix_mdev,
				     struct ap_matrix *shadow_apcb)
{
	unsigned long apid, apqi, apqn;

	bitmap_and(shadow_apcb->apm, matrix_mdev->matrix.apm,
		   (unsigned long *)matrix_dev->config_info.apm, AP_DEVICES);
	bitmap_and(shadow_apcb->aqm, matrix_mdev->matrix.aqm,
		   (unsigned long *)matrix_dev->config_info.aqm, AP_DOMAINS);
	bitmap_and(shadow_apcb->adm, matrix_mdev->matrix.adm,
		   (unsigned long *)matrix_dev->config_info.adm, AP_DOMAINS);

	for_each_set_bit_inv(apid, shadow_apcb->apm, AP_DEVICES) {
		for_each_set_bit_inv(apqi, shadow_apcb->aqm, AP_DOMAINS) {
			/*
			 * If the APQN is not bound to the vfio_ap device
			 * driver, then we can't assign it to the guest's
			 * AP configuration. The AP architecture won't
			 * allow filtering of a single APQN, so if we're
			 * filtering APIDs, then filter the APID; otherwise,
			 * filter the APQI.
			 */
			apqn = AP_MKQID(apid, apqi);
			if (!vfio_ap_mdev_get_queue(matrix_mdev, apqn)) {
				clear_bit_inv(apid, shadow_apcb->apm);
				break;
			}
		}
	}
}

/**
 * vfio_ap_mdev_refresh_apcb
 *
 * Refresh the guest's APCB by filtering the APQNs assigned to the matrix mdev
 * that do not reference an AP queue device bound to the vfio_ap device driver.
 *
 * @matrix_mdev:  the matrix mdev whose AP configuration is to be filtered
 */
static void vfio_ap_mdev_refresh_apcb(struct ap_matrix_mdev *matrix_mdev)
{
	struct ap_matrix shadow_apcb;

	vfio_ap_matrix_init(&matrix_dev->config_info, &shadow_apcb);
	vfio_ap_mdev_filter_apcb(matrix_mdev, &shadow_apcb);

	if (memcmp(&shadow_apcb, &matrix_mdev->shadow_apcb,
		   sizeof(struct ap_matrix)) != 0) {
		memcpy(&matrix_mdev->shadow_apcb, &shadow_apcb,
		       sizeof(struct ap_matrix));
		vfio_ap_mdev_commit_apcb(matrix_mdev);
	}
}

static int vfio_ap_mdev_create(struct kobject *kobj, struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev;

	if ((atomic_dec_if_positive(&matrix_dev->available_instances) < 0))
		return -EPERM;

	matrix_mdev = kzalloc(sizeof(*matrix_mdev), GFP_KERNEL);
	if (!matrix_mdev) {
		atomic_inc(&matrix_dev->available_instances);
		return -ENOMEM;
	}

	matrix_mdev->mdev = mdev;
	vfio_ap_matrix_init(&matrix_dev->config_info, &matrix_mdev->matrix);
	init_waitqueue_head(&matrix_mdev->wait_for_kvm);
	vfio_ap_matrix_init(&matrix_dev->config_info, &matrix_mdev->shadow_apcb);
	hash_init(matrix_mdev->qtable);
	mdev_set_drvdata(mdev, matrix_mdev);
	matrix_mdev->pqap_hook.hook = handle_pqap;
	matrix_mdev->pqap_hook.owner = THIS_MODULE;
	mutex_lock(&matrix_dev->lock);
	list_add(&matrix_mdev->node, &matrix_dev->mdev_list);
	mutex_unlock(&matrix_dev->lock);

	return 0;
}

static void vfio_ap_mdev_link_queue(struct ap_matrix_mdev *matrix_mdev,
				    struct vfio_ap_queue *q)
{
	if (q) {
		q->matrix_mdev = matrix_mdev;
		hash_add(matrix_mdev->qtable,
			 &q->mdev_qnode, q->apqn);
	}
}

static void vfio_ap_mdev_link_apqn(struct ap_matrix_mdev *matrix_mdev, int apqn)
{
	struct vfio_ap_queue *q;

	q = vfio_ap_find_queue(apqn);
	vfio_ap_mdev_link_queue(matrix_mdev, q);
}

static void vfio_ap_mdev_unlink_queue_fr_mdev(struct vfio_ap_queue *q)
{
	hash_del(&q->mdev_qnode);
}

static void vfio_ap_mdev_unlink_fr_queue(struct vfio_ap_queue *q)
{
	q->matrix_mdev = NULL;
}

static void vfio_ap_mdev_unlink_queue(struct vfio_ap_queue *q)
{
	if (q) {
		vfio_ap_mdev_unlink_queue_fr_mdev(q);
		vfio_ap_mdev_unlink_fr_queue(q);
	}
}

static void vfio_ap_mdev_unlink_apqn(int apqn)
{
	struct vfio_ap_queue *q;

	q = vfio_ap_find_queue(apqn);
	vfio_ap_mdev_unlink_queue(q);
}

static void vfio_ap_mdev_unlink_fr_queues(struct ap_matrix_mdev *matrix_mdev)
{
	struct vfio_ap_queue *q;
	unsigned long apid, apqi;

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, AP_DEVICES) {
		for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm,
				     AP_DOMAINS) {
			q = vfio_ap_mdev_get_queue(matrix_mdev,
						   AP_MKQID(apid, apqi));
			if (q)
				q->matrix_mdev = NULL;
		}
	}
}

static int vfio_ap_mdev_remove(struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	mutex_lock(&matrix_dev->lock);

	/*
	 * If the KVM pointer is in flux or the guest is running, disallow
	 * un-assignment of control domain.
	 */
	if (matrix_mdev->kvm_busy || matrix_mdev->kvm) {
		mutex_unlock(&matrix_dev->lock);
		return -EBUSY;
	}

	vfio_ap_mdev_reset_queues(mdev);
	vfio_ap_mdev_unlink_fr_queues(matrix_mdev);
	list_del(&matrix_mdev->node);
	kfree(matrix_mdev);
	mdev_set_drvdata(mdev, NULL);
	atomic_inc(&matrix_dev->available_instances);
	mutex_unlock(&matrix_dev->lock);

	return 0;
}

static ssize_t name_show(struct kobject *kobj, struct device *dev, char *buf)
{
	return sprintf(buf, "%s\n", VFIO_AP_MDEV_NAME_HWVIRT);
}

static MDEV_TYPE_ATTR_RO(name);

static ssize_t available_instances_show(struct kobject *kobj,
					struct device *dev, char *buf)
{
	return sprintf(buf, "%d\n",
		       atomic_read(&matrix_dev->available_instances));
}

static MDEV_TYPE_ATTR_RO(available_instances);

static ssize_t device_api_show(struct kobject *kobj, struct device *dev,
			       char *buf)
{
	return sprintf(buf, "%s\n", VFIO_DEVICE_API_AP_STRING);
}

static MDEV_TYPE_ATTR_RO(device_api);

static struct attribute *vfio_ap_mdev_type_attrs[] = {
	&mdev_type_attr_name.attr,
	&mdev_type_attr_device_api.attr,
	&mdev_type_attr_available_instances.attr,
	NULL,
};

static struct attribute_group vfio_ap_mdev_hwvirt_type_group = {
	.name = VFIO_AP_MDEV_TYPE_HWVIRT,
	.attrs = vfio_ap_mdev_type_attrs,
};

static struct attribute_group *vfio_ap_mdev_type_groups[] = {
	&vfio_ap_mdev_hwvirt_type_group,
	NULL,
};

#define MDEV_SHARING_ERR "Userspace may not re-assign queue %02lx.%04lx " \
			 "already assigned to %s"

static void vfio_ap_mdev_log_sharing_err(struct ap_matrix_mdev *matrix_mdev,
					 unsigned long *apm,
					 unsigned long *aqm)
{
	unsigned long apid, apqi;
	const struct device *dev = mdev_dev(matrix_mdev->mdev);
	const char *mdev_name = dev_name(dev);


	for_each_set_bit_inv(apid, apm, AP_DEVICES)
		for_each_set_bit_inv(apqi, aqm, AP_DOMAINS)
//			pr_warn(MDEV_SHARING_ERR, apid, apqi, mdev_name);
			dev_warn(dev, MDEV_SHARING_ERR, apid, apqi, mdev_name);
}

/**
 * vfio_ap_mdev_verify_no_sharing
 *
 * Verifies that each APQN derived from the Cartesian product of a bitmap of
 * AP adapter IDs and AP queue indexes is not configured for any matrix
 * mediated device. AP queue sharing is not allowed.
 *
 * @mdev_apm: mask indicating the APIDs of the APQNs to be verified
 * @mdev_aqm: mask indicating the APQIs of the APQNs to be verified
 *
 * Returns 0 if the APQNs are not shared, otherwise; returns -EADDRINUSE.
 */
static int vfio_ap_mdev_verify_no_sharing(unsigned long *mdev_apm,
					  unsigned long *mdev_aqm)
{
	struct ap_matrix_mdev *matrix_mdev;
	DECLARE_BITMAP(apm, AP_DEVICES);
	DECLARE_BITMAP(aqm, AP_DOMAINS);

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		/*
		 * If the input apm and aqm belong to the matrix_mdev's matrix,
		 * then move on to the next.
		 */
		if (mdev_apm == matrix_mdev->matrix.apm &&
		    mdev_aqm == matrix_mdev->matrix.aqm)
			continue;

		memset(apm, 0, sizeof(apm));
		memset(aqm, 0, sizeof(aqm));

		/*
		 * We work on full longs, as we can only exclude the leftover
		 * bits in non-inverse order. The leftover is all zeros.
		 */
		if (!bitmap_and(apm, mdev_apm, matrix_mdev->matrix.apm,
				AP_DEVICES))
			continue;

		if (!bitmap_and(aqm, mdev_aqm, matrix_mdev->matrix.aqm,
				AP_DOMAINS))
			continue;

		vfio_ap_mdev_log_sharing_err(matrix_mdev, apm, aqm);

		return -EADDRINUSE;
	}

	return 0;
}

static int vfio_ap_mdev_validate_masks(struct ap_matrix_mdev *matrix_mdev)
{
	if (ap_apqn_in_matrix_owned_by_def_drv(matrix_mdev->matrix.apm,
					       matrix_mdev->matrix.aqm))
		return -EADDRNOTAVAIL;

	return vfio_ap_mdev_verify_no_sharing(matrix_mdev->matrix.apm,
					      matrix_mdev->matrix.aqm);
}

static void vfio_ap_mdev_link_adapter(struct ap_matrix_mdev *matrix_mdev,
				      unsigned long apid)
{
	unsigned long apqi;

	for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm, AP_DOMAINS)
		vfio_ap_mdev_link_apqn(matrix_mdev,
				       AP_MKQID(apid, apqi));
}

/**
 * assign_adapter_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_adapter attribute
 * @buf:	a buffer containing the AP adapter number (APID) to
 *		be assigned
 * @count:	the number of bytes in @buf
 *
 * Parses the APID from @buf and sets the corresponding bit in the mediated
 * matrix device's APM.
 *
 * Returns the number of bytes processed if the APID is valid; otherwise,
 * returns one of the following errors:
 *
 *	1. -EINVAL
 *	   The APID is not a valid number
 *
 *	2. -ENODEV
 *	   The APID exceeds the maximum value configured for the system
 *
 *	3. -EADDRNOTAVAIL
 *	   An APQN derived from the cross product of the APID being assigned
 *	   and the APQIs previously assigned is not bound to the vfio_ap device
 *	   driver; or, if no APQIs have yet been assigned, the APID is not
 *	   contained in an APQN bound to the vfio_ap device driver.
 *
 *	4. -EADDRINUSE
 *	   An APQN derived from the cross product of the APID being assigned
 *	   and the APQIs previously assigned is being used by another mediated
 *	   matrix device.
 *
 *	5. -EAGAIN
 *	   The mdev lock could not be acquired which is required in order to
 *	   change the AP configuration for the mdev
 */
static ssize_t assign_adapter_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int ret;
	unsigned long apid;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	if (!mutex_trylock(&matrix_dev->lock))
		return -EAGAIN;

	/*
	 * If the KVM pointer is in flux or the guest is running, disallow
	 * un-assignment of adapter
	 */
	if (matrix_mdev->kvm_busy) {
		ret = -EBUSY;
		goto done;
	}

	ret = kstrtoul(buf, 0, &apid);
	if (ret)
		goto done;

	if (apid > matrix_mdev->matrix.apm_max) {
		ret = -ENODEV;
		goto done;
	}

	set_bit_inv(apid, matrix_mdev->matrix.apm);

	ret = vfio_ap_mdev_validate_masks(matrix_mdev);
	if (ret) {
		clear_bit_inv(apid, matrix_mdev->matrix.apm);
		goto done;
	}

	vfio_ap_mdev_link_adapter(matrix_mdev, apid);
	vfio_ap_mdev_refresh_apcb(matrix_mdev);
	ret = count;
done:
	mutex_unlock(&matrix_dev->lock);

	return ret;
}
static DEVICE_ATTR_WO(assign_adapter);

static void vfio_ap_mdev_unlink_adapter(struct ap_matrix_mdev *matrix_mdev,
					unsigned long apid)
{
	unsigned long apqi;

	for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm, AP_DOMAINS)
		vfio_ap_mdev_unlink_apqn(AP_MKQID(apid, apqi));
}

/**
 * unassign_adapter_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_adapter attribute
 * @buf:	a buffer containing the adapter number (APID) to be unassigned
 * @count:	the number of bytes in @buf
 *
 * Parses the APID from @buf and clears the corresponding bit in the mediated
 * matrix device's APM.
 *
 * Returns the number of bytes processed if the APID is valid; otherwise,
 * returns one of the following errors:
 *	-EINVAL if the APID is not a number
 *	-ENODEV if the APID it exceeds the maximum value configured for the
 *		system
 */
static ssize_t unassign_adapter_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned long apid;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	mutex_lock(&matrix_dev->lock);

	/*
	 * If the KVM pointer is in flux or the guest is running, disallow
	 * un-assignment of adapter
	 */
	if (matrix_mdev->kvm_busy) {
		ret = -EBUSY;
		goto done;
	}

	ret = kstrtoul(buf, 0, &apid);
	if (ret)
		goto done;

	if (apid > matrix_mdev->matrix.apm_max) {
		ret = -ENODEV;
		goto done;
	}

	clear_bit_inv((unsigned long)apid, matrix_mdev->matrix.apm);
	vfio_ap_mdev_unlink_adapter(matrix_mdev, apid);
	vfio_ap_mdev_refresh_apcb(matrix_mdev);
	ret = count;
done:
	mutex_unlock(&matrix_dev->lock);
	return ret;
}
static DEVICE_ATTR_WO(unassign_adapter);

static void vfio_ap_mdev_link_domain(struct ap_matrix_mdev *matrix_mdev,
				     unsigned long apqi)
{
	unsigned long apid;

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, AP_DEVICES)
		vfio_ap_mdev_link_apqn(matrix_mdev, AP_MKQID(apid, apqi));
}

/**
 * assign_domain_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_domain attribute
 * @buf:	a buffer containing the AP queue index (APQI) of the domain to
 *		be assigned
 * @count:	the number of bytes in @buf
 *
 * Parses the APQI from @buf and sets the corresponding bit in the mediated
 * matrix device's AQM.
 *
 * Returns the number of bytes processed if the APQI is valid; otherwise returns
 * one of the following errors:
 *
 *	1. -EINVAL
 *	   The APQI is not a valid number
 *
 *	2. -ENODEV
 *	   The APQI exceeds the maximum value configured for the system
 *
 *	3. -EADDRNOTAVAIL
 *	   An APQN derived from the cross product of the APQI being assigned
 *	   and the APIDs previously assigned is not bound to the vfio_ap device
 *	   driver; or, if no APIDs have yet been assigned, the APQI is not
 *	   contained in an APQN bound to the vfio_ap device driver.
 *
 *	4. -EADDRINUSE
 *	   An APQN derived from the cross product of the APQI being assigned
 *	   and the APIDs previously assigned is being used by another mediated
 *	   matrix device.
 *
 *	5. -EAGAIN
 *	   The mdev lock could not be acquired which is required in order to
 *	   change the AP configuration for the mdev
 */
static ssize_t assign_domain_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret;
	unsigned long apqi;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);
	unsigned long max_apqi = matrix_mdev->matrix.aqm_max;

	if (!mutex_trylock(&matrix_dev->lock))
		return -EAGAIN;

	/*
	 * If the KVM pointer is in flux or the guest is running, disallow
	 * assignment of domain
	 */
	if (matrix_mdev->kvm_busy) {
		ret = -EBUSY;
		goto done;
	}

	ret = kstrtoul(buf, 0, &apqi);
	if (ret)
		goto done;
	if (apqi > max_apqi) {
		ret = -ENODEV;
		goto done;
	}

	set_bit_inv(apqi, matrix_mdev->matrix.aqm);

	ret = vfio_ap_mdev_validate_masks(matrix_mdev);
	if (ret) {
		clear_bit_inv(apqi, matrix_mdev->matrix.aqm);
		goto done;
	}

	vfio_ap_mdev_link_domain(matrix_mdev, apqi);
	vfio_ap_mdev_refresh_apcb(matrix_mdev);
	ret = count;
done:
	mutex_unlock(&matrix_dev->lock);

	return ret;
}
static DEVICE_ATTR_WO(assign_domain);

static void vfio_ap_mdev_unlink_domain(struct ap_matrix_mdev *matrix_mdev,
				       unsigned long apqi)
{
	unsigned long apid;

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, AP_DEVICES)
		vfio_ap_mdev_unlink_apqn(AP_MKQID(apid, apqi));
}

/**
 * unassign_domain_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_domain attribute
 * @buf:	a buffer containing the AP queue index (APQI) of the domain to
 *		be unassigned
 * @count:	the number of bytes in @buf
 *
 * Parses the APQI from @buf and clears the corresponding bit in the
 * mediated matrix device's AQM.
 *
 * Returns the number of bytes processed if the APQI is valid; otherwise,
 * returns one of the following errors:
 *	-EINVAL if the APQI is not a number
 *	-ENODEV if the APQI exceeds the maximum value configured for the system
 */
static ssize_t unassign_domain_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int ret;
	unsigned long apqi;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	mutex_lock(&matrix_dev->lock);

	/*
	 * If the KVM pointer is in flux or the guest is running, disallow
	 * un-assignment of domain
	 */
	if (matrix_mdev->kvm_busy) {
		ret = -EBUSY;
		goto done;
	}

	ret = kstrtoul(buf, 0, &apqi);
	if (ret)
		goto done;

	if (apqi > matrix_mdev->matrix.aqm_max) {
		ret = -ENODEV;
		goto done;
	}

	clear_bit_inv((unsigned long)apqi, matrix_mdev->matrix.aqm);
	vfio_ap_mdev_unlink_domain(matrix_mdev, apqi);
	vfio_ap_mdev_refresh_apcb(matrix_mdev);
	ret = count;

done:
	mutex_unlock(&matrix_dev->lock);
	return ret;
}
static DEVICE_ATTR_WO(unassign_domain);

static void vfio_ap_mdev_hot_plug_cdom(struct ap_matrix_mdev *matrix_mdev,
				       unsigned long domid)
{
	if (!test_bit_inv(domid, matrix_mdev->shadow_apcb.adm) &&
	    test_bit_inv(domid,
			 (unsigned long *)matrix_dev->config_info.adm)) {
		set_bit_inv(domid, matrix_mdev->shadow_apcb.adm);
		vfio_ap_mdev_commit_apcb(matrix_mdev);
	}
}

/**
 * assign_control_domain_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_control_domain attribute
 * @buf:	a buffer containing the domain ID to be assigned
 * @count:	the number of bytes in @buf
 *
 * Parses the domain ID from @buf and sets the corresponding bit in the mediated
 * matrix device's ADM.
 *
 * Returns the number of bytes processed if the domain ID is valid; otherwise,
 * returns one of the following errors:
 *	-EINVAL if the ID is not a number
 *	-ENODEV if the ID exceeds the maximum value configured for the system
 *	-EAGAIN if the mdev lock could not be acquired
 */
static ssize_t assign_control_domain_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int ret;
	unsigned long id;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	if (!mutex_trylock(&matrix_dev->lock))
		return -EAGAIN;

	/*
	 * If the KVM pointer is in flux or the guest is running, disallow
	 * assignment of control domain.
	 */
	if (matrix_mdev->kvm_busy) {
		ret = -EBUSY;
		goto done;
	}

	ret = kstrtoul(buf, 0, &id);
	if (ret)
		goto done;

	if (id > matrix_mdev->matrix.adm_max) {
		ret = -ENODEV;
		goto done;
	}

	/* Set the bit in the ADM (bitmask) corresponding to the AP control
	 * domain number (id). The bits in the mask, from most significant to
	 * least significant, correspond to IDs 0 up to the one less than the
	 * number of control domains that can be assigned.
	 */
	set_bit_inv(id, matrix_mdev->matrix.adm);
	vfio_ap_mdev_hot_plug_cdom(matrix_mdev, id);
	ret = count;
done:
	mutex_unlock(&matrix_dev->lock);
	return ret;
}
static DEVICE_ATTR_WO(assign_control_domain);

static void vfio_ap_mdev_hot_unplug_cdom(struct ap_matrix_mdev *matrix_mdev,
					 unsigned long domid)
{
	if (test_bit_inv(domid, matrix_mdev->shadow_apcb.adm)) {
		clear_bit_inv(domid, matrix_mdev->shadow_apcb.adm);
		vfio_ap_mdev_commit_apcb(matrix_mdev);
	}
}

/**
 * unassign_control_domain_store
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_control_domain attribute
 * @buf:	a buffer containing the domain ID to be unassigned
 * @count:	the number of bytes in @buf
 *
 * Parses the domain ID from @buf and clears the corresponding bit in the
 * mediated matrix device's ADM.
 *
 * Returns the number of bytes processed if the domain ID is valid; otherwise,
 * returns one of the following errors:
 *	-EINVAL if the ID is not a number
 *	-ENODEV if the ID exceeds the maximum value configured for the system
 */
static ssize_t unassign_control_domain_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	int ret;
	unsigned long domid;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);
	unsigned long max_domid =  matrix_mdev->matrix.adm_max;

	mutex_lock(&matrix_dev->lock);

	/*
	 * If the KVM pointer is in flux or the guest is running, disallow
	 * un-assignment of control domain.
	 */
	if (matrix_mdev->kvm_busy) {
		ret = -EBUSY;
		goto done;
	}

	ret = kstrtoul(buf, 0, &domid);
	if (ret)
		goto done;
	if (domid > max_domid) {
		ret = -ENODEV;
		goto done;
	}

	clear_bit_inv(domid, matrix_mdev->matrix.adm);
	vfio_ap_mdev_hot_unplug_cdom(matrix_mdev, domid);
	ret = count;
done:
	mutex_unlock(&matrix_dev->lock);
	return ret;
}
static DEVICE_ATTR_WO(unassign_control_domain);

static ssize_t control_domains_show(struct device *dev,
				    struct device_attribute *dev_attr,
				    char *buf)
{
	unsigned long id;
	int nchars = 0;
	int n;
	char *bufpos = buf;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);
	unsigned long max_domid = matrix_mdev->matrix.adm_max;

	mutex_lock(&matrix_dev->lock);
	for_each_set_bit_inv(id, matrix_mdev->matrix.adm, max_domid + 1) {
		n = sprintf(bufpos, "%04lx\n", id);
		bufpos += n;
		nchars += n;
	}
	mutex_unlock(&matrix_dev->lock);

	return nchars;
}
static DEVICE_ATTR_RO(control_domains);

static ssize_t vfio_ap_mdev_matrix_show(struct ap_matrix *matrix, char *buf)
{
	char *bufpos = buf;
	unsigned long apid;
	unsigned long apqi;
	unsigned long apid1;
	unsigned long apqi1;
	unsigned long napm_bits = matrix->apm_max + 1;
	unsigned long naqm_bits = matrix->aqm_max + 1;
	int nchars = 0;
	int n;

	apid1 = find_first_bit_inv(matrix->apm, napm_bits);
	apqi1 = find_first_bit_inv(matrix->aqm, naqm_bits);

	if ((apid1 < napm_bits) && (apqi1 < naqm_bits)) {
		for_each_set_bit_inv(apid, matrix->apm, napm_bits) {
			for_each_set_bit_inv(apqi, matrix->aqm,
					     naqm_bits) {
				n = sprintf(bufpos, "%02lx.%04lx\n", apid,
					    apqi);
				bufpos += n;
				nchars += n;
			}
		}
	} else if (apid1 < napm_bits) {
		for_each_set_bit_inv(apid, matrix->apm, napm_bits) {
			n = sprintf(bufpos, "%02lx.\n", apid);
			bufpos += n;
			nchars += n;
		}
	} else if (apqi1 < naqm_bits) {
		for_each_set_bit_inv(apqi, matrix->aqm, naqm_bits) {
			n = sprintf(bufpos, ".%04lx\n", apqi);
			bufpos += n;
			nchars += n;
		}
	}

	return nchars;
}

static ssize_t matrix_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	ssize_t nchars;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	mutex_lock(&matrix_dev->lock);
	nchars = vfio_ap_mdev_matrix_show(&matrix_mdev->matrix, buf);
	mutex_unlock(&matrix_dev->lock);

	return nchars;
}
static DEVICE_ATTR_RO(matrix);

static ssize_t guest_matrix_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	ssize_t nchars;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	mutex_lock(&matrix_dev->lock);
	nchars = vfio_ap_mdev_matrix_show(&matrix_mdev->shadow_apcb, buf);
	mutex_unlock(&matrix_dev->lock);

	return nchars;
}
static DEVICE_ATTR_RO(guest_matrix);

static struct attribute *vfio_ap_mdev_attrs[] = {
	&dev_attr_assign_adapter.attr,
	&dev_attr_unassign_adapter.attr,
	&dev_attr_assign_domain.attr,
	&dev_attr_unassign_domain.attr,
	&dev_attr_assign_control_domain.attr,
	&dev_attr_unassign_control_domain.attr,
	&dev_attr_control_domains.attr,
	&dev_attr_matrix.attr,
	&dev_attr_guest_matrix.attr,
	NULL,
};

static struct attribute_group vfio_ap_mdev_attr_group = {
	.attrs = vfio_ap_mdev_attrs
};

static const struct attribute_group *vfio_ap_mdev_attr_groups[] = {
	&vfio_ap_mdev_attr_group,
	NULL
};

/**
 * vfio_ap_mdev_set_kvm
 *
 * @matrix_mdev: a mediated matrix device
 * @kvm: reference to KVM instance
 *
 * Sets all data for @matrix_mdev that are needed to manage AP resources
 * for the guest whose state is represented by @kvm.
 *
 * Note: The matrix_dev->lock must be taken prior to calling
 * this function; however, the lock will be temporarily released while the
 * guest's AP configuration is set to avoid a potential lockdep splat.
 * The kvm->lock is taken to set the guest's AP configuration which, under
 * certain circumstances, will result in a circular lock dependency if this is
 * done under the @matrix_mdev->lock.
 *
 * Return 0 if no other mediated matrix device has a reference to @kvm;
 * otherwise, returns an -EPERM.
 */
static int vfio_ap_mdev_set_kvm(struct ap_matrix_mdev *matrix_mdev,
				struct kvm *kvm)
{
	struct ap_matrix_mdev *m;

	if (kvm->arch.crypto.crycbd) {
		list_for_each_entry(m, &matrix_dev->mdev_list, node) {
			if (m != matrix_mdev && m->kvm == kvm)
				return -EPERM;
		}

		kvm_get_kvm(kvm);
		matrix_mdev->kvm_busy = true;
		mutex_unlock(&matrix_dev->lock);
		kvm_arch_crypto_set_masks(kvm, matrix_mdev->shadow_apcb.apm,
					  matrix_mdev->shadow_apcb.aqm,
					  matrix_mdev->shadow_apcb.adm);
		mutex_lock(&matrix_dev->lock);
		kvm->arch.crypto.pqap_hook = &matrix_mdev->pqap_hook;
		matrix_mdev->kvm = kvm;
		matrix_mdev->kvm_busy = false;
		wake_up_all(&matrix_mdev->wait_for_kvm);
	}

	return 0;
}

/*
 * vfio_ap_mdev_iommu_notifier: IOMMU notifier callback
 *
 * @nb: The notifier block
 * @action: Action to be taken
 * @data: data associated with the request
 *
 * For an UNMAP request, unpin the guest IOVA (the NIB guest address we
 * pinned before). Other requests are ignored.
 *
 */
static int vfio_ap_mdev_iommu_notifier(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	struct ap_matrix_mdev *matrix_mdev;

	matrix_mdev = container_of(nb, struct ap_matrix_mdev, iommu_notifier);

	if (action == VFIO_IOMMU_NOTIFY_DMA_UNMAP) {
		struct vfio_iommu_type1_dma_unmap *unmap = data;
		unsigned long g_pfn = unmap->iova >> PAGE_SHIFT;

		vfio_unpin_pages(mdev_dev(matrix_mdev->mdev), &g_pfn, 1);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

/**
 * vfio_ap_mdev_unset_kvm
 *
 * @matrix_mdev: a matrix mediated device
 *
 * Performs clean-up of resources no longer needed by @matrix_mdev.
 *
 * Note: The matrix_dev->lock must be taken prior to calling
 * this function; however, the lock will be temporarily released while the
 * guest's AP configuration is cleared to avoid a potential lockdep splat.
 * The kvm->lock is taken to clear the guest's AP configuration which, under
 * certain circumstances, will result in a circular lock dependency if this is
 * done under the @matrix_mdev->lock.
 *
 */
static void vfio_ap_mdev_unset_kvm(struct ap_matrix_mdev *matrix_mdev)
{
	/*
	 * If the KVM pointer is in the process of being set, wait until the
	 * process has completed.
	 */
	wait_event_cmd(matrix_mdev->wait_for_kvm,
		       !matrix_mdev->kvm_busy,
		       mutex_unlock(&matrix_dev->lock),
		       mutex_lock(&matrix_dev->lock));

	if (matrix_mdev->kvm) {
		matrix_mdev->kvm_busy = true;
		mutex_unlock(&matrix_dev->lock);
		kvm_arch_crypto_clear_masks(matrix_mdev->kvm);
		mutex_lock(&matrix_dev->lock);
		vfio_ap_mdev_reset_queues(matrix_mdev->mdev);
		matrix_mdev->kvm->arch.crypto.pqap_hook = NULL;
		kvm_put_kvm(matrix_mdev->kvm);
		matrix_mdev->kvm = NULL;
		matrix_mdev->kvm_busy = false;
		wake_up_all(&matrix_mdev->wait_for_kvm);
	}
}

static int vfio_ap_mdev_group_notifier(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	int notify_rc = NOTIFY_OK;
	struct ap_matrix_mdev *matrix_mdev;

	if (action != VFIO_GROUP_NOTIFY_SET_KVM)
		return NOTIFY_OK;

	mutex_lock(&matrix_dev->lock);
	matrix_mdev = container_of(nb, struct ap_matrix_mdev, group_notifier);

	if (!data)
		vfio_ap_mdev_unset_kvm(matrix_mdev);
	else if (vfio_ap_mdev_set_kvm(matrix_mdev, data))
		notify_rc = NOTIFY_DONE;

	mutex_unlock(&matrix_dev->lock);

	return notify_rc;
}

static struct vfio_ap_queue *vfio_ap_find_queue(int apqn)
{
	struct ap_queue *queue;
	struct vfio_ap_queue *q = NULL;

	queue = ap_get_qdev(apqn);
	if (!queue)
		return NULL;

	if (queue->ap_dev.device.driver == &matrix_dev->vfio_ap_drv->driver)
		q = dev_get_drvdata(&queue->ap_dev.device);

	put_device(&queue->ap_dev.device);

	return q;
}

int vfio_ap_mdev_reset_queue(struct vfio_ap_queue *q,
			     unsigned int retry)
{
	struct ap_queue_status status;
	int ret;
	int retry2 = 2;

	if (!q)
		return 0;

retry_zapq:
	status = ap_zapq(q->apqn);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		ret = 0;
		break;
	case AP_RESPONSE_RESET_IN_PROGRESS:
		if (retry--) {
			msleep(20);
			goto retry_zapq;
		}
		ret = -EBUSY;
		break;
	case AP_RESPONSE_Q_NOT_AVAIL:
	case AP_RESPONSE_DECONFIGURED:
	case AP_RESPONSE_CHECKSTOPPED:
		WARN_ON_ONCE(status.irq_enabled);
		ret = -EBUSY;
		goto free_resources;
	default:
		/* things are really broken, give up */
		WARN(true, "PQAP/ZAPQ completed with invalid rc (%x)\n",
		     status.response_code);
		return -EIO;
	}

	/* wait for the reset to take effect */
	while (retry2--) {
		if (status.queue_empty && !status.irq_enabled)
			break;
		msleep(20);
		status = ap_tapq(q->apqn, NULL);
	}
	WARN_ON_ONCE(retry2 <= 0);

free_resources:
	vfio_ap_free_aqic_resources(q);

	return ret;
}

static int vfio_ap_mdev_reset_queues(struct mdev_device *mdev)
{
	int ret;
	int rc = 0;
	unsigned long apid, apqi;
	struct vfio_ap_queue *q;
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm,
			     matrix_mdev->matrix.apm_max + 1) {
		for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm,
				     matrix_mdev->matrix.aqm_max + 1) {
			q = vfio_ap_find_queue(AP_MKQID(apid, apqi));
			ret = vfio_ap_mdev_reset_queue(q, 1);
			/*
			 * Regardless whether a queue turns out to be busy, or
			 * is not operational, we need to continue resetting
			 * the remaining queues.
			 */
			if (ret)
				rc = ret;
		}
	}

	return rc;
}

static int vfio_ap_mdev_open(struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);
	unsigned long events;
	int ret;


	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	matrix_mdev->group_notifier.notifier_call = vfio_ap_mdev_group_notifier;
	events = VFIO_GROUP_NOTIFY_SET_KVM;

	ret = vfio_register_notifier(mdev_dev(mdev), VFIO_GROUP_NOTIFY,
				     &events, &matrix_mdev->group_notifier);
	if (ret) {
		module_put(THIS_MODULE);
		return ret;
	}

	matrix_mdev->iommu_notifier.notifier_call = vfio_ap_mdev_iommu_notifier;
	events = VFIO_IOMMU_NOTIFY_DMA_UNMAP;
	ret = vfio_register_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY,
				     &events, &matrix_mdev->iommu_notifier);
	if (!ret)
		return ret;

	vfio_unregister_notifier(mdev_dev(mdev), VFIO_GROUP_NOTIFY,
				 &matrix_mdev->group_notifier);
	module_put(THIS_MODULE);
	return ret;
}

static void vfio_ap_mdev_release(struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev = mdev_get_drvdata(mdev);

	mutex_lock(&matrix_dev->lock);
	vfio_ap_mdev_unset_kvm(matrix_mdev);
	mutex_unlock(&matrix_dev->lock);

	vfio_unregister_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY,
				 &matrix_mdev->iommu_notifier);
	vfio_unregister_notifier(mdev_dev(mdev), VFIO_GROUP_NOTIFY,
				 &matrix_mdev->group_notifier);
	module_put(THIS_MODULE);
}

static int vfio_ap_mdev_get_device_info(unsigned long arg)
{
	unsigned long minsz;
	struct vfio_device_info info;

	minsz = offsetofend(struct vfio_device_info, num_irqs);

	if (copy_from_user(&info, (void __user *)arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	info.flags = VFIO_DEVICE_FLAGS_AP | VFIO_DEVICE_FLAGS_RESET;
	info.num_regions = 0;
	info.num_irqs = 0;

	return copy_to_user((void __user *)arg, &info, minsz);
}

static ssize_t vfio_ap_mdev_ioctl(struct mdev_device *mdev,
				    unsigned int cmd, unsigned long arg)
{
	int ret;
	struct ap_matrix_mdev *matrix_mdev;

	mutex_lock(&matrix_dev->lock);
	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
		ret = vfio_ap_mdev_get_device_info(arg);
		break;
	case VFIO_DEVICE_RESET:
		matrix_mdev = mdev_get_drvdata(mdev);
		if (WARN(!matrix_mdev, "Driver data missing from mdev!!")) {
			ret = -EINVAL;
			break;
		}

		/*
		 * If the KVM pointer is in the process of being set, wait until
		 * the process has completed.
		 */
		wait_event_cmd(matrix_mdev->wait_for_kvm,
			       !matrix_mdev->kvm_busy,
			       mutex_unlock(&matrix_dev->lock),
			       mutex_lock(&matrix_dev->lock));

		ret = vfio_ap_mdev_reset_queues(mdev);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&matrix_dev->lock);

	return ret;
}

static const struct mdev_parent_ops vfio_ap_matrix_ops = {
	.owner			= THIS_MODULE,
	.supported_type_groups	= vfio_ap_mdev_type_groups,
	.mdev_attr_groups	= vfio_ap_mdev_attr_groups,
	.create			= vfio_ap_mdev_create,
	.remove			= vfio_ap_mdev_remove,
	.open			= vfio_ap_mdev_open,
	.release		= vfio_ap_mdev_release,
	.ioctl			= vfio_ap_mdev_ioctl,
};

int vfio_ap_mdev_register(void)
{
	atomic_set(&matrix_dev->available_instances, MAX_ZDEV_ENTRIES_EXT);

	return mdev_register_device(&matrix_dev->device, &vfio_ap_matrix_ops);
}

void vfio_ap_mdev_unregister(void)
{
	mdev_unregister_device(&matrix_dev->device);
}

/*
 * vfio_ap_queue_link_mdev
 *
 * @q: The queue to link with the matrix mdev.
 *
 * Links @q with the matrix mdev to which the queue's APQN is assigned.
 */
static void vfio_ap_queue_link_mdev(struct vfio_ap_queue *q)
{
	unsigned long apid = AP_QID_CARD(q->apqn);
	unsigned long apqi = AP_QID_QUEUE(q->apqn);
	struct ap_matrix_mdev *matrix_mdev;

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		if (test_bit_inv(apid, matrix_mdev->matrix.apm) &&
		    test_bit_inv(apqi, matrix_mdev->matrix.aqm)) {
			vfio_ap_mdev_link_queue(matrix_mdev, q);
			break;
		}
	}
}

int vfio_ap_mdev_probe_queue(struct ap_device *apdev)
{
	struct vfio_ap_queue *q;

	q = kzalloc(sizeof(*q), GFP_KERNEL);
	if (!q)
		return -ENOMEM;
	mutex_lock(&matrix_dev->lock);
	q->apqn = to_ap_queue(&apdev->device)->qid;
	q->saved_isc = VFIO_AP_ISC_INVALID;
	vfio_ap_queue_link_mdev(q);
	if (q->matrix_mdev) {
		/*
		 * If we are in the process of adding adapters and/or domains
		 * due to a change to the host's AP configuration, it is more
		 * efficient to refresh the guest's APCB in a single operation
		 * after the AP bus scan is complete (see the
		 * vfio_ap_on_scan_complete function) rather than to do the
		 * refresh for each queue added, so if that is not the case,
		 * let's go ahead and refresh the guest's APCB here.
		 */
		if (bitmap_empty(matrix_dev->ap_add, AP_DEVICES) &&
		    bitmap_empty(matrix_dev->aq_add, AP_DOMAINS)) {
			/*
			 * If the KVM pointer is in the process of being set, wait until the
			 * process has completed.
			 */
			wait_event_cmd(q->matrix_mdev->wait_for_kvm,
				       !q->matrix_mdev->kvm_busy,
				       mutex_unlock(&matrix_dev->lock),
				       mutex_lock(&matrix_dev->lock));

			vfio_ap_mdev_refresh_apcb(q->matrix_mdev);
		}
	}
	dev_set_drvdata(&apdev->device, q);
	mutex_unlock(&matrix_dev->lock);

	return 0;
}

void vfio_ap_mdev_remove_queue(struct ap_device *apdev)
{
	struct vfio_ap_queue *q;

	mutex_lock(&matrix_dev->lock);
	q = dev_get_drvdata(&apdev->device);

	if (q->matrix_mdev) {
		vfio_ap_mdev_unlink_queue_fr_mdev(q);

		/*
		 * If the KVM pointer is in the process of being set, wait until the
		 * process has completed.
		 */
		wait_event_cmd(q->matrix_mdev->wait_for_kvm,
			       !q->matrix_mdev->kvm_busy,
			       mutex_unlock(&matrix_dev->lock),
			       mutex_lock(&matrix_dev->lock));

		vfio_ap_mdev_refresh_apcb(q->matrix_mdev);
	}

	vfio_ap_mdev_reset_queue(q, 1);
	dev_set_drvdata(&apdev->device, NULL);
	kfree(q);
	mutex_unlock(&matrix_dev->lock);
}

int vfio_ap_mdev_resource_in_use(unsigned long *apm, unsigned long *aqm)
{
	int ret;

	if (!mutex_trylock(&matrix_dev->lock))
		return -EBUSY;

	ret = vfio_ap_mdev_verify_no_sharing(apm, aqm);
	mutex_unlock(&matrix_dev->lock);

	return ret;
}

/*
 * vfio_ap_mdev_unlink_apids
 *
 * @matrix_mdev: The matrix mediated device
 *
 * @apid_rem: The bitmap specifying the APIDs of the adapters removed from
 *	      the host's AP configuration
 *
 * Unlinks @matrix_mdev from each queue assigned to @matrix_mdev whose APQN
 * contains an APID specified in @apid_rem.
 *
 * Returns true if one or more AP queue devices were unlinked; otherwise,
 * returns false.
 */
static bool vfio_ap_mdev_unlink_apids(struct ap_matrix_mdev *matrix_mdev,
				      unsigned long *apid_rem)
{
	int bkt, apid;
	bool q_unlinked = false;
	struct vfio_ap_queue *q;

	hash_for_each(matrix_mdev->qtable, bkt, q, mdev_qnode) {
		apid = AP_QID_CARD(q->apqn);
		if (test_bit_inv(apid, apid_rem)) {
			vfio_ap_mdev_reset_queue(q, 1);
			vfio_ap_mdev_unlink_queue(q);
			q_unlinked = true;
		}
	}

	return q_unlinked;
}

/*
 * vfio_ap_mdev_unlink_apqis
 *
 * @matrix_mdev: The matrix mediated device
 *
 * @apqi_rem: The bitmap specifying the APQIs of the domains removed from
 *	      the host's AP configuration
 *
 * Unlinks @matrix_mdev from each queue assigned to @matrix_mdev whose APQN
 * contains an APQI specified in @apqi_rem.
 *
 * Returns true if one or more AP queue devices were unlinked; otherwise,
 * returns false.
 */
static bool vfio_ap_mdev_unlink_apqis(struct ap_matrix_mdev *matrix_mdev,
				      unsigned long *apqi_rem)
{
	int bkt, apqi;
	bool q_unlinked = false;
	struct vfio_ap_queue *q;

	hash_for_each(matrix_mdev->qtable, bkt, q, mdev_qnode) {
		apqi = AP_QID_QUEUE(q->apqn);
		if (test_bit_inv(apqi, apqi_rem)) {
			vfio_ap_mdev_reset_queue(q, 1);
			vfio_ap_mdev_unlink_queue(q);
			q_unlinked = true;
		}
	}

	return q_unlinked;
}

static void vfio_ap_mdev_on_cfg_remove(void)
{
	bool refresh_apcb = false;
	int ap_remove, aq_remove;
	struct ap_matrix_mdev *matrix_mdev;
	DECLARE_BITMAP(aprem, AP_DEVICES);
	DECLARE_BITMAP(aqrem, AP_DOMAINS);
	unsigned long *cur_apm, *cur_aqm, *prev_apm, *prev_aqm;

	cur_apm = (unsigned long *)matrix_dev->config_info.apm;
	cur_aqm = (unsigned long *)matrix_dev->config_info.aqm;
	prev_apm = (unsigned long *)matrix_dev->config_info_prev.apm;
	prev_aqm = (unsigned long *)matrix_dev->config_info_prev.aqm;

	ap_remove = bitmap_andnot(aprem, prev_apm, cur_apm, AP_DEVICES);
	aq_remove = bitmap_andnot(aqrem, prev_aqm, cur_aqm, AP_DOMAINS);

	if (!ap_remove && !aq_remove)
		return;

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		if (ap_remove)
			refresh_apcb = vfio_ap_mdev_unlink_apids(matrix_mdev,
								 aprem);

		if (aq_remove)
			refresh_apcb = vfio_ap_mdev_unlink_apqis(matrix_mdev,
								 aqrem);

		if (refresh_apcb)
			vfio_ap_mdev_refresh_apcb(matrix_mdev);
	}
}

static void vfio_ap_mdev_on_cfg_add(void)
{
	unsigned long *cur_apm, *cur_aqm, *cur_adm;
	unsigned long *prev_apm, *prev_aqm, *prev_adm;

	cur_apm = (unsigned long *)matrix_dev->config_info.apm;
	cur_aqm = (unsigned long *)matrix_dev->config_info.aqm;
	cur_adm = (unsigned long *)matrix_dev->config_info.adm;

	prev_apm = (unsigned long *)matrix_dev->config_info_prev.apm;
	prev_aqm = (unsigned long *)matrix_dev->config_info_prev.aqm;
	prev_adm = (unsigned long *)matrix_dev->config_info_prev.adm;

	bitmap_andnot(matrix_dev->ap_add, cur_apm, prev_apm, AP_DEVICES);
	bitmap_andnot(matrix_dev->aq_add, cur_aqm, prev_aqm, AP_DOMAINS);
	bitmap_andnot(matrix_dev->ad_add, cur_adm, prev_adm, AP_DOMAINS);
}

void vfio_ap_init_mdev_matrixes(struct ap_config_info *config_info)
{
	struct ap_matrix_mdev *matrix_mdev;

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		vfio_ap_matrix_init(config_info, &matrix_mdev->matrix);
		vfio_ap_matrix_init(config_info, &matrix_mdev->shadow_apcb);
	}
}

void vfio_ap_on_cfg_changed(struct ap_config_info *new_config_info,
			    struct ap_config_info *old_config_info)
{
	mutex_lock(&matrix_dev->lock);
	memcpy(&matrix_dev->config_info, new_config_info,
	       sizeof(struct ap_config_info));
	memcpy(&matrix_dev->config_info_prev, old_config_info,
	       sizeof(struct ap_config_info));
	vfio_ap_init_mdev_matrixes(new_config_info);
	vfio_ap_mdev_on_cfg_remove();
	vfio_ap_mdev_on_cfg_add();
	mutex_unlock(&matrix_dev->lock);
}

void vfio_ap_on_scan_complete(struct ap_config_info *new_config_info,
			      struct ap_config_info *old_config_info)
{
	struct ap_matrix_mdev *matrix_mdev;

	mutex_lock(&matrix_dev->lock);
	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		if (bitmap_intersects(matrix_mdev->matrix.apm,
				      matrix_dev->ap_add, AP_DEVICES) ||
		    bitmap_intersects(matrix_mdev->matrix.aqm,
				      matrix_dev->aq_add, AP_DOMAINS) ||
		    bitmap_intersects(matrix_mdev->matrix.adm,
				      matrix_dev->ad_add, AP_DOMAINS))
			vfio_ap_mdev_refresh_apcb(matrix_mdev);
	}

	bitmap_clear(matrix_dev->ap_add, 0, AP_DEVICES);
	bitmap_clear(matrix_dev->aq_add, 0, AP_DOMAINS);
	mutex_unlock(&matrix_dev->lock);
}
