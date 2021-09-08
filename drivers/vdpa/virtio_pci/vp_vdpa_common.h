/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _DRIVERS_VDPA_VIRTIO_PCI_VP_VDPA_COMMON_H
#define _DRIVERS_VDPA_VIRTIO_PCI_VP_VDPA_COMMON_H

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vdpa.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include <linux/virtio_pci_modern.h>
#include <linux/virtio_pci_legacy.h>

#define VP_VDPA_DRIVER_NAME "vp_vdpa"
#define VP_VDPA_NAME_SIZE 256

struct vp_vring {
	void __iomem *notify;
	char msix_name[VP_VDPA_NAME_SIZE];
	struct vdpa_callback cb;
	resource_size_t notify_pa;
	int irq;
};

struct vp_vdpa {
	struct vdpa_device vdpa;
	struct pci_dev *pci_dev;
	struct virtio_pci_modern_device mdev;
	struct virtio_pci_legacy_device ldev;
	struct vp_vring *vring;
	struct vdpa_callback config_cb;
	char msix_name[VP_VDPA_NAME_SIZE];
	int config_irq;
	int queues;
	int vectors;
	u16 (*queue_vector)(struct vp_vdpa *vp_vdpa, u16 idx, u16 vector);
	u16 (*config_vector)(struct vp_vdpa *vp_vdpa, u16 vector);
};

static struct vp_vdpa *vdpa_to_vp(struct vdpa_device *vdpa)
{
	return container_of(vdpa, struct vp_vdpa, vdpa);
}

int vp_vdpa_get_vq_irq(struct vdpa_device *vdev, u16 idx);
void vp_vdpa_free_irq(struct vp_vdpa *vp_vdpa);
int vp_vdpa_request_irq(struct vp_vdpa *vp_vdpa);
int vp_vdpa_get_vq_state(struct vdpa_device *vdpa, u16 qid, struct vdpa_vq_state *state);
void vp_vdpa_set_vq_cb(struct vdpa_device *vdpa, u16 qid, struct vdpa_callback *cb);
void vp_vdpa_kick_vq(struct vdpa_device *vdpa, u16 qid);
u32 vp_vdpa_get_vq_align(struct vdpa_device *vdpa);
void vp_vdpa_set_config_cb(struct vdpa_device *vdpa, struct vdpa_callback *cb);
void vp_vdpa_free_irq_vectors(void *data);

struct vp_vdpa *vp_vdpa_modern_probe(struct pci_dev *pdev);

#if IS_ENABLED(CONFIG_VP_VDPA_LEGACY)
struct vp_vdpa *vp_vdpa_legacy_probe(struct pci_dev *pdev);
#else
static inline struct vp_vdpa *vp_vdpa_legacy_probe(struct pci_dev *pdev)
{
	return ERR_PTR(-ENODEV);
}
#endif

#endif
