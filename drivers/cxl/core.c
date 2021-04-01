// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020-2021 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include "cxl.h"

/**
 * DOC: cxl core
 *
 * The CXL core provides a sysfs hierarchy for control devices and a rendezvous
 * point for cross-device interleave coordination through cxl ports.
 */

static DEFINE_IDA(cxl_port_ida);

static ssize_t devtype_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sysfs_emit(buf, "%s\n", dev->type->name);
}
static DEVICE_ATTR_RO(devtype);

static struct attribute *cxl_base_attributes[] = {
	&dev_attr_devtype.attr,
	NULL,
};

static struct attribute_group cxl_base_attribute_group = {
	.attrs = cxl_base_attributes,
};

static struct cxl_address_space *dev_to_address_space(struct device *dev)
{
	struct cxl_address_space_dev *cxl_asd = to_cxl_address_space(dev);

	return cxl_asd->address_space;
}

static ssize_t start_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct cxl_address_space *space = dev_to_address_space(dev);

	return sysfs_emit(buf, "%#llx\n", space->range.start);
}
static DEVICE_ATTR_RO(start);

static ssize_t end_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct cxl_address_space *space = dev_to_address_space(dev);

	return sysfs_emit(buf, "%#llx\n", space->range.end);
}
static DEVICE_ATTR_RO(end);

#define CXL_ATTR_SUPPORTS(name, flag)                                  \
static ssize_t supports_##name##_show(                                 \
	struct device *dev, struct device_attribute *attr, char *buf)  \
{                                                                      \
	struct cxl_address_space *space = dev_to_address_space(dev);   \
                                                                       \
	return sysfs_emit(buf, "%s\n",                                 \
			  (space->flags & (flag)) ? "1" : "0");        \
}                                                                      \
static DEVICE_ATTR_RO(supports_##name)

CXL_ATTR_SUPPORTS(pmem, CXL_ADDRSPACE_PMEM);
CXL_ATTR_SUPPORTS(ram, CXL_ADDRSPACE_RAM);
CXL_ATTR_SUPPORTS(type2, CXL_ADDRSPACE_TYPE2);
CXL_ATTR_SUPPORTS(type3, CXL_ADDRSPACE_TYPE3);

static struct attribute *cxl_address_space_attributes[] = {
	&dev_attr_start.attr,
	&dev_attr_end.attr,
	&dev_attr_supports_pmem.attr,
	&dev_attr_supports_ram.attr,
	&dev_attr_supports_type2.attr,
	&dev_attr_supports_type3.attr,
	NULL,
};

static umode_t cxl_address_space_visible(struct kobject *kobj,
					 struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct cxl_address_space *space = dev_to_address_space(dev);

	if (a == &dev_attr_supports_pmem.attr &&
	    !(space->flags & CXL_ADDRSPACE_PMEM))
		return 0;

	if (a == &dev_attr_supports_ram.attr &&
	    !(space->flags & CXL_ADDRSPACE_RAM))
		return 0;

	if (a == &dev_attr_supports_type2.attr &&
	    !(space->flags & CXL_ADDRSPACE_TYPE2))
		return 0;

	if (a == &dev_attr_supports_type3.attr &&
	    !(space->flags & CXL_ADDRSPACE_TYPE3))
		return 0;

	return a->mode;
}

static struct attribute_group cxl_address_space_attribute_group = {
	.attrs = cxl_address_space_attributes,
	.is_visible = cxl_address_space_visible,
};

static const struct attribute_group *cxl_address_space_attribute_groups[] = {
	&cxl_address_space_attribute_group,
	&cxl_base_attribute_group,
	NULL,
};

static void cxl_address_space_release(struct device *dev)
{
	struct cxl_address_space_dev *cxl_asd = to_cxl_address_space(dev);

	remove_resource(&cxl_asd->res);
	kfree(cxl_asd);
}

static const struct device_type cxl_address_space_type = {
	.name = "cxl_address_space",
	.release = cxl_address_space_release,
	.groups = cxl_address_space_attribute_groups,
};

struct cxl_address_space_dev *to_cxl_address_space(struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type != &cxl_address_space_type,
			  "not a cxl_address_space device\n"))
		return NULL;
	return container_of(dev, struct cxl_address_space_dev, dev);
}

static void cxl_root_release(struct device *dev)
{
	struct cxl_root *cxl_root = to_cxl_root(dev);

	ida_free(&cxl_port_ida, cxl_root->port.id);
	kfree(cxl_root);
}

static ssize_t target_id_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct cxl_port *cxl_port = to_cxl_port(dev);

	return sysfs_emit(buf, "%d\n", cxl_port->target_id);
}
static DEVICE_ATTR_RO(target_id);

static struct attribute *cxl_port_attributes[] = {
	&dev_attr_target_id.attr,
	NULL,
};

static struct attribute_group cxl_port_attribute_group = {
	.attrs = cxl_port_attributes,
};

static const struct attribute_group *cxl_port_attribute_groups[] = {
	&cxl_port_attribute_group,
	&cxl_base_attribute_group,
	NULL,
};

static const struct device_type cxl_root_type = {
	.name = "cxl_root",
	.release = cxl_root_release,
	.groups = cxl_port_attribute_groups,
};

struct cxl_root *to_cxl_root(struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type != &cxl_root_type,
			  "not a cxl_root device\n"))
		return NULL;
	return container_of(dev, struct cxl_root, port.dev);
}

struct cxl_port *to_cxl_port(struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type != &cxl_root_type,
			  "not a cxl_port device\n"))
		return NULL;
	return container_of(dev, struct cxl_port, dev);
}

static void unregister_dev(void *dev)
{
	device_unregister(dev);
}

static struct cxl_root *cxl_root_alloc(struct device *parent,
				       struct cxl_address_space *cxl_space,
				       int nr_spaces)
{
	struct cxl_root *cxl_root;
	struct cxl_port *port;
	struct device *dev;
	int rc;

	cxl_root = kzalloc(struct_size(cxl_root, address_space, nr_spaces),
			   GFP_KERNEL);
	if (!cxl_root)
		return ERR_PTR(-ENOMEM);

	memcpy(cxl_root->address_space, cxl_space,
	       flex_array_size(cxl_root, address_space, nr_spaces));
	cxl_root->nr_spaces = nr_spaces;

	rc = ida_alloc(&cxl_port_ida, GFP_KERNEL);
	if (rc < 0)
		goto err;
	port = &cxl_root->port;
	port->id = rc;

	/*
	 * Root does not have a cxl_port as its parent and it does not
	 * have any corresponding component registers it is only a
	 * logical anchor to the first level of actual ports that decode
	 * the root address spaces.
	 */
	port->port_host = parent;
	port->target_id = -1;
	port->component_regs_phys = -1;

	dev = &port->dev;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = parent;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_root_type;

	return cxl_root;

err:
	kfree(cxl_root);
	return ERR_PTR(rc);
}

static struct cxl_address_space_dev *
cxl_address_space_dev_alloc(struct device *parent,
			    struct cxl_address_space *space)
{
	struct cxl_address_space_dev *cxl_asd;
	struct resource *res;
	struct device *dev;
	int rc;

	cxl_asd = kzalloc(sizeof(*cxl_asd), GFP_KERNEL);
	if (!cxl_asd)
		return ERR_PTR(-ENOMEM);

	res = &cxl_asd->res;
	res->name = "CXL Address Space";
	res->start = space->range.start;
	res->end = space->range.end;
	res->flags = IORESOURCE_MEM;

	rc = insert_resource(&iomem_resource, res);
	if (rc)
		goto err;

	cxl_asd->address_space = space;
	dev = &cxl_asd->dev;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = parent;
	dev->type = &cxl_address_space_type;

	return cxl_asd;

err:
	kfree(cxl_asd);
	return ERR_PTR(rc);
}

static int cxl_address_space_dev_add(struct device *host,
				     struct cxl_address_space_dev *cxl_asd,
				     int id)
{
	struct device *dev = &cxl_asd->dev;
	int rc;

	rc = dev_set_name(dev, "address_space%d", id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	dev_dbg(host, "%s: register %s\n", dev_name(dev->parent),
		dev_name(dev));

	return devm_add_action_or_reset(host, unregister_dev, dev);

err:
	put_device(dev);
	return rc;
}

struct cxl_root *devm_cxl_add_root(struct device *host,
				   struct cxl_address_space *cxl_space,
				   int nr_spaces)
{
	struct cxl_root *cxl_root;
	struct cxl_port *port;
	struct device *dev;
	int i, rc;

	cxl_root = cxl_root_alloc(host, cxl_space, nr_spaces);
	if (IS_ERR(cxl_root))
		return cxl_root;

	port = &cxl_root->port;
	dev = &port->dev;
	rc = dev_set_name(dev, "root%d", port->id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(host, unregister_dev, dev);
	if (rc)
		return ERR_PTR(rc);

	for (i = 0; i < nr_spaces; i++) {
		struct cxl_address_space *space = &cxl_root->address_space[i];
		struct cxl_address_space_dev *cxl_asd;

		if (!range_len(&space->range))
			continue;

		cxl_asd = cxl_address_space_dev_alloc(dev, space);
		if (IS_ERR(cxl_asd))
			return ERR_CAST(cxl_asd);

		rc = cxl_address_space_dev_add(host, cxl_asd, i);
		if (rc)
			return ERR_PTR(rc);
	}

	return cxl_root;

err:
	put_device(dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(devm_cxl_add_root);

/*
 * cxl_setup_device_regs() - Detect CXL Device register blocks
 * @dev: Host device of the @base mapping
 * @base: mapping of CXL 2.0 8.2.8 CXL Device Register Interface
 */
void cxl_setup_device_regs(struct device *dev, void __iomem *base,
			   struct cxl_device_regs *regs)
{
	int cap, cap_count;
	u64 cap_array;

	*regs = (struct cxl_device_regs) { 0 };

	cap_array = readq(base + CXLDEV_CAP_ARRAY_OFFSET);
	if (FIELD_GET(CXLDEV_CAP_ARRAY_ID_MASK, cap_array) !=
	    CXLDEV_CAP_ARRAY_CAP_ID)
		return;

	cap_count = FIELD_GET(CXLDEV_CAP_ARRAY_COUNT_MASK, cap_array);

	for (cap = 1; cap <= cap_count; cap++) {
		void __iomem *register_block;
		u32 offset;
		u16 cap_id;

		cap_id = FIELD_GET(CXLDEV_CAP_HDR_CAP_ID_MASK,
				   readl(base + cap * 0x10));
		offset = readl(base + cap * 0x10 + 0x4);
		register_block = base + offset;

		switch (cap_id) {
		case CXLDEV_CAP_CAP_ID_DEVICE_STATUS:
			dev_dbg(dev, "found Status capability (0x%x)\n", offset);
			regs->status = register_block;
			break;
		case CXLDEV_CAP_CAP_ID_PRIMARY_MAILBOX:
			dev_dbg(dev, "found Mailbox capability (0x%x)\n", offset);
			regs->mbox = register_block;
			break;
		case CXLDEV_CAP_CAP_ID_SECONDARY_MAILBOX:
			dev_dbg(dev, "found Secondary Mailbox capability (0x%x)\n", offset);
			break;
		case CXLDEV_CAP_CAP_ID_MEMDEV:
			dev_dbg(dev, "found Memory Device capability (0x%x)\n", offset);
			regs->memdev = register_block;
			break;
		default:
			dev_dbg(dev, "Unknown cap ID: %d (0x%x)\n", cap_id, offset);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(cxl_setup_device_regs);

struct bus_type cxl_bus_type = {
	.name = "cxl",
};
EXPORT_SYMBOL_GPL(cxl_bus_type);

static __init int cxl_core_init(void)
{
	return bus_register(&cxl_bus_type);
}

static void cxl_core_exit(void)
{
	bus_unregister(&cxl_bus_type);
}

module_init(cxl_core_init);
module_exit(cxl_core_exit);
MODULE_LICENSE("GPL v2");
