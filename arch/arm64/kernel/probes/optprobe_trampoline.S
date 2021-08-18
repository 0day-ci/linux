/* SPDX-License-Identifier: GPL-2.0 */
/*
 * trampoline entry and return code for optprobes.
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>

	.global optprobe_template_entry
optprobe_template_entry:
	sub sp, sp, #PT_REGS_SIZE
	save_all_base_regs
	/* Get parameters to optimized_callback() */
	ldr	x0, 1f
	mov	x1, sp
	/* Branch to optimized_callback() */
	.global optprobe_template_call
optprobe_template_call:
	nop
	restore_all_base_regs
	ldr lr, [sp, #S_LR]
        add sp, sp, #PT_REGS_SIZE
	.global optprobe_template_restore_orig_insn
optprobe_template_restore_orig_insn:
	nop
	.global optprobe_template_restore_end
optprobe_template_restore_end:
	nop
	.global optprobe_template_end
optprobe_template_end:
	.global optprobe_template_val
optprobe_template_val:
	1:	.long 0
		.long 0
	.global optprobe_template_max_length
optprobe_template_max_length:
