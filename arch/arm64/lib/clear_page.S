/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/linkage.h>
#include <linux/const.h>
#include <asm/assembler.h>
#include <asm/page.h>

/*
 * Clear page @dest
 *
 * Parameters:
 *	x0 - dest
 */
SYM_FUNC_START_PI(clear_page)
	mov	x1, #0
	mov	x2, #0
1:
	stnp	x1, x2, [x0]
	stnp	x1, x2, [x0, #16]
	stnp	x1, x2, [x0, #32]
	stnp	x1, x2, [x0, #48]
	stnp	x1, x2, [x0, #64]
	stnp	x1, x2, [x0, #80]
	stnp	x1, x2, [x0, #96]
	stnp	x1, x2, [x0, #112]
	add	x0, x0, #128
	tst	x0, #(PAGE_SIZE - 1)
	b.ne	1b
	ret
SYM_FUNC_END_PI(clear_page)
EXPORT_SYMBOL(clear_page)
