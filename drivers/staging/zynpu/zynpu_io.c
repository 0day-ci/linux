// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_io.c
 * Implementations of ZYNPU IO R/W API
 */

#include <asm/io.h>
#include "zynpu_io.h"

struct io_region *zynpu_create_ioregion(struct device *dev, u64 phys_base, u32 size)
{
	struct io_region *region = NULL;

	if ((!size) || (!dev)) {
		dev_dbg(dev, "invalid input args size/dev!");
		goto fail;
	}

	region = devm_kzalloc(dev, sizeof(struct io_region), GFP_KERNEL);
	if (!region)
		goto fail;

	if (!request_mem_region(phys_base, size, "zynpu")) {
		dev_err(dev, "request IO region failed");
		goto fail;
	}

	region->kern = ioremap(phys_base, size);
	if (!region->kern) {
		dev_err(dev, "ioremap failed");
		goto fail;
	}

	region->phys = phys_base;
	region->size = size;

	/* success */
	goto finish;

fail:
	dev_err(dev, "creating IO region [0x%llx, 0x%llx] failed",
		phys_base, phys_base + size - 1);

finish:
	return region;
}

void zynpu_destroy_ioregion(struct io_region *region)
{
	if (region && region->kern) {
		iounmap(region->kern);
		release_mem_region(region->phys, region->size);
		region->kern = NULL;
		region->phys = 0;
		region->size = 0;
	}
}

u8 zynpu_read8(struct io_region *region, __IO offset)
{
	if (region && region->kern && (offset < region->size))
		return readb((void __iomem *)((__IO)(region->kern) + offset));
	else {
		pr_err("KMD io error: read8 invalid operation or args!");
		return 0;
	}
}

u16 zynpu_read16(struct io_region *region, __IO offset)
{
	if (region && region->kern && (offset < region->size))
		return readw((void __iomem *)((__IO)(region->kern) + offset));
	else {
		pr_err("KMD io error: read16 invalid operation or args!");
		return 0;
	}
}

u32 zynpu_read32(struct io_region *region, __IO offset)
{
	if (region && region->kern && (offset < region->size))
		return readl((void __iomem *)((__IO)(region->kern) + offset));
	else {
		if (region) {
			pr_err("KMD io error: read32 invalid operation or args! \
			    (offset 0x%lx, region_max 0x%lx)",
			    (unsigned long)offset, (unsigned long)region->size);
		} else {
			pr_err("KMD io error: read32 invalid args to be NULL!");
		}
		return 0;
	}
}

void zynpu_write8(struct io_region *region, __IO offset, unsigned int data)
{
	data = data & 0xFF;
	if (region && region->kern && (offset < region->size))
		return writeb((u8)data, (void __iomem *)((__IO)(region->kern) + offset));
	else
		pr_err("KMD io error: write8 invalid operation or args!");
}

void zynpu_write16(struct io_region *region, __IO offset, unsigned int data)
{
	data = data & 0xFFFF;
	if (region && region->kern && (offset < region->size))
		return writew((u16)data, (void __iomem *)((__IO)(region->kern) + offset));
	else
		pr_err("KMD io error: write16 invalid operation or args!");
}

void zynpu_write32(struct io_region *region, __IO offset, unsigned int data)
{
	if (region && region->kern && (offset < region->size))
		return writel((u32)data, (void __iomem *)((__IO)(region->kern) + offset));
	else {
		if (region)
			pr_err("KMD io error: write32 invalid operation or args! \
			(offset 0x%lx, region_max 0x%lx)",
			(unsigned long)offset, (unsigned long)region->size);
		else
			pr_err("KMD io error: write32 invalid args to be NULL!");
	}
}