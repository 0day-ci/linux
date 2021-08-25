// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - ARM Ltd
 */

#include <linux/kvm_host.h>

#include <asm/kvm_hyp.h>
#include <asm/kprobes.h>

#include <hyp/spe-sr.h>

static void __spe_save_host_buffer(u64 *pmscr_el2)
{
	u64 pmblimitr;

	/* Disable guest profiling. */
	write_sysreg_el1(0, SYS_PMSCR);

	pmblimitr = read_sysreg_s(SYS_PMBLIMITR_EL1);
	if (!(pmblimitr & BIT(SYS_PMBLIMITR_EL1_E_SHIFT))) {
		*pmscr_el2 = 0;
		return;
	}

	*pmscr_el2 = read_sysreg_el2(SYS_PMSCR);

	/* Disable profiling at EL2 so we can drain the buffer. */
	write_sysreg_el2(0, SYS_PMSCR);
	isb();

	/*
	 * We're going to change the buffer owning exception level when we
	 * activate traps, drain the buffer now.
	 */
	psb_csync();
	dsb(nsh);
}
NOKPROBE_SYMBOL(__spe_save_host_buffer);

/*
 * Disable host profiling, drain the buffer and save the host SPE context.
 * Extra care must be taken because profiling might be in progress.
 */
void __spe_save_host_state_vhe(struct kvm_vcpu *vcpu,
			       struct kvm_cpu_context *host_ctxt)
{
	u64 pmblimitr, pmscr_el2;

	if (kvm_spe_profiling_stopped(vcpu)) {
		__spe_save_host_buffer(__ctxt_sys_reg(host_ctxt, PMSCR_EL2));
		return;
	}

	/* Disable profiling while the SPE context is being switched. */
	pmscr_el2 = read_sysreg_el2(SYS_PMSCR);
	write_sysreg_el2(__vcpu_sys_reg(vcpu, PMSCR_EL2), SYS_PMSCR);
	isb();

	pmblimitr = read_sysreg_s(SYS_PMBLIMITR_EL1);
	if (pmblimitr & BIT(SYS_PMBLIMITR_EL1_E_SHIFT)) {
		psb_csync();
		dsb(nsh);
		/* Ensure hardware updates to PMBPTR_EL1 are visible. */
		isb();
	}

	ctxt_sys_reg(host_ctxt, PMBPTR_EL1) = read_sysreg_s(SYS_PMBPTR_EL1);
	ctxt_sys_reg(host_ctxt, PMBSR_EL1) = read_sysreg_s(SYS_PMBSR_EL1);
	ctxt_sys_reg(host_ctxt, PMBLIMITR_EL1) = pmblimitr;
	ctxt_sys_reg(host_ctxt, PMSCR_EL2) = pmscr_el2;

	__spe_save_common_state(host_ctxt);
}
NOKPROBE_SYMBOL(__spe_save_host_state_vhe);

/*
 * Drain the guest's buffer and save the SPE state. Profiling is disabled
 * because we're at EL2 and the buffer owning exceptions level is EL1.
 */
void __spe_save_guest_state_vhe(struct kvm_vcpu *vcpu,
				struct kvm_cpu_context *guest_ctxt)
{
	u64 pmblimitr, pmbsr;

	if (kvm_spe_profiling_stopped(vcpu))
		return;

	/*
	 * We're at EL2 and the buffer owning regime is EL1, which means that
	 * profiling is disabled. After we disable traps and restore the host's
	 * MDCR_EL2, profiling will remain disabled because we've disabled it
	 * via PMSCR_EL2 when we saved the host's SPE state. All it's needed
	 * here is to drain the buffer.
	 */
	pmblimitr = read_sysreg_s(SYS_PMBLIMITR_EL1);
	if (pmblimitr & BIT(SYS_PMBLIMITR_EL1_E_SHIFT)) {
		psb_csync();
		dsb(nsh);
		/* Ensure hardware updates to PMBPTR_EL1 are visible. */
		isb();
	}

	ctxt_sys_reg(guest_ctxt, PMBPTR_EL1) = read_sysreg_s(SYS_PMBPTR_EL1);
	pmbsr = read_sysreg_s(SYS_PMBSR_EL1);
	if (pmbsr & BIT(SYS_PMBSR_EL1_S_SHIFT)) {
		ctxt_sys_reg(guest_ctxt, PMBSR_EL1) = pmbsr;
		vcpu->arch.spe.hwirq_level = true;
	}
	/* PMBLIMITR_EL1 is updated only on a trapped write. */
	ctxt_sys_reg(guest_ctxt, PMSCR_EL1) = read_sysreg_el1(SYS_PMSCR);

	__spe_save_common_state(guest_ctxt);
}
NOKPROBE_SYMBOL(__spe_save_guest_state_vhe);

static void __spe_restore_host_buffer(u64 pmscr_el2)
{
	if (!pmscr_el2)
		return;

	/* Synchronize MDCR_EL2 write. */
	isb();

	write_sysreg_el2(pmscr_el2, SYS_PMSCR);
}
NOKPROBE_SYMBOL(__spe_restore_host_buffer);

/*
 * Restore the host SPE context. Special care must be taken because we're
 * potentially resuming a profiling session which was stopped when we saved the
 * host SPE register state.
 */
void __spe_restore_host_state_vhe(struct kvm_vcpu *vcpu,
				  struct kvm_cpu_context *host_ctxt)
{
	if (kvm_spe_profiling_stopped(vcpu)) {
		__spe_restore_host_buffer(ctxt_sys_reg(host_ctxt, PMSCR_EL2));
		return;
	}

	__spe_restore_common_state(host_ctxt);

	write_sysreg_s(ctxt_sys_reg(host_ctxt, PMBPTR_EL1), SYS_PMBPTR_EL1);
	write_sysreg_s(ctxt_sys_reg(host_ctxt, PMBLIMITR_EL1), SYS_PMBLIMITR_EL1);
	write_sysreg_s(ctxt_sys_reg(host_ctxt, PMBSR_EL1), SYS_PMBSR_EL1);

	/*
	 * Make sure buffer pointer and limit is updated first, so we don't end
	 * up in a situation where profiling is enabled and the buffer uses the
	 * values programmed by the guest.
	 *
	 * This also serves to make sure the write to MDCR_EL2 which changes the
	 * buffer owning Exception level is visible.
	 *
	 * After the synchronization, profiling is still disabled at EL2,
	 * because we cleared PMSCR_EL2 when we saved the host context.
	 */
	isb();

	write_sysreg_el2(ctxt_sys_reg(host_ctxt, PMSCR_EL2), SYS_PMSCR);
}
NOKPROBE_SYMBOL(__spe_restore_host_state_vhe);

/*
 * Restore the guest SPE context while profiling is disabled at EL2.
 */
void __spe_restore_guest_state_vhe(struct kvm_vcpu *vcpu,
				   struct kvm_cpu_context *guest_ctxt)
{
	if (kvm_spe_profiling_stopped(vcpu))
		return;

	__spe_restore_common_state(guest_ctxt);

	/*
	 * No synchronization needed here. Profiling is disabled at EL2 because
	 * PMSCR_EL2 has been cleared when saving the host's context, and the
	 * buffer has already been drained.
	 */

	write_sysreg_s(ctxt_sys_reg(guest_ctxt, PMBPTR_EL1), SYS_PMBPTR_EL1);
	/* The buffer management interrupt is virtual. */
	write_sysreg_s(0, SYS_PMBSR_EL1);
	/* The buffer is disabled when the interrupt is asserted. */
	if (vcpu->arch.spe.irq_level)
		write_sysreg_s(0, SYS_PMBLIMITR_EL1);
	else
		write_sysreg_s(ctxt_sys_reg(guest_ctxt, PMBLIMITR_EL1), SYS_PMBLIMITR_EL1);
	write_sysreg_el1(ctxt_sys_reg(guest_ctxt, PMSCR_EL1), SYS_PMSCR);
	/* PMSCR_EL2 has been cleared when saving the host state. */
}
NOKPROBE_SYMBOL(__spe_restore_guest_state_vhe);
