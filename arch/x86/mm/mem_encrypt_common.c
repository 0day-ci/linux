// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memory Encryption Support Common Code
 *
 * Copyright (C) 2021 Intel Corporation
 *
 * Author: Kuppuswamy Sathyanarayanan <sathyanarayanan.kuppuswamy@linux.intel.com>
 */

#include <linux/mem_encrypt.h>
#include <linux/dma-mapping.h>

bool amd_force_dma_unencrypted(struct device *dev);

/* Override for DMA direct allocation check - ARCH_HAS_FORCE_DMA_UNENCRYPTED */
bool force_dma_unencrypted(struct device *dev)
{
	if (sev_active() || sme_active())
		return amd_force_dma_unencrypted(dev);

	return false;
}
