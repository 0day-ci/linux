/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_BOOTINFO_H
#define _ASM_BOOTINFO_H

#include <linux/types.h>
#include <asm/setup.h>

const char *get_system_type(void);

extern void early_memblock_init(void);
extern void detect_memory_region(phys_addr_t start, phys_addr_t sz_min,  phys_addr_t sz_max);

extern void early_init(void);
extern void platform_init(void);
extern void plat_swiotlb_setup(void);

extern void free_init_pages(const char *what, unsigned long begin, unsigned long end);

/*
 * Initial kernel command line, usually setup by fw_init_cmdline()
 */
extern char arcs_cmdline[COMMAND_LINE_SIZE];

/*
 * Registers a0, a1, a3 and a4 as passed to the kernel entry by firmware
 */
extern unsigned long fw_arg0, fw_arg1, fw_arg2, fw_arg3;

extern unsigned long initrd_start, initrd_end;

#endif /* _ASM_BOOTINFO_H */
