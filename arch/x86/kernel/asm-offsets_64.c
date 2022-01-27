// SPDX-License-Identifier: GPL-2.0
#ifndef __LINUX_KBUILD_H
# error "Please do not build this file directly, build asm-offsets.c instead"
#endif

#include <asm/ia32.h>
#include <asm/ptrace.h>

#if defined(CONFIG_KVM_GUEST) && defined(CONFIG_PARAVIRT_SPINLOCKS)
#include <asm/kvm_para.h>
#endif

int main(void)
{
#ifdef CONFIG_PARAVIRT
#ifdef CONFIG_PARAVIRT_XXL
#ifdef CONFIG_DEBUG_ENTRY
	OFFSET(PV_IRQ_save_fl, paravirt_patch_template, irq.save_fl);
#endif
#endif
	BLANK();
#endif

#if defined(CONFIG_KVM_GUEST) && defined(CONFIG_PARAVIRT_SPINLOCKS)
	OFFSET(KVM_STEAL_TIME_preempted, kvm_steal_time, preempted);
	BLANK();
#endif

#define ENTRY(entry) OFFSET(pt_regs_ ## entry, pt_regs, entry)
	ENTRY(bx);
	ENTRY(cx);
	ENTRY(dx);
	ENTRY(sp);
	ENTRY(bp);
	ENTRY(si);
	ENTRY(di);
	ENTRY(r8);
	ENTRY(r9);
	ENTRY(r10);
	ENTRY(r11);
	ENTRY(r12);
	ENTRY(r13);
	ENTRY(r14);
	ENTRY(r15);
	ENTRY(flags);
	BLANK();
#undef ENTRY

#define ENTRY(entry) OFFSET(saved_context_ ## entry, saved_context, entry)
	ENTRY(cr0);
	ENTRY(cr2);
	ENTRY(cr3);
	ENTRY(cr4);
	ENTRY(gdt_desc);
	BLANK();
#undef ENTRY

	BLANK();

#ifdef CONFIG_STACKPROTECTOR
	DEFINE(stack_canary_offset, offsetof(struct fixed_percpu_data, stack_canary));
	BLANK();
#endif

#ifdef CONFIG_ARCH_HAS_PTREGS_AUXILIARY
	/* Size of Auxiliary pt_regs data */
	DEFINE(PTREGS_AUX_SIZE, sizeof(struct pt_regs_extended) -
				sizeof(struct pt_regs));
#else
	/*
	 * Adding data to struct pt_regs_auxiliary requires setting
	 * ARCH_HAS_PTREGS_AUXILIARY
	 */
	BUILD_BUG_ON((sizeof(struct pt_regs_extended) -
		      sizeof(struct pt_regs)) != 0);
#endif

	return 0;
}
