/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Authors: Jun Yi <yijun@loongson.cn>
 *          Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_SWAB_H
#define _ASM_SWAB_H

#include <linux/compiler.h>
#include <linux/types.h>

#define __SWAB_64_THRU_32__

static inline __attribute_const__ __u16 __arch_swab16(__u16 x)
{
	__asm__(
	"	revb.2h	%0, %1			\n"
	: "=r" (x)
	: "r" (x));

	return x;
}
#define __arch_swab16 __arch_swab16

static inline __attribute_const__ __u32 __arch_swab32(__u32 x)
{
	__asm__(
	"	revb.2h	%0, %1			\n"
	"	rotri.w	%0, %0, 16		\n"
	: "=r" (x)
	: "r" (x));

	return x;
}
#define __arch_swab32 __arch_swab32

#ifdef __loongarch64
static inline __attribute_const__ __u64 __arch_swab64(__u64 x)
{
	__asm__(
	"	revb.4h	%0, %1			\n"
	"	revh.d	%0, %0			\n"
	: "=r" (x)
	: "r" (x));

	return x;
}
#define __arch_swab64 __arch_swab64
#endif /* __loongarch64 */
#endif /* _ASM_SWAB_H */
