// SPDX-License-Identifier: GPL-2.0-only
/*
 * RISC-V specific functions to support DMA for non-coherent devices
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/libfdt.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <asm/dma-noncoherent.h>

static struct riscv_dma_cache_sync *dma_cache_sync;
unsigned long riscv_dma_uc_offset;

static void __dma_sync(phys_addr_t paddr, size_t size, enum dma_data_direction dir)
{
	if ((dir == DMA_FROM_DEVICE) && (dma_cache_sync->cache_invalidate))
		dma_cache_sync->cache_invalidate(paddr, size);
	else if ((dir == DMA_TO_DEVICE) && (dma_cache_sync->cache_clean))
		dma_cache_sync->cache_clean(paddr, size);
	else if ((dir == DMA_BIDIRECTIONAL) && dma_cache_sync->cache_flush)
		dma_cache_sync->cache_flush(paddr, size);
}

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size, enum dma_data_direction dir)
{
	if (!dma_cache_sync)
		return;

	__dma_sync(paddr, size, dir);
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size, enum dma_data_direction dir)
{
	if (!dma_cache_sync || dir == DMA_TO_DEVICE)
		return;

	__dma_sync(paddr, size, DMA_FROM_DEVICE);
}

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
		const struct iommu_ops *iommu, bool coherent)
{
	/* If a specific device is dma-coherent, set it here */
	dev->dma_coherent = coherent;
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	void *flush_addr = page_address(page);

	memset(flush_addr, 0, size);
	if (dma_cache_sync && dma_cache_sync->cache_flush)
		dma_cache_sync->cache_flush(__pa(flush_addr), size);
}

void riscv_dma_cache_sync_set(struct riscv_dma_cache_sync *ops)
{
	dma_cache_sync = ops;
}
