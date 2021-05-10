/*
 * Copyright (C) 2016 - Linaro and Columbia University
 * Author: Jintack Lim <jintack.lim@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_nested.h>

#include "hyp/include/hyp/adjust_pc.h"

#include "trace.h"

bool __forward_traps(struct kvm_vcpu *vcpu, unsigned int reg, u64 control_bit)
{
	bool control_bit_set;

	if (!nested_virt_in_use(vcpu))
		return false;

	control_bit_set = __vcpu_sys_reg(vcpu, reg) & control_bit;
	if (!vcpu_mode_el2(vcpu) && control_bit_set) {
		kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));
		return true;
	}
	return false;
}

bool forward_traps(struct kvm_vcpu *vcpu, u64 control_bit)
{
	return __forward_traps(vcpu, HCR_EL2, control_bit);
}

bool forward_nv_traps(struct kvm_vcpu *vcpu)
{
	return forward_traps(vcpu, HCR_NV);
}

void kvm_emulate_nested_eret(struct kvm_vcpu *vcpu)
{
	u64 spsr, elr, mode;
	bool direct_eret;

	/*
	 * Forward this trap to the virtual EL2 if the virtual
	 * HCR_EL2.NV bit is set and this is coming from !EL2.
	 */
	if (forward_nv_traps(vcpu))
		return;

	/*
	 * Going through the whole put/load motions is a waste of time
	 * if this is a VHE guest hypervisor returning to its own
	 * userspace, or the hypervisor performing a local exception
	 * return. No need to save/restore registers, no need to
	 * switch S2 MMU. Just do the canonical ERET.
	 */
	spsr = vcpu_read_sys_reg(vcpu, SPSR_EL2);
	mode = spsr & (PSR_MODE_MASK | PSR_MODE32_BIT);

	direct_eret  = (mode == PSR_MODE_EL0t &&
			vcpu_el2_e2h_is_set(vcpu) &&
			vcpu_el2_tge_is_set(vcpu));
	direct_eret |= (mode == PSR_MODE_EL2h || mode == PSR_MODE_EL2t);

	if (direct_eret) {
		*vcpu_pc(vcpu) = vcpu_read_sys_reg(vcpu, ELR_EL2);
		*vcpu_cpsr(vcpu) = spsr;
		trace_kvm_nested_eret(vcpu, *vcpu_pc(vcpu), spsr);
		return;
	}

	preempt_disable();
	kvm_arch_vcpu_put(vcpu);

	elr = __vcpu_sys_reg(vcpu, ELR_EL2);

	trace_kvm_nested_eret(vcpu, elr, spsr);

	/*
	 * Note that the current exception level is always the virtual EL2,
	 * since we set HCR_EL2.NV bit only when entering the virtual EL2.
	 */
	*vcpu_pc(vcpu) = elr;
	*vcpu_cpsr(vcpu) = spsr;

	kvm_arch_vcpu_load(vcpu, smp_processor_id());
	preempt_enable();
}

static void kvm_inject_el2_exception(struct kvm_vcpu *vcpu, u64 esr_el2,
				     enum exception_type type)
{
	trace_kvm_inject_nested_exception(vcpu, esr_el2, type);

	switch (type) {
	case except_type_sync:
		vcpu->arch.flags |= KVM_ARM64_EXCEPT_AA64_ELx_SYNC;
		break;
	case except_type_irq:
		vcpu->arch.flags |= KVM_ARM64_EXCEPT_AA64_ELx_IRQ;
		break;
	default:
		WARN_ONCE(1, "Unsupported EL2 exception injection %d\n", type);
	}

	vcpu->arch.flags |= (KVM_ARM64_EXCEPT_AA64_EL2		|
			     KVM_ARM64_PENDING_EXCEPTION);

	vcpu_write_sys_reg(vcpu, esr_el2, ESR_EL2);
}

/*
 * Emulate taking an exception to EL2.
 * See ARM ARM J8.1.2 AArch64.TakeException()
 */
static int kvm_inject_nested(struct kvm_vcpu *vcpu, u64 esr_el2,
			     enum exception_type type)
{
	u64 pstate, mode;
	bool direct_inject;

	if (!nested_virt_in_use(vcpu)) {
		kvm_err("Unexpected call to %s for the non-nesting configuration\n",
				__func__);
		return -EINVAL;
	}

	/*
	 * As for ERET, we can avoid doing too much on the injection path by
	 * checking that we either took the exception from a VHE host
	 * userspace or from vEL2. In these cases, there is no change in
	 * translation regime (or anything else), so let's do as little as
	 * possible.
	 */
	pstate = *vcpu_cpsr(vcpu);
	mode = pstate & (PSR_MODE_MASK | PSR_MODE32_BIT);

	direct_inject  = (mode == PSR_MODE_EL0t &&
			  vcpu_el2_e2h_is_set(vcpu) &&
			  vcpu_el2_tge_is_set(vcpu));
	direct_inject |= (mode == PSR_MODE_EL2h || mode == PSR_MODE_EL2t);

	if (direct_inject) {
		kvm_inject_el2_exception(vcpu, esr_el2, type);
		return 1;
	}

	preempt_disable();
	kvm_arch_vcpu_put(vcpu);

	kvm_inject_el2_exception(vcpu, esr_el2, type);

	/*
	 * A hard requirement is that a switch between EL1 and EL2
	 * contexts has to happen between a put/load, so that we can
	 * pick the correct timer and interrupt configuration, among
	 * other things.
	 *
	 * Make sure the exception actually took place before we load
	 * the new context.
	 */
	__adjust_pc(vcpu);

	kvm_arch_vcpu_load(vcpu, smp_processor_id());
	preempt_enable();

	return 1;
}

int kvm_inject_nested_sync(struct kvm_vcpu *vcpu, u64 esr_el2)
{
	return kvm_inject_nested(vcpu, esr_el2, except_type_sync);
}

int kvm_inject_nested_irq(struct kvm_vcpu *vcpu)
{
	/*
	 * Do not inject an irq if the:
	 *  - Current exception level is EL2, and
	 *  - virtual HCR_EL2.TGE == 0
	 *  - virtual HCR_EL2.IMO == 0
	 *
	 * See Table D1-17 "Physical interrupt target and masking when EL3 is
	 * not implemented and EL2 is implemented" in ARM DDI 0487C.a.
	 */

	if (vcpu_mode_el2(vcpu) && !vcpu_el2_tge_is_set(vcpu) &&
	    !(__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_IMO))
		return 1;

	/* esr_el2 value doesn't matter for exits due to irqs. */
	return kvm_inject_nested(vcpu, 0, except_type_irq);
}
