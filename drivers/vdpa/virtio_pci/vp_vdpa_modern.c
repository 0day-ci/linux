// SPDX-License-Identifier: GPL-2.0-only
/*
 * vDPA bridge driver for modern virtio-pci device
 *
 * Copyright (c) 2020, Red Hat Inc. All rights reserved.
 * Author: Jason Wang <jasowang@redhat.com>
 *
 * Based on virtio_pci_modern.c.
 */

#include "linux/pci.h"
#include "linux/vdpa.h"
#include "vp_vdpa_common.h"

#define VP_VDPA_QUEUE_MAX 256

static struct virtio_pci_modern_device *vdpa_to_mdev(struct vdpa_device *vdpa)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);

	return &vp_vdpa->mdev;
}

static u64 vp_vdpa_get_features(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return vp_modern_get_features(mdev);
}

static int vp_vdpa_set_features(struct vdpa_device *vdpa, u64 features)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	vp_modern_set_features(mdev, features);

	return 0;
}

static u8 vp_vdpa_get_status(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return vp_modern_get_status(mdev);
}

static void vp_vdpa_set_status(struct vdpa_device *vdpa, u8 status)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_modern_device *mdev = &vp_vdpa->mdev;
	u8 s = vp_vdpa_get_status(vdpa);

	if (status & VIRTIO_CONFIG_S_DRIVER_OK &&
	    !(s & VIRTIO_CONFIG_S_DRIVER_OK)) {
		vp_vdpa_request_irq(vp_vdpa);
	}

	vp_modern_set_status(mdev, status);

	if (!(status & VIRTIO_CONFIG_S_DRIVER_OK) &&
	    (s & VIRTIO_CONFIG_S_DRIVER_OK))
		vp_vdpa_free_irq(vp_vdpa);
}

static u16 vp_vdpa_get_vq_num_max(struct vdpa_device *vdpa)
{
	return VP_VDPA_QUEUE_MAX;
}

static int vp_vdpa_set_vq_state_split(struct vdpa_device *vdpa,
				      const struct vdpa_vq_state *state)
{
	const struct vdpa_vq_state_split *split = &state->split;

	if (split->avail_index == 0)
		return 0;

	return -EOPNOTSUPP;
}

static int vp_vdpa_set_vq_state_packed(struct vdpa_device *vdpa,
				       const struct vdpa_vq_state *state)
{
	const struct vdpa_vq_state_packed *packed = &state->packed;

	if (packed->last_avail_counter == 1 &&
	    packed->last_avail_idx == 0 &&
	    packed->last_used_counter == 1 &&
	    packed->last_used_idx == 0)
		return 0;

	return -EOPNOTSUPP;
}

static int vp_vdpa_set_vq_state(struct vdpa_device *vdpa, u16 qid,
				const struct vdpa_vq_state *state)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	/* Note that this is not supported by virtio specification.
	 * But if the state is by chance equal to the device initial
	 * state, we can let it go.
	 */
	if ((vp_modern_get_status(mdev) & VIRTIO_CONFIG_S_FEATURES_OK) &&
	    !vp_modern_get_queue_enable(mdev, qid)) {
		if (vp_modern_get_driver_features(mdev) &
		    BIT_ULL(VIRTIO_F_RING_PACKED))
			return vp_vdpa_set_vq_state_packed(vdpa, state);
		else
			return vp_vdpa_set_vq_state_split(vdpa,	state);
	}

	return -EOPNOTSUPP;
}

static void vp_vdpa_set_vq_ready(struct vdpa_device *vdpa,
				 u16 qid, bool ready)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	vp_modern_set_queue_enable(mdev, qid, ready);
}

static bool vp_vdpa_get_vq_ready(struct vdpa_device *vdpa, u16 qid)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return vp_modern_get_queue_enable(mdev, qid);
}

static void vp_vdpa_set_vq_num(struct vdpa_device *vdpa, u16 qid,
			       u32 num)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	vp_modern_set_queue_size(mdev, qid, num);
}

static int vp_vdpa_set_vq_address(struct vdpa_device *vdpa, u16 qid,
				  u64 desc_area, u64 driver_area,
				  u64 device_area)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	vp_modern_queue_address(mdev, qid, desc_area,
				driver_area, device_area);

	return 0;
}

static u32 vp_vdpa_get_generation(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return vp_modern_generation(mdev);
}

static u32 vp_vdpa_get_device_id(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return mdev->id.device;
}

static u32 vp_vdpa_get_vendor_id(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return mdev->id.vendor;
}

static size_t vp_vdpa_get_config_size(struct vdpa_device *vdpa)
{
	struct virtio_pci_modern_device *mdev = vdpa_to_mdev(vdpa);

	return mdev->device_len;
}

static void vp_vdpa_get_config(struct vdpa_device *vdpa,
			       unsigned int offset,
			       void *buf, unsigned int len)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_modern_device *mdev = &vp_vdpa->mdev;
	u8 old, new;
	u8 *p;
	int i;

	do {
		old = vp_ioread8(&mdev->common->config_generation);
		p = buf;
		for (i = 0; i < len; i++)
			*p++ = vp_ioread8(mdev->device + offset + i);

		new = vp_ioread8(&mdev->common->config_generation);
	} while (old != new);
}

static void vp_vdpa_set_config(struct vdpa_device *vdpa,
			       unsigned int offset, const void *buf,
			       unsigned int len)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_modern_device *mdev = &vp_vdpa->mdev;
	const u8 *p = buf;
	int i;

	for (i = 0; i < len; i++)
		vp_iowrite8(*p++, mdev->device + offset + i);
}

static struct vdpa_notification_area
vp_vdpa_get_vq_notification(struct vdpa_device *vdpa, u16 qid)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_modern_device *mdev = &vp_vdpa->mdev;
	struct vdpa_notification_area notify;

	notify.addr = vp_vdpa->vring[qid].notify_pa;
	notify.size = mdev->notify_offset_multiplier;

	return notify;
}

static const struct vdpa_config_ops vp_vdpa_ops = {
	.get_features	= vp_vdpa_get_features,
	.set_features	= vp_vdpa_set_features,
	.get_status	= vp_vdpa_get_status,
	.set_status	= vp_vdpa_set_status,
	.get_vq_num_max	= vp_vdpa_get_vq_num_max,
	.get_vq_state	= vp_vdpa_get_vq_state,
	.get_vq_notification = vp_vdpa_get_vq_notification,
	.set_vq_state	= vp_vdpa_set_vq_state,
	.set_vq_cb	= vp_vdpa_set_vq_cb,
	.set_vq_ready	= vp_vdpa_set_vq_ready,
	.get_vq_ready	= vp_vdpa_get_vq_ready,
	.set_vq_num	= vp_vdpa_set_vq_num,
	.set_vq_address	= vp_vdpa_set_vq_address,
	.kick_vq	= vp_vdpa_kick_vq,
	.get_generation	= vp_vdpa_get_generation,
	.get_device_id	= vp_vdpa_get_device_id,
	.get_vendor_id	= vp_vdpa_get_vendor_id,
	.get_vq_align	= vp_vdpa_get_vq_align,
	.get_config_size = vp_vdpa_get_config_size,
	.get_config	= vp_vdpa_get_config,
	.set_config	= vp_vdpa_set_config,
	.set_config_cb  = vp_vdpa_set_config_cb,
	.get_vq_irq	= vp_vdpa_get_vq_irq,
};

static u16 vp_vdpa_queue_vector(struct vp_vdpa *vp_vdpa, u16 idx, u16 vector)
{
	return vp_modern_queue_vector(&vp_vdpa->mdev, idx, vector);
}

static u16 vp_vdpa_config_vector(struct vp_vdpa *vp_vdpa, u16 vector)
{
	return vp_modern_config_vector(&vp_vdpa->mdev, vector);
}

struct vp_vdpa *vp_vdpa_modern_probe(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct vp_vdpa *vp_vdpa;
	struct virtio_pci_modern_device *mdev;
	int ret, i;

	vp_vdpa = vdpa_alloc_device(struct vp_vdpa, vdpa,
				    dev, &vp_vdpa_ops, NULL);
	if (IS_ERR(vp_vdpa)) {
		dev_err(dev, "vp_vdpa: Failed to allocate vDPA structure\n");
		return vp_vdpa;
	}

	mdev = &vp_vdpa->mdev;
	mdev->pci_dev = pdev;

	ret = vp_modern_probe(mdev);
	if (ret) {
		dev_err(dev, "Failed to probe modern PCI device\n");
		goto err;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, vp_vdpa);

	vp_vdpa->vdpa.dma_dev = dev;
	vp_vdpa->queues = vp_modern_get_num_queues(mdev);

	ret = devm_add_action_or_reset(dev, vp_vdpa_free_irq_vectors, pdev);
	if (ret) {
		dev_err(dev,
			"Failed for adding devres for freeing irq vectors\n");
		goto err;
	}

	vp_vdpa->vring = devm_kcalloc(&pdev->dev, vp_vdpa->queues,
				      sizeof(*vp_vdpa->vring),
				      GFP_KERNEL);
	if (!vp_vdpa->vring) {
		ret = -ENOMEM;
		dev_err(dev, "Fail to allocate virtqueues\n");
		goto err;
	}

	for (i = 0; i < vp_vdpa->queues; i++) {
		vp_vdpa->vring[i].irq = VIRTIO_MSI_NO_VECTOR;
		vp_vdpa->vring[i].notify =
			vp_modern_map_vq_notify(mdev, i,
						&vp_vdpa->vring[i].notify_pa);
		if (!vp_vdpa->vring[i].notify) {
			ret = -EINVAL;
			dev_warn(dev, "Fail to map vq notify %d\n", i);
			goto err;
		}
	}
	vp_vdpa->config_irq = VIRTIO_MSI_NO_VECTOR;

	vp_vdpa->queue_vector = vp_vdpa_queue_vector;
	vp_vdpa->config_vector = vp_vdpa_config_vector;

	return vp_vdpa;

err:
	put_device(&vp_vdpa->vdpa.dev);
	return ERR_PTR(ret);
}
