/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_SPINLOCK_H
#define _ASM_RISCV_SPINLOCK_H

#ifdef CONFIG_RISCV_TICKET_LOCK
#ifdef CONFIG_32BIT
#define __ASM_SLLIW "slli\t"
#define __ASM_SRLIW "srli\t"
#else
#define __ASM_SLLIW "slliw\t"
#define __ASM_SRLIW "srliw\t"
#endif

/*
 * Ticket-based spin-locking.
 */
static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	arch_spinlock_t lockval;
	u32 tmp;

	asm volatile (
		"1:	lr.w	%0, %2		\n"
		"	mv	%1, %0		\n"
		"	addw	%0, %0, %3	\n"
		"	sc.w	%0, %0, %2	\n"
		"	bnez	%0, 1b		\n"
		: "=&r" (tmp), "=&r" (lockval), "+A" (lock->lock)
		: "r" (1 << TICKET_NEXT)
		: "memory");

	smp_cond_load_acquire(&lock->tickets.owner,
					VAL == lockval.tickets.next);
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	u32 tmp, contended, res;

	do {
		asm volatile (
		"	lr.w	%0, %3		\n"
		__ASM_SRLIW    "%1, %0, %5	\n"
		__ASM_SLLIW    "%2, %0, %5	\n"
		"	or	%1, %2, %1	\n"
		"	li	%2, 0		\n"
		"	sub	%1, %1, %0	\n"
		"	bnez	%1, 1f		\n"
		"	addw	%0, %0, %4	\n"
		"	sc.w	%2, %0, %3	\n"
		"1:				\n"
		: "=&r" (tmp), "=&r" (contended), "=&r" (res),
		  "+A" (lock->lock)
		: "r" (1 << TICKET_NEXT), "I" (TICKET_NEXT)
		: "memory");
	} while (res);

	if (!contended)
		__atomic_acquire_fence();

	return !contended;
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	smp_store_release(&lock->tickets.owner, lock->tickets.owner + 1);
}

static inline int arch_spin_value_unlocked(arch_spinlock_t lock)
{
	return lock.tickets.owner == lock.tickets.next;
}

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	return !arch_spin_value_unlocked(READ_ONCE(*lock));
}

static inline int arch_spin_is_contended(arch_spinlock_t *lock)
{
	struct __raw_tickets tickets = READ_ONCE(lock->tickets);

	return (tickets.next - tickets.owner) > 1;
}
#define arch_spin_is_contended	arch_spin_is_contended
#else /* CONFIG_RISCV_TICKET_LOCK */
#include <asm/qspinlock.h>
#endif /* CONFIG_RISCV_TICKET_LOCK */

#include <asm/qrwlock.h>

#endif /* _ASM_RISCV_SPINLOCK_H */
