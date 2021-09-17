/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_BITOPS_H
#define _ASM_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/byteorder.h>
#include <asm/compiler.h>
#include <asm/cpu-features.h>

#if _LOONGARCH_SZLONG == 32
#define __LL		"ll.w	"
#define __SC		"sc.w	"
#define __AMADD		"amadd.w	"
#define __AMAND_SYNC	"amand_db.w	"
#define __AMOR_SYNC	"amor_db.w	"
#define __AMXOR_SYNC	"amxor_db.w	"
#elif _LOONGARCH_SZLONG == 64
#define __LL		"ll.d	"
#define __SC		"sc.d	"
#define __AMADD		"amadd.d	"
#define __AMAND_SYNC	"amand_db.d	"
#define __AMOR_SYNC	"amor_db.d	"
#define __AMXOR_SYNC	"amxor_db.d	"
#endif

/*
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 */
static inline void set_bit(unsigned long nr, volatile unsigned long *addr)
{
	int bit = nr % BITS_PER_LONG;
	volatile unsigned long *m = &addr[BIT_WORD(nr)];

	__asm__ __volatile__(
	"   " __AMOR_SYNC "$zero, %1, %0        \n"
	: "+ZB" (*m)
	: "r" (1UL << bit)
	: "memory");
}

/*
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 */
static inline void clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	int bit = nr % BITS_PER_LONG;
	volatile unsigned long *m = &addr[BIT_WORD(nr)];

	__asm__ __volatile__(
	"   " __AMAND_SYNC "$zero, %1, %0       \n"
	: "+ZB" (*m)
	: "r" (~(1UL << bit))
	: "memory");
}

/*
 * clear_bit_unlock - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 */
static inline void clear_bit_unlock(unsigned long nr, volatile unsigned long *addr)
{
	clear_bit(nr, addr);
}

/*
 * change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 */
static inline void change_bit(unsigned long nr, volatile unsigned long *addr)
{
	int bit = nr % BITS_PER_LONG;
	volatile unsigned long *m = &addr[BIT_WORD(nr)];

	__asm__ __volatile__(
	"   " __AMXOR_SYNC "$zero, %1, %0       \n"
	: "+ZB" (*m)
	: "r" (1UL << bit)
	: "memory");
}

/*
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 */
static inline int test_and_set_bit(unsigned long nr,
	volatile unsigned long *addr)
{
	int bit = nr % BITS_PER_LONG;
	unsigned long res;
	volatile unsigned long *m = &addr[BIT_WORD(nr)];

	__asm__ __volatile__(
	"   " __AMOR_SYNC "%1, %2, %0       \n"
	: "+ZB" (*m), "=&r" (res)
	: "r" (1UL << bit)
	: "memory");

	res = res & (1UL << bit);

	return res != 0;
}

/*
 * test_and_set_bit_lock - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 */
static inline int test_and_set_bit_lock(unsigned long nr,
	volatile unsigned long *addr)
{
	int bit = nr % BITS_PER_LONG;
	unsigned long res;
	volatile unsigned long *m = &addr[BIT_WORD(nr)];

	__asm__ __volatile__(
	"   " __AMOR_SYNC "%1, %2, %0       \n"
	: "+ZB" (*m), "=&r" (res)
	: "r" (1UL << bit)
	: "memory");

	res = res & (1UL << bit);

	return res != 0;
}
/*
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 */
static inline int test_and_clear_bit(unsigned long nr,
	volatile unsigned long *addr)
{
	int bit = nr % BITS_PER_LONG;
	unsigned long res, temp;
	volatile unsigned long *m = &addr[BIT_WORD(nr)];

	__asm__ __volatile__(
	"   " __AMAND_SYNC "%1, %2, %0      \n"
	: "+ZB" (*m), "=&r" (temp)
	: "r" (~(1UL << bit))
	: "memory");

	res = temp & (1UL << bit);

	return res != 0;
}

/*
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 */
static inline int test_and_change_bit(unsigned long nr,
	volatile unsigned long *addr)
{
	int bit = nr % BITS_PER_LONG;
	unsigned long res;
	volatile unsigned long *m = &addr[BIT_WORD(nr)];

	__asm__ __volatile__(
	"   " __AMXOR_SYNC "%1, %2, %0      \n"
	: "+ZB" (*m), "=&r" (res)
	: "r" (1UL << bit)
	: "memory");

	res = res & (1UL << bit);

	return res != 0;
}

#include <asm-generic/bitops/non-atomic.h>

/*
 * __clear_bit_unlock - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 */
static inline void __clear_bit_unlock(unsigned long nr, volatile unsigned long *addr)
{
	clear_bit(nr, addr);
}

#include <asm-generic/bitops/builtin-ffs.h>
#include <asm-generic/bitops/builtin-fls.h>
#include <asm-generic/bitops/builtin-__ffs.h>
#include <asm-generic/bitops/builtin-__fls.h>

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/find.h>

#ifdef __KERNEL__

#include <asm-generic/bitops/sched.h>

#include <asm-generic/bitops/arch_hweight.h>
#include <asm-generic/bitops/const_hweight.h>

#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic.h>

#endif /* __KERNEL__ */

#endif /* _ASM_BITOPS_H */
