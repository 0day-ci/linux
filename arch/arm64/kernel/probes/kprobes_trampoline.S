/* SPDX-License-Identifier: GPL-2.0 */
/*
 * trampoline entry and return code for kretprobes.
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>
#include "base_regs.h"

	.text

SYM_CODE_START(kretprobe_trampoline)

	save_all_base_regs

	mov x0, sp
	bl trampoline_probe_handler
	/*
	 * Replace trampoline address in lr with actual orig_ret_addr return
	 * address.
	 */
	mov lr, x0

	restore_all_base_regs

	ret

SYM_CODE_END(kretprobe_trampoline)
