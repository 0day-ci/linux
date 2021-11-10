// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2007 Lemote Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 */
#include <linux/export.h>
#include <linux/init.h>
#include <asm/wbflush.h>
#include <asm/sync.h>

#ifdef CONFIG_CPU_HAS_WB

static void wbflush_loongson(void)
{
	asm(".set push\n\t"
	    ".set noreorder\n\t"
	    ".set mips64r2\n\t"
	    "sync\n\t"
	    "nop\n\t"
	    ".set pop\n\t");
}

void (*__wbflush)(void) = wbflush_loongson;
EXPORT_SYMBOL(__wbflush);

#endif
