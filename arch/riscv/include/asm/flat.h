/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_FLAT_H
#define _ASM_RISCV_FLAT_H

#include <asm/unaligned.h>

static inline int flat_get_addr_from_rp(u32 __user *rp, u32 relval, u32 flags,
					u32 *addr)
{
	*addr = get_unaligned((__force u32 *)rp);
	return 0;
}

static inline int flat_put_addr_at_rp(u32 __user *rp, u32 addr, u32 rel)
{
	put_unaligned(addr, (__force u32 *)rp);
	return 0;
}

/*
 * uclibc/gcc fully resolve the PC relative __global_pointer value
 * at compile time and do not generate a relocation entry to set a
 * runtime gp value. As a result, the flatbin loader must not introduce
 * a gap between the text and data sections and keep them contiguous to
 * avoid invalid address accesses.
 */
#define FLAT_TEXT_DATA_NO_GAP	(1)

#endif /* _ASM_RISCV_FLAT_H */
