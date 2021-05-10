// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <hyp/adjust_pc.h>
#include <hyp/switch.h>

#include <linux/arm-smccc.h>
#include <linux/kvm_host.h>
#include <linux/types.h>
#include <linux/jump_label.h>
#include <uapi/linux/psci.h>

#include <kvm/arm_psci.h>

#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/kprobes.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/fpsimd.h>
#include <asm/debug-monitors.h>
#include <asm/processor.h>
#include <asm/thread_info.h>

/* VHE specific context */
DEFINE_PER_CPU(struct kvm_host_data, kvm_host_data);
DEFINE_PER_CPU(struct kvm_cpu_context, kvm_hyp_ctxt);
DEFINE_PER_CPU(unsigned long, kvm_hyp_vector);

static void __activate_traps(struct kvm_vcpu *vcpu)
{
	u64 hcr = vcpu->arch.hcr_el2;
	u64 val;

	if (is_hyp_ctxt(vcpu)) {
		hcr |= HCR_NV;

		if (!vcpu_el2_e2h_is_set(vcpu)) {
			/*
			 * For a guest hypervisor on v8.0, trap and emulate
			 * the EL1 virtual memory control register accesses
			 * as well as the AT S1 operations.
			 */
			if (enhanced_nested_virt_in_use(vcpu)) {
				hcr &= ~HCR_TVM;
			} else {
				hcr |= HCR_TVM | HCR_TRVM | HCR_TTLB;
			}

			hcr |= HCR_AT | HCR_NV1;
		} else {
			/*
			 * For a guest hypervisor on v8.1 (VHE), allow to
			 * access the EL1 virtual memory control registers
			 * natively. These accesses are to access EL2 register
			 * states.
			 * Note that we still need to respect the virtual
			 * HCR_EL2 state.
			 */
			u64 vhcr_el2 = __vcpu_sys_reg(vcpu, HCR_EL2);

			/*
			 * We already set TVM to handle set/way cache maint
			 * ops traps, this somewhat collides with the nested
			 * virt trapping for nVHE. So turn this off for now
			 * here, in the hope that VHE guests won't ever do this.
			 * TODO: find out whether it's worth to support both
			 * cases at the same time.
			 */
			hcr &= ~HCR_TVM;

			hcr |= vhcr_el2 & (HCR_TVM | HCR_TRVM);

			/*
			 * If we're using the EL1 translation regime
			 * (TGE clear), then ensure that AT S1 and
			 * TLBI E1 ops are trapped too.
			 */
			if (!vcpu_el2_tge_is_set(vcpu))
				hcr |= HCR_AT | HCR_TTLB;
		}

		if (enhanced_nested_virt_in_use(vcpu)) {
			hcr |= HCR_AT | HCR_TTLB | HCR_NV2;
			write_sysreg_s(vcpu->arch.ctxt.vncr_array,
				       SYS_VNCR_EL2);
		}
	} else if (nested_virt_in_use(vcpu)) {
		hcr |= __vcpu_sys_reg(vcpu, HCR_EL2);
	}

	___activate_traps(vcpu, hcr);

	val = read_sysreg(cpacr_el1);
	val |= CPACR_EL1_TTA;
	val &= ~CPACR_EL1_ZEN;

	/*
	 * With VHE (HCR.E2H == 1), accesses to CPACR_EL1 are routed to
	 * CPTR_EL2. In general, CPACR_EL1 has the same layout as CPTR_EL2,
	 * except for some missing controls, such as TAM.
	 * In this case, CPTR_EL2.TAM has the same position with or without
	 * VHE (HCR.E2H == 1) which allows us to use here the CPTR_EL2.TAM
	 * shift value for trapping the AMU accesses.
	 */

	val |= CPTR_EL2_TAM;

	if (update_fp_enabled(vcpu)) {
		if (vcpu_has_sve(vcpu))
			val |= CPACR_EL1_ZEN;
	} else {
		val &= ~CPACR_EL1_FPEN;
		__activate_traps_fpsimd32(vcpu);
	}

	if (vcpu_mode_el2(vcpu) && !vcpu_el2_e2h_is_set(vcpu))
		val |= CPTR_EL2_TCPAC;
	
	write_sysreg(val, cpacr_el1);

	write_sysreg(__this_cpu_read(kvm_hyp_vector), vbar_el1);
}
NOKPROBE_SYMBOL(__activate_traps);

static void __deactivate_traps(struct kvm_vcpu *vcpu)
{
	extern char vectors[];	/* kernel exception vectors */

	___deactivate_traps(vcpu);

	write_sysreg(HCR_HOST_VHE_FLAGS, hcr_el2);

	/*
	 * ARM errata 1165522 and 1530923 require the actual execution of the
	 * above before we can switch to the EL2/EL0 translation regime used by
	 * the host.
	 */
	asm(ALTERNATIVE("nop", "isb", ARM64_WORKAROUND_SPECULATIVE_AT));

	write_sysreg(CPACR_EL1_DEFAULT, cpacr_el1);
	write_sysreg(vectors, vbar_el1);
}
NOKPROBE_SYMBOL(__deactivate_traps);

void activate_traps_vhe_load(struct kvm_vcpu *vcpu)
{
	__activate_traps_common(vcpu);
}

void deactivate_traps_vhe_put(void)
{
	u64 mdcr_el2 = read_sysreg(mdcr_el2);

	mdcr_el2 &= MDCR_EL2_HPMN_MASK |
		    MDCR_EL2_E2PB_MASK << MDCR_EL2_E2PB_SHIFT |
		    MDCR_EL2_TPMS;

	write_sysreg(mdcr_el2, mdcr_el2);

	__deactivate_traps_common();
}

static bool __hyp_handle_tlbi_el1(struct kvm_vcpu *vcpu)
{
	u32 instr;
	u64 val;

	/*
	 * Ideally, we would never trap on EL1 TLB invalidations when the
	 * guest's HCR_EL2.{E2H,TGE} == {1,1}. But "thanks" to ARMv8.4, we
	 * don't trap writes to HCR_EL2, meaning that we can't track
	 * changes to the virtual TGE bit. So we leave HCR_EL2.TTLB set on
	 * the host. Oopsie...
	 *
	 * In order to speed-up EL1 TLBIs from the vEL2 guest when TGE is
	 * set, try and handle these invalidation as quickly as possible,
	 * without fully exiting (unless this needs forwarding).
	 */
	if (kvm_vcpu_trap_get_class(vcpu) != ESR_ELx_EC_SYS64 ||
	    !vcpu_mode_el2(vcpu) ||
	    (__vcpu_sys_reg(vcpu, HCR_EL2) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE))
		return false;

	instr = esr_sys64_to_sysreg(kvm_vcpu_get_esr(vcpu));
	if (sys_reg_Op0(instr) != TLBI_Op0 ||
	    sys_reg_Op1(instr) != TLBI_Op1_EL1)
		return false;

	val = vcpu_get_reg(vcpu, kvm_vcpu_sys_get_rt(vcpu));
	__kvm_tlb_el1_instr(NULL, val, instr);
	__kvm_skip_instr(vcpu);

	return true;
}

static bool __hyp_handle_eret(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *ctxt = &vcpu->arch.ctxt;
	u64 spsr, mode;

	/*
	 * Going through the whole put/load motions is a waste of time
	 * if this is a VHE guest hypervisor returning to its own
	 * userspace, or the hypervisor performing a local exception
	 * return. No need to save/restore registers, no need to
	 * switch S2 MMU. Just do the canonical ERET. Unless the trap
	 * has to be forwarded further down the line, of course...
	 */
	if (kvm_vcpu_trap_get_class(vcpu) != ESR_ELx_EC_ERET)
		return false;

	/*
	 * Let the trap forwarding be handled by the normal exception
	 * handling code.
	 */
	if (__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_NV)
		return false;

	spsr = read_sysreg_el1(SYS_SPSR);
	spsr = __fixup_spsr_el2_read(ctxt, spsr);
	mode = spsr & (PSR_MODE_MASK | PSR_MODE32_BIT);

	switch (mode) {
	case PSR_MODE_EL0t:
		if (!(vcpu_el2_e2h_is_set(vcpu) && vcpu_el2_tge_is_set(vcpu)))
			return false;
		break;
	case PSR_MODE_EL2t:
		mode = PSR_MODE_EL1t;
		break;
	case PSR_MODE_EL2h:
		mode = PSR_MODE_EL1h;
		break;
	default:
		return false;
	}

	spsr = (spsr & ~(PSR_MODE_MASK | PSR_MODE32_BIT)) | mode;

	write_sysreg_el2(spsr, SYS_SPSR);
	write_sysreg_el2(read_sysreg_el1(SYS_ELR), SYS_ELR);

	return true;
}

static bool fixup_guest_exit_vhe(struct kvm_vcpu *vcpu, u64 *exit_code,
				 bool hyp_ctxt)
{
	u64 pstate = read_sysreg_el2(SYS_SPSR);

	/*
	 * Sync pstate back as early as possible, so that is_hyp_ctxt()
	 * reflects the exact context. It is otherwise possible to get
	 * confused with a VHE guest and ARMv8.4-NV, such as:
	 *
	 * (1) enter guest in host EL0
	 * (2) guest traps to guest vEL2 (no hypervisor intervention)
	 * (3) guest clears virtual HCR_EL2.TGE (no trap either)
	 * (4) host interrupt fires, exit
	 * (5) is_hyp_ctxt() now says "guest" (pstate.M==EL1 and TGE==0)
	 *
	 * If host preemption occurs, vcpu_load/put() will be very confused.
	 *
	 * Consider this as the prologue before the fixup prologue...
	 */

	if (unlikely(hyp_ctxt)) {
		u64 mode = pstate & PSR_MODE_MASK;

		switch (mode) {
		case PSR_MODE_EL1t:
			mode = PSR_MODE_EL2t;
			break;
		case PSR_MODE_EL1h:
			mode = PSR_MODE_EL2h;
			break;
		}

		pstate = (pstate & ~PSR_MODE_MASK) | mode;
	}

	*vcpu_cpsr(vcpu) = pstate;

	fixup_guest_exit_prologue(vcpu, exit_code);

	if (*exit_code == ARM_EXCEPTION_TRAP) {
		if (__hyp_handle_eret(vcpu))
			return true;

		if (__hyp_handle_tlbi_el1(vcpu))
			return true;
	}

	return fixup_guest_exit(vcpu, exit_code);
}

/* Switch to the guest for VHE systems running in EL2 */
static int __kvm_vcpu_run_vhe(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *host_ctxt;
	struct kvm_cpu_context *guest_ctxt;
	u64 exit_code;
	bool hyp_ctxt;

	host_ctxt = &this_cpu_ptr(&kvm_host_data)->host_ctxt;
	host_ctxt->__hyp_running_vcpu = vcpu;
	guest_ctxt = &vcpu->arch.ctxt;

	sysreg_save_host_state_vhe(host_ctxt);

	/*
	 * ARM erratum 1165522 requires us to configure both stage 1 and
	 * stage 2 translation for the guest context before we clear
	 * HCR_EL2.TGE.
	 *
	 * We have already configured the guest's stage 1 translation in
	 * kvm_vcpu_load_sysregs_vhe above.  We must now call
	 * __load_guest_stage2 before __activate_traps, because
	 * __load_guest_stage2 configures stage 2 translation, and
	 * __activate_traps clear HCR_EL2.TGE (among other things).
	 */
	__load_guest_stage2(vcpu->arch.hw_mmu);
	__activate_traps(vcpu);

	__adjust_pc(vcpu);

	sysreg_restore_guest_state_vhe(guest_ctxt);
	__debug_switch_to_guest(vcpu);

	/*
	 * Being in HYP context or not is an invariant here. If we enter in
	 * a given context, we exit in the same context. We can thus only
	 * sample the context once.
	 */
	WRITE_ONCE(hyp_ctxt, is_hyp_ctxt(vcpu));

	do {
		/* Jump in the fire! */
		exit_code = __guest_enter(vcpu);

		/* And we're baaack! */
	} while (fixup_guest_exit_vhe(vcpu, &exit_code, READ_ONCE(hyp_ctxt)));

	sysreg_save_guest_state_vhe(guest_ctxt);

	__deactivate_traps(vcpu);

	sysreg_restore_host_state_vhe(host_ctxt);

	if (vcpu->arch.flags & KVM_ARM64_FP_ENABLED)
		__fpsimd_save_fpexc32(vcpu);

	__debug_switch_to_host(vcpu);

	return exit_code;
}
NOKPROBE_SYMBOL(__kvm_vcpu_run_vhe);

int __kvm_vcpu_run(struct kvm_vcpu *vcpu)
{
	int ret;

	local_daif_mask();

	/*
	 * Having IRQs masked via PMR when entering the guest means the GIC
	 * will not signal the CPU of interrupts of lower priority, and the
	 * only way to get out will be via guest exceptions.
	 * Naturally, we want to avoid this.
	 *
	 * local_daif_mask() already sets GIC_PRIO_PSR_I_SET, we just need a
	 * dsb to ensure the redistributor is forwards EL2 IRQs to the CPU.
	 */
	pmr_sync();

	ret = __kvm_vcpu_run_vhe(vcpu);

	/*
	 * local_daif_restore() takes care to properly restore PSTATE.DAIF
	 * and the GIC PMR if the host is using IRQ priorities.
	 */
	local_daif_restore(DAIF_PROCCTX_NOIRQ);

	/*
	 * When we exit from the guest we change a number of CPU configuration
	 * parameters, such as traps.  Make sure these changes take effect
	 * before running the host or additional guests.
	 */
	isb();

	return ret;
}

static void __hyp_call_panic(u64 spsr, u64 elr, u64 par)
{
	struct kvm_cpu_context *host_ctxt;
	struct kvm_vcpu *vcpu;

	host_ctxt = &this_cpu_ptr(&kvm_host_data)->host_ctxt;
	vcpu = host_ctxt->__hyp_running_vcpu;

	__deactivate_traps(vcpu);
	sysreg_restore_host_state_vhe(host_ctxt);

	panic("HYP panic:\nPS:%08llx PC:%016llx ESR:%08llx\nFAR:%016llx HPFAR:%016llx PAR:%016llx\nVCPU:%p\n",
	      spsr, elr,
	      read_sysreg_el2(SYS_ESR), read_sysreg_el2(SYS_FAR),
	      read_sysreg(hpfar_el2), par, vcpu);
}
NOKPROBE_SYMBOL(__hyp_call_panic);

void __noreturn hyp_panic(void)
{
	u64 spsr = read_sysreg_el2(SYS_SPSR);
	u64 elr = read_sysreg_el2(SYS_ELR);
	u64 par = read_sysreg_par();

	__hyp_call_panic(spsr, elr, par);
	unreachable();
}

asmlinkage void kvm_unexpected_el2_exception(void)
{
	return __kvm_unexpected_el2_exception();
}
