/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_SPINLOCK_H
#define _ASM_RISCV_SPINLOCK_H

static __always_inline void ticket_lock(arch_spinlock_t *lock)
{
	u32 val = atomic_fetch_add(1<<16, lock); /* SC, gives us RCsc */
	u16 ticket = val >> 16;

	if (ticket == (u16)val)
		return;

	atomic_cond_read_acquire(lock, ticket == (u16)VAL);
}

static __always_inline bool ticket_trylock(arch_spinlock_t *lock)
{
	u32 old = atomic_read(lock);

	if ((old >> 16) != (old & 0xffff))
		return false;

	return atomic_try_cmpxchg(lock, &old, old + (1<<16)); /* SC, for RCsc */
}

static __always_inline void ticket_unlock(arch_spinlock_t *lock)
{
	u16 *ptr = (u16 *)lock;
	u32 val = atomic_read(lock);

	smp_store_release(ptr, (u16)val + 1);
}

static __always_inline int ticket_value_unlocked(arch_spinlock_t lock)
{
	return (((u32)lock.counter >> 16) == ((u32)lock.counter & 0xffff));
}

static __always_inline int ticket_is_locked(arch_spinlock_t *lock)
{
	return !ticket_value_unlocked(READ_ONCE(*lock));
}

static __always_inline int ticket_is_contended(arch_spinlock_t *lock)
{
	u32 val = atomic_read(lock);

	return (s16)((val >> 16) - (val & 0xffff)) > 1;
}

#define arch_spin_lock(l)		ticket_lock(l)
#define arch_spin_trylock(l)		ticket_trylock(l)
#define arch_spin_unlock(l)		ticket_unlock(l)
#define arch_spin_value_unlocked(l)	ticket_value_unlocked(l)
#define arch_spin_is_locked(l)		ticket_is_locked(l)
#define arch_spin_is_contended(l)	ticket_is_contended(l)

#include <asm/qrwlock.h>

#endif /* _ASM_RISCV_SPINLOCK_H */
