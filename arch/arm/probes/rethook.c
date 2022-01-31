// SPDX-License-Identifier: GPL-2.0-only
/*
 * arm implementation of rethook. Mostly copied from arch/arm/probes/kprobes/core.c
 */

#include <linux/kprobes.h>
#include <linux/rethook.h>

/* Called from arch_rethook_trampoline */
static __used unsigned long arch_rethook_trampoline_callback(struct pt_regs *regs)
{
	return rethook_trampoline_handler(regs, regs->ARM_fp);
}
NOKPROBE_SYMBOL(arch_rethook_trampoline_callback);

/*
 * When a rethook'ed function returns, it returns to arch_rethook_trampoline
 * which calls rethook callback. We construct a struct pt_regs to
 * give a view of registers r0-r11, sp, lr, and pc to the user
 * return-handler. This is not a complete pt_regs structure, but that
 * should be enough for stacktrace from the return handler with or
 * without pt_regs.
 */
void __naked arch_rethook_trampoline(void)
{
	__asm__ __volatile__ (
#ifdef CONFIG_FRAME_POINTER
		"ldr	lr, =arch_rethook_trampoline	\n\t"
	/* this makes a framepointer on pt_regs. */
#ifdef CONFIG_CC_IS_CLANG
		"stmdb	sp, {sp, lr, pc}	\n\t"
		"sub	sp, sp, #12		\n\t"
		/* In clang case, pt_regs->ip = lr. */
		"stmdb	sp!, {r0 - r11, lr}	\n\t"
		/* fp points regs->r11 (fp) */
		"add	fp, sp,	#44		\n\t"
#else /* !CONFIG_CC_IS_CLANG */
		/* In gcc case, pt_regs->ip = fp. */
		"stmdb	sp, {fp, sp, lr, pc}	\n\t"
		"sub	sp, sp, #16		\n\t"
		"stmdb	sp!, {r0 - r11}		\n\t"
		/* fp points regs->r15 (pc) */
		"add	fp, sp, #60		\n\t"
#endif /* CONFIG_CC_IS_CLANG */
#else /* !CONFIG_FRAME_POINTER */
		"sub	sp, sp, #16		\n\t"
		"stmdb	sp!, {r0 - r11}		\n\t"
#endif /* CONFIG_FRAME_POINTER */
		"mov	r0, sp			\n\t"
		"bl	arch_rethook_trampoline_callback	\n\t"
		"mov	lr, r0			\n\t"
		"ldmia	sp!, {r0 - r11}		\n\t"
		"add	sp, sp, #16		\n\t"
#ifdef CONFIG_THUMB2_KERNEL
		"bx	lr			\n\t"
#else
		"mov	pc, lr			\n\t"
#endif
		: : : "memory");
}
NOKPROBE_SYMBOL(arch_rethook_trampoline);

void arch_rethook_prepare(struct rethook_node *rh, struct pt_regs *regs)
{
	rh->ret_addr = regs->ARM_lr;
	rh->frame = regs->ARM_fp;

	/* Replace the return addr with trampoline addr. */
	regs->ARM_lr = (unsigned long)arch_rethook_trampoline;
}
NOKPROBE_SYMBOL(arch_rethook_prepare);
