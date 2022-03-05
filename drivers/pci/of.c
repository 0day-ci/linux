// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI <-> OF mapping helpers
 *
 * Copyright 2011 IBM Corp.
 */
#define pr_fmt(fmt)	"PCI: OF: " fmt

#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include "pci.h"

#ifdef CONFIG_PCI
void pci_set_of_node(struct pci_dev *dev)
{
	if (!dev->bus->dev.of_node)
		return;
	dev->dev.of_node = of_pci_find_child_device(dev->bus->dev.of_node,
						    dev->devfn);
	if (dev->dev.of_node)
		dev->dev.fwnode = &dev->dev.of_node->fwnode;
}

void pci_release_of_node(struct pci_dev *dev)
{
	of_node_put(dev->dev.of_node);
	dev->dev.of_node = NULL;
	dev->dev.fwnode = NULL;
}

void pci_set_bus_of_node(struct pci_bus *bus)
{
	struct device_node *node;

	if (bus->self == NULL) {
		node = pcibios_get_phb_of_node(bus);
	} else {
		node = of_node_get(bus->self->dev.of_node);
		if (node && of_property_read_bool(node, "external-facing"))
			bus->self->external_facing = true;
	}

	bus->dev.of_node = node;

	if (bus->dev.of_node)
		bus->dev.fwnode = &bus->dev.of_node->fwnode;
}

void pci_release_bus_of_node(struct pci_bus *bus)
{
	of_node_put(bus->dev.of_node);
	bus->dev.of_node = NULL;
	bus->dev.fwnode = NULL;
}

struct device_node * __weak pcibios_get_phb_of_node(struct pci_bus *bus)
{
	/* This should only be called for PHBs */
	if (WARN_ON(bus->self || bus->parent))
		return NULL;

	/*
	 * Look for a node pointer in either the intermediary device we
	 * create above the root bus or its own parent. Normally only
	 * the later is populated.
	 */
	if (bus->bridge->of_node)
		return of_node_get(bus->bridge->of_node);
	if (bus->bridge->parent && bus->bridge->parent->of_node)
		return of_node_get(bus->bridge->parent->of_node);
	return NULL;
}

struct irq_domain *pci_host_bridge_of_msi_domain(struct pci_bus *bus)
{
#ifdef CONFIG_IRQ_DOMAIN
	struct irq_domain *d;

	if (!bus->dev.of_node)
		return NULL;

	/* Start looking for a phandle to an MSI controller. */
	d = of_msi_get_domain(&bus->dev, bus->dev.of_node, DOMAIN_BUS_PCI_MSI);
	if (d)
		return d;

	/*
	 * If we don't have an msi-parent property, look for a domain
	 * directly attached to the host bridge.
	 */
	d = irq_find_matching_host(bus->dev.of_node, DOMAIN_BUS_PCI_MSI);
	if (d)
		return d;

	return irq_find_host(bus->dev.of_node);
#else
	return NULL;
#endif
}

bool pci_host_of_has_msi_map(struct device *dev)
{
	if (dev && dev->of_node)
		return of_get_property(dev->of_node, "msi-map", NULL);
	return false;
}

static inline int __of_pci_pci_compare(struct device_node *node,
				       unsigned int data)
{
	int devfn;

	devfn = of_pci_get_devfn(node);
	if (devfn < 0)
		return 0;

	return devfn == data;
}

struct device_node *of_pci_find_child_device(struct device_node *parent,
					     unsigned int devfn)
{
	struct device_node *node, *node2;

	for_each_child_of_node(parent, node) {
		if (__of_pci_pci_compare(node, devfn))
			return node;
		/*
		 * Some OFs create a parent node "multifunc-device" as
		 * a fake root for all functions of a multi-function
		 * device we go down them as well.
		 */
		if (of_node_name_eq(node, "multifunc-device")) {
			for_each_child_of_node(node, node2) {
				if (__of_pci_pci_compare(node2, devfn)) {
					of_node_put(node);
					return node2;
				}
			}
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(of_pci_find_child_device);

/**
 * of_pci_get_devfn() - Get device and function numbers for a device node
 * @np: device node
 *
 * Parses a standard 5-cell PCI resource and returns an 8-bit value that can
 * be passed to the PCI_SLOT() and PCI_FUNC() macros to extract the device
 * and function numbers respectively. On error a negative error code is
 * returned.
 */
int of_pci_get_devfn(struct device_node *np)
{
	u32 reg[5];
	int error;

	error = of_property_read_u32_array(np, "reg", reg, ARRAY_SIZE(reg));
	if (error)
		return error;

	return (reg[0] >> 8) & 0xff;
}
EXPORT_SYMBOL_GPL(of_pci_get_devfn);

/**
 * of_pci_parse_bus_range() - parse the bus-range property of a PCI device
 * @node: device node
 * @res: address to a struct resource to return the bus-range
 *
 * Returns 0 on success or a negative error-code on failure.
 */
int of_pci_parse_bus_range(struct device_node *node, struct resource *res)
{
	u32 bus_range[2];
	int error;

	error = of_property_read_u32_array(node, "bus-range", bus_range,
					   ARRAY_SIZE(bus_range));
	if (error)
		return error;

	res->name = node->name;
	res->start = bus_range[0];
	res->end = bus_range[1];
	res->flags = IORESOURCE_BUS;

	return 0;
}
EXPORT_SYMBOL_GPL(of_pci_parse_bus_range);

/**
 * of_get_pci_domain_nr - Find the host bridge domain number
 *			  of the given device node.
 * @node: Device tree node with the domain information.
 *
 * This function will try to obtain the host bridge domain number by finding
 * a property called "linux,pci-domain" of the given device node.
 *
 * Return:
 * * > 0	- On success, an associated domain number.
 * * -EINVAL	- The property "linux,pci-domain" does not exist.
 * * -ENODATA	- The linux,pci-domain" property does not have value.
 * * -EOVERFLOW	- Invalid "linux,pci-domain" property value.
 *
 * Returns the associated domain number from DT in the range [0-0xffff], or
 * a negative value if the required property is not found.
 */
int of_get_pci_domain_nr(struct device_node *node)
{
	u32 domain;
	int error;

	error = of_property_read_u32(node, "linux,pci-domain", &domain);
	if (error)
		return error;

	return (u16)domain;
}
EXPORT_SYMBOL_GPL(of_get_pci_domain_nr);

/**
 * of_pci_check_probe_only - Setup probe only mode if linux,pci-probe-only
 *                           is present and valid
 */
void of_pci_check_probe_only(void)
{
	u32 val;
	int ret;

	ret = of_property_read_u32(of_chosen, "linux,pci-probe-only", &val);
	if (ret) {
		if (ret == -ENODATA || ret == -EOVERFLOW)
			pr_warn("linux,pci-probe-only without valid value, ignoring\n");
		return;
	}

	if (val)
		pci_add_flags(PCI_PROBE_ONLY);
	else
		pci_clear_flags(PCI_PROBE_ONLY);

	pr_info("PROBE_ONLY %s\n", val ? "enabled" : "disabled");
}
EXPORT_SYMBOL_GPL(of_pci_check_probe_only);

/**
 * devm_of_pci_get_host_bridge_resources() - Resource-managed parsing of PCI
 *                                           host bridge resources from DT
 * @dev: host bridge device
 * @busno: bus number associated with the bridge root bus
 * @bus_max: maximum number of buses for this bridge
 * @resources: list where the range of resources will be added after DT parsing
 * @ib_resources: list where the range of inbound resources (with addresses
 *                from 'dma-ranges') will be added after DT parsing
 * @io_base: pointer to a variable that will contain on return the physical
 * address for the start of the I/O range. Can be NULL if the caller doesn't
 * expect I/O ranges to be present in the device tree.
 *
 * This function will parse the "ranges" property of a PCI host bridge device
 * node and setup the resource mapping based on its content. It is expected
 * that the property conforms with the Power ePAPR document.
 *
 * It returns zero if the range parsing has been successful or a standard error
 * value if it failed.
 */
static int devm_of_pci_get_host_bridge_resources(struct device *dev,
			unsigned char busno, unsigned char bus_max,
			struct list_head *resources,
			struct list_head *ib_resources,
			resource_size_t *io_base)
{
	struct device_node *dev_node = dev->of_node;
	struct resource *res, tmp_res;
	struct resource *bus_range;
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	const char *range_type;
	int err;

	if (io_base)
		*io_base = (resource_size_t)OF_BAD_ADDR;

	bus_range = devm_kzalloc(dev, sizeof(*bus_range), GFP_KERNEL);
	if (!bus_range)
		return -ENOMEM;

	dev_info(dev, "host bridge %pOF ranges:\n", dev_node);

	err = of_pci_parse_bus_range(dev_node, bus_range);
	if (err) {
		bus_range->start = busno;
		bus_range->end = bus_max;
		bus_range->flags = IORESOURCE_BUS;
		dev_info(dev, "  No bus range found for %pOF, using %pR\n",
			 dev_node, bus_range);
	} else {
		if (bus_range->end > bus_range->start + bus_max)
			bus_range->end = bus_range->start + bus_max;
	}
	pci_add_resource(resources, bus_range);

	/* Check for ranges property */
	err = of_pci_range_parser_init(&parser, dev_node);
	if (err)
		return 0;

	dev_dbg(dev, "Parsing ranges property...\n");
	for_each_of_pci_range(&parser, &range) {
		/* Read next ranges element */
		if ((range.flags & IORESOURCE_TYPE_BITS) == IORESOURCE_IO)
			range_type = "IO";
		else if ((range.flags & IORESOURCE_TYPE_BITS) == IORESOURCE_MEM)
			range_type = "MEM";
		else
			range_type = "err";
		dev_info(dev, "  %6s %#012llx..%#012llx -> %#012llx\n",
			 range_type, range.cpu_addr,
			 range.cpu_addr + range.size - 1, range.pci_addr);

		/*
		 * If we failed translation or got a zero-sized region
		 * then skip this range
		 */
		if (range.cpu_addr == OF_BAD_ADDR || range.size == 0)
			continue;

		err = of_pci_range_to_resource(&range, dev_node, &tmp_res);
		if (err)
			continue;

		res = devm_kmemdup(dev, &tmp_res, sizeof(tmp_res), GFP_KERNEL);
		if (!res) {
			err = -ENOMEM;
			goto failed;
		}

		if (resource_type(res) == IORESOURCE_IO) {
			if (!io_base) {
				dev_err(dev, "I/O range found for %pOF. Please provide an io_base pointer to save CPU base address\n",
					dev_node);
				err = -EINVAL;
				goto failed;
			}
			if (*io_base != (resource_size_t)OF_BAD_ADDR)
				dev_warn(dev, "More than one I/O resource converted for %pOF. CPU base address for old range lost!\n",
					 dev_node);
			*io_base = range.cpu_addr;
		} else if (resource_type(res) == IORESOURCE_MEM) {
			res->flags &= ~IORESOURCE_MEM_64;
		}

		pci_add_resource_offset(resources, res,	res->start - range.pci_addr);
	}

	/* Check for dma-ranges property */
	if (!ib_resources)
		return 0;
	err = of_pci_dma_range_parser_init(&parser, dev_node);
	if (err)
		return 0;

	dev_dbg(dev, "Parsing dma-ranges property...\n");
	for_each_of_pci_range(&parser, &range) {
		struct resource_entry *entry;
		/*
		 * If we failed translation or got a zero-sized region
		 * then skip this range
		 */
		if (((range.flags & IORESOURCE_TYPE_BITS) != IORESOURCE_MEM) ||
		    range.cpu_addr == OF_BAD_ADDR || range.size == 0)
			continue;

		dev_info(dev, "  %6s %#012llx..%#012llx -> %#012llx\n",
			 "IB MEM", range.cpu_addr,
			 range.cpu_addr + range.size - 1, range.pci_addr);


		err = of_pci_range_to_resource(&range, dev_node, &tmp_res);
		if (err)
			continue;

		res = devm_kmemdup(dev, &tmp_res, sizeof(tmp_res), GFP_KERNEL);
		if (!res) {
			err = -ENOMEM;
			goto failed;
		}

		/* Keep the resource list sorted */
		resource_list_for_each_entry(entry, ib_resources)
			if (entry->res->start > res->start)
				break;

		pci_add_resource_offset(&entry->node, res,
					res->start - range.pci_addr);
	}

	return 0;

failed:
	pci_free_resource_list(resources);
	return err;
}

#if IS_ENABLED(CONFIG_OF_IRQ)
/**
 * of_irq_parse_pci - Resolve the interrupt for a PCI device
 * @pdev:       the device whose interrupt is to be resolved
 * @out_irq:    structure of_phandle_args filled by this function
 *
 * This function resolves the PCI interrupt for a given PCI device. If a
 * device-node exists for a given pci_dev, it will use normal OF tree
 * walking. If not, it will implement standard swizzling and walk up the
 * PCI tree until an device-node is found, at which point it will finish
 * resolving using the OF tree walking.
 */
static int of_irq_parse_pci(const struct pci_dev *pdev, struct of_phandle_args *out_irq)
{
	struct device_node *dn, *ppnode = NULL;
	struct pci_dev *ppdev;
	__be32 laddr[3];
	u8 pin;
	int rc;

	/*
	 * Check if we have a device node, if yes, fallback to standard
	 * device tree parsing
	 */
	dn = pci_device_to_OF_node(pdev);
	if (dn) {
		rc = of_irq_parse_one(dn, 0, out_irq);
		if (!rc)
			return rc;
	}

	/*
	 * Ok, we don't, time to have fun. Let's start by building up an
	 * interrupt spec.  we assume #interrupt-cells is 1, which is standard
	 * for PCI. If you do different, then don't use that routine.
	 */
	rc = pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &pin);
	if (rc != 0)
		goto err;
	/* No pin, exit with no error message. */
	if (pin == 0)
		return -ENODEV;

	/* Local interrupt-map in the device node? Use it! */
	if (of_get_property(dn, "interrupt-map", NULL)) {
		pin = pci_swizzle_interrupt_pin(pdev, pin);
		ppnode = dn;
	}

	/* Now we walk up the PCI tree */
	while (!ppnode) {
		/* Get the pci_dev of our parent */
		ppdev = pdev->bus->self;

		/* Ouch, it's a host bridge... */
		if (ppdev == NULL) {
			ppnode = pci_bus_to_OF_node(pdev->bus);

			/* No node for host bridge ? give up */
			if (ppnode == NULL) {
				rc = -EINVAL;
				goto err;
			}
		} else {
			/* We found a P2P bridge, check if it has a node */
			ppnode = pci_device_to_OF_node(ppdev);
		}

		/*
		 * Ok, we have found a parent with a device-node, hand over to
		 * the OF parsing code.
		 * We build a unit address from the linux device to be used for
		 * resolution. Note that we use the linux bus number which may
		 * not match your firmware bus numbering.
		 * Fortunately, in most cases, interrupt-map-mask doesn't
		 * include the bus number as part of the matching.
		 * You should still be careful about that though if you intend
		 * to rely on this function (you ship a firmware that doesn't
		 * create device nodes for all PCI devices).
		 */
		if (ppnode)
			break;

		/*
		 * We can only get here if we hit a P2P bridge with no node;
		 * let's do standard swizzling and try again
		 */
		pin = pci_swizzle_interrupt_pin(pdev, pin);
		pdev = ppdev;
	}

	out_irq->np = ppnode;
	out_irq->args_count = 1;
	out_irq->args[0] = pin;
	laddr[0] = cpu_to_be32((pdev->bus->number << 16) | (pdev->devfn << 8));
	laddr[1] = laddr[2] = cpu_to_be32(0);
	rc = of_irq_parse_raw(laddr, out_irq);
	if (rc)
		goto err;
	return 0;
err:
	if (rc == -ENOENT) {
		dev_warn(&pdev->dev,
			"%s: no interrupt-map found, INTx interrupts not available\n",
			__func__);
		pr_warn_once("%s: possibly some PCI slots don't have level triggered interrupts capability\n",
			__func__);
	} else {
		dev_err(&pdev->dev, "%s: failed with rc=%d\n", __func__, rc);
	}
	return rc;
}

/**
 * of_irq_parse_and_map_pci() - Decode a PCI IRQ from the device tree and map to a VIRQ
 * @dev: The PCI device needing an IRQ
 * @slot: PCI slot number; passed when used as map_irq callback. Unused
 * @pin: PCI IRQ pin number; passed when used as map_irq callback. Unused
 *
 * @slot and @pin are unused, but included in the function so that this
 * function can be used directly as the map_irq callback to
 * pci_assign_irq() and struct pci_host_bridge.map_irq pointer
 */
int of_irq_parse_and_map_pci(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct of_phandle_args oirq;
	int ret;

	ret = of_irq_parse_pci(dev, &oirq);
	if (ret)
		return 0; /* Proper return code 0 == NO_IRQ */

	return irq_create_of_mapping(&oirq);
}
EXPORT_SYMBOL_GPL(of_irq_parse_and_map_pci);
#endif	/* CONFIG_OF_IRQ */

static int pci_parse_request_of_pci_ranges(struct device *dev,
					   struct pci_host_bridge *bridge)
{
	int err, res_valid = 0;
	resource_size_t iobase;
	struct resource_entry *win, *tmp;

	INIT_LIST_HEAD(&bridge->windows);
	INIT_LIST_HEAD(&bridge->dma_ranges);

	err = devm_of_pci_get_host_bridge_resources(dev, 0, 0xff, &bridge->windows,
						    &bridge->dma_ranges, &iobase);
	if (err)
		return err;

	err = devm_request_pci_bus_resources(dev, &bridge->windows);
	if (err)
		return err;

	resource_list_for_each_entry_safe(win, tmp, &bridge->windows) {
		struct resource *res = win->res;

		switch (resource_type(res)) {
		case IORESOURCE_IO:
			err = devm_pci_remap_iospace(dev, res, iobase);
			if (err) {
				dev_warn(dev, "error %d: failed to map resource %pR\n",
					 err, res);
				resource_list_destroy_entry(win);
			}
			break;
		case IORESOURCE_MEM:
			res_valid |= !(res->flags & IORESOURCE_PREFETCH);

			if (!(res->flags & IORESOURCE_PREFETCH))
				if (upper_32_bits(resource_size(res)))
					dev_warn(dev, "Memory resource size exceeds max for 32 bits\n");

			break;
		}
	}

	if (!res_valid)
		dev_warn(dev, "non-prefetchable memory resource required\n");

	return 0;
}

int devm_of_pci_bridge_init(struct device *dev, struct pci_host_bridge *bridge)
{
	if (!dev->of_node)
		return 0;

	bridge->swizzle_irq = pci_common_swizzle;
	bridge->map_irq = of_irq_parse_and_map_pci;

	return pci_parse_request_of_pci_ranges(dev, bridge);
}

#if IS_ENABLED(CONFIG_OF_DYNAMIC)

static void devm_of_pci_destroy_bus_endpoint(struct device *dev, void *res)
{
	struct device_node *node = res;

	of_detach_node(node);
}

static int of_ep_add_property(struct device *dev, struct property **proplist, const char *name,
			      const int length, void *value)
{
	struct property *new;

	new = devm_kzalloc(dev, sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	new->name = devm_kstrdup(dev, name, GFP_KERNEL);
	if (!new->name)
		return -ENOMEM;

	new->value = devm_kmalloc(dev, length, GFP_KERNEL);
	if (!new->value)
		return -ENOMEM;

	memcpy(new->value, value, length);
	new->length = length;
	new->next = *proplist;
	*proplist = new;

	return 0;
}

static struct device_node *of_ep_alloc_node(struct pci_dev *pdev, const char *name)
{
	struct device_node *node;
	char *full_name;

	node = devres_alloc(devm_of_pci_destroy_bus_endpoint, sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;

	full_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "/%s@%llx", name,
				   (u64)pci_resource_start(pdev, 0));
	if (!full_name)
		return NULL;

	node->parent = of_root;
	node->full_name = full_name;
	of_node_set_flag(node, OF_DYNAMIC);
	of_node_init(node);

	return node;
}

/**
 * devm_of_pci_create_bus_endpoint - Create a device node for the given pci device.
 * @pdev: PCI device pointer.
 *
 * For PCI device which uses flattened device tree to describe apertures in its BARs,
 * a device node for the given pci device is required. Then the flattened device tree
 * overlay from the device can be applied to the base tree.
 * The device node is under root node and act like bus node. It contains a "ranges"
 * property which is used for address translation of its children. Each child node
 * corresponds an aperture and use BAR index and offset as its address.

 * Returns 0 on success or a negative error-code on failure.
 */
int devm_of_pci_create_bus_endpoint(struct pci_dev *pdev)
{
	struct property *proplist = NULL;
	struct device *dev = &pdev->dev;
	int range_ncells, addr_ncells;
	struct device_node *node;
	void *prop = NULL;
	u32 *range_cell;
	__be32 val;
	int i, ret;

	node = of_ep_alloc_node(pdev, "pci-ep-bus");
	if (!node)
		return -ENOMEM;

	/* the endpoint node works as 'simple-bus' to translate aperture addresses. */
	prop = "simple-bus";
	ret = of_ep_add_property(dev, &proplist, "compatible", strlen(prop) + 1, prop);
	if (ret)
		goto cleanup;

	/* The address and size cells of nodes underneath are 2 */
	val = cpu_to_be32(2);
	ret = of_ep_add_property(dev, &proplist, "#address-cells", sizeof(u32), &val);
	if (ret)
		goto cleanup;

	ret = of_ep_add_property(dev, &proplist, "#size-cells", sizeof(u32), &val);
	if (ret)
		goto cleanup;

	/* child address format: 0xIooooooo oooooooo, I = bar index, o = offset on bar */
	addr_ncells = of_n_addr_cells(node);
	if (addr_ncells > 2) {
		/* does not support number of address cells greater than 2 */
		ret = -EINVAL;
		goto cleanup;
	}

	/* range cells include <node addr cells> <child addr cells> <child size cells> */
	range_ncells = addr_ncells + 4;
	prop = kzalloc(range_ncells * sizeof(u32) * PCI_STD_NUM_BARS, GFP_KERNEL);
	if (!prop) {
		ret = -ENOMEM;
		goto cleanup;
	}

	range_cell = prop;
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (!pci_resource_len(pdev, i))
			continue;
		/* highest 4 bits of address are bar index */
		*(__be64 *)range_cell = cpu_to_be64((u64)i << 60);
		range_cell += 2;
		if (addr_ncells == 2)
			*(__be64 *)range_cell = cpu_to_be64((u64)pci_resource_start(pdev, i));
		else
			*(__be32 *)range_cell = cpu_to_be32((u32)pci_resource_start(pdev, i));

		range_cell += addr_ncells;
		*(__be64 *)range_cell = cpu_to_be64((u64)pci_resource_len(pdev, i));
		range_cell += 2;
	}

	/* error out if there is not PCI BAR been found */
	if ((void *)range_cell == prop) {
		ret = -EINVAL;
		goto cleanup;
	}

	ret = of_ep_add_property(dev, &proplist, "ranges", (void *)range_cell - prop, prop);
	kfree(prop);
	if (ret)
		goto cleanup;

	node->properties = proplist;
	ret = of_attach_node(node);
	if (ret)
		goto cleanup;

	devres_add(dev, node);

	return 0;

cleanup:
	kfree(prop);
	if (node)
		devres_free(node);

	return ret;
}
EXPORT_SYMBOL_GPL(devm_of_pci_create_bus_endpoint);

struct device_node *of_pci_find_bus_endpoint(struct pci_dev *pdev)
{
	struct device_node *dn;
	char *path;

	path = kasprintf(GFP_KERNEL, "/pci-ep-bus@%llx",
			 (u64)pci_resource_start(pdev, 0));
	if (!path)
		return NULL;

	dn = of_find_node_by_path(path);
	kfree(path);

	return dn;
}
EXPORT_SYMBOL_GPL(of_pci_find_bus_endpoint);
#endif /* CONFIG_OF_DYNAMIC */

#endif /* CONFIG_PCI */

/**
 * of_pci_get_max_link_speed - Find the maximum link speed of the given device node.
 * @node: Device tree node with the maximum link speed information.
 *
 * This function will try to find the limitation of link speed by finding
 * a property called "max-link-speed" of the given device node.
 *
 * Return:
 * * > 0	- On success, a maximum link speed.
 * * -EINVAL	- Invalid "max-link-speed" property value, or failure to access
 *		  the property of the device tree node.
 *
 * Returns the associated max link speed from DT, or a negative value if the
 * required property is not found or is invalid.
 */
int of_pci_get_max_link_speed(struct device_node *node)
{
	u32 max_link_speed;

	if (of_property_read_u32(node, "max-link-speed", &max_link_speed) ||
	    max_link_speed == 0 || max_link_speed > 4)
		return -EINVAL;

	return max_link_speed;
}
EXPORT_SYMBOL_GPL(of_pci_get_max_link_speed);
