// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvpanic PCI Device Support
 *
 *  Copyright (C) 2021 Oracle.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include "pvpanic.h"

#define PCI_VENDOR_ID_REDHAT             0x1b36
#define PCI_DEVICE_ID_REDHAT_PVPANIC     0x0011

static void __iomem *base;
static const struct pci_device_id pvpanic_pci_id_tbl[]  = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REDHAT, PCI_DEVICE_ID_REDHAT_PVPANIC),},
	{}
};
static unsigned int capability = PVPANIC_PANICKED | PVPANIC_CRASH_LOADED;
static unsigned int events;

static ssize_t capability_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%x\n", capability);
}
static DEVICE_ATTR_RO(capability);

static ssize_t events_show(struct device *dev,  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%x\n", events);
}

static ssize_t events_store(struct device *dev,  struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned int tmp;
	int err;

	err = kstrtouint(buf, 16, &tmp);
	if (err)
		return err;

	if ((tmp & capability) != tmp)
		return -EINVAL;

	events = tmp;

	pvpanic_set_events(base, events);

	return count;

}
static DEVICE_ATTR_RW(events);

static struct attribute *pvpanic_pci_dev_attrs[] = {
	&dev_attr_capability.attr,
	&dev_attr_events.attr,
	NULL
};
ATTRIBUTE_GROUPS(pvpanici_pci_dev);

static int pvpanic_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *ent)
{
	int ret;
	struct resource res;

	ret = pci_enable_device(pdev);
	if (ret < 0)
		return ret;

	base = pci_iomap(pdev, 0, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/* initlize capability by RDPT */
	capability &= ioread8(base);
	events = capability;

	return pvpanic_probe(base, capability);
}

static void pvpanic_pci_remove(struct pci_dev *pdev)
{
	pvpanic_remove(base);
	iounmap(base);
	pci_disable_device(pdev);
}

static struct pci_driver pvpanic_pci_driver = {
	.name =         "pvpanic-pci",
	.id_table =     pvpanic_pci_id_tbl,
	.groups =       pvpanic_pci_dev_groups,
	.probe =        pvpanic_pci_probe,
	.remove =       pvpanic_pci_remove,
};

module_pci_driver(pvpanic_pci_driver);
