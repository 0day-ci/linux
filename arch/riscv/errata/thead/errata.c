// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <asm/patch.h>
#include <asm/alternative.h>
#include <asm/vendorid_list.h>
#include <asm/errata_list.h>
#include <asm/pgtable-bits.h>

#ifdef CONFIG_64BIT
/*
 * T-HEAD C9xx PTE format:
 * | 63 | 62 | 61 | 60 | 59-8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *   SO   C    B    SH   RSW    D   A   G   U   X   W   R   V
 *   ^    ^    ^    ^    ^
 * BIT(63): SO - Strong Order
 * BIT(62): C  - Cacheable
 * BIT(61): B  - Bufferable
 * BIT(60): SH - Shareable
 *
 * MT_MASK : [63 - 59]
 * MT_PMA  : C + SH
 * MT_NC   : (none)
 * MT_IO   : SO
 */
#undef _PAGE_MT_MASK
#undef _PAGE_MT_PMA
#undef _PAGE_MT_NC
#undef _PAGE_MT_IO

#define _PAGE_MT_MASK	0xf800000000000000
#define _PAGE_MT_PMA	0x5000000000000000
#define _PAGE_MT_NC	0x0
#define _PAGE_MT_IO	0x8000000000000000
#endif

void __init thead_errata_setup_vm(unsigned long archid, unsigned long impid)
{
#ifdef CONFIG_64BIT
	__riscv_pbmt.mask	= _PAGE_MT_MASK;
	__riscv_pbmt.mt[MT_PMA]	= _PAGE_MT_PMA;
	__riscv_pbmt.mt[MT_NC]	= _PAGE_MT_NC;
	__riscv_pbmt.mt[MT_IO]	= _PAGE_MT_IO;
#endif
}
