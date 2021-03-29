/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_SECTIONS_H
#define _ASM_ARM_SECTIONS_H

#define arch_is_kernel_initmem_freed arch_is_kernel_initmem_freed

#include <asm-generic/sections.h>

extern char _exiprom[];

extern char __idmap_text_start[];
extern char __idmap_text_end[];
extern char __entry_text_start[];
extern char __entry_text_end[];

extern bool initmem_freed;

static inline int arch_is_kernel_initmem_freed(unsigned long addr)
{
	if (!initmem_freed)
		return 0;
	return addr >= (unsigned long)__init_begin &&
	       addr < (unsigned long)__init_end;
}


static inline bool in_entry_text(unsigned long addr)
{
	return memory_contains(__entry_text_start, __entry_text_end,
			       (void *)addr, 1);
}

static inline bool in_idmap_text(unsigned long addr)
{
	void *a = (void *)addr;
	return memory_contains(__idmap_text_start, __idmap_text_end, a, 1);
}

#endif	/* _ASM_ARM_SECTIONS_H */
