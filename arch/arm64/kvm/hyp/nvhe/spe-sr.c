// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - ARM Ltd
 * Author: Alexandru Elisei <alexandru.elisei@arm.com>
 */

#include <linux/kvm_host.h>

#include <asm/kvm_hyp.h>

#include <hyp/spe-sr.h>

/*
 * The owning exception level remains unchange from EL1 during the world switch,
 * which means that profiling is disabled for as long as we execute at EL2. KVM
 * does not need to explicitely disable profiling, like it does when the VCPU
 * does not have SPE and we change buffer owning exception level, nor does it
 * need to do any synchronization around sysreg save/restore.
 */

void __spe_save_host_state_nvhe(struct kvm_vcpu *vcpu,
				struct kvm_cpu_context *host_ctxt)
{
	u64 pmblimitr;

	pmblimitr = read_sysreg_s(SYS_PMBLIMITR_EL1);
	if (pmblimitr & BIT(SYS_PMBLIMITR_EL1_E_SHIFT)) {
		psb_csync();
		dsb(nsh);
		/*
		 * The buffer performs indirect writes to system registers, a
		 * context synchronization event is needed before the new
		 * PMBPTR_EL1 value is visible to subsequent direct reads.
		 */
		isb();
	}

	ctxt_sys_reg(host_ctxt, PMBPTR_EL1) = read_sysreg_s(SYS_PMBPTR_EL1);
	ctxt_sys_reg(host_ctxt, PMBSR_EL1) = read_sysreg_s(SYS_PMBSR_EL1);
	ctxt_sys_reg(host_ctxt, PMBLIMITR_EL1) = pmblimitr;
	ctxt_sys_reg(host_ctxt, PMSCR_EL1) = read_sysreg_s(SYS_PMSCR_EL1);
	ctxt_sys_reg(host_ctxt, PMSCR_EL2) = read_sysreg_el2(SYS_PMSCR);

	__spe_save_common_state(host_ctxt);
}

void __spe_save_guest_state_nvhe(struct kvm_vcpu *vcpu,
				 struct kvm_cpu_context *guest_ctxt)
{
	if (read_sysreg_s(SYS_PMBLIMITR_EL1) & BIT(SYS_PMBLIMITR_EL1_E_SHIFT)) {
		psb_csync();
		dsb(nsh);
		/* Ensure hardware updates to PMBPTR_EL1 are visible. */
		isb();
	}

	ctxt_sys_reg(guest_ctxt, PMBPTR_EL1) = read_sysreg_s(SYS_PMBPTR_EL1);
	ctxt_sys_reg(guest_ctxt, PMBSR_EL1) = read_sysreg_s(SYS_PMBSR_EL1);
	/* PMBLIMITR_EL1 is updated only on a trapped write. */
	ctxt_sys_reg(guest_ctxt, PMSCR_EL1) = read_sysreg_s(SYS_PMSCR_EL1);

	__spe_save_common_state(guest_ctxt);
}

void __spe_restore_host_state_nvhe(struct kvm_vcpu *vcpu,
				   struct kvm_cpu_context *host_ctxt)
{
	__spe_restore_common_state(host_ctxt);

	write_sysreg_s(ctxt_sys_reg(host_ctxt, PMBPTR_EL1), SYS_PMBPTR_EL1);
	write_sysreg_s(ctxt_sys_reg(host_ctxt, PMBSR_EL1), SYS_PMBSR_EL1);
	write_sysreg_s(ctxt_sys_reg(host_ctxt, PMBLIMITR_EL1), SYS_PMBLIMITR_EL1);
	write_sysreg_s(ctxt_sys_reg(host_ctxt, PMSCR_EL1), SYS_PMSCR_EL1);
	write_sysreg_el2(ctxt_sys_reg(host_ctxt, PMSCR_EL2), SYS_PMSCR);
}

void __spe_restore_guest_state_nvhe(struct kvm_vcpu *vcpu,
				    struct kvm_cpu_context *guest_ctxt)
{
	__spe_restore_common_state(guest_ctxt);

	write_sysreg_s(ctxt_sys_reg(guest_ctxt, PMBPTR_EL1), SYS_PMBPTR_EL1);
	write_sysreg_s(ctxt_sys_reg(guest_ctxt, PMBSR_EL1), SYS_PMBSR_EL1);
	write_sysreg_s(ctxt_sys_reg(guest_ctxt, PMBLIMITR_EL1), SYS_PMBLIMITR_EL1);
	write_sysreg_s(ctxt_sys_reg(guest_ctxt, PMSCR_EL1), SYS_PMSCR_EL1);
	write_sysreg_el2(ctxt_sys_reg(guest_ctxt, PMSCR_EL2), SYS_PMSCR);
}
