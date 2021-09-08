// SPDX-License-Identifier: GPL-2.0-only
/*
 * vDPA bridge driver for modern virtio-pci device
 *
 * Copyright (c) 2020, Red Hat Inc. All rights reserved.
 * Author: Jason Wang <jasowang@redhat.com>
 *
 * Based on virtio_pci_modern.c.
 */

#include "linux/err.h"
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include "vp_vdpa_common.h"

int vp_vdpa_get_vq_irq(struct vdpa_device *vdev, u16 idx)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdev);

	return vp_vdpa->vring[idx].irq;
}

void vp_vdpa_free_irq(struct vp_vdpa *vp_vdpa)
{
	struct virtio_pci_modern_device *mdev = &vp_vdpa->mdev;
	struct pci_dev *pdev = mdev->pci_dev;
	int i;

	for (i = 0; i < vp_vdpa->queues; i++) {
		if (vp_vdpa->vring[i].irq != VIRTIO_MSI_NO_VECTOR) {
			vp_modern_queue_vector(mdev, i, VIRTIO_MSI_NO_VECTOR);
			devm_free_irq(&pdev->dev, vp_vdpa->vring[i].irq,
				      &vp_vdpa->vring[i]);
			vp_vdpa->vring[i].irq = VIRTIO_MSI_NO_VECTOR;
		}
	}

	if (vp_vdpa->config_irq != VIRTIO_MSI_NO_VECTOR) {
		vp_modern_config_vector(mdev, VIRTIO_MSI_NO_VECTOR);
		devm_free_irq(&pdev->dev, vp_vdpa->config_irq, vp_vdpa);
		vp_vdpa->config_irq = VIRTIO_MSI_NO_VECTOR;
	}

	if (vp_vdpa->vectors) {
		pci_free_irq_vectors(pdev);
		vp_vdpa->vectors = 0;
	}
}

static irqreturn_t vp_vdpa_vq_handler(int irq, void *arg)
{
	struct vp_vring *vring = arg;

	if (vring->cb.callback)
		return vring->cb.callback(vring->cb.private);

	return IRQ_HANDLED;
}

static irqreturn_t vp_vdpa_config_handler(int irq, void *arg)
{
	struct vp_vdpa *vp_vdpa = arg;

	if (vp_vdpa->config_cb.callback)
		return vp_vdpa->config_cb.callback(vp_vdpa->config_cb.private);

	return IRQ_HANDLED;
}

int vp_vdpa_request_irq(struct vp_vdpa *vp_vdpa)
{
	struct pci_dev *pdev = vp_vdpa->pci_dev;
	int i, ret, irq;
	int queues = vp_vdpa->queues;
	int vectors = queues + 1;

	ret = pci_alloc_irq_vectors(pdev, vectors, vectors, PCI_IRQ_MSIX);
	if (ret != vectors) {
		dev_err(&pdev->dev,
			"vp_vdpa: fail to allocate irq vectors want %d but %d\n",
			vectors, ret);
		return ret;
	}

	vp_vdpa->vectors = vectors;

	for (i = 0; i < queues; i++) {
		snprintf(vp_vdpa->vring[i].msix_name, VP_VDPA_NAME_SIZE,
			"vp-vdpa[%s]-%d\n", pci_name(pdev), i);
		irq = pci_irq_vector(pdev, i);
		ret = devm_request_irq(&pdev->dev, irq,
				       vp_vdpa_vq_handler,
				       0, vp_vdpa->vring[i].msix_name,
				       &vp_vdpa->vring[i]);
		if (ret) {
			dev_err(&pdev->dev,
				"vp_vdpa: fail to request irq for vq %d\n", i);
			goto err;
		}
		vp_vdpa->queue_vector(vp_vdpa, i, i);
		vp_vdpa->vring[i].irq = irq;
	}

	snprintf(vp_vdpa->msix_name, VP_VDPA_NAME_SIZE, "vp-vdpa[%s]-config\n",
		 pci_name(pdev));
	irq = pci_irq_vector(pdev, queues);
	ret = devm_request_irq(&pdev->dev, irq,	vp_vdpa_config_handler, 0,
			       vp_vdpa->msix_name, vp_vdpa);
	if (ret) {
		dev_err(&pdev->dev,
			"vp_vdpa: fail to request irq for vq %d\n", i);
			goto err;
	}
	vp_vdpa->config_vector(vp_vdpa, queues);
	vp_vdpa->config_irq = irq;

	return 0;
err:
	vp_vdpa_free_irq(vp_vdpa);
	return ret;
}

int vp_vdpa_get_vq_state(struct vdpa_device *vdpa, u16 qid,
			struct vdpa_vq_state *state)
{
	/* Note that this is not supported by virtio specification, so
	 * we return -EOPNOTSUPP here. This means we can't support live
	 * migration, vhost device start/stop.
	 */
	return -EOPNOTSUPP;
}

void vp_vdpa_set_vq_cb(struct vdpa_device *vdpa, u16 qid,
			struct vdpa_callback *cb)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);

	vp_vdpa->vring[qid].cb = *cb;
}

void vp_vdpa_kick_vq(struct vdpa_device *vdpa, u16 qid)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);

	vp_iowrite16(qid, vp_vdpa->vring[qid].notify);
}

u32 vp_vdpa_get_vq_align(struct vdpa_device *vdpa)
{
	return PAGE_SIZE;
}

void vp_vdpa_set_config_cb(struct vdpa_device *vdpa,
				  struct vdpa_callback *cb)
{
	struct vp_vdpa *vp_vdpa = vdpa_to_vp(vdpa);

	vp_vdpa->config_cb = *cb;
}

void vp_vdpa_free_irq_vectors(void *data)
{
	pci_free_irq_vectors(data);
}

static int vp_vdpa_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct vp_vdpa *vp_vdpa;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	vp_vdpa = vp_vdpa_modern_probe(pdev);
	if (PTR_ERR(vp_vdpa) == -ENODEV) {
		dev_info(&pdev->dev, "Tring legacy driver");
		vp_vdpa = vp_vdpa_legacy_probe(pdev);
	}
	if (IS_ERR(vp_vdpa))
		return PTR_ERR(vp_vdpa);

	vp_vdpa->pci_dev = pdev;

	pci_set_master(pdev);

	ret = vdpa_register_device(&vp_vdpa->vdpa, vp_vdpa->queues);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register to vdpa bus\n");
		goto err;
	}

	return 0;

err:
	put_device(&vp_vdpa->vdpa.dev);
	return ret;
}

static void vp_vdpa_remove(struct pci_dev *pdev)
{
	struct vp_vdpa *vp_vdpa = pci_get_drvdata(pdev);

	vdpa_unregister_device(&vp_vdpa->vdpa);
	vp_modern_remove(&vp_vdpa->mdev);
}

static struct pci_driver vp_vdpa_driver = {
	.name		= "vp-vdpa",
	.id_table	= NULL, /* only dynamic ids */
	.probe		= vp_vdpa_probe,
	.remove		= vp_vdpa_remove,
};

module_pci_driver(vp_vdpa_driver);

MODULE_AUTHOR("Jason Wang <jasowang@redhat.com>");
MODULE_DESCRIPTION("vp-vdpa");
MODULE_LICENSE("GPL");
MODULE_VERSION("1");
