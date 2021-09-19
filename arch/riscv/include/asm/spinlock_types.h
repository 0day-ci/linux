/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef _ASM_RISCV_SPINLOCK_TYPES_H
#define _ASM_RISCV_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_H
# error "please don't include this file directly"
#endif

#include <linux/types.h>
typedef atomic_t arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	ATOMIC_INIT(0)

#include <asm-generic/qrwlock_types.h>

#endif /* _ASM_RISCV_SPINLOCK_TYPES_H */
