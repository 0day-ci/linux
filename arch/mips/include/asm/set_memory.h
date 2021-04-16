/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_MIPS_SET_MEMORY_H
#define _ASM_MIPS_SET_MEMORY_H

#include <asm-generic/set_memory.h>

struct pageattr_masks {
	pgprot_t set_mask;
	pgprot_t clear_mask;
};

/*
 * Functions to change pages attributes.
 */
int set_pages_ro(struct page *page, int numpages);
int set_pages_rw(struct page *page, int numpages);

#endif /* _ASM_MIPS_SET_MEMORY_H */
