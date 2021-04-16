// SPDX-License-Identifier: GPL-2.0
/*
 * (C) Copyright 2002-2004 Greg Kroah-Hartman <greg@kroah.com>
 * (C) Copyright 2002-2004 IBM Corp.
 * (C) Copyright 2003 Matthew Wilcox
 * (C) Copyright 2003 Hewlett-Packard
 * (C) Copyright 2004 Jon Smirl <jonsmirl@yahoo.com>
 * (C) Copyright 2004 Silicon Graphics, Inc. Jesse Barnes <jbarnes@sgi.com>
 *
 * File attributes for PCI devices
 *
 * Modeled after usb's driverfs.c
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/stat.h>
#include <linux/export.h>
#include <linux/topology.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/capability.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/vgaarb.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include "pci.h"

static int sysfs_initialized;	/* = 0 */

static ssize_t pci_dev_show_local_cpu(struct device *dev, bool list,
				      struct device_attribute *attr, char *buf)
{
	const struct cpumask *mask;
#ifdef CONFIG_NUMA
	int node = dev_to_node(dev);

	if (node == NUMA_NO_NODE)
		mask = cpu_online_mask;
	else
		mask = cpumask_of_node(node);
#else
	struct pci_dev *pdev = to_pci_dev(dev);

	mask = cpumask_of_pcibus(pdev->bus);
#endif
	return cpumap_print_to_pagebuf(list, buf, mask);
}

static ssize_t power_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "%s\n", pci_power_name(pdev->current_state));
}
static DEVICE_ATTR_RO(power_state);

static ssize_t resource_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int i, max;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct resource *res;
	resource_size_t start, end;
	size_t len = 0;

	max = PCI_BRIDGE_RESOURCES;
	if (pdev->subordinate)
		max = DEVICE_COUNT_RESOURCE;

	for (i = 0; i < max; i++) {
		res = &pdev->resource[i];
		pci_resource_to_user(pdev, i, res, &start, &end);
		len += sysfs_emit_at(buf, len, "0x%016llx 0x%016llx 0x%016llx\n",
				     (unsigned long long)start,
				     (unsigned long long)end,
				     (unsigned long long)res->flags);
	}

	return len;
}
static DEVICE_ATTR_RO(resource);

static ssize_t vendor_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "0x%04x\n", pdev->vendor);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t device_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "0x%04x\n", pdev->device);
}
static DEVICE_ATTR_RO(device);

static ssize_t subsystem_vendor_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "0x%04x\n", pdev->subsystem_vendor);
}
static DEVICE_ATTR_RO(subsystem_vendor);

static ssize_t subsystem_device_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "0x%04x\n", pdev->subsystem_device);
}
static DEVICE_ATTR_RO(subsystem_device);

static ssize_t revision_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "0x%02x\n", pdev->revision);
}
static DEVICE_ATTR_RO(revision);

static ssize_t class_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "0x%06x\n", pdev->class);
}
static DEVICE_ATTR_RO(class);

static ssize_t irq_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "%u\n", pdev->irq);
}
static DEVICE_ATTR_RO(irq);

static ssize_t local_cpus_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return pci_dev_show_local_cpu(dev, false, attr, buf);
}
static DEVICE_ATTR_RO(local_cpus);

static ssize_t local_cpulist_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return pci_dev_show_local_cpu(dev, true, attr, buf);
}
static DEVICE_ATTR_RO(local_cpulist);

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "pci:v%08Xd%08Xsv%08Xsd%08Xbc%02Xsc%02Xi%02X\n",
			  pdev->vendor, pdev->device,
			  pdev->subsystem_vendor, pdev->subsystem_device,
			  (u8)(pdev->class >> 16), (u8)(pdev->class >> 8),
			  (u8)(pdev->class));
}
static DEVICE_ATTR_RO(modalias);

#ifdef CONFIG_NUMA
static ssize_t numa_node_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", dev->numa_node);
}

static ssize_t numa_node_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	int node;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (kstrtoint(buf, 0, &node) < 0)
		return -EINVAL;

	if ((node < 0 && node != NUMA_NO_NODE) || node >= MAX_NUMNODES)
		return -EINVAL;

	if (node != NUMA_NO_NODE && !node_online(node))
		return -EINVAL;

	add_taint(TAINT_FIRMWARE_WORKAROUND, LOCKDEP_STILL_OK);
	pci_alert(pdev, FW_BUG "Overriding NUMA node to %d.  Contact your vendor for updates.",
		  node);

	dev->numa_node = node;

	return count;
}
static DEVICE_ATTR_RW(numa_node);
#endif

static ssize_t dma_mask_bits_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "%d\n", fls64(pdev->dma_mask));
}
static DEVICE_ATTR_RO(dma_mask_bits);

static ssize_t consistent_dma_mask_bits_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	return sysfs_emit(buf, "%d\n", fls64(dev->coherent_dma_mask));
}
static DEVICE_ATTR_RO(consistent_dma_mask_bits);

static ssize_t enable_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "%u\n", atomic_read(&pdev->enable_cnt));
}

static ssize_t enable_store(struct device *dev,
			    struct device_attribute *attr, const char *buf,
			    size_t count)
{
	bool enable;
	struct pci_dev *pdev = to_pci_dev(dev);
	ssize_t ret = 0;

	/* this can crash the machine when done on the "wrong" device */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (kstrtobool(buf, &enable) < 0)
		return -EINVAL;

	device_lock(dev);
	if (dev->driver)
		ret = -EBUSY;
	else if (enable)
		ret = pci_enable_device(pdev);
	else if (pci_is_enabled(pdev))
		pci_disable_device(pdev);
	else
		ret = -EIO;
	device_unlock(dev);

	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(enable);

static ssize_t broken_parity_status_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "%u\n", pdev->broken_parity_status);
}

static ssize_t broken_parity_status_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	bool broken;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (kstrtobool(buf, &broken) < 0)
		return -EINVAL;

	pdev->broken_parity_status = !!broken;

	return count;
}
static DEVICE_ATTR_RW(broken_parity_status);

static ssize_t msi_bus_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_bus *subordinate = pdev->subordinate;

	return sysfs_emit(buf, "%u\n", subordinate ?
			  !(subordinate->bus_flags & PCI_BUS_FLAGS_NO_MSI)
			    : !pdev->no_msi);
}

static ssize_t msi_bus_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	bool allowed;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_bus *subordinate = pdev->subordinate;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (kstrtobool(buf, &allowed) < 0)
		return -EINVAL;

	/*
	 * "no_msi" and "bus_flags" only affect what happens when a driver
	 * requests MSI or MSI-X.  They don't affect any drivers that have
	 * already requested MSI or MSI-X.
	 */
	if (!subordinate) {
		pdev->no_msi = !allowed;
		pci_info(pdev, "MSI/MSI-X %s for future drivers\n",
			 allowed ? "allowed" : "disallowed");
		return count;
	}

	if (allowed)
		subordinate->bus_flags &= ~PCI_BUS_FLAGS_NO_MSI;
	else
		subordinate->bus_flags |= PCI_BUS_FLAGS_NO_MSI;

	dev_info(&subordinate->dev, "MSI/MSI-X %s for future drivers of devices on this bus\n",
		 allowed ? "allowed" : "disallowed");

	return count;
}
static DEVICE_ATTR_RW(msi_bus);

#if defined(CONFIG_PM) && defined(CONFIG_ACPI)
static ssize_t d3cold_allowed_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "%u\n", pdev->d3cold_allowed);
}

static ssize_t d3cold_allowed_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	bool allowed;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (kstrtobool(buf, &allowed) < 0)
		return -EINVAL;

	pdev->d3cold_allowed = !!allowed;
	if (pdev->d3cold_allowed)
		pci_d3cold_enable(pdev);
	else
		pci_d3cold_disable(pdev);

	pm_runtime_resume(dev);

	return count;
}
static DEVICE_ATTR_RW(d3cold_allowed);
#endif

#ifdef CONFIG_OF
static ssize_t devspec_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct device_node *np = pci_device_to_OF_node(pdev);

	if (!np)
		return 0;

	return sysfs_emit(buf, "%pOF", np);
}
static DEVICE_ATTR_RO(devspec);
#endif

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	ssize_t len;

	device_lock(dev);
	len = sysfs_emit(buf, "%s\n", pdev->driver_override);
	device_unlock(dev);

	return len;
}

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	char *driver_override, *old, *cp;
	struct pci_dev *pdev = to_pci_dev(dev);

	/* We need to keep extra room for a newline */
	if (count >= (PAGE_SIZE - 1))
		return -EINVAL;

	driver_override = kstrndup(buf, count, GFP_KERNEL);
	if (!driver_override)
		return -ENOMEM;

	cp = strchr(driver_override, '\n');
	if (cp)
		*cp = '\0';

	device_lock(dev);
	old = pdev->driver_override;
	if (strlen(driver_override)) {
		pdev->driver_override = driver_override;
	} else {
		kfree(driver_override);
		pdev->driver_override = NULL;
	}
	device_unlock(dev);

	kfree(old);

	return count;
}
static DEVICE_ATTR_RW(driver_override);

static ssize_t ari_enabled_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "%u\n", pci_ari_enabled(pdev->bus));
}
static DEVICE_ATTR_RO(ari_enabled);

static struct attribute *pci_dev_attrs[] = {
	&dev_attr_power_state.attr,
	&dev_attr_resource.attr,
	&dev_attr_vendor.attr,
	&dev_attr_device.attr,
	&dev_attr_subsystem_vendor.attr,
	&dev_attr_subsystem_device.attr,
	&dev_attr_revision.attr,
	&dev_attr_class.attr,
	&dev_attr_irq.attr,
	&dev_attr_local_cpus.attr,
	&dev_attr_local_cpulist.attr,
	&dev_attr_modalias.attr,
#ifdef CONFIG_NUMA
	&dev_attr_numa_node.attr,
#endif
	&dev_attr_dma_mask_bits.attr,
	&dev_attr_consistent_dma_mask_bits.attr,
	&dev_attr_enable.attr,
	&dev_attr_broken_parity_status.attr,
	&dev_attr_msi_bus.attr,
#if defined(CONFIG_PM) && defined(CONFIG_ACPI)
	&dev_attr_d3cold_allowed.attr,
#endif
#ifdef CONFIG_OF
	&dev_attr_devspec.attr,
#endif
	&dev_attr_driver_override.attr,
	&dev_attr_ari_enabled.attr,
	NULL,
};

static const struct attribute_group pci_dev_group = {
	.attrs = pci_dev_attrs,
};

static ssize_t config_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr, char *buf,
			   loff_t off, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	unsigned int size = 64;
	loff_t init_off = off;
	u8 *data = (u8 *)buf;

	/* Several chips lock up trying to read undefined config space */
	if (file_ns_capable(filp, &init_user_ns, CAP_SYS_ADMIN))
		size = pdev->cfg_size;
	else if (pdev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
		size = 128;

	if (off > size)
		return 0;

	if (off + count > size) {
		size -= off;
		count = size;
	} else {
		size = count;
	}

	pci_config_pm_runtime_get(pdev);

	if ((off & 1) && size) {
		u8 val;
		pci_user_read_config_byte(pdev, off, &val);
		data[off - init_off] = val;
		off++;
		size--;
	}

	if ((off & 3) && size > 2) {
		u16 val;
		pci_user_read_config_word(pdev, off, &val);
		data[off - init_off] = val & 0xff;
		data[off - init_off + 1] = (val >> 8) & 0xff;
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val;
		pci_user_read_config_dword(pdev, off, &val);
		data[off - init_off] = val & 0xff;
		data[off - init_off + 1] = (val >> 8) & 0xff;
		data[off - init_off + 2] = (val >> 16) & 0xff;
		data[off - init_off + 3] = (val >> 24) & 0xff;
		off += 4;
		size -= 4;
		cond_resched();
	}

	if (size >= 2) {
		u16 val;
		pci_user_read_config_word(pdev, off, &val);
		data[off - init_off] = val & 0xff;
		data[off - init_off + 1] = (val >> 8) & 0xff;
		off += 2;
		size -= 2;
	}

	if (size > 0) {
		u8 val;
		pci_user_read_config_byte(pdev, off, &val);
		data[off - init_off] = val;
		off++;
		--size;
	}

	pci_config_pm_runtime_put(pdev);

	return count;
}

static ssize_t config_write(struct file *filp, struct kobject *kobj,
			    struct bin_attribute *bin_attr, char *buf,
			    loff_t off, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	unsigned int size = count;
	loff_t init_off = off;
	u8 *data = (u8 *)buf;
	size_t ret;

	ret = security_locked_down(LOCKDOWN_PCI_ACCESS);
	if (ret)
		return ret;

	if (off > pdev->cfg_size)
		return 0;

	if (off + count > pdev->cfg_size) {
		size = pdev->cfg_size - off;
		count = size;
	}

	pci_config_pm_runtime_get(pdev);

	if ((off & 1) && size) {
		pci_user_write_config_byte(pdev, off, data[off - init_off]);
		off++;
		size--;
	}

	if ((off & 3) && size > 2) {
		u16 val = data[off - init_off];
		val |= (u16) data[off - init_off + 1] << 8;
		pci_user_write_config_word(pdev, off, val);
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val = data[off - init_off];
		val |= (u32) data[off - init_off + 1] << 8;
		val |= (u32) data[off - init_off + 2] << 16;
		val |= (u32) data[off - init_off + 3] << 24;
		pci_user_write_config_dword(pdev, off, val);
		off += 4;
		size -= 4;
	}

	if (size >= 2) {
		u16 val = data[off - init_off];
		val |= (u16) data[off - init_off + 1] << 8;
		pci_user_write_config_word(pdev, off, val);
		off += 2;
		size -= 2;
	}

	if (size) {
		pci_user_write_config_byte(pdev, off, data[off - init_off]);
		off++;
		--size;
	}

	pci_config_pm_runtime_put(pdev);

	return count;
}
static BIN_ATTR_RW(config, 0);

static struct bin_attribute *pci_dev_config_attrs[] = {
	&bin_attr_config,
	NULL,
};

static umode_t pci_dev_config_attr_is_visible(struct kobject *kobj,
					      struct bin_attribute *a, int n)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));

	a->size = PCI_CFG_SPACE_SIZE;
	if (pdev->cfg_size > PCI_CFG_SPACE_SIZE)
		a->size = PCI_CFG_SPACE_EXP_SIZE;

	return a->attr.mode;
}

static const struct attribute_group pci_dev_config_attr_group = {
	.bin_attrs = pci_dev_config_attrs,
	.is_bin_visible = pci_dev_config_attr_is_visible,
};

/**
 * rom_read - read a PCI ROM
 * @filp: sysfs file
 * @kobj: kernel object handle
 * @bin_attr: struct bin_attribute for this file
 * @buf: where to put the data we read from the ROM
 * @off: file offset
 * @count: number of bytes to read
 *
 * Put @count bytes starting at @off into @buf from the ROM in the PCI
 * device corresponding to @kobj.
 */
static ssize_t rom_read(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr, char *buf,
			loff_t off, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	void __iomem *rom;
	size_t size;

	if (!pdev->rom_attr_enabled)
		return -EINVAL;

	rom = pci_map_rom(pdev, &size);	/* size starts out as PCI window size */
	if (!rom || !size)
		return -EIO;

	if (off >= size) {
		count = 0;
	} else {
		if (off + count > size)
			count = size - off;

		memcpy_fromio(buf, rom + off, count);
	}
	pci_unmap_rom(pdev, rom);

	return count;
}

/**
 * rom_write - used to enable access to the PCI ROM display
 * @filp: sysfs file
 * @kobj: kernel object handle
 * @bin_attr: struct bin_attribute for this file
 * @buf: user input
 * @off: file offset
 * @count: number of byte in input
 *
 * writing anything except 0 enables it
 */
static ssize_t rom_write(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *bin_attr, char *buf,
			 loff_t off, size_t count)
{
	bool allowed;
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));

	if (kstrtobool(buf, &allowed) < 0)
		return -EINVAL;

	pdev->rom_attr_enabled = !!allowed;

	return count;
}
static BIN_ATTR_ADMIN_RW(rom, 0);

static struct bin_attribute *pci_dev_rom_attrs[] = {
	&bin_attr_rom,
	NULL,
};

static umode_t pci_dev_rom_attr_is_visible(struct kobject *kobj,
					   struct bin_attribute *a, int n)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	size_t rom_size;

	/* If the device has a ROM, try to expose it in sysfs. */
	rom_size = pci_resource_len(pdev, PCI_ROM_RESOURCE);
	if (!rom_size)
		return 0;

	a->size = rom_size;

	return a->attr.mode;
}

static const struct attribute_group pci_dev_rom_attr_group = {
	.bin_attrs = pci_dev_rom_attrs,
	.is_bin_visible = pci_dev_rom_attr_is_visible,
};

/*
 * PCI Bus Class Devices
 */
static ssize_t cpuaffinity_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pci_bus *bus = to_pci_bus(dev);
	const struct cpumask *cpumask = cpumask_of_pcibus(bus);

	return cpumap_print_to_pagebuf(false, buf, cpumask);
}
static DEVICE_ATTR_RO(cpuaffinity);

static ssize_t cpulistaffinity_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct pci_bus *bus = to_pci_bus(dev);
	const struct cpumask *cpumask = cpumask_of_pcibus(bus);

	return cpumap_print_to_pagebuf(true, buf, cpumask);
}
static DEVICE_ATTR_RO(cpulistaffinity);

static ssize_t max_link_speed_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "%s\n",
			  pci_speed_string(pcie_get_speed_cap(pdev)));
}
static DEVICE_ATTR_RO(max_link_speed);

static ssize_t max_link_width_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sysfs_emit(buf, "%u\n", pcie_get_width_cap(pdev));
}
static DEVICE_ATTR_RO(max_link_width);

static ssize_t current_link_speed_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	enum pci_bus_speed speed;

	pcie_bandwidth_available(pdev, NULL, &speed, NULL);

	return sysfs_emit(buf, "%s\n", pci_speed_string(speed));
}
static DEVICE_ATTR_RO(current_link_speed);

static ssize_t current_link_width_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	enum pcie_link_width width;

	pcie_bandwidth_available(pdev, NULL, NULL, &width);

	return sysfs_emit(buf, "%u\n", width);
}
static DEVICE_ATTR_RO(current_link_width);

static ssize_t secondary_bus_number_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	u8 sec_bus;
	int err;

	err = pci_read_config_byte(pdev, PCI_SECONDARY_BUS, &sec_bus);
	if (err)
		return -EINVAL;

	return sysfs_emit(buf, "%u\n", sec_bus);
}
static DEVICE_ATTR_RO(secondary_bus_number);

static ssize_t subordinate_bus_number_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	u8 sub_bus;
	int err;

	err = pci_read_config_byte(pdev, PCI_SUBORDINATE_BUS, &sub_bus);
	if (err)
		return -EINVAL;

	return sysfs_emit(buf, "%u\n", sub_bus);
}
static DEVICE_ATTR_RO(subordinate_bus_number);

static ssize_t rescan_store(struct bus_type *bus, const char *buf, size_t count)
{
	bool rescan;
	struct pci_bus *b = NULL;

	if (kstrtobool(buf, &rescan) < 0)
		return -EINVAL;

	if (rescan) {
		pci_lock_rescan_remove();
		while ((b = pci_find_next_bus(b)) != NULL)
			pci_rescan_bus(b);
		pci_unlock_rescan_remove();
	}

	return count;
}
static BUS_ATTR_WO(rescan);

static struct attribute *pci_bus_attrs[] = {
	&bus_attr_rescan.attr,
	NULL,
};

static const struct attribute_group pci_bus_group = {
	.attrs = pci_bus_attrs,
};

const struct attribute_group *pci_bus_groups[] = {
	&pci_bus_group,
	NULL,
};

static ssize_t dev_rescan_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	bool rescan;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (kstrtobool(buf, &rescan) < 0)
		return -EINVAL;

	if (rescan) {
		pci_lock_rescan_remove();
		pci_rescan_bus(pdev->bus);
		pci_unlock_rescan_remove();
	}

	return count;
}
static struct device_attribute dev_attr_dev_rescan = __ATTR(rescan, 0200, NULL,
							    dev_rescan_store);

static ssize_t remove_store(struct device *dev,
			    struct device_attribute *attr, const char *buf,
			    size_t count)
{
	bool remove;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (kstrtobool(buf, &remove) < 0)
		return -EINVAL;

	if (remove && device_remove_file_self(dev, attr))
		pci_stop_and_remove_bus_device_locked(pdev);

	return count;
}
static DEVICE_ATTR_IGNORE_LOCKDEP(remove, 0220, NULL, remove_store);

static ssize_t bus_rescan_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	bool rescan;
	struct pci_bus *bus = to_pci_bus(dev);

	if (kstrtobool(buf, &rescan) < 0)
		return -EINVAL;

	if (rescan) {
		pci_lock_rescan_remove();
		if (!pci_is_root_bus(bus) && list_empty(&bus->devices))
			pci_rescan_bus_bridge_resize(bus->self);
		else
			pci_rescan_bus(bus);
		pci_unlock_rescan_remove();
	}

	return count;
}
static struct device_attribute dev_attr_bus_rescan = __ATTR(rescan, 0200, NULL,
							    bus_rescan_store);

static struct attribute *pci_bridge_attrs[] = {
	&dev_attr_subordinate_bus_number.attr,
	&dev_attr_secondary_bus_number.attr,
	NULL,
};

static struct attribute *pcie_dev_attrs[] = {
	&dev_attr_current_link_speed.attr,
	&dev_attr_current_link_width.attr,
	&dev_attr_max_link_width.attr,
	&dev_attr_max_link_speed.attr,
	NULL,
};

static struct attribute *pcibus_attrs[] = {
	&dev_attr_bus_rescan.attr,
	&dev_attr_cpuaffinity.attr,
	&dev_attr_cpulistaffinity.attr,
	NULL,
};

static const struct attribute_group pcibus_group = {
	.attrs = pcibus_attrs,
};

const struct attribute_group *pcibus_groups[] = {
	&pcibus_group,
	NULL,
};

static ssize_t boot_vga_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_dev *vga_dev = vga_default_device();

	if (vga_dev)
		return sysfs_emit(buf, "%u\n", (pdev == vga_dev));

	return sysfs_emit(buf, "%u\n",
			  !!(pdev->resource[PCI_ROM_RESOURCE].flags &
			     IORESOURCE_ROM_SHADOW));
}
static DEVICE_ATTR_RO(boot_vga);

#ifdef HAVE_PCI_LEGACY
/**
 * pci_read_legacy_io - read byte(s) from legacy I/O port space
 * @filp: open sysfs file
 * @kobj: kobject corresponding to file to read from
 * @bin_attr: struct bin_attribute for this file
 * @buf: buffer to store results
 * @off: offset into legacy I/O port space
 * @count: number of bytes to read
 *
 * Reads 1, 2, or 4 bytes from legacy I/O port space using an arch specific
 * callback routine (pci_legacy_read).
 */
static ssize_t pci_read_legacy_io(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr, char *buf,
				  loff_t off, size_t count)
{
	struct pci_bus *bus = to_pci_bus(kobj_to_dev(kobj));

	/* Only support 1, 2 or 4 byte accesses */
	if (count != 1 && count != 2 && count != 4)
		return -EINVAL;

	return pci_legacy_read(bus, off, (u32 *)buf, count);
}

/**
 * pci_write_legacy_io - write byte(s) to legacy I/O port space
 * @filp: open sysfs file
 * @kobj: kobject corresponding to file to read from
 * @bin_attr: struct bin_attribute for this file
 * @buf: buffer containing value to be written
 * @off: offset into legacy I/O port space
 * @count: number of bytes to write
 *
 * Writes 1, 2, or 4 bytes from legacy I/O port space using an arch specific
 * callback routine (pci_legacy_write).
 */
static ssize_t pci_write_legacy_io(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *bin_attr, char *buf,
				   loff_t off, size_t count)
{
	struct pci_bus *bus = to_pci_bus(kobj_to_dev(kobj));

	/* Only support 1, 2 or 4 byte accesses */
	if (count != 1 && count != 2 && count != 4)
		return -EINVAL;

	return pci_legacy_write(bus, off, *(u32 *)buf, count);
}

/**
 * pci_mmap_legacy_mem - map legacy PCI memory into user memory space
 * @filp: open sysfs file
 * @kobj: kobject corresponding to device to be mapped
 * @bin_attr: struct bin_attribute for this file
 * @vma: struct vm_area_struct passed to mmap
 *
 * Uses an arch specific callback, pci_mmap_legacy_mem_page_range, to mmap
 * legacy memory space (first meg of bus space) into application virtual
 * memory space.
 */
static int pci_mmap_legacy_mem(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       struct vm_area_struct *vma)
{
	struct pci_bus *bus = to_pci_bus(kobj_to_dev(kobj));

	return pci_mmap_legacy_page_range(bus, vma, pci_mmap_mem);
}

/**
 * pci_mmap_legacy_io - map legacy PCI IO into user memory space
 * @filp: open sysfs file
 * @kobj: kobject corresponding to device to be mapped
 * @bin_attr: struct bin_attribute for this file
 * @vma: struct vm_area_struct passed to mmap
 *
 * Uses an arch specific callback, pci_mmap_legacy_io_page_range, to mmap
 * legacy IO space (first meg of bus space) into application virtual
 * memory space. Returns -ENOSYS if the operation isn't supported
 */
static int pci_mmap_legacy_io(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *bin_attr,
			      struct vm_area_struct *vma)
{
	struct pci_bus *bus = to_pci_bus(kobj_to_dev(kobj));

	return pci_mmap_legacy_page_range(bus, vma, pci_mmap_io);
}

/**
 * pci_adjust_legacy_attr - adjustment of legacy file attributes
 * @bus: bus to create files under
 * @mmap_type: I/O port or memory
 *
 * Stub implementation. Can be overridden by arch if necessary.
 */
void __weak pci_adjust_legacy_attr(struct pci_bus *bus,
				   enum pci_mmap_state mmap_type)
{
}

/**
 * pci_create_legacy_files - create legacy I/O port and memory files
 * @bus: bus to create files under
 *
 * Some platforms allow access to legacy I/O port and ISA memory space on
 * a per-bus basis.  This routine creates the files and ties them into
 * their associated read, write and mmap files from pci-sysfs.c
 *
 * On error unwind, but don't propagate the error to the caller
 * as it is ok to set up the PCI bus without these files.
 */
void pci_create_legacy_files(struct pci_bus *bus)
{
	int ret;

	if (!sysfs_initialized)
		return;

	bus->legacy_io = kcalloc(2, sizeof(struct bin_attribute),
				 GFP_ATOMIC);
	if (!bus->legacy_io)
		goto kzalloc_err;

	sysfs_bin_attr_init(bus->legacy_io);
	bus->legacy_io->attr.name = "legacy_io";
	bus->legacy_io->size = 0xffff;
	bus->legacy_io->attr.mode = 0600;
	bus->legacy_io->read = pci_read_legacy_io;
	bus->legacy_io->write = pci_write_legacy_io;
	bus->legacy_io->mmap = pci_mmap_legacy_io;
	bus->legacy_io->mapping = iomem_get_mapping();

	pci_adjust_legacy_attr(bus, pci_mmap_io);

	ret = device_create_bin_file(&bus->dev, bus->legacy_io);
	if (ret)
		goto legacy_io_err;

	/* Allocated above after the legacy_io struct */
	bus->legacy_mem = bus->legacy_io + 1;

	sysfs_bin_attr_init(bus->legacy_mem);
	bus->legacy_mem->attr.name = "legacy_mem";
	bus->legacy_mem->size = 1024*1024;
	bus->legacy_mem->attr.mode = 0600;
	bus->legacy_mem->mmap = pci_mmap_legacy_mem;
	bus->legacy_io->mapping = iomem_get_mapping();

	pci_adjust_legacy_attr(bus, pci_mmap_mem);

	ret = device_create_bin_file(&bus->dev, bus->legacy_mem);
	if (ret)
		goto legacy_mem_err;

	return;

legacy_mem_err:
	device_remove_bin_file(&bus->dev, bus->legacy_io);
legacy_io_err:
	kfree(bus->legacy_io);
	bus->legacy_io = NULL;
kzalloc_err:
	dev_warn(&bus->dev, "could not create legacy I/O port and ISA memory resources in sysfs\n");
}

void pci_remove_legacy_files(struct pci_bus *bus)
{
	if (bus->legacy_io) {
		device_remove_bin_file(&bus->dev, bus->legacy_io);
		device_remove_bin_file(&bus->dev, bus->legacy_mem);
		kfree(bus->legacy_io); /* both are allocated here */
	}
}
#endif /* HAVE_PCI_LEGACY */

#if defined(HAVE_PCI_MMAP) || defined(ARCH_GENERIC_PCI_MMAP_RESOURCE)
int pci_mmap_fits(struct pci_dev *pdev, int resno, struct vm_area_struct *vma,
		  enum pci_mmap_api mmap_api)
{
	unsigned long nr, start, size;
	resource_size_t pci_start = 0, pci_end;

	if (pci_resource_len(pdev, resno) == 0)
		return 0;

	nr = vma_pages(vma);
	start = vma->vm_pgoff;
	size = ((pci_resource_len(pdev, resno) - 1) >> PAGE_SHIFT) + 1;

	if (mmap_api == PCI_MMAP_PROCFS) {
		pci_resource_to_user(pdev, resno, &pdev->resource[resno],
				     &pci_start, &pci_end);
		pci_start >>= PAGE_SHIFT;
	}

	if (start >= pci_start && start < pci_start + size &&
			start + nr <= pci_start + size)
		return 1;

	return 0;
}

/**
 * pci_mmap_resource - map a PCI resource into user memory space
 * @kobj: kobject for mapping
 * @bin_attr: struct bin_attribute for the file being mapped
 * @vma: struct vm_area_struct passed into the mmap
 * @write_combine: 1 for write_combine mapping
 *
 * Use the regular PCI mapping routines to map a PCI resource into userspace.
 */
static int pci_mmap_resource(struct kobject *kobj,
			     struct bin_attribute *bin_attr,
			     struct vm_area_struct *vma, int write_combine)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	int bar = (unsigned long)bin_attr->private;
	struct resource *res = &pdev->resource[bar];
	enum pci_mmap_state mmap_type;
	int ret;

	ret = security_locked_down(LOCKDOWN_PCI_ACCESS);
	if (ret)
		return ret;

	if (res->flags & IORESOURCE_MEM && iomem_is_exclusive(res->start))
		return -EINVAL;

	if (!pci_mmap_fits(pdev, bar, vma, PCI_MMAP_SYSFS))
		return -EINVAL;

	mmap_type = pci_mmap_io;
	if (res->flags & IORESOURCE_MEM)
		mmap_type = pci_mmap_mem;

	return pci_mmap_resource_range(pdev, bar, vma, mmap_type, write_combine);
}

static int pci_mmap_resource_uc(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				struct vm_area_struct *vma)
{
	return pci_mmap_resource(kobj, bin_attr, vma, 0);
}

static int pci_mmap_resource_wc(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				struct vm_area_struct *vma)
{
	return pci_mmap_resource(kobj, bin_attr, vma, 1);
}

static ssize_t pci_resource_io(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t off, size_t count, bool write)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	int bar = (unsigned long)bin_attr->private;
	unsigned long port = off;

	port += pci_resource_start(pdev, bar);

	if (port > pci_resource_end(pdev, bar))
		return 0;

	if (port + count - 1 > pci_resource_end(pdev, bar))
		return -EINVAL;

	switch (count) {
	case 1:
		if (write)
			outb(*(u8 *)buf, port);
		else
			*(u8 *)buf = inb(port);
		return 1;
	case 2:
		if (write)
			outw(*(u16 *)buf, port);
		else
			*(u16 *)buf = inw(port);
		return 2;
	case 4:
		if (write)
			outl(*(u32 *)buf, port);
		else
			*(u32 *)buf = inl(port);
		return 4;
	}

	return -EINVAL;
}

static ssize_t pci_read_resource_io(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *bin_attr, char *buf,
				    loff_t off, size_t count)
{
	return pci_resource_io(filp, kobj, bin_attr, buf, off, count, false);
}

static ssize_t pci_write_resource_io(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *bin_attr, char *buf,
				     loff_t off, size_t count)
{
	size_t ret;

	ret = security_locked_down(LOCKDOWN_PCI_ACCESS);
	if (ret)
		return ret;

	return pci_resource_io(filp, kobj, bin_attr, buf, off, count, true);
}

/**
 * pci_remove_resource_files - cleanup resource files
 * @pdev: dev to cleanup
 *
 * If we created resource files for @pdev, remove them from sysfs and
 * free their resources.
 */
static void pci_remove_resource_files(struct pci_dev *pdev)
{
	int i;
	struct bin_attribute *res_attr;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		res_attr = pdev->res_attr[i];
		if (res_attr) {
			sysfs_remove_bin_file(&pdev->dev.kobj, res_attr);
			kfree(res_attr);
		}

		res_attr = pdev->res_attr_wc[i];
		if (res_attr) {
			sysfs_remove_bin_file(&pdev->dev.kobj, res_attr);
			kfree(res_attr);
		}
	}
}

static int pci_create_attr(struct pci_dev *pdev, int resno, int write_combine)
{
	/* allocate attribute structure, piggyback attribute name */
	int name_len = write_combine ? 13 : 10;
	struct bin_attribute *res_attr;
	char *res_attr_name;
	int ret;

	res_attr = kzalloc(sizeof(*res_attr) + name_len, GFP_ATOMIC);
	if (!res_attr)
		return -ENOMEM;

	res_attr_name = (char *)(res_attr + 1);

	sysfs_bin_attr_init(res_attr);
	if (write_combine) {
		pdev->res_attr_wc[resno] = res_attr;
		sprintf(res_attr_name, "resource%d_wc", resno);
		res_attr->mmap = pci_mmap_resource_wc;
	} else {
		pdev->res_attr[resno] = res_attr;
		sprintf(res_attr_name, "resource%d", resno);
		if (pci_resource_flags(pdev, resno) & IORESOURCE_IO) {
			res_attr->read = pci_read_resource_io;
			res_attr->write = pci_write_resource_io;
			if (arch_can_pci_mmap_io())
				res_attr->mmap = pci_mmap_resource_uc;
		} else {
			res_attr->mmap = pci_mmap_resource_uc;
		}
	}

	if (res_attr->mmap)
		res_attr->mapping = iomem_get_mapping();

	res_attr->attr.name = res_attr_name;
	res_attr->attr.mode = 0600;
	res_attr->size = pci_resource_len(pdev, resno);
	res_attr->private = (void *)(unsigned long)resno;

	ret = sysfs_create_bin_file(&pdev->dev.kobj, res_attr);
	if (ret)
		kfree(res_attr);

	return ret;
}

/**
 * pci_create_resource_files - create resource files in sysfs for @dev
 * @pdev: dev in question
 *
 * Walk the resources in @pdev creating files for each resource available.
 */
static int pci_create_resource_files(struct pci_dev *pdev)
{
	int i, ret;

	/* Expose the PCI resources from this device as files */
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		/* skip empty resources */
		if (!pci_resource_len(pdev, i))
			continue;

		ret = pci_create_attr(pdev, i, 0);
		/* for prefetchable resources, create a WC mappable file */
		if (!ret && arch_can_pci_mmap_wc() &&
		    pdev->resource[i].flags & IORESOURCE_PREFETCH)
			ret = pci_create_attr(pdev, i, 1);

		if (ret) {
			pci_remove_resource_files(pdev);
			return ret;
		}
	}

	return 0;
}
#else /* !(defined(HAVE_PCI_MMAP) || defined(ARCH_GENERIC_PCI_MMAP_RESOURCE)) */
int __weak pci_create_resource_files(struct pci_dev *pdev) { return 0; }
void __weak pci_remove_resource_files(struct pci_dev *pdev) { return; }
#endif

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr, const char *buf,
			   size_t count)
{
	bool reset;
	struct pci_dev *pdev = to_pci_dev(dev);
	ssize_t ret;

	if (kstrtobool(buf, &reset) < 0)
		return -EINVAL;

	if (!reset)
		return -EINVAL;

	pm_runtime_get_sync(dev);
	ret = pci_reset_function(pdev);
	pm_runtime_put(dev);
	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(reset);

static struct attribute *pci_dev_reset_attrs[] = {
	&dev_attr_reset.attr,
	NULL,
};

static umode_t pci_dev_reset_attr_is_visible(struct kobject *kobj,
					     struct attribute *a, int n)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));

	if (!pdev->reset_fn)
		return 0;

	return a->mode;
}

static const struct attribute_group pci_dev_reset_attr_group = {
	.attrs = pci_dev_reset_attrs,
	.is_visible = pci_dev_reset_attr_is_visible,
};

int __must_check pci_create_sysfs_dev_files(struct pci_dev *pdev)
{
	if (!sysfs_initialized)
		return -EACCES;

	return pci_create_resource_files(pdev);
}

/**
 * pci_remove_sysfs_dev_files - cleanup PCI specific sysfs files
 * @pdev: device whose entries we should free
 *
 * Cleanup when @pdev is removed from sysfs.
 */
void pci_remove_sysfs_dev_files(struct pci_dev *pdev)
{
	if (!sysfs_initialized)
		return;

	pci_remove_resource_files(pdev);
}

static int __init pci_sysfs_init(void)
{
	struct pci_dev *pdev = NULL;
	struct pci_bus *bus = NULL;
	int ret;

	sysfs_initialized = 1;
	for_each_pci_dev(pdev) {
		ret = pci_create_sysfs_dev_files(pdev);
		if (ret) {
			pci_dev_put(pdev);
			return ret;
		}
	}

	while ((bus = pci_find_next_bus(bus)))
		pci_create_legacy_files(bus);

	return 0;
}
late_initcall(pci_sysfs_init);

static struct attribute *pci_dev_dev_attrs[] = {
	&dev_attr_boot_vga.attr,
	NULL,
};

static umode_t pci_dev_attr_is_visible(struct kobject *kobj,
				       struct attribute *a, int n)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));

	if (a == &dev_attr_boot_vga.attr)
		if ((pdev->class >> 8) != PCI_CLASS_DISPLAY_VGA)
			return 0;

	return a->mode;
}

static struct attribute *pci_dev_hp_attrs[] = {
	&dev_attr_remove.attr,
	&dev_attr_dev_rescan.attr,
	NULL,
};

static umode_t pci_dev_hp_attr_is_visible(struct kobject *kobj,
					  struct attribute *a, int n)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));

	if (pdev->is_virtfn)
		return 0;

	return a->mode;
}

static umode_t pci_bridge_attr_is_visible(struct kobject *kobj,
					  struct attribute *a, int n)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));

	if (!pci_is_bridge(pdev))
		return 0;

	return a->mode;
}

static umode_t pcie_dev_attr_is_visible(struct kobject *kobj,
					struct attribute *a, int n)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));

	if (!pci_is_pcie(pdev))
		return 0;

	return a->mode;
}

const struct attribute_group *pci_dev_groups[] = {
	&pci_dev_group,
	&pci_dev_config_attr_group,
	&pci_dev_rom_attr_group,
	&pci_dev_reset_attr_group,
	&vpd_attr_group,
#ifdef CONFIG_DMI
	&smbios_attr_group,
#endif
#ifdef CONFIG_ACPI
	&acpi_attr_group,
#endif
	NULL,
};

static const struct attribute_group pci_dev_hp_attr_group = {
	.attrs = pci_dev_hp_attrs,
	.is_visible = pci_dev_hp_attr_is_visible,
};

static const struct attribute_group pci_dev_attr_group = {
	.attrs = pci_dev_dev_attrs,
	.is_visible = pci_dev_attr_is_visible,
};

static const struct attribute_group pci_bridge_attr_group = {
	.attrs = pci_bridge_attrs,
	.is_visible = pci_bridge_attr_is_visible,
};

static const struct attribute_group pcie_dev_attr_group = {
	.attrs = pcie_dev_attrs,
	.is_visible = pcie_dev_attr_is_visible,
};

static const struct attribute_group *pci_dev_attr_groups[] = {
	&pci_dev_attr_group,
	&pci_dev_hp_attr_group,
#ifdef CONFIG_PCI_IOV
	&sriov_dev_attr_group,
#endif
	&pci_bridge_attr_group,
	&pcie_dev_attr_group,
#ifdef CONFIG_PCIEAER
	&aer_stats_attr_group,
#endif
#ifdef CONFIG_PCIEASPM
	&aspm_ctrl_attr_group,
#endif
	NULL,
};

const struct device_type pci_dev_type = {
	.groups = pci_dev_attr_groups,
};
