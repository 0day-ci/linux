// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2021 suijingfeng@loongson.cn
 */
#include <linux/export.h>
#include <linux/init.h>
#include <asm/wbflush.h>
#include <asm/barrier.h>

#ifdef CONFIG_CPU_HAS_WB

/*
 * I/O ASIC systems use a standard writeback buffer that gets flushed
 * upon an uncached read.
 */
static void wbflush_mips(void)
{
	__fast_iob();
}

void (*__wbflush)(void) = wbflush_mips;
EXPORT_SYMBOL(__wbflush);

#endif
