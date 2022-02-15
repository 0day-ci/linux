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

static int vfio_ap_mdev_reset_queues(struct ap_queue_table *qtable);
static struct vfio_ap_queue *vfio_ap_find_queue(int apqn);
static const struct vfio_device_ops vfio_ap_matrix_dev_ops;
static int vfio_ap_mdev_reset_queue(struct vfio_ap_queue *q, unsigned int retry);

/**
 * vfio_ap_mdev_get_queue - retrieve a queue with a specific APQN from a
 *			    hash table of queues assigned to a matrix mdev
 * @matrix_mdev: the matrix mdev
 * @apqn: The APQN of a queue device
 *
 * Return: the pointer to the vfio_ap_queue struct representing the queue or
 *	   NULL if the queue is not assigned to @matrix_mdev
 */
static struct vfio_ap_queue *vfio_ap_mdev_get_queue(
					struct ap_matrix_mdev *matrix_mdev,
					int apqn)
{
	struct vfio_ap_queue *q;

	hash_for_each_possible(matrix_mdev->qtable.queues, q, mdev_qnode,
			       apqn) {
		if (q && q->apqn == apqn)
			return q;
	}

	return NULL;
}

/**
 * vfio_ap_wait_for_irqclear - clears the IR bit or gives up after 5 tries
 * @apqn: The AP Queue number
 *
 * Checks the IRQ bit for the status of this APQN using ap_tapq.
 * Returns if the ap_tapq function succeeded and the bit is clear.
 * Returns if ap_tapq function failed with invalid, deconfigured or
 * checkstopped AP.
 * Otherwise retries up to 5 times after waiting 20ms.
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
 * vfio_ap_free_aqic_resources - free vfio_ap_queue resources
 * @q: The vfio_ap_queue
 *
 * Unregisters the ISC in the GIB when the saved ISC not invalid.
 * Unpins the guest's page holding the NIB when it exists.
 * Resets the saved_pfn and saved_isc to invalid values.
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
 * vfio_ap_irq_disable - disables and clears an ap_queue interrupt
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
 *
 * Return: &struct ap_queue_status
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
 * vfio_ap_irq_enable - Enable Interruption for a APQN
 *
 * @q:	 the vfio_ap_queue holding AQIC parameters
 * @isc: the guest ISC to register with the GIB interface
 * @nib: the notification indicator byte to pin.
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
 *
 * Return: &struct ap_queue_status
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
 * handle_pqap - PQAP instruction callback
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
 * Return: 0 if we could handle the request inside KVM.
 * Otherwise, returns -EOPNOTSUPP to let QEMU handle the fault.
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
	mutex_lock(&matrix_dev->mdevs_lock);

	if (!vcpu->kvm->arch.crypto.pqap_hook)
		goto out_unlock;
	matrix_mdev = container_of(vcpu->kvm->arch.crypto.pqap_hook,
				   struct ap_matrix_mdev, pqap_hook);

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
	mutex_unlock(&matrix_dev->mdevs_lock);
	return 0;
}

static void vfio_ap_matrix_init(struct ap_config_info *info,
				struct ap_matrix *matrix)
{
	matrix->apm_max = info->apxa ? info->Na : 63;
	matrix->aqm_max = info->apxa ? info->Nd : 15;
	matrix->adm_max = info->apxa ? info->Nd : 15;
}

static void vfio_ap_mdev_hotplug_apcb(struct ap_matrix_mdev *matrix_mdev)
{
	if (matrix_mdev->kvm)
		kvm_arch_crypto_set_masks(matrix_mdev->kvm,
					  matrix_mdev->shadow_apcb.apm,
					  matrix_mdev->shadow_apcb.aqm,
					  matrix_mdev->shadow_apcb.adm);
}

static bool vfio_ap_mdev_filter_cdoms(struct ap_matrix_mdev *matrix_mdev)
{
	DECLARE_BITMAP(shadow_adm, AP_DOMAINS);

	bitmap_copy(shadow_adm, matrix_mdev->shadow_apcb.adm, AP_DOMAINS);
	bitmap_and(matrix_mdev->shadow_apcb.adm, matrix_mdev->matrix.adm,
		   (unsigned long *)matrix_dev->info.adm, AP_DOMAINS);

	return !bitmap_equal(shadow_adm, matrix_mdev->shadow_apcb.adm,
			     AP_DOMAINS);
}

/*
 * vfio_ap_mdev_filter_matrix - copy the mdev's AP configuration to the KVM
 *				guest's APCB then filter the APIDs that do not
 *				comprise at least one APQN that references a
 *				queue device bound to the vfio_ap device driver.
 *
 * @matrix_mdev: the mdev whose AP configuration is to be filtered.
 *
 * Return: a boolean value indicating whether the KVM guest's APCB was changed
 *	   by the filtering or not.
 */
static bool vfio_ap_mdev_filter_matrix(unsigned long *apm, unsigned long *aqm,
				       struct ap_matrix_mdev *matrix_mdev)
{
	int ret;
	unsigned long apid, apqi, apqn;
	DECLARE_BITMAP(shadow_apm, AP_DEVICES);
	DECLARE_BITMAP(shadow_aqm, AP_DOMAINS);
	struct vfio_ap_queue *q;

	ret = ap_qci(&matrix_dev->info);
	if (ret)
		return false;

	bitmap_copy(shadow_apm, matrix_mdev->shadow_apcb.apm, AP_DEVICES);
	bitmap_copy(shadow_aqm, matrix_mdev->shadow_apcb.aqm, AP_DOMAINS);
	vfio_ap_matrix_init(&matrix_dev->info, &matrix_mdev->shadow_apcb);

	/*
	 * Copy the adapters, domains and control domains to the shadow_apcb
	 * from the matrix mdev, but only those that are assigned to the host's
	 * AP configuration.
	 */
	bitmap_and(matrix_mdev->shadow_apcb.apm, matrix_mdev->matrix.apm,
		   (unsigned long *)matrix_dev->info.apm, AP_DEVICES);
	bitmap_and(matrix_mdev->shadow_apcb.aqm, matrix_mdev->matrix.aqm,
		   (unsigned long *)matrix_dev->info.aqm, AP_DOMAINS);

	for_each_set_bit_inv(apid, apm, AP_DEVICES) {
		for_each_set_bit_inv(apqi, aqm, AP_DOMAINS) {
			/*
			 * If the APQN is not bound to the vfio_ap device
			 * driver, then we can't assign it to the guest's
			 * AP configuration. The AP architecture won't
			 * allow filtering of a single APQN, so let's filter
			 * the APID since an adapter represents a physical
			 * hardware device.
			 */
			apqn = AP_MKQID(apid, apqi);
			q = vfio_ap_mdev_get_queue(matrix_mdev, apqn);
			if (!q || q->reset_rc) {
				clear_bit_inv(apid,
					      matrix_mdev->shadow_apcb.apm);
				break;
			}
		}
	}

	return !bitmap_equal(shadow_apm, matrix_mdev->shadow_apcb.apm,
			     AP_DEVICES) ||
	       !bitmap_equal(shadow_aqm, matrix_mdev->shadow_apcb.aqm,
			     AP_DOMAINS);
}

static int vfio_ap_mdev_probe(struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev;
	int ret;

	if ((atomic_dec_if_positive(&matrix_dev->available_instances) < 0))
		return -EPERM;

	matrix_mdev = kzalloc(sizeof(*matrix_mdev), GFP_KERNEL);
	if (!matrix_mdev) {
		ret = -ENOMEM;
		goto err_dec_available;
	}
	vfio_init_group_dev(&matrix_mdev->vdev, &mdev->dev,
			    &vfio_ap_matrix_dev_ops);

	matrix_mdev->mdev = mdev;
	vfio_ap_matrix_init(&matrix_dev->info, &matrix_mdev->matrix);
	matrix_mdev->pqap_hook = handle_pqap;
	vfio_ap_matrix_init(&matrix_dev->info, &matrix_mdev->shadow_apcb);
	hash_init(matrix_mdev->qtable.queues);
	mdev_set_drvdata(mdev, matrix_mdev);
	mutex_lock(&matrix_dev->guests_lock);
	list_add(&matrix_mdev->node, &matrix_dev->mdev_list);
	mutex_unlock(&matrix_dev->guests_lock);

	ret = vfio_register_emulated_iommu_dev(&matrix_mdev->vdev);
	if (ret)
		goto err_list;
	dev_set_drvdata(&mdev->dev, matrix_mdev);
	return 0;

err_list:
	mutex_lock(&matrix_dev->guests_lock);
	list_del(&matrix_mdev->node);
	mutex_unlock(&matrix_dev->guests_lock);
	vfio_uninit_group_dev(&matrix_mdev->vdev);
	kfree(matrix_mdev);
err_dec_available:
	atomic_inc(&matrix_dev->available_instances);
	return ret;
}

static void vfio_ap_mdev_link_queue(struct ap_matrix_mdev *matrix_mdev,
				    struct vfio_ap_queue *q)
{
	if (q) {
		q->matrix_mdev = matrix_mdev;
		hash_add(matrix_mdev->qtable.queues, &q->mdev_qnode, q->apqn);
	}
}

static void vfio_ap_mdev_link_apqn(struct ap_matrix_mdev *matrix_mdev, int apqn)
{
	struct vfio_ap_queue *q;

	q = vfio_ap_find_queue(apqn);
	vfio_ap_mdev_link_queue(matrix_mdev, q);
}

static void vfio_ap_unlink_queue_fr_mdev(struct vfio_ap_queue *q)
{
	hash_del(&q->mdev_qnode);
}

static void vfio_ap_unlink_mdev_fr_queue(struct vfio_ap_queue *q)
{
	q->matrix_mdev = NULL;
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

static void vfio_ap_mdev_remove(struct mdev_device *mdev)
{
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(&mdev->dev);

	vfio_unregister_group_dev(&matrix_mdev->vdev);

	mutex_lock(&matrix_dev->guests_lock);
	mutex_lock(&matrix_dev->mdevs_lock);
	vfio_ap_mdev_reset_queues(&matrix_mdev->qtable);
	vfio_ap_mdev_unlink_fr_queues(matrix_mdev);
	list_del(&matrix_mdev->node);
	mutex_unlock(&matrix_dev->mdevs_lock);
	mutex_unlock(&matrix_dev->guests_lock);
	vfio_uninit_group_dev(&matrix_mdev->vdev);
	kfree(matrix_mdev);
	atomic_inc(&matrix_dev->available_instances);
}

static ssize_t name_show(struct mdev_type *mtype,
			 struct mdev_type_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", VFIO_AP_MDEV_NAME_HWVIRT);
}

static MDEV_TYPE_ATTR_RO(name);

static ssize_t available_instances_show(struct mdev_type *mtype,
					struct mdev_type_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n",
		       atomic_read(&matrix_dev->available_instances));
}

static MDEV_TYPE_ATTR_RO(available_instances);

static ssize_t device_api_show(struct mdev_type *mtype,
			       struct mdev_type_attribute *attr, char *buf)
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
			dev_warn(dev, MDEV_SHARING_ERR, apid, apqi, mdev_name);
}

/**
 * vfio_ap_mdev_verify_no_sharing - verify APQNs are not shared by matrix mdevs
 *
 * @mdev_apm: mask indicating the APIDs of the APQNs to be verified
 * @mdev_aqm: mask indicating the APQIs of the APQNs to be verified
 *
 * Verifies that each APQN derived from the Cartesian product of a bitmap of
 * AP adapter IDs and AP queue indexes is not configured for any matrix
 * mediated device. AP queue sharing is not allowed.
 *
 * Return: 0 if the APQNs are not shared; otherwise return -EADDRINUSE.
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

/**
 * vfio_ap_mdev_validate_masks - verify that the APQNs assigned to the mdev are
 *				 not reserved for the default zcrypt driver and
 *				 are not assigned to another mdev.
 *
 * @matrix_mdev: the mdev to which the APQNs being validated are assigned.
 *
 * Return: One of the following values:
 * o the error returned from the ap_apqn_in_matrix_owned_by_def_drv() function,
 *   most likely -EBUSY indicating the ap_perms_mutex lock is already held.
 * o EADDRNOTAVAIL if an APQN assigned to @matrix_mdev is reserved for the
 *		   zcrypt default driver.
 * o EADDRINUSE if an APQN assigned to @matrix_mdev is assigned to another mdev
 * o A zero indicating validation succeeded.
 */
static int vfio_ap_mdev_validate_masks(struct ap_matrix_mdev *matrix_mdev)
{
	int ret;

	ret = ap_apqn_in_matrix_owned_by_def_drv(matrix_mdev->matrix.apm,
						 matrix_mdev->matrix.aqm);

	if (ret < 0)
		return ret;

	if (ret == 1)
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
 * vfio_ap_mdev_get_locks - acquire the locks required to assign/unassign AP
 *			    adapters, domains and control domains for an mdev in
 *			    the proper locking order.
 *
 * @matrix_mdev: the matrix mediated device object
 */
static void vfio_ap_mdev_get_locks(struct ap_matrix_mdev *matrix_mdev)
{
	/* Lock the mutex required to access the KVM guest's state */
	mutex_lock(&matrix_dev->guests_lock);

	/* If a KVM guest is running, lock the mutex required to plug/unplug the
	 * AP devices passed through to the guest
	 */
	if (matrix_mdev->kvm)
		mutex_lock(&matrix_mdev->kvm->lock);

	/* The lock required to access the mdev's state */
	mutex_lock(&matrix_dev->mdevs_lock);
}

/**
 * vfio_ap_mdev_put_locks - release the locks used to assign/unassign AP
 *			    adapters, domains and control domains in the proper
 *			    unlocking order.
 *
 * @matrix_mdev: the matrix mediated device object
 */
static void vfio_ap_mdev_put_locks(struct ap_matrix_mdev *matrix_mdev)
{
	/* Unlock the mutex taken to access the matrix_mdev's state */
	mutex_unlock(&matrix_dev->mdevs_lock);

	/*
	 * If a KVM guest is running, unlock the mutex taken to plug/unplug the
	 * AP devices passed through to the guest.
	 */
	if (matrix_mdev->kvm)
		mutex_unlock(&matrix_mdev->kvm->lock);

	/* Unlock the mutex taken to allow access to the KVM guest's state */
	mutex_unlock(&matrix_dev->guests_lock);
}

/**
 * assign_adapter_store - parses the APID from @buf and sets the
 * corresponding bit in the mediated matrix device's APM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_adapter attribute
 * @buf:	a buffer containing the AP adapter number (APID) to
 *		be assigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the APID is valid; otherwise,
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
 *	   matrix device
 *
 *	5. -EAGAIN
 *	   A lock required to validate the mdev's AP configuration could not
 *	   be obtained.
 */
static ssize_t assign_adapter_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int ret;
	unsigned long apid;
	DECLARE_BITMAP(apm, AP_DEVICES);
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	vfio_ap_mdev_get_locks(matrix_mdev);

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

	memset(apm, 0, sizeof(apm));
	set_bit_inv(apid, apm);
	vfio_ap_mdev_link_adapter(matrix_mdev, apid);

	if (vfio_ap_mdev_filter_matrix(apm,
				       matrix_mdev->matrix.aqm, matrix_mdev))
		vfio_ap_mdev_hotplug_apcb(matrix_mdev);

	ret = count;
done:
	vfio_ap_mdev_put_locks(matrix_mdev);

	return ret;
}
static DEVICE_ATTR_WO(assign_adapter);

static void vfio_ap_unlink_apqn_fr_mdev(struct ap_matrix_mdev *matrix_mdev,
					unsigned long apid, unsigned long apqi,
					struct ap_queue_table *qtable)
{
	struct vfio_ap_queue *q;

	q = vfio_ap_mdev_get_queue(matrix_mdev, AP_MKQID(apid, apqi));
	/* If the queue is assigned to the matrix mdev, unlink it. */
	if (q)
		vfio_ap_unlink_queue_fr_mdev(q);

	/* If the queue is assigned to the APCB, store it in @qtable. */
	if (test_bit_inv(apid, matrix_mdev->shadow_apcb.apm) &&
	    test_bit_inv(apqi, matrix_mdev->shadow_apcb.aqm))
		hash_add(qtable->queues, &q->mdev_qnode, q->apqn);
}

/**
 * vfio_ap_mdev_unlink_adapter - unlink all queues associated with unassigned
 *				 adapter from the matrix mdev to which the
 *				 adapter was assigned.
 * @matrix_mdev: the matrix mediated device to which the adapter was assigned.
 * @apid: the APID of the unassigned adapter.
 * @qtable: table for storing queues associated with unassigned adapter.
 */
static void vfio_ap_mdev_unlink_adapter(struct ap_matrix_mdev *matrix_mdev,
					unsigned long apid,
					struct ap_queue_table *qtable)
{
	unsigned long apqi;

	for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm, AP_DOMAINS)
		vfio_ap_unlink_apqn_fr_mdev(matrix_mdev, apid, apqi, qtable);
}

static void vfio_ap_mdev_hot_unplug_adapter(struct ap_matrix_mdev *matrix_mdev,
					    unsigned long apid)
{
	int bkt;
	struct vfio_ap_queue *q;
	struct ap_queue_table qtable;

	hash_init(qtable.queues);
	vfio_ap_mdev_unlink_adapter(matrix_mdev, apid, &qtable);

	if (test_bit_inv(apid, matrix_mdev->shadow_apcb.apm)) {
		clear_bit_inv(apid, matrix_mdev->shadow_apcb.apm);
		vfio_ap_mdev_hotplug_apcb(matrix_mdev);
	}

	vfio_ap_mdev_reset_queues(&qtable);

	hash_for_each(qtable.queues, bkt, q, mdev_qnode) {
		vfio_ap_unlink_mdev_fr_queue(q);
		hash_del(&q->mdev_qnode);
	}
}

/**
 * unassign_adapter_store - parses the APID from @buf and clears the
 * corresponding bit in the mediated matrix device's APM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_adapter attribute
 * @buf:	a buffer containing the adapter number (APID) to be unassigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the APID is valid; otherwise,
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
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	vfio_ap_mdev_get_locks(matrix_mdev);

	ret = kstrtoul(buf, 0, &apid);
	if (ret)
		goto done;

	if (apid > matrix_mdev->matrix.apm_max) {
		ret = -ENODEV;
		goto done;
	}

	clear_bit_inv((unsigned long)apid, matrix_mdev->matrix.apm);
	vfio_ap_mdev_hot_unplug_adapter(matrix_mdev, apid);
	ret = count;
done:
	vfio_ap_mdev_put_locks(matrix_mdev);

	return ret;
}
static DEVICE_ATTR_WO(unassign_adapter);

static void vfio_ap_mdev_link_domain(struct ap_matrix_mdev *matrix_mdev,
				      unsigned long apqi)
{
	unsigned long apid;

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, AP_DEVICES)
		vfio_ap_mdev_link_apqn(matrix_mdev,
				       AP_MKQID(apid, apqi));
}

/**
 * assign_domain_store - parses the APQI from @buf and sets the
 * corresponding bit in the mediated matrix device's AQM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_domain attribute
 * @buf:	a buffer containing the AP queue index (APQI) of the domain to
 *		be assigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the APQI is valid; otherwise returns
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
 *	   matrix device
 *
 *	5. -EAGAIN
 *	   The lock required to validate the mdev's AP configuration could not
 *	   be obtained.
 */
static ssize_t assign_domain_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret;
	unsigned long apqi;
	DECLARE_BITMAP(aqm, AP_DOMAINS);
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	vfio_ap_mdev_get_locks(matrix_mdev);

	ret = kstrtoul(buf, 0, &apqi);
	if (ret)
		goto done;

	if (apqi > matrix_mdev->matrix.apm_max) {
		ret = -ENODEV;
		goto done;
	}

	set_bit_inv(apqi, matrix_mdev->matrix.aqm);

	ret = vfio_ap_mdev_validate_masks(matrix_mdev);
	if (ret) {
		clear_bit_inv(apqi, matrix_mdev->matrix.aqm);
		goto done;
	}

	memset(aqm, 0, sizeof(aqm));
	set_bit_inv(apqi, aqm);
	vfio_ap_mdev_link_domain(matrix_mdev, apqi);

	if (vfio_ap_mdev_filter_matrix(matrix_mdev->matrix.apm, aqm,
				       matrix_mdev))
		vfio_ap_mdev_hotplug_apcb(matrix_mdev);

	ret = count;
done:
	vfio_ap_mdev_put_locks(matrix_mdev);

	return ret;
}
static DEVICE_ATTR_WO(assign_domain);

static void vfio_ap_mdev_unlink_domain(struct ap_matrix_mdev *matrix_mdev,
				       unsigned long apqi,
				       struct ap_queue_table *qtable)
{
	unsigned long apid;

	for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, AP_DEVICES)
		vfio_ap_unlink_apqn_fr_mdev(matrix_mdev, apid, apqi, qtable);
}

static void vfio_ap_mdev_hot_unplug_domain(struct ap_matrix_mdev *matrix_mdev,
					   unsigned long apqi)
{
	int bkt;
	struct vfio_ap_queue *q;
	struct ap_queue_table qtable;

	hash_init(qtable.queues);
	vfio_ap_mdev_unlink_domain(matrix_mdev, apqi, &qtable);

	if (test_bit_inv(apqi, matrix_mdev->shadow_apcb.aqm)) {
		clear_bit_inv(apqi, matrix_mdev->shadow_apcb.aqm);
		vfio_ap_mdev_hotplug_apcb(matrix_mdev);
	}

	vfio_ap_mdev_reset_queues(&qtable);

	hash_for_each(qtable.queues, bkt, q, mdev_qnode) {
		vfio_ap_unlink_mdev_fr_queue(q);
		hash_del(&q->mdev_qnode);
	}
}

/**
 * unassign_domain_store - parses the APQI from @buf and clears the
 * corresponding bit in the mediated matrix device's AQM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_domain attribute
 * @buf:	a buffer containing the AP queue index (APQI) of the domain to
 *		be unassigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the APQI is valid; otherwise,
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
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	vfio_ap_mdev_get_locks(matrix_mdev);

	ret = kstrtoul(buf, 0, &apqi);
	if (ret)
		goto done;

	if (apqi > matrix_mdev->matrix.apm_max) {
		ret = -ENODEV;
		goto done;
	}

	clear_bit_inv((unsigned long)apqi, matrix_mdev->matrix.aqm);
	vfio_ap_mdev_hot_unplug_domain(matrix_mdev, apqi);
	ret = count;
done:
	vfio_ap_mdev_put_locks(matrix_mdev);

	return ret;
}
static DEVICE_ATTR_WO(unassign_domain);

/**
 * assign_control_domain_store - parses the domain ID from @buf and sets
 * the corresponding bit in the mediated matrix device's ADM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's assign_control_domain attribute
 * @buf:	a buffer containing the domain ID to be assigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the domain ID is valid; otherwise,
 * returns one of the following errors:
 *	-EINVAL if the ID is not a number
 *	-ENODEV if the ID exceeds the maximum value configured for the system
 */
static ssize_t assign_control_domain_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int ret;
	unsigned long id;
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	vfio_ap_mdev_get_locks(matrix_mdev);

	ret = kstrtoul(buf, 0, &id);
	if (ret)
		goto done;

	if (id > matrix_mdev->matrix.apm_max) {
		ret = -ENODEV;
		goto done;
	}

	/* Set the bit in the ADM (bitmask) corresponding to the AP control
	 * domain number (id). The bits in the mask, from most significant to
	 * least significant, correspond to IDs 0 up to the one less than the
	 * number of control domains that can be assigned.
	 */
	set_bit_inv(id, matrix_mdev->matrix.adm);
	if (vfio_ap_mdev_filter_cdoms(matrix_mdev))
		vfio_ap_mdev_hotplug_apcb(matrix_mdev);

	ret = count;
done:
	vfio_ap_mdev_put_locks(matrix_mdev);

	return ret;
}
static DEVICE_ATTR_WO(assign_control_domain);

/**
 * unassign_control_domain_store - parses the domain ID from @buf and
 * clears the corresponding bit in the mediated matrix device's ADM
 *
 * @dev:	the matrix device
 * @attr:	the mediated matrix device's unassign_control_domain attribute
 * @buf:	a buffer containing the domain ID to be unassigned
 * @count:	the number of bytes in @buf
 *
 * Return: the number of bytes processed if the domain ID is valid; otherwise,
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
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);

	vfio_ap_mdev_get_locks(matrix_mdev);

	ret = kstrtoul(buf, 0, &domid);
	if (ret)
		goto done;

	if (domid > matrix_mdev->matrix.apm_max) {
		ret = -ENODEV;
		goto done;
	}

	clear_bit_inv(domid, matrix_mdev->matrix.adm);

	if (test_bit_inv(domid, matrix_mdev->shadow_apcb.adm)) {
		clear_bit_inv(domid, matrix_mdev->shadow_apcb.adm);
		vfio_ap_mdev_hotplug_apcb(matrix_mdev);
	}

	ret = count;
done:
	vfio_ap_mdev_put_locks(matrix_mdev);

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
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);
	unsigned long max_domid = matrix_mdev->matrix.adm_max;

	mutex_lock(&matrix_dev->mdevs_lock);
	for_each_set_bit_inv(id, matrix_mdev->matrix.adm, max_domid + 1) {
		n = sprintf(bufpos, "%04lx\n", id);
		bufpos += n;
		nchars += n;
	}
	mutex_unlock(&matrix_dev->mdevs_lock);

	return nchars;
}
static DEVICE_ATTR_RO(control_domains);

static ssize_t matrix_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct ap_matrix_mdev *matrix_mdev = dev_get_drvdata(dev);
	char *bufpos = buf;
	unsigned long apid;
	unsigned long apqi;
	unsigned long apid1;
	unsigned long apqi1;
	unsigned long napm_bits = matrix_mdev->matrix.apm_max + 1;
	unsigned long naqm_bits = matrix_mdev->matrix.aqm_max + 1;
	int nchars = 0;
	int n;

	apid1 = find_first_bit_inv(matrix_mdev->matrix.apm, napm_bits);
	apqi1 = find_first_bit_inv(matrix_mdev->matrix.aqm, naqm_bits);

	mutex_lock(&matrix_dev->mdevs_lock);

	if ((apid1 < napm_bits) && (apqi1 < naqm_bits)) {
		for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, napm_bits) {
			for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm,
					     naqm_bits) {
				n = sprintf(bufpos, "%02lx.%04lx\n", apid,
					    apqi);
				bufpos += n;
				nchars += n;
			}
		}
	} else if (apid1 < napm_bits) {
		for_each_set_bit_inv(apid, matrix_mdev->matrix.apm, napm_bits) {
			n = sprintf(bufpos, "%02lx.\n", apid);
			bufpos += n;
			nchars += n;
		}
	} else if (apqi1 < naqm_bits) {
		for_each_set_bit_inv(apqi, matrix_mdev->matrix.aqm, naqm_bits) {
			n = sprintf(bufpos, ".%04lx\n", apqi);
			bufpos += n;
			nchars += n;
		}
	}

	mutex_unlock(&matrix_dev->mdevs_lock);

	return nchars;
}
static DEVICE_ATTR_RO(matrix);

static struct attribute *vfio_ap_mdev_attrs[] = {
	&dev_attr_assign_adapter.attr,
	&dev_attr_unassign_adapter.attr,
	&dev_attr_assign_domain.attr,
	&dev_attr_unassign_domain.attr,
	&dev_attr_assign_control_domain.attr,
	&dev_attr_unassign_control_domain.attr,
	&dev_attr_control_domains.attr,
	&dev_attr_matrix.attr,
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
 * vfio_ap_mdev_set_kvm - sets all data for @matrix_mdev that are needed
 * to manage AP resources for the guest whose state is represented by @kvm
 *
 * @matrix_mdev: a mediated matrix device
 * @kvm: reference to KVM instance
 *
 * Note: The matrix_dev->lock must be taken prior to calling
 * this function; however, the lock will be temporarily released while the
 * guest's AP configuration is set to avoid a potential lockdep splat.
 * The kvm->lock is taken to set the guest's AP configuration which, under
 * certain circumstances, will result in a circular lock dependency if this is
 * done under the @matrix_mdev->lock.
 *
 * Return: 0 if no other mediated matrix device has a reference to @kvm;
 * otherwise, returns an -EPERM.
 */
static int vfio_ap_mdev_set_kvm(struct ap_matrix_mdev *matrix_mdev,
				struct kvm *kvm)
{
	struct ap_matrix_mdev *m;

	if (kvm->arch.crypto.crycbd) {
		down_write(&kvm->arch.crypto.pqap_hook_rwsem);
		kvm->arch.crypto.pqap_hook = &matrix_mdev->pqap_hook;
		up_write(&kvm->arch.crypto.pqap_hook_rwsem);

		mutex_lock(&matrix_dev->guests_lock);
		mutex_lock(&kvm->lock);
		mutex_lock(&matrix_dev->mdevs_lock);

		list_for_each_entry(m, &matrix_dev->mdev_list, node) {
			if (m != matrix_mdev && m->kvm == kvm) {
				mutex_unlock(&matrix_dev->mdevs_lock);
				mutex_unlock(&kvm->lock);
				mutex_unlock(&matrix_dev->guests_lock);
				return -EPERM;
			}
		}

		kvm_get_kvm(kvm);
		matrix_mdev->kvm = kvm;
		kvm_arch_crypto_set_masks(kvm, matrix_mdev->shadow_apcb.apm,
					  matrix_mdev->shadow_apcb.aqm,
					  matrix_mdev->shadow_apcb.adm);

		mutex_unlock(&matrix_dev->mdevs_lock);
		mutex_unlock(&kvm->lock);
		mutex_unlock(&matrix_dev->guests_lock);
	}

	return 0;
}

/**
 * vfio_ap_mdev_iommu_notifier - IOMMU notifier callback
 *
 * @nb: The notifier block
 * @action: Action to be taken
 * @data: data associated with the request
 *
 * For an UNMAP request, unpin the guest IOVA (the NIB guest address we
 * pinned before). Other requests are ignored.
 *
 * Return: for an UNMAP request, NOFITY_OK; otherwise NOTIFY_DONE.
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
 * vfio_ap_mdev_unset_kvm - performs clean-up of resources no longer needed
 * by @matrix_mdev.
 *
 * @matrix_mdev: a matrix mediated device
 * @kvm: the pointer to the kvm structure being unset.
 *
 * Note: The matrix_dev->lock must be taken prior to calling
 * this function; however, the lock will be temporarily released while the
 * guest's AP configuration is cleared to avoid a potential lockdep splat.
 * The kvm->lock is taken to clear the guest's AP configuration which, under
 * certain circumstances, will result in a circular lock dependency if this is
 * done under the @matrix_mdev->lock.
 */
static void vfio_ap_mdev_unset_kvm(struct ap_matrix_mdev *matrix_mdev,
				   struct kvm *kvm)
{
	if (kvm && kvm->arch.crypto.crycbd) {
		down_write(&kvm->arch.crypto.pqap_hook_rwsem);
		kvm->arch.crypto.pqap_hook = NULL;
		up_write(&kvm->arch.crypto.pqap_hook_rwsem);

		mutex_lock(&matrix_dev->guests_lock);
		mutex_lock(&kvm->lock);
		mutex_lock(&matrix_dev->mdevs_lock);

		kvm_arch_crypto_clear_masks(matrix_mdev->kvm);
		vfio_ap_mdev_reset_queues(&matrix_mdev->qtable);
		kvm_put_kvm(matrix_mdev->kvm);
		matrix_mdev->kvm = NULL;

		mutex_unlock(&matrix_dev->mdevs_lock);
		mutex_unlock(&kvm->lock);
		mutex_unlock(&matrix_dev->guests_lock);
	}
}

static int vfio_ap_mdev_group_notifier(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	int notify_rc = NOTIFY_OK;
	struct ap_matrix_mdev *matrix_mdev;

	if (action != VFIO_GROUP_NOTIFY_SET_KVM)
		return NOTIFY_OK;

	matrix_mdev = container_of(nb, struct ap_matrix_mdev, group_notifier);

	if (!data)
		vfio_ap_mdev_unset_kvm(matrix_mdev, matrix_mdev->kvm);
	else if (vfio_ap_mdev_set_kvm(matrix_mdev, data))
		notify_rc = NOTIFY_DONE;

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

static int vfio_ap_mdev_reset_queue(struct vfio_ap_queue *q, unsigned int retry)
{
	struct ap_queue_status status;
	int ret;
	int retry2 = 2;

	if (!q)
		return 0;
	q->reset_rc = 0;

retry_zapq:
	status = ap_zapq(q->apqn);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		ret = 0;
		q->reset_rc = status.response_code;
		break;
	case AP_RESPONSE_RESET_IN_PROGRESS:
		q->reset_rc = status.response_code;
		if (retry--) {
			msleep(20);
			goto retry_zapq;
		}
		ret = -EBUSY;
		break;
	case AP_RESPONSE_Q_NOT_AVAIL:
	case AP_RESPONSE_DECONFIGURED:
	case AP_RESPONSE_CHECKSTOPPED:
		WARN_ONCE(status.irq_enabled,
			  "PQAP/ZAPQ for %02x.%04x failed with rc=%u while IRQ enabled",
			  AP_QID_CARD(q->apqn), AP_QID_QUEUE(q->apqn),
			  status.response_code);
		q->reset_rc = status.response_code;
		ret = -EBUSY;
		goto free_resources;
	default:
		/* things are really broken, give up */
		WARN(true,
		     "PQAP/ZAPQ for %02x.%04x failed with invalid rc=%u\n",
		     AP_QID_CARD(q->apqn), AP_QID_QUEUE(q->apqn),
		     status.response_code);
		q->reset_rc = status.response_code;
		return -EIO;
	}

	/* wait for the reset to take effect */
	while (retry2--) {
		if (status.queue_empty && !status.irq_enabled)
			break;
		msleep(20);
		status = ap_tapq(q->apqn, NULL);
	}
	WARN_ONCE(retry2 <= 0, "unable to verify reset of queue %02x.%04x",
		  AP_QID_CARD(q->apqn), AP_QID_QUEUE(q->apqn));

free_resources:
	vfio_ap_free_aqic_resources(q);

	return ret;
}

static int vfio_ap_mdev_reset_queues(struct ap_queue_table *qtable)
{
	int rc = 0, ret, bkt;
	struct vfio_ap_queue *q;

	hash_for_each(qtable->queues, bkt, q, mdev_qnode) {
		ret = vfio_ap_mdev_reset_queue(q, 1);
		/*
		 * Regardless whether a queue turns out to be busy, or
		 * is not operational, we need to continue resetting
		 * the remaining queues.
		 */
		if (ret)
			rc = ret;
	}

	return rc;
}

static int vfio_ap_mdev_open_device(struct vfio_device *vdev)
{
	struct ap_matrix_mdev *matrix_mdev =
		container_of(vdev, struct ap_matrix_mdev, vdev);
	unsigned long events;
	int ret;

	matrix_mdev->group_notifier.notifier_call = vfio_ap_mdev_group_notifier;
	events = VFIO_GROUP_NOTIFY_SET_KVM;

	ret = vfio_register_notifier(vdev->dev, VFIO_GROUP_NOTIFY,
				     &events, &matrix_mdev->group_notifier);
	if (ret)
		return ret;

	matrix_mdev->iommu_notifier.notifier_call = vfio_ap_mdev_iommu_notifier;
	events = VFIO_IOMMU_NOTIFY_DMA_UNMAP;
	ret = vfio_register_notifier(vdev->dev, VFIO_IOMMU_NOTIFY,
				     &events, &matrix_mdev->iommu_notifier);
	if (ret)
		goto out_unregister_group;
	return 0;

out_unregister_group:
	vfio_unregister_notifier(vdev->dev, VFIO_GROUP_NOTIFY,
				 &matrix_mdev->group_notifier);
	return ret;
}

static void vfio_ap_mdev_close_device(struct vfio_device *vdev)
{
	struct ap_matrix_mdev *matrix_mdev =
		container_of(vdev, struct ap_matrix_mdev, vdev);

	vfio_unregister_notifier(vdev->dev, VFIO_IOMMU_NOTIFY,
				 &matrix_mdev->iommu_notifier);
	vfio_unregister_notifier(vdev->dev, VFIO_GROUP_NOTIFY,
				 &matrix_mdev->group_notifier);
	vfio_ap_mdev_unset_kvm(matrix_mdev, matrix_mdev->kvm);
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

	return copy_to_user((void __user *)arg, &info, minsz) ? -EFAULT : 0;
}

static ssize_t vfio_ap_mdev_ioctl(struct vfio_device *vdev,
				    unsigned int cmd, unsigned long arg)
{
	struct ap_matrix_mdev *matrix_mdev =
		container_of(vdev, struct ap_matrix_mdev, vdev);
	int ret;

	mutex_lock(&matrix_dev->mdevs_lock);
	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
		ret = vfio_ap_mdev_get_device_info(arg);
		break;
	case VFIO_DEVICE_RESET:
		ret = vfio_ap_mdev_reset_queues(&matrix_mdev->qtable);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&matrix_dev->mdevs_lock);

	return ret;
}

static const struct vfio_device_ops vfio_ap_matrix_dev_ops = {
	.open_device = vfio_ap_mdev_open_device,
	.close_device = vfio_ap_mdev_close_device,
	.ioctl = vfio_ap_mdev_ioctl,
};

static struct mdev_driver vfio_ap_matrix_driver = {
	.driver = {
		.name = "vfio_ap_mdev",
		.owner = THIS_MODULE,
		.mod_name = KBUILD_MODNAME,
		.dev_groups = vfio_ap_mdev_attr_groups,
	},
	.probe = vfio_ap_mdev_probe,
	.remove = vfio_ap_mdev_remove,
};

static const struct mdev_parent_ops vfio_ap_matrix_ops = {
	.owner			= THIS_MODULE,
	.device_driver		= &vfio_ap_matrix_driver,
	.supported_type_groups	= vfio_ap_mdev_type_groups,
};

int vfio_ap_mdev_register(void)
{
	int ret;

	atomic_set(&matrix_dev->available_instances, MAX_ZDEV_ENTRIES_EXT);

	ret = mdev_register_driver(&vfio_ap_matrix_driver);
	if (ret)
		return ret;

	ret = mdev_register_device(&matrix_dev->device, &vfio_ap_matrix_ops);
	if (ret)
		goto err_driver;
	return 0;

err_driver:
	mdev_unregister_driver(&vfio_ap_matrix_driver);
	return ret;
}

void vfio_ap_mdev_unregister(void)
{
	mdev_unregister_device(&matrix_dev->device);
	mdev_unregister_driver(&vfio_ap_matrix_driver);
}



/**
 * vfio_ap_mdev_get_qlocks_4_probe: acquire all of the locks required to probe
 *				    a queue device.
 *
 * @apqn: the APQN of the queue device being probed
 *
 * Return: the matrix mdev to which @apqn is assigned.
 */
static struct ap_matrix_mdev *vfio_ap_mdev_get_qlocks_4_probe(int apqn)
{
	struct ap_matrix_mdev *matrix_mdev;
	unsigned long apid = AP_QID_CARD(apqn);
	unsigned long apqi = AP_QID_QUEUE(apqn);

	/*
	 * Lock the mutex required to access the list of mdevs under the control
	 * of the vfio_ap device driver and access the KVM guest's state
	 */
	mutex_lock(&matrix_dev->guests_lock);

	list_for_each_entry(matrix_mdev, &matrix_dev->mdev_list, node) {
		if (test_bit_inv(apid, matrix_mdev->matrix.apm) &&
		    test_bit_inv(apqi, matrix_mdev->matrix.aqm)) {
			/*
			 * If the KVM guest is running, lock the mutex required
			 * to plug/unplug AP devices passed through to the
			 * guest.
			 */
			if (matrix_mdev->kvm)
				mutex_lock(&matrix_mdev->kvm->lock);

			/*
			 * Lock the mutex required to access the mdev's state.
			 */
			mutex_lock(&matrix_dev->mdevs_lock);

			return matrix_mdev;
		}
	}

	return NULL;
}

/**
 * vfio_ap_mdev_put_qlocks - unlock all of the locks acquired for probing or
 *			     removing a queue device.
 *
 * @matrix_mdev: the mdev to which the queue being probed/removed is assigned.
 */
static void vfio_ap_mdev_put_qlocks(struct ap_matrix_mdev *matrix_mdev)
{
	if (matrix_mdev) {
		/*
		 * Unlock the queue required for accessing the state of
		 * matrix_mdev
		 */
		mutex_unlock(&matrix_dev->mdevs_lock);

		/*
		 * If a KVM guest is currently running, unlock the mutex
		 * required to plug/unplug AP devices passed through to the
		 * guest.
		 */
		if (matrix_mdev && matrix_mdev->kvm)
			mutex_unlock(&matrix_mdev->kvm->lock);
	}

	/* Unlock the mutex required to access the KVM guest's state */
	mutex_unlock(&matrix_dev->guests_lock);
}

int vfio_ap_mdev_probe_queue(struct ap_device *apdev)
{
	struct vfio_ap_queue *q;
	DECLARE_BITMAP(apm, AP_DEVICES);
	struct ap_matrix_mdev *matrix_mdev;

	q = kzalloc(sizeof(*q), GFP_KERNEL);
	if (!q)
		return -ENOMEM;

	q->apqn = to_ap_queue(&apdev->device)->qid;
	q->saved_isc = VFIO_AP_ISC_INVALID;
	matrix_mdev = vfio_ap_mdev_get_qlocks_4_probe(q->apqn);
	if (matrix_mdev) {
		vfio_ap_mdev_link_queue(matrix_mdev, q);
		memset(apm, 0, sizeof(apm));
		set_bit_inv(AP_QID_CARD(q->apqn), apm);
		if (vfio_ap_mdev_filter_matrix(apm, q->matrix_mdev->matrix.aqm,
					       q->matrix_mdev))
			vfio_ap_mdev_hotplug_apcb(q->matrix_mdev);
	}
	dev_set_drvdata(&apdev->device, q);
	vfio_ap_mdev_put_qlocks(matrix_mdev);

	return 0;
}

/**
 * vfio_ap_get_qlocks_4_rem: acquire all of the locks required to remove a
 *			     queue device.
 *
 * @matrix_mdev: the device to which the APQN of the queue device being removed is
 *		 assigned.
 */
static struct vfio_ap_queue *vfio_ap_get_qlocks_4_rem(struct ap_device *apdev)
{
	struct vfio_ap_queue *q;

	/* Lock the mutex required to access the KVM guest's state */
	mutex_lock(&matrix_dev->guests_lock);

	q = dev_get_drvdata(&apdev->device);

	/*
	 * If the queue is assigned to a mediated device and a KVM guest is
	 * currently running, lock the mutex required to plug/unplug AP devices
	 * passed through to the guest.
	 */
	if (q->matrix_mdev) {
		if (q->matrix_mdev->kvm)
			mutex_lock(&q->matrix_mdev->kvm->lock);
		/*
		 * Lock the mutex required to access the state of the
		 * matrix_mdev
		 */
		mutex_lock(&matrix_dev->mdevs_lock);
	}

	return q;
}

void vfio_ap_mdev_remove_queue(struct ap_device *apdev)
{
	unsigned long apid, apqi;
	struct vfio_ap_queue *q;
	struct ap_matrix_mdev *matrix_mdev;

	q = vfio_ap_get_qlocks_4_rem(apdev);
	matrix_mdev = q->matrix_mdev;

	if (matrix_mdev) {
		vfio_ap_unlink_queue_fr_mdev(q);

		apid = AP_QID_CARD(q->apqn);
		apqi = AP_QID_QUEUE(q->apqn);
		if (test_bit_inv(apid, matrix_mdev->shadow_apcb.apm) &&
		    test_bit_inv(apqi, matrix_mdev->shadow_apcb.aqm)) {
			clear_bit_inv(apid, matrix_mdev->shadow_apcb.apm);
			vfio_ap_mdev_hotplug_apcb(matrix_mdev);
		}
	}

	vfio_ap_mdev_reset_queue(q, 1);
	dev_set_drvdata(&apdev->device, NULL);
	kfree(q);
	vfio_ap_mdev_put_qlocks(matrix_mdev);
}

/**
 * vfio_ap_mdev_resource_in_use: check whether any of a set of APQNs is
 *				 assigned to a mediated device under the control
 *				 of the vfio_ap device driver.
 *
 * @apm: a bitmap specifying a set of APIDs comprising the APQNs to check.
 * @aqm: a bitmap specifying a set of APQIs comprising the APQNs to check.
 *
 * This function is invoked by the AP bus when changes to the apmask/aqmask
 * attributes will result in giving control of the queue devices specified via
 * @apm and @aqm to the default zcrypt device driver. Prior to calling this
 * function, the AP bus locks the ap_perms_mutex. If this function is called
 * while an adapter or domain is being assigned to a mediated device, the
 * assignment operations will take the matrix_dev->guests_lock and
 * matrix_dev->mdevs_lock then call the ap_apqn_in_matrix_owned_by_def_drv
 * function, which also locks the ap_perms_mutex. This could result in a
 * deadlock.
 *
 * To avoid a deadlock, this function will verify that the
 * matrix_dev->guests_lock and matrix_dev->mdevs_lock are not currently held and
 * will return -EBUSY if the locks can not be obtained.
 *
 * Return:
 *	* -EBUSY if the locks required by this function are already locked.
 *	* -EADDRINUSE if one or more of the APQNs specified via @apm/@aqm are
 *	  assigned to a mediated device under the control of the vfio_ap
 *	  device driver.
 */
int vfio_ap_mdev_resource_in_use(unsigned long *apm, unsigned long *aqm)
{
	int ret;

	if (!mutex_trylock(&matrix_dev->guests_lock))
		return -EBUSY;

	if (!mutex_trylock(&matrix_dev->mdevs_lock)) {
		mutex_unlock(&matrix_dev->guests_lock);
		return -EBUSY;
	}

	ret = vfio_ap_mdev_verify_no_sharing(apm, aqm);
	mutex_unlock(&matrix_dev->mdevs_lock);
	mutex_unlock(&matrix_dev->guests_lock);

	return ret;
}
