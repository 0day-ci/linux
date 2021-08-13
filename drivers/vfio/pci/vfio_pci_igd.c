// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO PCI Intel Graphics support
 *
 * Copyright (C) 2016 Red Hat, Inc.  All rights reserved.
 *	Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * Register a device specific region through which to provide read-only
 * access to the Intel IGD opregion.  The register defining the opregion
 * address is also virtualized to prevent user modification.
 */

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>

#include "vfio_pci_private.h"

#define OPREGION_SIGNATURE	"IntelGraphicsMem"
#define OPREGION_SIZE		(8 * 1024)
#define OPREGION_PCI_ADDR	0xfc

#define OPREGION_RVDA		0x3ba
#define OPREGION_RVDS		0x3c2
#define OPREGION_VERSION	0x16

static ssize_t vfio_pci_igd_rw(struct vfio_pci_device *vdev, char __user *buf,
			       size_t count, loff_t *ppos, bool iswrite)
{
	unsigned int i = VFIO_PCI_OFFSET_TO_INDEX(*ppos) - VFIO_PCI_NUM_REGIONS;
	void *base = vdev->region[i].data;
	loff_t pos = *ppos & VFIO_PCI_OFFSET_MASK;

	if (pos >= vdev->region[i].size || iswrite)
		return -EINVAL;

	count = min(count, (size_t)(vdev->region[i].size - pos));

	if (copy_to_user(buf, base + pos, count))
		return -EFAULT;

	*ppos += count;

	return count;
}

static void vfio_pci_igd_release(struct vfio_pci_device *vdev,
				 struct vfio_pci_region *region)
{
	if (is_ioremap_addr(region->data))
		memunmap(region->data);
	else
		kfree(region->data);
}

static const struct vfio_pci_regops vfio_pci_igd_regops = {
	.rw		= vfio_pci_igd_rw,
	.release	= vfio_pci_igd_release,
};

static int vfio_pci_igd_opregion_init(struct vfio_pci_device *vdev)
{
	__le32 *dwordp = (__le32 *)(vdev->vconfig + OPREGION_PCI_ADDR);
	u32 addr, size, rvds = 0;
	void *base, *opregionvbt;
	int ret;
	u16 version;
	u64 rvda = 0;

	ret = pci_read_config_dword(vdev->pdev, OPREGION_PCI_ADDR, &addr);
	if (ret)
		return ret;

	if (!addr || !(~addr))
		return -ENODEV;

	base = memremap(addr, OPREGION_SIZE, MEMREMAP_WB);
	if (!base)
		return -ENOMEM;

	if (memcmp(base, OPREGION_SIGNATURE, 16)) {
		memunmap(base);
		return -EINVAL;
	}

	size = le32_to_cpu(*(__le32 *)(base + 16));
	if (!size) {
		memunmap(base);
		return -EINVAL;
	}

	size *= 1024; /* In KB */

	/*
	 * OpRegion and VBT:
	 * When VBT data doesn't exceed 6KB, it's stored in Mailbox #4.
	 * When VBT data exceeds 6KB size, Mailbox #4 is no longer large enough
	 * to hold the VBT data, the Extended VBT region is introduced since
	 * OpRegion 2.0 to hold the VBT data. Since OpRegion 2.0, RVDA/RVDS are
	 * introduced to define the extended VBT data location and size.
	 * OpRegion 2.0: RVDA defines the absolute physical address of the
	 *   extended VBT data, RVDS defines the VBT data size.
	 * OpRegion 2.1 and above: RVDA defines the relative address of the
	 *   extended VBT data to OpRegion base, RVDS defines the VBT data size.
	 *
	 * Due to the RVDA difference in OpRegion VBT (also the only diff between
	 * 2.0 and 2.1), while for OpRegion 2.1 and above it's possible to map
	 * a contigious memory to expose OpRegion and VBT r/w via the vfio
	 * region, for OpRegion 2.0 shadow and amendment mechanism is used to
	 * expose OpRegion and VBT r/w properly. So that from r/w ops view, only
	 * OpRegion 2.1 is exposed regardless underneath Region is 2.0 or 2.1.
	 */
	version = le16_to_cpu(*(__le16 *)(base + OPREGION_VERSION));

	if (version >= 0x0200) {
		rvda = le64_to_cpu(*(__le64 *)(base + OPREGION_RVDA));
		rvds = le32_to_cpu(*(__le32 *)(base + OPREGION_RVDS));

		/* The extended VBT is valid only when RVDA/RVDS are non-zero. */
		if (rvda && rvds) {
			size += rvds;
		}

		/* The extended VBT must follows OpRegion for OpRegion 2.1+ */
		if (rvda != size && version > 0x0200) {
			memunmap(base);
			pci_err(vdev->pdev,
				"Extended VBT does not follow opregion on version 0x%04x\n",
				version);
			return -EINVAL;
		}
	}

	if (size != OPREGION_SIZE) {
		/* Allocate memory for OpRegion and extended VBT for 2.0 */
		if (rvda && rvds && version == 0x0200) {
			void *vbt_base;

			vbt_base = memremap(rvda, rvds, MEMREMAP_WB);
			if (!vbt_base) {
				memunmap(base);
				return -ENOMEM;
			}

			opregionvbt = kzalloc(size, GFP_KERNEL);
			if (!opregionvbt) {
				memunmap(base);
				memunmap(vbt_base);
				return -ENOMEM;
			}

			/* Stitch VBT after OpRegion noncontigious */
			memcpy(opregionvbt, base, OPREGION_SIZE);
			memcpy(opregionvbt + OPREGION_SIZE, vbt_base, rvds);

			/* Patch OpRegion 2.0 to 2.1 */
			*(__le16 *)(opregionvbt + OPREGION_VERSION) = 0x0201;
			/* Patch RVDA to relative address after OpRegion */
			*(__le64 *)(opregionvbt + OPREGION_RVDA) = OPREGION_SIZE;

			memunmap(vbt_base);
			memunmap(base);

			/* Register shadow instead of map as vfio_region */
			base = opregionvbt;
		/* Remap OpRegion + extended VBT for 2.1+ */
		} else {
			memunmap(base);
			base = memremap(addr, size, MEMREMAP_WB);
			if (!base)
				return -ENOMEM;
		}
	}

	ret = vfio_pci_register_dev_region(vdev,
		PCI_VENDOR_ID_INTEL | VFIO_REGION_TYPE_PCI_VENDOR_TYPE,
		VFIO_REGION_SUBTYPE_INTEL_IGD_OPREGION,
		&vfio_pci_igd_regops, size, VFIO_REGION_INFO_FLAG_READ, base);
	if (ret) {
		if (is_ioremap_addr(base))
			memunmap(base);
		else
			kfree(base);
		return ret;
	}

	/* Fill vconfig with the hw value and virtualize register */
	*dwordp = cpu_to_le32(addr);
	memset(vdev->pci_config_map + OPREGION_PCI_ADDR,
	       PCI_CAP_ID_INVALID_VIRT, 4);

	return ret;
}

static ssize_t vfio_pci_igd_cfg_rw(struct vfio_pci_device *vdev,
				   char __user *buf, size_t count, loff_t *ppos,
				   bool iswrite)
{
	unsigned int i = VFIO_PCI_OFFSET_TO_INDEX(*ppos) - VFIO_PCI_NUM_REGIONS;
	struct pci_dev *pdev = vdev->region[i].data;
	loff_t pos = *ppos & VFIO_PCI_OFFSET_MASK;
	size_t size;
	int ret;

	if (pos >= vdev->region[i].size || iswrite)
		return -EINVAL;

	size = count = min(count, (size_t)(vdev->region[i].size - pos));

	if ((pos & 1) && size) {
		u8 val;

		ret = pci_user_read_config_byte(pdev, pos, &val);
		if (ret)
			return ret;

		if (copy_to_user(buf + count - size, &val, 1))
			return -EFAULT;

		pos++;
		size--;
	}

	if ((pos & 3) && size > 2) {
		u16 val;

		ret = pci_user_read_config_word(pdev, pos, &val);
		if (ret)
			return ret;

		val = cpu_to_le16(val);
		if (copy_to_user(buf + count - size, &val, 2))
			return -EFAULT;

		pos += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val;

		ret = pci_user_read_config_dword(pdev, pos, &val);
		if (ret)
			return ret;

		val = cpu_to_le32(val);
		if (copy_to_user(buf + count - size, &val, 4))
			return -EFAULT;

		pos += 4;
		size -= 4;
	}

	while (size >= 2) {
		u16 val;

		ret = pci_user_read_config_word(pdev, pos, &val);
		if (ret)
			return ret;

		val = cpu_to_le16(val);
		if (copy_to_user(buf + count - size, &val, 2))
			return -EFAULT;

		pos += 2;
		size -= 2;
	}

	while (size) {
		u8 val;

		ret = pci_user_read_config_byte(pdev, pos, &val);
		if (ret)
			return ret;

		if (copy_to_user(buf + count - size, &val, 1))
			return -EFAULT;

		pos++;
		size--;
	}

	*ppos += count;

	return count;
}

static void vfio_pci_igd_cfg_release(struct vfio_pci_device *vdev,
				     struct vfio_pci_region *region)
{
	struct pci_dev *pdev = region->data;

	pci_dev_put(pdev);
}

static const struct vfio_pci_regops vfio_pci_igd_cfg_regops = {
	.rw		= vfio_pci_igd_cfg_rw,
	.release	= vfio_pci_igd_cfg_release,
};

static int vfio_pci_igd_cfg_init(struct vfio_pci_device *vdev)
{
	struct pci_dev *host_bridge, *lpc_bridge;
	int ret;

	host_bridge = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (!host_bridge)
		return -ENODEV;

	if (host_bridge->vendor != PCI_VENDOR_ID_INTEL ||
	    host_bridge->class != (PCI_CLASS_BRIDGE_HOST << 8)) {
		pci_dev_put(host_bridge);
		return -EINVAL;
	}

	ret = vfio_pci_register_dev_region(vdev,
		PCI_VENDOR_ID_INTEL | VFIO_REGION_TYPE_PCI_VENDOR_TYPE,
		VFIO_REGION_SUBTYPE_INTEL_IGD_HOST_CFG,
		&vfio_pci_igd_cfg_regops, host_bridge->cfg_size,
		VFIO_REGION_INFO_FLAG_READ, host_bridge);
	if (ret) {
		pci_dev_put(host_bridge);
		return ret;
	}

	lpc_bridge = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0x1f, 0));
	if (!lpc_bridge)
		return -ENODEV;

	if (lpc_bridge->vendor != PCI_VENDOR_ID_INTEL ||
	    lpc_bridge->class != (PCI_CLASS_BRIDGE_ISA << 8)) {
		pci_dev_put(lpc_bridge);
		return -EINVAL;
	}

	ret = vfio_pci_register_dev_region(vdev,
		PCI_VENDOR_ID_INTEL | VFIO_REGION_TYPE_PCI_VENDOR_TYPE,
		VFIO_REGION_SUBTYPE_INTEL_IGD_LPC_CFG,
		&vfio_pci_igd_cfg_regops, lpc_bridge->cfg_size,
		VFIO_REGION_INFO_FLAG_READ, lpc_bridge);
	if (ret) {
		pci_dev_put(lpc_bridge);
		return ret;
	}

	return 0;
}

int vfio_pci_igd_init(struct vfio_pci_device *vdev)
{
	int ret;

	ret = vfio_pci_igd_opregion_init(vdev);
	if (ret)
		return ret;

	ret = vfio_pci_igd_cfg_init(vdev);
	if (ret)
		return ret;

	return 0;
}
