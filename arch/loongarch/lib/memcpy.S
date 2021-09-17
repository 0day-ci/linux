/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */

#include <asm/asmmacro.h>
#include <asm/export.h>
#include <asm/regdef.h>

/*
 * void *memcpy(void *dst, const void *src, size_t n)
 *
 * a0: dst
 * a1: src
 * a2: n
 */
SYM_FUNC_START(memcpy)
	move	a3, a0
	beqz	a2, 2f

1:	ld.b	t0, a1, 0
	st.b	t0, a0, 0
	addi.d	a0, a0, 1
	addi.d	a1, a1, 1
	addi.d	a2, a2, -1
	bgt	a2, zero, 1b

2:	move	v0, a3
	jr	ra
SYM_FUNC_END(memcpy)

EXPORT_SYMBOL(memcpy)
