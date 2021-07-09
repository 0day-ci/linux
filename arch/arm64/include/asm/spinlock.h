/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/qspinlock.h>
#include <asm/qrwlock.h>
#include <asm/paravirt.h>

/* See include/linux/spinlock.h */
#define smp_mb__after_spinlock()	smp_mb()

#define vcpu_is_preempted vcpu_is_preempted

#ifdef CONFIG_PARAVIRT
static inline bool vcpu_is_preempted(unsigned int cpu)
{
	return paravirt_vcpu_is_preempted(cpu);
}
#else
static inline bool vcpu_is_preempted(unsigned int cpu)
{
	return false;
}
#endif /* CONFIG_PARAVIRT */

#endif /* __ASM_SPINLOCK_H */
