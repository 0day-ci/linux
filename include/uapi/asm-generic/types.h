/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_GENERIC_TYPES_H
#define _ASM_GENERIC_TYPES_H

#include <asm/bitsperlong.h>

/*
 * int-ll64 is used everywhere in kernel now.
 */
#if __BITS_PER_LONG == 64 && !defined(__KERNEL__)
# include <asm-generic/int-l64.h>
#else
# include <asm-generic/int-ll64.h>
#endif

#endif /* _ASM_GENERIC_TYPES_H */
