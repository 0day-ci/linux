/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019 Intel Corporation */

#ifndef _IECM_MEM_H_
#define _IECM_MEM_H_

#include <linux/io.h>

struct iecm_dma_mem {
	void *va;
	dma_addr_t pa;
	size_t size;
};

#define wr32(a, reg, value)	writel((value), ((a)->hw_addr + (reg)))
#define rd32(a, reg)		readl((a)->hw_addr + (reg))
#define wr64(a, reg, value)	writeq((value), ((a)->hw_addr + (reg)))
#define rd64(a, reg)		readq((a)->hw_addr + (reg))

#endif /* _IECM_MEM_H_ */
