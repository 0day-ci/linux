/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef _ASM_RISCV_SPINLOCK_TYPES_H
#define _ASM_RISCV_SPINLOCK_TYPES_H

#if !defined(__LINUX_SPINLOCK_TYPES_H) && !defined(_ASM_RISCV_SPINLOCK_H)
# error "please don't include this file directly"
#endif

#ifdef CONFIG_RISCV_TICKET_LOCK
#define TICKET_NEXT	16

typedef struct {
	union {
		u32 lock;
		struct __raw_tickets {
			/* little endian */
			u16 owner;
			u16 next;
		} tickets;
	};
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ { 0 } }
#else
#include <asm-generic/qspinlock_types.h>
#endif
#include <asm-generic/qrwlock_types.h>

#endif /* _ASM_RISCV_SPINLOCK_TYPES_H */
