// SPDX-License-Identifier: GPL-2.0-only

#include <linux/arm-smccc.h>
#include <linux/kvm_host.h>

#include <asm/kvm_mmu.h>
#include <asm/paravirt.h>

#include <kvm/arm_psci.h>

int kvm_init_vcpu_state(struct kvm_vcpu *vcpu, gpa_t addr)
{
	struct kvm *kvm = vcpu->kvm;
	int ret;
	u64 idx;

	if (kvm_arm_is_vcpu_state_enabled(&vcpu->arch))
		return 0;

	idx = srcu_read_lock(&kvm->srcu);
	ret = kvm_gfn_to_hva_cache_init(vcpu->kvm,
					&vcpu->arch.vcpu_state.ghc,
					addr,
					sizeof(struct vcpu_state));
	srcu_read_unlock(&kvm->srcu, idx);

	if (!ret)
		vcpu->arch.vcpu_state.base = addr;
	return ret;
}

int kvm_release_vcpu_state(struct kvm_vcpu *vcpu)
{
	if (!kvm_arm_is_vcpu_state_enabled(&vcpu->arch))
		return 0;

	kvm_arm_vcpu_state_init(&vcpu->arch);
	return 0;
}

void kvm_update_vcpu_preempted(struct kvm_vcpu *vcpu, bool preempted)
{
	struct kvm *kvm = vcpu->kvm;
	u64 idx;

	if (!kvm_arm_is_vcpu_state_enabled(&vcpu->arch))
		return;

	/*
	 * This function is called from atomic context, so we need to
	 * disable page faults. kvm_write_guest_cached() will call
	 * might_fault().
	 */
	pagefault_disable();
	/*
	 * Need to take the SRCU lock because kvm_write_guest_offset_cached()
	 * calls kvm_memslots();
	 */
	idx = srcu_read_lock(&kvm->srcu);
	kvm_write_guest_cached(kvm, &vcpu->arch.vcpu_state.ghc,
			       &preempted, sizeof(bool));
	srcu_read_unlock(&kvm->srcu, idx);
	pagefault_enable();
}
