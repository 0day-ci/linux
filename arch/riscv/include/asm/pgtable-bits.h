/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PGTABLE_BITS_H
#define _ASM_RISCV_PGTABLE_BITS_H

/*
 * rv32 PTE format:
 * | XLEN-1  10 | 9             8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *       PFN      reserved for SW   D   A   G   U   X   W   R   V
 */

#define _PAGE_ACCESSED_OFFSET 6

#define _PAGE_PRESENT   (1 << 0)
#define _PAGE_READ      (1 << 1)    /* Readable */
#define _PAGE_WRITE     (1 << 2)    /* Writable */
#define _PAGE_EXEC      (1 << 3)    /* Executable */
#define _PAGE_USER      (1 << 4)    /* User */
#define _PAGE_GLOBAL    (1 << 5)    /* Global */
#define _PAGE_ACCESSED  (1 << 6)    /* Set by hardware on any access */
#define _PAGE_DIRTY     (1 << 7)    /* Set by hardware on any write */
#define _PAGE_SOFT      (1 << 8)    /* Reserved for software */

#ifndef __ASSEMBLY__
#ifdef CONFIG_64BIT
/*
 * rv64 PTE format:
 * | 63 | 62 61 | 60 54 | 53  10 | 9             8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *   N      MT     RSV    PFN      reserved for SW   D   A   G   U   X   W   R   V
 * [62:61] Memory Type definitions:
 *  00 - PMA    Normal Cacheable, No change to implied PMA memory type
 *  01 - NC     Non-cacheable, idempotent, weakly-ordered Main Memory
 *  10 - IO     Non-cacheable, non-idempotent, strongly-ordered I/O memory
 *  11 - Rsvd   Reserved for future standard use
 */
#define _PAGE_MT_MASK		((u64)0x3 << 61)
#define _PAGE_MT_PMA		((u64)0x0 << 61)
#define _PAGE_MT_NC		((u64)0x1 << 61)
#define _PAGE_MT_IO		((u64)0x2 << 61)

enum {
	MT_PMA,
	MT_NC,
	MT_IO,
	MT_MAX
};

extern struct __riscv_pbmt_struct {
	unsigned long mask;
	unsigned long mt[MT_MAX];
} __riscv_pbmt;

#define _PAGE_DMA_MASK		__riscv_pbmt.mask
#define _PAGE_DMA_PMA		__riscv_pbmt.mt[MT_PMA]
#define _PAGE_DMA_NC		__riscv_pbmt.mt[MT_NC]
#define _PAGE_DMA_IO		__riscv_pbmt.mt[MT_IO]
#else
#define _PAGE_DMA_MASK		0
#define _PAGE_DMA_PMA		0
#define _PAGE_DMA_NC		0
#define _PAGE_DMA_IO		0
#endif /* CONFIG_64BIT */
#endif /* __ASSEMBLY__ */

#define _PAGE_SPECIAL   _PAGE_SOFT
#define _PAGE_TABLE     _PAGE_PRESENT

/*
 * _PAGE_PROT_NONE is set on not-present pages (and ignored by the hardware) to
 * distinguish them from swapped out pages
 */
#define _PAGE_PROT_NONE _PAGE_READ

#define _PAGE_PFN_SHIFT 10

/* Set of bits to preserve across pte_modify() */
#define _PAGE_CHG_MASK  (~(unsigned long)(_PAGE_PRESENT | _PAGE_READ |	\
					  _PAGE_WRITE | _PAGE_EXEC |	\
					  _PAGE_USER | _PAGE_GLOBAL |	\
					  _PAGE_DMA_MASK))
/*
 * when all of R/W/X are zero, the PTE is a pointer to the next level
 * of the page table; otherwise, it is a leaf PTE.
 */
#define _PAGE_LEAF (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)

#endif /* _ASM_RISCV_PGTABLE_BITS_H */
