/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ASM_ARC_TLB_H
#define _ASM_ARC_TLB_H

#include <linux/pagemap.h>
#include <asm-generic/tlb.h>

#define arch_supports_page_tables_move arch_supports_page_tables_move
static inline bool arch_supports_page_tables_move(void)
{
	return true;
}
#endif /* _ASM_ARC_TLB_H */
