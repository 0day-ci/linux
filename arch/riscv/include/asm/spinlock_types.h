/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef _ASM_RISCV_SPINLOCK_TYPES_H
#define _ASM_RISCV_SPINLOCK_TYPES_H

#if !defined(__LINUX_SPINLOCK_TYPES_H) && !defined(_ASM_RISCV_SPINLOCK_H)
# error "please don't include this file directly"
#endif

#include <asm-generic/qspinlock_types.h>
#include <asm-generic/qrwlock_types.h>

#endif /* _ASM_RISCV_SPINLOCK_TYPES_H */
