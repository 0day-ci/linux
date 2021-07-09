// SPDX-License-Identifier: GPL-2.0-or-later

#include <asm/code-patching.h>
#include <asm/kup.h>
#include <asm/smp.h>

struct static_key_false disable_kuep_key;

void setup_kuep(bool disabled)
{
	u32 insn;

	if (!disabled) {
		init_mm.context.sr0 |= SR_NX;
		current->thread.sr0 |= SR_NX;
		update_user_segments(init_mm.context.sr0);
	}

	if (smp_processor_id() != boot_cpuid)
		return;

	if (disabled)
		static_branch_enable(&disable_kuep_key);

	if (disabled)
		return;

	insn = PPC_RAW_LWZ(_R4, _R2, offsetof(struct task_struct, thread.sr0));
	patch_instruction_site(&patch__kuep_interrupt_entry, ppc_inst(insn));

	insn = PPC_RAW_LWZ(_R9, _R10, offsetof(struct thread_struct, sr0));
	patch_instruction_site(&patch__kuep_syscall_entry, ppc_inst(insn));

	insn = PPC_RAW_LWZ(_R7, _R2, offsetof(struct task_struct, thread.sr0));
	patch_instruction_site(&patch__kuep_syscall_exit, ppc_inst(insn));
	patch_instruction_site(&patch__kuep_interrupt_exit, ppc_inst(insn));

	pr_info("Activating Kernel Userspace Execution Prevention\n");
}
