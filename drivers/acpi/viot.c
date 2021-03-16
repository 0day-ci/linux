// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual I/O topology
 */
#define pr_fmt(fmt) "ACPI: VIOT: " fmt

#include <linux/acpi_viot.h>
#include <linux/dma-iommu.h>
#include <linux/dma-map-ops.h>
#include <linux/fwnode.h>
#include <linux/iommu.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

struct viot_dev_id {
	unsigned int			type;
#define VIOT_DEV_TYPE_PCI		1
#define VIOT_DEV_TYPE_MMIO		2
	union {
		/* PCI endpoint or range */
		struct {
			u16		segment_start;
			u16		segment_end;
			u16		bdf_start;
			u16		bdf_end;
		};
		/* MMIO region */
		u64			base;
	};
};

struct viot_iommu {
	unsigned int			offset;
	struct viot_dev_id		dev_id;
	struct list_head		list;

	struct device			*dev; /* transport device */
	struct iommu_ops		*ops;
	bool				static_fwnode;
};

struct viot_endpoint {
	struct viot_dev_id		dev_id;
	u32				endpoint_id;
	struct list_head		list;
	struct viot_iommu		*viommu;
};

static struct acpi_table_viot *viot;
static LIST_HEAD(viot_iommus);
static LIST_HEAD(viot_endpoints);
static DEFINE_MUTEX(viommus_lock);

/*
 * VIOT parsing functions
 */

static int __init viot_check_bounds(const struct acpi_viot_header *hdr)
{
	struct acpi_viot_header *start, *end, *hdr_end;

	start = ACPI_ADD_PTR(struct acpi_viot_header, viot,
			     max_t(size_t, sizeof(*viot), viot->node_offset));
	end = ACPI_ADD_PTR(struct acpi_viot_header, viot, viot->header.length);
	hdr_end = ACPI_ADD_PTR(struct acpi_viot_header, hdr, sizeof(*hdr));

	if (hdr < start || hdr_end > end) {
		pr_err("Node pointer overflows, bad table\n");
		return -EOVERFLOW;
	}
	if (hdr->length < sizeof(*hdr)) {
		pr_err("Empty node, bad table\n");
		return -EINVAL;
	}
	return 0;
}

static struct viot_iommu * __init viot_get_iommu(unsigned int offset)
{
	struct viot_iommu *viommu;
	struct acpi_viot_header *hdr = ACPI_ADD_PTR(struct acpi_viot_header,
						    viot, offset);
	union {
		struct acpi_viot_virtio_iommu_pci pci;
		struct acpi_viot_virtio_iommu_mmio mmio;
	} *node = (void *)hdr;

	list_for_each_entry(viommu, &viot_iommus, list)
		if (viommu->offset == offset)
			return viommu;

	if (viot_check_bounds(hdr))
		return NULL;

	viommu = kzalloc(sizeof(*viommu), GFP_KERNEL);
	if (!viommu)
		return NULL;

	viommu->offset = offset;
	switch (hdr->type) {
	case ACPI_VIOT_NODE_VIRTIO_IOMMU_PCI:
		if (hdr->length < sizeof(node->pci))
			goto err_free;

		viommu->dev_id.type = VIOT_DEV_TYPE_PCI;
		viommu->dev_id.segment_start = node->pci.segment;
		viommu->dev_id.segment_end = node->pci.segment;
		viommu->dev_id.bdf_start = node->pci.bdf;
		viommu->dev_id.bdf_end = node->pci.bdf;
		break;
	case ACPI_VIOT_NODE_VIRTIO_IOMMU_MMIO:
		if (hdr->length < sizeof(node->mmio))
			goto err_free;

		viommu->dev_id.type = VIOT_DEV_TYPE_MMIO;
		viommu->dev_id.base = node->mmio.base_address;
		break;
	default:
		kfree(viommu);
		return NULL;
	}

	list_add(&viommu->list, &viot_iommus);
	return viommu;

err_free:
	kfree(viommu);
	return NULL;
}

static int __init viot_parse_node(const struct acpi_viot_header *hdr)
{
	int ret = -EINVAL;
	struct viot_endpoint *ep;
	union {
		struct acpi_viot_mmio mmio;
		struct acpi_viot_pci_range pci;
	} *node = (void *)hdr;

	if (viot_check_bounds(hdr))
		return -EINVAL;

	if (hdr->type == ACPI_VIOT_NODE_VIRTIO_IOMMU_PCI ||
	    hdr->type == ACPI_VIOT_NODE_VIRTIO_IOMMU_MMIO)
		return 0;

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	switch (hdr->type) {
	case ACPI_VIOT_NODE_PCI_RANGE:
		if (hdr->length < sizeof(node->pci))
			goto err_free;

		ep->dev_id.type = VIOT_DEV_TYPE_PCI;
		ep->dev_id.segment_start = node->pci.segment_start;
		ep->dev_id.segment_end = node->pci.segment_end;
		ep->dev_id.bdf_start = node->pci.bdf_start;
		ep->dev_id.bdf_end = node->pci.bdf_end;
		ep->endpoint_id = node->pci.endpoint_start;
		ep->viommu = viot_get_iommu(node->pci.output_node);
		break;
	case ACPI_VIOT_NODE_MMIO:
		if (hdr->length < sizeof(node->mmio))
			goto err_free;

		ep->dev_id.type = VIOT_DEV_TYPE_MMIO;
		ep->dev_id.base = node->mmio.base_address;
		ep->endpoint_id = node->mmio.endpoint;
		ep->viommu = viot_get_iommu(node->mmio.output_node);
		break;
	default:
		goto err_free;
	}

	if (!ep->viommu) {
		ret = -ENODEV;
		goto err_free;
	}

	list_add(&ep->list, &viot_endpoints);
	return 0;

err_free:
	kfree(ep);
	return ret;
}

void __init acpi_viot_init(void)
{
	int i;
	acpi_status status;
	struct acpi_table_header *hdr;
	struct acpi_viot_header *node;

	status = acpi_get_table(ACPI_SIG_VIOT, 0, &hdr);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND) {
			const char *msg = acpi_format_exception(status);

			pr_err("Failed to get table, %s\n", msg);
		}
		return;
	}

	viot = (void *)hdr;

	node = ACPI_ADD_PTR(struct acpi_viot_header, viot, viot->node_offset);
	for (i = 0; i < viot->node_count; i++) {
		if (viot_parse_node(node))
			return;

		node = ACPI_ADD_PTR(struct acpi_viot_header, node,
				    node->length);
	}
}

/*
 * VIOT access functions
 */

static bool viot_device_match(struct device *dev, struct viot_dev_id *id,
			      u32 *epid_base)
{
	if (id->type == VIOT_DEV_TYPE_PCI && dev_is_pci(dev)) {
		struct pci_dev *pdev = to_pci_dev(dev);
		u16 dev_id = pci_dev_id(pdev);
		u16 domain_nr = pci_domain_nr(pdev->bus);

		if (domain_nr >= id->segment_start &&
		    domain_nr <= id->segment_end &&
		    dev_id >= id->bdf_start &&
		    dev_id <= id->bdf_end) {
			*epid_base = ((u32)(domain_nr - id->segment_start) << 16) +
				dev_id - id->bdf_start;
			return true;
		}
	} else if (id->type == VIOT_DEV_TYPE_MMIO && dev_is_platform(dev)) {
		struct platform_device *plat_dev = to_platform_device(dev);
		struct resource *mem;

		mem = platform_get_resource(plat_dev, IORESOURCE_MEM, 0);
		if (mem && mem->start == id->base) {
			*epid_base = 0;
			return true;
		}
	}
	return false;
}

static const struct iommu_ops *viot_iommu_setup(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct viot_iommu *viommu = NULL;
	struct viot_endpoint *ep;
	u32 epid;
	int ret;

	/* Already translated? */
	if (fwspec && fwspec->ops)
		return NULL;

	mutex_lock(&viommus_lock);
	list_for_each_entry(ep, &viot_endpoints, list) {
		if (viot_device_match(dev, &ep->dev_id, &epid)) {
			epid += ep->endpoint_id;
			viommu = ep->viommu;
			break;
		}
	}
	mutex_unlock(&viommus_lock);
	if (!viommu)
		return NULL;

	/* We're not translating ourself */
	if (viot_device_match(dev, &viommu->dev_id, &epid))
		return NULL;

	/*
	 * If we found a PCI range managed by the viommu, we're the one that has
	 * to request ACS.
	 */
	if (dev_is_pci(dev))
		pci_request_acs();

	if (!viommu->ops || WARN_ON(!viommu->dev))
		return ERR_PTR(-EPROBE_DEFER);

	ret = iommu_fwspec_init(dev, viommu->dev->fwnode, viommu->ops);
	if (ret)
		return ERR_PTR(ret);

	iommu_fwspec_add_ids(dev, &epid, 1);

	/*
	 * If we have reason to believe the IOMMU driver missed the initial
	 * add_device callback for dev, replay it to get things in order.
	 */
	if (dev->bus && !device_iommu_mapped(dev))
		iommu_probe_device(dev);

	return viommu->ops;
}

/**
 * acpi_viot_dma_setup - Configure DMA for an endpoint described in VIOT
 * @dev: the endpoint
 * @attr: coherency property of the endpoint
 *
 * Setup the DMA and IOMMU ops for an endpoint described by the VIOT table.
 *
 * Return:
 * * 0 - @dev doesn't match any VIOT node
 * * 1 - ops for @dev were successfully installed
 * * -EPROBE_DEFER - ops for @dev aren't yet available
 */
int acpi_viot_dma_setup(struct device *dev, enum dev_dma_attr attr)
{
	const struct iommu_ops *iommu_ops = viot_iommu_setup(dev);

	if (IS_ERR_OR_NULL(iommu_ops)) {
		int ret = PTR_ERR(iommu_ops);

		if (ret == -EPROBE_DEFER || ret == 0)
			return ret;
		dev_err(dev, "error %d while setting up virt IOMMU\n", ret);
		return 0;
	}

#ifdef CONFIG_ARCH_HAS_SETUP_DMA_OPS
	arch_setup_dma_ops(dev, 0, ~0ULL, iommu_ops, attr == DEV_DMA_COHERENT);
#else
	iommu_setup_dma_ops(dev, 0, ~0ULL);
#endif
	return 1;
}

static int viot_set_iommu_ops(struct viot_iommu *viommu, struct device *dev,
			      struct iommu_ops *ops)
{
	/*
	 * The IOMMU subsystem relies on fwnode for identifying the IOMMU that
	 * manages an endpoint. Create one if necessary, because PCI devices
	 * don't always get a fwnode.
	 */
	if (!dev->fwnode) {
		dev->fwnode = acpi_alloc_fwnode_static();
		if (!dev->fwnode)
			return -ENOMEM;
		viommu->static_fwnode = true;
	}
	viommu->dev = dev;
	viommu->ops = ops;

	return 0;
}

static int viot_clear_iommu_ops(struct viot_iommu *viommu)
{
	struct device *dev = viommu->dev;

	viommu->dev = NULL;
	viommu->ops = NULL;
	if (dev && viommu->static_fwnode) {
		acpi_free_fwnode_static(dev->fwnode);
		dev->fwnode = NULL;
		viommu->static_fwnode = false;
	}
	return 0;
}

/**
 * acpi_viot_set_iommu_ops - Set the IOMMU ops of a virtual IOMMU device
 * @dev: the IOMMU device (transport)
 * @ops: the new IOMMU ops or NULL
 *
 * Once the IOMMU driver is loaded and the device probed, associate the IOMMU
 * ops to its VIOT node. Before disabling the IOMMU device, dissociate the ops
 * from the VIOT node.
 *
 * Return 0 on success, an error otherwise
 */
int acpi_viot_set_iommu_ops(struct device *dev, struct iommu_ops *ops)
{
	int ret = -EINVAL;
	struct viot_iommu *viommu;

	mutex_lock(&viommus_lock);
	list_for_each_entry(viommu, &viot_iommus, list) {
		u32 epid;

		if (!viot_device_match(dev, &viommu->dev_id, &epid))
			continue;

		if (ops)
			ret = viot_set_iommu_ops(viommu, dev, ops);
		else
			ret = viot_clear_iommu_ops(viommu);
		break;
	}
	mutex_unlock(&viommus_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_viot_set_iommu_ops);
