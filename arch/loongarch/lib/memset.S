/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */

#include <asm/asmmacro.h>
#include <asm/export.h>
#include <asm/regdef.h>

/*
 * void *memset(void *s, int c, size_t n)
 *
 * a0: s
 * a1: c
 * a2: n
 */
SYM_FUNC_START(memset)
	move	a3, a0
	beqz	a2, 2f

1:	st.b	a1, a0, 0
	addi.d	a0, a0, 1
	addi.d	a2, a2, -1
	bgt	a2, zero, 1b

2:	move	v0, a3
	jr	ra
SYM_FUNC_END(memset)

EXPORT_SYMBOL(memset)
