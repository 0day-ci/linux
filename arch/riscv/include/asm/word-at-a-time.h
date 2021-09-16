/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 *
 * Derived from arch/x86/include/asm/word-at-a-time.h
 */

#ifndef _ASM_RISCV_WORD_AT_A_TIME_H
#define _ASM_RISCV_WORD_AT_A_TIME_H


#include <linux/kernel.h>

struct word_at_a_time {
	const unsigned long one_bits, high_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0x01), REPEAT_BYTE(0x80) }

static inline unsigned long has_zero(unsigned long val,
	unsigned long *bits, const struct word_at_a_time *c)
{
	unsigned long mask = ((val - c->one_bits) & ~val) & c->high_bits;
	*bits = mask;
	return mask;
}

static inline unsigned long prep_zero_mask(unsigned long val,
	unsigned long bits, const struct word_at_a_time *c)
{
	return bits;
}

static inline unsigned long create_zero_mask(unsigned long bits)
{
	bits = (bits - 1) & ~bits;
	return bits >> 7;
}

static inline unsigned long find_zero(unsigned long mask)
{
	return fls64(mask) >> 3;
}

/* The mask we created is directly usable as a bytemask */
#define zero_bytemask(mask) (mask)

#ifdef CONFIG_DCACHE_WORD_ACCESS
#include <asm/asm.h>

/*
 * Load an unaligned word from kernel space.
 *
 * In the (very unlikely) case of the word being a page-crosser
 * and the next page not being mapped, take the exception and
 * return zeroes in the non-existing part.
 */
static inline unsigned long load_unaligned_zeropad(const void *addr)
{
	unsigned long ret, tmp;

	/* Load word from unaligned pointer addr */
	asm(
	"1:	" REG_L " %0, %3\n"
	"2:\n"
	"	.section .fixup,\"ax\"\n"
	"	.balign 2\n"
	"3:	andi	%1, %2, ~0x7\n"
	"	" REG_L " %0, (%1)\n"
	"	andi	%1, %2, 0x7\n"
	"	slli	%1, %1, 0x3\n"
	"	srl	%0, %0, %1\n"
	"	jump	2b, %1\n"
	"	.previous\n"
	"	.section __ex_table,\"a\"\n"
	"	.balign	" RISCV_SZPTR "\n"
	"	" RISCV_PTR "	1b, 3b\n"
	"	.previous"
	: "=&r" (ret), "=&r" (tmp)
	: "r" (addr), "m" (*(unsigned long *)addr));

	return ret;
}
#endif	/* DCACHE_WORD_ACCESS */
#endif /* _ASM_RISCV_WORD_AT_A_TIME_H */
