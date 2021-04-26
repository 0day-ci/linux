/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _UAPI__ASM_ARC_PAGE_H
#define _UAPI__ASM_ARC_PAGE_H

#include <linux/const.h>

/* PAGE_SHIFT determines the page size */
#if defined(CONFIG_ARC_PAGE_SIZE_16K)
#define PAGE_SHIFT 14
#elif defined(CONFIG_ARC_PAGE_SIZE_4K)
#define PAGE_SHIFT 12
#else
/*
 * Default 8k
 * done this way (instead of under CONFIG_ARC_PAGE_SIZE_8K) because adhoc
 * user code (busybox appletlib.h) expects PAGE_SHIFT to be defined w/o
 * using the correct uClibc header and in their build our autoconf.h is
 * not available
 */
#define PAGE_SHIFT 13
#endif

#define PAGE_SIZE	_BITUL(PAGE_SHIFT)	/* Default 8K */
#define PAGE_OFFSET	_AC(0x80000000, UL)	/* Kernel starts at 2G onwrds */

#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef CONFIG_ARC_HAS_PAE40
#define MAX_POSSIBLE_PHYSMEM_BITS 40
#define PHYSICAL_PAGE_MASK	(0xff00000000ull | PAGE_MASK)
#else
#define MAX_POSSIBLE_PHYSMEM_BITS 32
#define PHYSICAL_PAGE_MASK	PAGE_MASK
#endif

#endif /* _UAPI__ASM_ARC_PAGE_H */
