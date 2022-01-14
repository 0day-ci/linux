// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO ZPCI devices support
 *
 * Copyright (C) IBM Corp. 2020.  All rights reserved.
 *	Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *                 Matthew Rosato <mjrosato@linux.ibm.com>
 */
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <linux/vfio_zdev.h>
#include <asm/pci_clp.h>
#include <asm/pci_io.h>
#include <asm/kvm_pci.h>

#include <linux/vfio_pci_core.h>

/*
 * Add the Base PCI Function information to the device info region.
 */
static int zpci_base_cap(struct zpci_dev *zdev, struct vfio_info_cap *caps)
{
	struct vfio_device_info_cap_zpci_base cap = {
		.header.id = VFIO_DEVICE_INFO_CAP_ZPCI_BASE,
		.header.version = 1,
		.start_dma = zdev->start_dma,
		.end_dma = zdev->end_dma,
		.pchid = zdev->pchid,
		.vfn = zdev->vfn,
		.fmb_length = zdev->fmb_length,
		.pft = zdev->pft,
		.gid = zdev->pfgid
	};

	return vfio_info_add_capability(caps, &cap.header, sizeof(cap));
}

/*
 * Add the Base PCI Function Group information to the device info region.
 */
static int zpci_group_cap(struct zpci_dev *zdev, struct vfio_info_cap *caps)
{
	struct vfio_device_info_cap_zpci_group cap = {
		.header.id = VFIO_DEVICE_INFO_CAP_ZPCI_GROUP,
		.header.version = 1,
		.dasm = zdev->dma_mask,
		.msi_addr = zdev->msi_addr,
		.flags = VFIO_DEVICE_INFO_ZPCI_FLAG_REFRESH,
		.mui = zdev->fmb_update,
		.noi = zdev->max_msi,
		.maxstbl = ZPCI_MAX_WRITE_SIZE,
		.version = zdev->version
	};

	/* Some values are different for interpreted devices */
	if (zdev->kzdev && zdev->kzdev->interp)
		cap.maxstbl = zdev->maxstbl;

	return vfio_info_add_capability(caps, &cap.header, sizeof(cap));
}

/*
 * Add the device utility string to the device info region.
 */
static int zpci_util_cap(struct zpci_dev *zdev, struct vfio_info_cap *caps)
{
	struct vfio_device_info_cap_zpci_util *cap;
	int cap_size = sizeof(*cap) + CLP_UTIL_STR_LEN;
	int ret;

	cap = kmalloc(cap_size, GFP_KERNEL);
	if (!cap)
		return -ENOMEM;

	cap->header.id = VFIO_DEVICE_INFO_CAP_ZPCI_UTIL;
	cap->header.version = 1;
	cap->size = CLP_UTIL_STR_LEN;
	memcpy(cap->util_str, zdev->util_str, cap->size);

	ret = vfio_info_add_capability(caps, &cap->header, cap_size);

	kfree(cap);

	return ret;
}

/*
 * Add the function path string to the device info region.
 */
static int zpci_pfip_cap(struct zpci_dev *zdev, struct vfio_info_cap *caps)
{
	struct vfio_device_info_cap_zpci_pfip *cap;
	int cap_size = sizeof(*cap) + CLP_PFIP_NR_SEGMENTS;
	int ret;

	cap = kmalloc(cap_size, GFP_KERNEL);
	if (!cap)
		return -ENOMEM;

	cap->header.id = VFIO_DEVICE_INFO_CAP_ZPCI_PFIP;
	cap->header.version = 1;
	cap->size = CLP_PFIP_NR_SEGMENTS;
	memcpy(cap->pfip, zdev->pfip, cap->size);

	ret = vfio_info_add_capability(caps, &cap->header, cap_size);

	kfree(cap);

	return ret;
}

/*
 * Add all supported capabilities to the VFIO_DEVICE_GET_INFO capability chain.
 */
int vfio_pci_info_zdev_add_caps(struct vfio_pci_core_device *vdev,
				struct vfio_info_cap *caps)
{
	struct zpci_dev *zdev = to_zpci(vdev->pdev);
	int ret;

	if (!zdev)
		return -ENODEV;

	ret = zpci_base_cap(zdev, caps);
	if (ret)
		return ret;

	ret = zpci_group_cap(zdev, caps);
	if (ret)
		return ret;

	if (zdev->util_str_avail) {
		ret = zpci_util_cap(zdev, caps);
		if (ret)
			return ret;
	}

	ret = zpci_pfip_cap(zdev, caps);

	return ret;
}

int vfio_pci_zdev_feat_interp(struct vfio_pci_core_device *vdev,
			      struct vfio_device_feature feature,
			      unsigned long arg)
{
	struct zpci_dev *zdev = to_zpci(vdev->pdev);
	struct vfio_device_zpci_interp *data;
	struct vfio_device_feature *feat;
	unsigned long minsz;
	int size, rc;

	if (!zdev || !zdev->kzdev)
		return -EINVAL;

	/* If PROBE specified, return probe results immediately */
	if (feature.flags & VFIO_DEVICE_FEATURE_PROBE)
		return kvm_s390_pci_interp_probe(zdev);

	/* GET and SET are mutually exclusive */
	if ((feature.flags & VFIO_DEVICE_FEATURE_GET) &&
	    (feature.flags & VFIO_DEVICE_FEATURE_SET))
		return -EINVAL;

	size = sizeof(*feat) + sizeof(*data);
	feat = kzalloc(size, GFP_KERNEL);
	if (!feat)
		return -ENOMEM;

	data = (struct vfio_device_zpci_interp *)&feat->data;
	minsz = offsetofend(struct vfio_device_feature, flags);

	if (feature.argsz < minsz + sizeof(*data))
		return -EINVAL;

	/* Get the rest of the payload for GET/SET */
	rc = copy_from_user(data, (void __user *)(arg + minsz),
			    sizeof(*data));
	if (rc)
		rc = -EINVAL;

	if (feature.flags & VFIO_DEVICE_FEATURE_GET) {
		if (zdev->gd != 0)
			data->flags = VFIO_DEVICE_ZPCI_FLAG_INTERP;
		else
			data->flags = 0;
		data->fh = zdev->fh;
		/* userspace is using host fh, give interpreted clp values */
		zdev->kzdev->interp = true;

		if (copy_to_user((void __user *)arg, feat, size))
			rc = -EFAULT;
	} else if (feature.flags & VFIO_DEVICE_FEATURE_SET) {
		if (data->flags == VFIO_DEVICE_ZPCI_FLAG_INTERP)
			rc = kvm_s390_pci_interp_enable(zdev);
		else if (data->flags == 0)
			rc = kvm_s390_pci_interp_disable(zdev);
		else
			rc = -EINVAL;
	} else {
		/* Neither GET nor SET were specified */
		rc = -EINVAL;
	}

	kfree(feat);
	return rc;
}

static int vfio_pci_zdev_group_notifier(struct notifier_block *nb,
					unsigned long action, void *data)
{
	struct kvm_zdev *kzdev = container_of(nb, struct kvm_zdev, nb);

	if (action == VFIO_GROUP_NOTIFY_SET_KVM) {
		if (!data || !kzdev->zdev)
			return NOTIFY_DONE;
		kvm_s390_pci_attach_kvm(kzdev->zdev, data);
	}

	return NOTIFY_OK;
}

void vfio_pci_zdev_open(struct vfio_pci_core_device *vdev)
{
	unsigned long events = VFIO_GROUP_NOTIFY_SET_KVM;
	struct zpci_dev *zdev = to_zpci(vdev->pdev);

	if (!zdev)
		return;

	if (kvm_s390_pci_dev_open(zdev))
		return;

	zdev->kzdev->nb.notifier_call = vfio_pci_zdev_group_notifier;
	zdev->kzdev->interp = false;

	if (vfio_register_notifier(vdev->vdev.dev, VFIO_GROUP_NOTIFY,
				   &events, &zdev->kzdev->nb))
		kvm_s390_pci_dev_release(zdev);
}

void vfio_pci_zdev_release(struct vfio_pci_core_device *vdev)
{
	struct zpci_dev *zdev = to_zpci(vdev->pdev);

	if (!zdev || !zdev->kzdev)
		return;

	vfio_unregister_notifier(vdev->vdev.dev, VFIO_GROUP_NOTIFY,
				 &zdev->kzdev->nb);

	/*
	 * If the device was using interpretation, don't trust that userspace
	 * did the appropriate cleanup
	 */
	if (zdev->gd != 0)
		kvm_s390_pci_interp_disable(zdev);

	kvm_s390_pci_dev_release(zdev);
}
