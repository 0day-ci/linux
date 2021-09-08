// SPDX-License-Identifier: GPL-2.0-only
/*
 * vDPA bridge driver for legacy virtio-pci device
 *
 * Copyright (c) 2021, Alibaba Inc. All rights reserved.
 * Author: Wu Zongyong <wuzongyong@linux.alibaba.com>
 */

#include "linux/pci.h"
#include "linux/virtio_byteorder.h"
#include "linux/virtio_pci_legacy.h"
#include <uapi/linux/virtio_net.h>
#include <uapi/linux/virtio_blk.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_pci.h>
#include "vp_vdpa_common.h"

static struct virtio_pci_legacy_device *vdpa_to_ldev(struct vdpa_device *vdpa)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);

	return &vp_vdpa->ldev;
}

static u64 vp_vdpa_get_features(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return vp_legacy_get_features(ldev);
}

static int vp_vdpa_set_features(struct vdpa_device *vdpa, u64 features)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	vp_legacy_set_features(ldev, features);

	return 0;
}

static u8 vp_vdpa_get_status(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return vp_legacy_get_status(ldev);
}

static int vp_vdpa_set_vq_state_split(struct vdpa_device *vdpa,
				      const struct vdpa_vq_state *state)
{
	const struct vdpa_vq_state_split *split = &state->split;

	if (split->avail_index == 0)
		return 0;

	return -EOPNOTSUPP;
}

static int vp_vdpa_set_vq_state(struct vdpa_device *vdpa, u16 qid,
				const struct vdpa_vq_state *state)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	/* Note that this is not supported by virtio specification.
	 * But if the state is by chance equal to the device initial
	 * state, we can let it go.
	 */
	if (!vp_legacy_get_queue_enable(ldev, qid))
		return vp_vdpa_set_vq_state_split(vdpa,	state);

	return -EOPNOTSUPP;
}

static void vp_vdpa_set_vq_ready(struct vdpa_device *vdpa,
				 u16 qid, bool ready)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	/* Legacy devices can only be activated by setting vq address,
	 * and queue_enable is not supported by specification. So for
	 * legacy devices, we use @vp_vdpa_set_vq_address to set vq
	 * ready instead.
	 */
	if (!ready)
		vp_legacy_set_queue_address(ldev, qid, 0);
}

static bool vp_vdpa_get_vq_ready(struct vdpa_device *vdpa, u16 qid)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return vp_legacy_get_queue_enable(ldev, qid);
}

/* Legacy devices don't support set vq num by specification,
 * just report an error if someone try to set it.
 */
static void vp_vdpa_set_vq_num(struct vdpa_device *vdpa, u16 qid,
			       u32 num)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	dev_err(&ldev->pci_dev->dev, "legacy device don't support set vq num\n");
}

static u16 vp_vdpa_get_vq_num_max(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	/* assume all virtqueues have the same size */
	return vp_legacy_get_queue_size(ldev, 0);
}

static int vp_vdpa_set_vq_address(struct vdpa_device *vdpa, u16 qid,
				  u64 desc_area, u64 driver_area,
				  u64 device_area)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	vp_legacy_set_queue_address(ldev, qid, desc_area >> VIRTIO_PCI_QUEUE_ADDR_SHIFT);

	return 0;
}

static u32 vp_vdpa_get_device_id(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return ldev->id.device;
}

static u32 vp_vdpa_get_vendor_id(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);

	return ldev->id.vendor;
}

static size_t vp_vdpa_get_config_size(struct vdpa_device *vdpa)
{
	struct virtio_pci_legacy_device *ldev = vdpa_to_ldev(vdpa);
	size_t size;

	switch (ldev->id.device) {
	case VIRTIO_ID_NET:
		size = sizeof(struct virtio_net_config);
		break;
	case VIRTIO_ID_BLOCK:
		size = sizeof(struct virtio_blk_config);
		break;
	default:
		size = 0;
		dev_err(&ldev->pci_dev->dev, "VIRTIO ID %u not support\n", ldev->id.device);
	}

	return size;
}

static void vp_vdpa_get_config(struct vdpa_device *vdpa,
			       unsigned int offset,
			       void *buf, unsigned int len)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_legacy_device *ldev = &vp_vdpa->ldev;
	void __iomem *ioaddr = ldev->ioaddr +
		VIRTIO_PCI_CONFIG_OFF(vp_vdpa->vectors) +
		offset;
	u8 *p = buf;
	int i;

	/* legacy devices don't have a configuration generation field,
	 * so we just read it once.
	 */
	for (i = 0; i < len; i++)
		*p++ = ioread8(ioaddr + i);
}

static void vp_vdpa_set_config(struct vdpa_device *vdpa,
			       unsigned int offset, const void *buf,
			       unsigned int len)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_legacy_device *ldev = &vp_vdpa->ldev;
	void __iomem *ioaddr = ldev->ioaddr +
		VIRTIO_PCI_CONFIG_OFF(vp_vdpa->vectors) +
		offset;
	const u8 *p = buf;
	int i;

	for (i = 0; i < len; i++)
		iowrite8(*p++, ioaddr + i);
}

static void vp_vdpa_set_status(struct vdpa_device *vdpa, u8 status)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);
	struct virtio_pci_legacy_device *ldev = &vp_vdpa->ldev;
	u8 s = vp_vdpa_get_status(vdpa);

	if (status & VIRTIO_CONFIG_S_DRIVER_OK &&
	    !(s & VIRTIO_CONFIG_S_DRIVER_OK)) {
		vp_vdpa_request_irq(vp_vdpa);
	}

	vp_legacy_set_status(ldev, status);

	if (!(status & VIRTIO_CONFIG_S_DRIVER_OK) &&
	    (s & VIRTIO_CONFIG_S_DRIVER_OK)) {
		vp_vdpa_free_irq(vp_vdpa);
	}
}

static bool vp_vdpa_get_vq_num_unchangeable(struct vdpa_device *vdpa)
{
	return true;
}

static const struct vdpa_config_ops vp_vdpa_ops = {
	.get_features	= vp_vdpa_get_features,
	.set_features	= vp_vdpa_set_features,
	.get_status	= vp_vdpa_get_status,
	.set_status	= vp_vdpa_set_status,
	.get_vq_num_max	= vp_vdpa_get_vq_num_max,
	.get_vq_state	= vp_vdpa_get_vq_state,
	.set_vq_state	= vp_vdpa_set_vq_state,
	.set_vq_cb	= vp_vdpa_set_vq_cb,
	.set_vq_ready	= vp_vdpa_set_vq_ready,
	.get_vq_ready	= vp_vdpa_get_vq_ready,
	.set_vq_num	= vp_vdpa_set_vq_num,
	.set_vq_address	= vp_vdpa_set_vq_address,
	.kick_vq	= vp_vdpa_kick_vq,
	.get_device_id	= vp_vdpa_get_device_id,
	.get_vendor_id	= vp_vdpa_get_vendor_id,
	.get_vq_align	= vp_vdpa_get_vq_align,
	.get_config_size = vp_vdpa_get_config_size,
	.get_config	= vp_vdpa_get_config,
	.set_config	= vp_vdpa_set_config,
	.set_config_cb  = vp_vdpa_set_config_cb,
	.get_vq_irq	= vp_vdpa_get_vq_irq,
	.get_vq_num_unchangeable = vp_vdpa_get_vq_num_unchangeable,
};

static u16 vp_vdpa_get_num_queues(struct vp_vdpa *vp_vdpa)
{
	struct virtio_pci_legacy_device *ldev = &vp_vdpa->ldev;
	u32 features = vp_legacy_get_features(ldev);
	u16 num;

	switch (ldev->id.device) {
	case VIRTIO_ID_NET:
		num = 2;
		if (features & VIRTIO_NET_F_MQ) {
			__virtio16 max_virtqueue_pairs;

			vp_vdpa_get_config(&vp_vdpa->vdpa,
				offsetof(struct virtio_net_config, max_virtqueue_pairs),
				&max_virtqueue_pairs,
				sizeof(max_virtqueue_pairs));
			num = 2 * __virtio16_to_cpu(virtio_legacy_is_little_endian(),
						max_virtqueue_pairs);
		}

		if (features & VIRTIO_NET_F_CTRL_VQ)
			num += 1;
		break;
	case VIRTIO_ID_BLOCK:
		num = 1;
		break;
	default:
		num = 0;
		dev_err(&ldev->pci_dev->dev, "VIRTIO ID %u not support\n", ldev->id.device);
	}

	return num;
}

static u16 vp_vdpa_queue_vector(struct vp_vdpa *vp_vdpa, u16 idx, u16 vector)
{
	return vp_legacy_queue_vector(&vp_vdpa->ldev, idx, vector);
}

static u16 vp_vdpa_config_vector(struct vp_vdpa *vp_vdpa, u16 vector)
{
	return vp_legacy_config_vector(&vp_vdpa->ldev, vector);
}

struct vp_vdpa *vp_vdpa_legacy_probe(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct vp_vdpa *vp_vdpa;
	struct virtio_pci_legacy_device *ldev;
	int ret, i;

	vp_vdpa = vdpa_alloc_device(struct vp_vdpa, vdpa, dev, &vp_vdpa_ops, NULL);
	if (vp_vdpa == NULL) {
		dev_err(dev, "vp_vdpa: Failed to allocate vDPA structure\n");
		return ERR_PTR(-ENOMEM);
	}

	ldev = &vp_vdpa->ldev;
	ldev->pci_dev = pdev;

	ret = vp_legacy_probe(ldev);
	if (ret) {
		dev_err(dev, "Failed to probe legacy PCI device\n");
		goto err;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, vp_vdpa);

	vp_vdpa->vdpa.dma_dev = &pdev->dev;
	vp_vdpa->queues = vp_vdpa_get_num_queues(vp_vdpa);

	ret = devm_add_action_or_reset(dev, vp_vdpa_free_irq_vectors, pdev);
	if (ret) {
		dev_err(dev,
			"Failed for adding devres for freeing irq vectors\n");
		goto err;
	}

	vp_vdpa->vring = devm_kcalloc(dev, vp_vdpa->queues,
				      sizeof(*vp_vdpa->vring),
				      GFP_KERNEL);
	if (!vp_vdpa->vring) {
		ret = -ENOMEM;
		dev_err(dev, "Fail to allocate virtqueues\n");
		goto err;
	}

	for (i = 0; i < vp_vdpa->queues; i++) {
		vp_vdpa->vring[i].irq = VIRTIO_MSI_NO_VECTOR;
		vp_vdpa->vring[i].notify = ldev->ioaddr + VIRTIO_PCI_QUEUE_NOTIFY;
		vp_vdpa->vring[i].notify_pa = pci_resource_start(pdev, 0) + VIRTIO_PCI_QUEUE_NOTIFY;
	}
	vp_vdpa->config_irq = VIRTIO_MSI_NO_VECTOR;

	vp_vdpa->queue_vector = vp_vdpa_queue_vector;
	vp_vdpa->config_vector = vp_vdpa_config_vector;

	return vp_vdpa;

err:
	put_device(&vp_vdpa->vdpa.dev);
	return ERR_PTR(ret);
}
