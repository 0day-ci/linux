// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <hyp/sysreg-sr.h>

#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kprobes.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_nested.h>

static void __sysreg_save_vel2_state(struct kvm_cpu_context *ctxt)
{
	/* These registers are common with EL1 */
	ctxt_sys_reg(ctxt, CSSELR_EL1)	= read_sysreg(csselr_el1);
	ctxt_sys_reg(ctxt, PAR_EL1)	= read_sysreg(par_el1);
	ctxt_sys_reg(ctxt, TPIDR_EL1)	= read_sysreg(tpidr_el1);

	ctxt_sys_reg(ctxt, ESR_EL2)	= read_sysreg_el1(SYS_ESR);
	ctxt_sys_reg(ctxt, AFSR0_EL2)	= read_sysreg_el1(SYS_AFSR0);
	ctxt_sys_reg(ctxt, AFSR1_EL2)	= read_sysreg_el1(SYS_AFSR1);
	ctxt_sys_reg(ctxt, FAR_EL2)	= read_sysreg_el1(SYS_FAR);
	ctxt_sys_reg(ctxt, MAIR_EL2)	= read_sysreg_el1(SYS_MAIR);
	ctxt_sys_reg(ctxt, VBAR_EL2)	= read_sysreg_el1(SYS_VBAR);
	ctxt_sys_reg(ctxt, CONTEXTIDR_EL2) = read_sysreg_el1(SYS_CONTEXTIDR);
	ctxt_sys_reg(ctxt, AMAIR_EL2)	= read_sysreg_el1(SYS_AMAIR);

	/*
	 * In VHE mode those registers are compatible between EL1 and EL2,
	 * and the guest uses the _EL1 versions on the CPU naturally.
	 * So we save them into their _EL2 versions here.
	 * For nVHE mode we trap accesses to those registers, so our
	 * _EL2 copy in sys_regs[] is always up-to-date and we don't need
	 * to save anything here.
	 */
	if (__vcpu_el2_e2h_is_set(ctxt)) {
		ctxt_sys_reg(ctxt, SCTLR_EL2)	= read_sysreg_el1(SYS_SCTLR);
		ctxt_sys_reg(ctxt, CPTR_EL2)	= read_sysreg_el1(SYS_CPACR);
		ctxt_sys_reg(ctxt, TTBR0_EL2)	= read_sysreg_el1(SYS_TTBR0);
		ctxt_sys_reg(ctxt, TTBR1_EL2)	= read_sysreg_el1(SYS_TTBR1);
		ctxt_sys_reg(ctxt, TCR_EL2)	= read_sysreg_el1(SYS_TCR);
		ctxt_sys_reg(ctxt, CNTHCTL_EL2)	= read_sysreg_el1(SYS_CNTKCTL);
	}

	ctxt_sys_reg(ctxt, SP_EL2)	= read_sysreg(sp_el1);
	ctxt_sys_reg(ctxt, ELR_EL2)	= read_sysreg_el1(SYS_ELR);
	ctxt_sys_reg(ctxt, SPSR_EL2)	= __fixup_spsr_el2_read(ctxt, read_sysreg_el1(SYS_SPSR));
}

static void __sysreg_restore_vel2_state(struct kvm_cpu_context *ctxt)
{
	u64 val;

	/* These registers are common with EL1 */
	write_sysreg(ctxt_sys_reg(ctxt, CSSELR_EL1),	csselr_el1);
	write_sysreg(ctxt_sys_reg(ctxt, PAR_EL1),	par_el1);
	write_sysreg(ctxt_sys_reg(ctxt, TPIDR_EL1),	tpidr_el1);

	write_sysreg(read_cpuid_id(),			vpidr_el2);
	write_sysreg(ctxt_sys_reg(ctxt, MPIDR_EL1),	vmpidr_el2);
	write_sysreg_el1(ctxt_sys_reg(ctxt, MAIR_EL2),	SYS_MAIR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, VBAR_EL2),	SYS_VBAR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, CONTEXTIDR_EL2),SYS_CONTEXTIDR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, AMAIR_EL2),	SYS_AMAIR);

	if (__vcpu_el2_e2h_is_set(ctxt)) {
		/*
		 * In VHE mode those registers are compatible between
		 * EL1 and EL2.
		 */
		write_sysreg_el1(ctxt_sys_reg(ctxt, SCTLR_EL2),	SYS_SCTLR);
		write_sysreg_el1(ctxt_sys_reg(ctxt, CPTR_EL2),	SYS_CPACR);
		write_sysreg_el1(ctxt_sys_reg(ctxt, TTBR0_EL2),	SYS_TTBR0);
		write_sysreg_el1(ctxt_sys_reg(ctxt, TTBR1_EL2),	SYS_TTBR1);
		write_sysreg_el1(ctxt_sys_reg(ctxt, TCR_EL2),	SYS_TCR);
		write_sysreg_el1(ctxt_sys_reg(ctxt, CNTHCTL_EL2), SYS_CNTKCTL);
	} else {
		val = translate_sctlr_el2_to_sctlr_el1(ctxt_sys_reg(ctxt, SCTLR_EL2));
		write_sysreg_el1(val, SYS_SCTLR);
		val = translate_cptr_el2_to_cpacr_el1(ctxt_sys_reg(ctxt, CPTR_EL2));
		write_sysreg_el1(val, SYS_CPACR);
		val = translate_ttbr0_el2_to_ttbr0_el1(ctxt_sys_reg(ctxt, TTBR0_EL2));
		write_sysreg_el1(val, SYS_TTBR0);
		val = translate_tcr_el2_to_tcr_el1(ctxt_sys_reg(ctxt, TCR_EL2));
		write_sysreg_el1(val, SYS_TCR);
		val = translate_cnthctl_el2_to_cntkctl_el1(ctxt_sys_reg(ctxt, CNTHCTL_EL2));
		write_sysreg_el1(val, SYS_CNTKCTL);
	}

	write_sysreg_el1(ctxt_sys_reg(ctxt, ESR_EL2),	SYS_ESR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, AFSR0_EL2),	SYS_AFSR0);
	write_sysreg_el1(ctxt_sys_reg(ctxt, AFSR1_EL2),	SYS_AFSR1);
	write_sysreg_el1(ctxt_sys_reg(ctxt, FAR_EL2),	SYS_FAR);
	write_sysreg(ctxt_sys_reg(ctxt, SP_EL2),	sp_el1);
	write_sysreg_el1(ctxt_sys_reg(ctxt, ELR_EL2),	SYS_ELR);

	val = __fixup_spsr_el2_write(ctxt, ctxt_sys_reg(ctxt, SPSR_EL2));
	write_sysreg_el1(val,	SYS_SPSR);
}

/*
 * VHE: Host and guest must save mdscr_el1 and sp_el0 (and the PC and
 * pstate, which are handled as part of the el2 return state) on every
 * switch (sp_el0 is being dealt with in the assembly code).
 * tpidr_el0 and tpidrro_el0 only need to be switched when going
 * to host userspace or a different VCPU.  EL1 registers only need to be
 * switched when potentially going to run a different VCPU.  The latter two
 * classes are handled as part of kvm_arch_vcpu_load and kvm_arch_vcpu_put.
 */

void sysreg_save_host_state_vhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_save_common_state(ctxt);
}
NOKPROBE_SYMBOL(sysreg_save_host_state_vhe);

void sysreg_save_guest_state_vhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_save_common_state(ctxt);
	__sysreg_save_el2_return_state(ctxt);
}
NOKPROBE_SYMBOL(sysreg_save_guest_state_vhe);

void sysreg_restore_host_state_vhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_restore_common_state(ctxt);
}
NOKPROBE_SYMBOL(sysreg_restore_host_state_vhe);

void sysreg_restore_guest_state_vhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_restore_common_state(ctxt);
	__sysreg_restore_el2_return_state(ctxt);
}
NOKPROBE_SYMBOL(sysreg_restore_guest_state_vhe);

/**
 * kvm_vcpu_load_sysregs_vhe - Load guest system registers to the physical CPU
 *
 * @vcpu: The VCPU pointer
 *
 * Load system registers that do not affect the host's execution, for
 * example EL1 system registers on a VHE system where the host kernel
 * runs at EL2.  This function is called from KVM's vcpu_load() function
 * and loading system register state early avoids having to load them on
 * every entry to the VM.
 */
void kvm_vcpu_load_sysregs_vhe(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *guest_ctxt = &vcpu->arch.ctxt;
	struct kvm_cpu_context *host_ctxt;
	u64 mpidr;

	host_ctxt = &this_cpu_ptr(&kvm_host_data)->host_ctxt;
	__sysreg_save_user_state(host_ctxt);

	/*
	 * Load guest EL1 and user state
	 *
	 * We must restore the 32-bit state before the sysregs, thanks
	 * to erratum #852523 (Cortex-A57) or #853709 (Cortex-A72).
	 */
	__sysreg32_restore_state(vcpu);
	__sysreg_restore_user_state(guest_ctxt);

	if (unlikely(__is_hyp_ctxt(guest_ctxt))) {
		__sysreg_restore_vel2_state(guest_ctxt);
	} else {
		if (nested_virt_in_use(vcpu)) {
			/*
			 * Only set VPIDR_EL2 for nested VMs, as this is the
			 * only time it changes. We'll restore the MIDR_EL1
			 * view on put.
			 */
			write_sysreg(ctxt_sys_reg(guest_ctxt, VPIDR_EL2), vpidr_el2);

			/*
			 * As we're restoring a nested guest, set the value
			 * provided by the guest hypervisor.
			 */
			mpidr = ctxt_sys_reg(guest_ctxt, VMPIDR_EL2);
		} else {
			mpidr = ctxt_sys_reg(guest_ctxt, MPIDR_EL1);
		}

		__sysreg_restore_el1_state(guest_ctxt, mpidr);
	}

	vcpu->arch.sysregs_loaded_on_cpu = true;

	activate_traps_vhe_load(vcpu);
}

/**
 * kvm_vcpu_put_sysregs_vhe - Restore host system registers to the physical CPU
 *
 * @vcpu: The VCPU pointer
 *
 * Save guest system registers that do not affect the host's execution, for
 * example EL1 system registers on a VHE system where the host kernel
 * runs at EL2.  This function is called from KVM's vcpu_put() function
 * and deferring saving system register state until we're no longer running the
 * VCPU avoids having to save them on every exit from the VM.
 */
void kvm_vcpu_put_sysregs_vhe(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *guest_ctxt = &vcpu->arch.ctxt;
	struct kvm_cpu_context *host_ctxt;

	host_ctxt = &this_cpu_ptr(&kvm_host_data)->host_ctxt;
	deactivate_traps_vhe_put();

	if (unlikely(__is_hyp_ctxt(guest_ctxt)))
		__sysreg_save_vel2_state(guest_ctxt);
	else
		__sysreg_save_el1_state(guest_ctxt);

	__sysreg_save_user_state(guest_ctxt);
	__sysreg32_save_state(vcpu);

	/* Restore host user state */
	__sysreg_restore_user_state(host_ctxt);

	/* If leaving a nesting guest, restore MPIDR_EL1 default view */
	if (nested_virt_in_use(vcpu))
		write_sysreg(read_cpuid_id(),	vpidr_el2);

	vcpu->arch.sysregs_loaded_on_cpu = false;
}
