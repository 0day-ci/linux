// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 2017 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */

#include <linux/bitops.h>
#include <asm/cmpxchg.h>

unsigned long __xchg_small(volatile void *ptr, unsigned long val, unsigned int size)
{
	u32 old32, mask, temp;
	volatile u32 *ptr32;
	unsigned int shift;

	/* Check that ptr is naturally aligned */
	WARN_ON((unsigned long)ptr & (size - 1));

	/* Mask value to the correct size. */
	mask = GENMASK((size * BITS_PER_BYTE) - 1, 0);
	val &= mask;

	/*
	 * Calculate a shift & mask that correspond to the value we wish to
	 * exchange within the naturally aligned 4 byte integerthat includes
	 * it.
	 */
	shift = (unsigned long)ptr & 0x3;
	shift *= BITS_PER_BYTE;
	mask <<= shift;

	/*
	 * Calculate a pointer to the naturally aligned 4 byte integer that
	 * includes our byte of interest, and load its value.
	 */
	ptr32 = (volatile u32 *)((unsigned long)ptr & ~0x3);

	asm volatile (
	"1:	ll.w		%0, %3		\n"
	"	and		%1, %0, %4	\n"
	"	or		%1, %1, %5	\n"
	"	sc.w		%1, %2		\n"
	"	beqz		%1, 1b		\n"
	: "=&r" (old32), "=&r" (temp), "=" GCC_OFF_SMALL_ASM() (*ptr32)
	: GCC_OFF_SMALL_ASM() (*ptr32), "Jr" (~mask), "Jr" (val << shift)
	: "memory");

	return (old32 & mask) >> shift;
}

unsigned long __cmpxchg_small(volatile void *ptr, unsigned long old,
			      unsigned long new, unsigned int size)
{
	u32 mask, old32, new32, load32, load;
	volatile u32 *ptr32;
	unsigned int shift;

	/* Check that ptr is naturally aligned */
	WARN_ON((unsigned long)ptr & (size - 1));

	/* Mask inputs to the correct size. */
	mask = GENMASK((size * BITS_PER_BYTE) - 1, 0);
	old &= mask;
	new &= mask;

	/*
	 * Calculate a shift & mask that correspond to the value we wish to
	 * compare & exchange within the naturally aligned 4 byte integer
	 * that includes it.
	 */
	shift = (unsigned long)ptr & 0x3;
	shift *= BITS_PER_BYTE;
	mask <<= shift;

	/*
	 * Calculate a pointer to the naturally aligned 4 byte integer that
	 * includes our byte of interest, and load its value.
	 */
	ptr32 = (volatile u32 *)((unsigned long)ptr & ~0x3);
	load32 = *ptr32;

	while (true) {
		/*
		 * Ensure the byte we want to exchange matches the expected
		 * old value, and if not then bail.
		 */
		load = (load32 & mask) >> shift;
		if (load != old)
			return load;

		/*
		 * Calculate the old & new values of the naturally aligned
		 * 4 byte integer that include the byte we want to exchange.
		 * Attempt to exchange the old value for the new value, and
		 * return if we succeed.
		 */
		old32 = (load32 & ~mask) | (old << shift);
		new32 = (load32 & ~mask) | (new << shift);
		load32 = arch_cmpxchg(ptr32, old32, new32);
		if (load32 == old32)
			return old;
	}
}
