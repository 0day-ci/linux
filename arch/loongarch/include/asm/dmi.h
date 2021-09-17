/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_DMI_H
#define _ASM_DMI_H

#include <linux/io.h>
#include <linux/memblock.h>

#define dmi_early_remap(x, l)	dmi_remap(x, l)
#define dmi_early_unmap(x, l)	dmi_unmap(x)
#define dmi_alloc(l)		memblock_alloc_low(l, PAGE_SIZE)

static inline void *dmi_remap(u64 phys_addr, unsigned long size)
{
	return ((void *)TO_CAC(phys_addr));
}

static inline void dmi_unmap(void *addr)
{
}

#endif /* _ASM_DMI_H */
