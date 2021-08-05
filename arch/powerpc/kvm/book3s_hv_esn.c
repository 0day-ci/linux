// SPDX-License-Identifier: GPL-2.0
/*
 * Async page fault support via PAPR Expropriation/Subvention Notification
 * option(ESN)
 *
 * Copyright 2020 Bharata B Rao, IBM Corp. <bharata@linux.ibm.com>
 */

#include <linux/kvm_host.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s_esn.h>

static DEFINE_SPINLOCK(async_exp_lock); /* for updating exp_corr_nr */
static DEFINE_SPINLOCK(async_sns_lock); /* SNS buffer updated under this lock */

int kvm_arch_setup_async_pf(struct kvm_vcpu *vcpu,
			       unsigned long gpa, unsigned long hva)
{
	struct kvm_arch_async_pf arch;
	struct lppaca *vpa = vcpu->arch.vpa.pinned_addr;
	u64 msr = kvmppc_get_msr(vcpu);
	struct kvmppc_sns *sns = &vcpu->kvm->arch.sns;

	/*
	 * If VPA hasn't been registered yet, can't support
	 * async pf.
	 */
	if (!vpa)
		return 0;

	/*
	 * If SNS memory area hasn't been registered yet,
	 * can't support async pf.
	 */
	if (!vcpu->kvm->arch.sns.eq)
		return 0;

	/*
	 * If guest hasn't enabled expropriation interrupt,
	 * don't try async pf.
	 */
	if (!(vpa->byte_b9 & LPPACA_EXP_INT_ENABLED))
		return 0;

	/*
	 * If the fault is in the guest kernel, don,t
	 * try async pf.
	 */
	if (!(msr & MSR_PR) && !(msr & MSR_HV))
		return 0;

	spin_lock(&async_sns_lock);
	/*
	 * Check if subvention event queue can
	 * overflow, if so, don't try async pf.
	 */
	if (*(sns->eq + sns->next_eq_entry)) {
		pr_err("%s: SNS buffer overflow\n", __func__);
		spin_unlock(&async_sns_lock);
		return 0;
	}
	spin_unlock(&async_sns_lock);

	/*
	 * TODO:
	 *
	 * 1. Update exp flags bit 7 to 1
	 * ("The Subvened page data will be restored")
	 *
	 * 2. Check if request to this page has been
	 * notified to guest earlier, if so send back
	 * the same exp corr number.
	 *
	 * 3. exp_corr_nr could be a random but non-zero
	 * number. Not taking care of wrapping here. Fix
	 * it.
	 */
	spin_lock(&async_exp_lock);
	vpa->exp_corr_nr = cpu_to_be16(vcpu->kvm->arch.sns.exp_corr_nr);
	arch.exp_token = vcpu->kvm->arch.sns.exp_corr_nr++;
	spin_unlock(&async_exp_lock);

	return kvm_setup_async_pf(vcpu, gpa, hva, &arch);
}

bool kvm_arch_async_page_not_present(struct kvm_vcpu *vcpu,
				     struct kvm_async_pf *work)
{
	/* Inject DSI to guest with srr1 bit 46 set */
	kvmppc_core_queue_data_storage(vcpu, kvmppc_get_dar(vcpu), DSISR_NOHPTE, SRR1_PROGTRAP);
	return true;
}

void kvm_arch_async_page_present(struct kvm_vcpu *vcpu,
				 struct kvm_async_pf *work)
{
	struct kvmppc_sns *sns = &vcpu->kvm->arch.sns;

	spin_lock(&async_sns_lock);
	if (*sns->eq_cntrl != SNS_EQ_CNTRL_TRIGGER) {
		pr_err("%s: SNS Notification Trigger not set by guest\n", __func__);
		spin_unlock(&async_sns_lock);
		/* TODO: Terminate the guest? */
		return;
	}

	if (arch_cmpxchg(sns->eq + sns->next_eq_entry, 0,
	    work->arch.exp_token)) {
		*sns->eq_state |= SNS_EQ_STATE_OVERFLOW;
		pr_err("%s: SNS buffer overflow\n", __func__);
		spin_unlock(&async_sns_lock);
		/* TODO: Terminate the guest? */
		return;
	}

	sns->next_eq_entry = (sns->next_eq_entry + 1) % sns->nr_eq_entries;
	spin_unlock(&async_sns_lock);

	/*
	 * Request a guest exit so that ESN virtual interrupt can
	 * be injected by QEMU.
	 */
	kvm_make_request(KVM_REQ_ESN_EXIT, vcpu);
}

void kvm_arch_async_page_ready(struct kvm_vcpu *vcpu, struct kvm_async_pf *work)
{
	/* We will inject the page directly */
}

bool kvm_arch_can_dequeue_async_page_present(struct kvm_vcpu *vcpu)
{
	/*
	 * PowerPC will always inject the page directly,
	 * but we still want check_async_completion to cleanup
	 */
	return true;
}

long kvm_vm_ioctl_set_sns(struct kvm *kvm, struct kvm_ppc_sns_reg *sns_reg)
{
	unsigned long nb;

	/* Deregister */
	if (sns_reg->addr == -1) {
		if (!kvm->arch.sns.hva)
			return 0;

		pr_info("%s: Deregistering SNS buffer for LPID %d\n",
			__func__, kvm->arch.lpid);
		kvmppc_unpin_guest_page(kvm, kvm->arch.sns.hva, kvm->arch.sns.gpa, false);
		kvm->arch.sns.gpa = -1;
		kvm->arch.sns.hva = 0;
		return 0;
	}

	/*
	 * Already registered with the same address?
	 */
	if (sns_reg->addr == kvm->arch.sns.gpa)
		return 0;

	/* If previous registration exists, free it */
	if (kvm->arch.sns.hva) {
		pr_info("%s: Deregistering Previous SNS buffer for LPID %d\n",
			__func__, kvm->arch.lpid);
		kvmppc_unpin_guest_page(kvm, kvm->arch.sns.hva, kvm->arch.sns.gpa, false);
		kvm->arch.sns.gpa = -1;
		kvm->arch.sns.hva = 0;
	}

	kvm->arch.sns.gpa = sns_reg->addr;
	kvm->arch.sns.hva = kvmppc_pin_guest_page(kvm, kvm->arch.sns.gpa, &nb);
	kvm->arch.sns.len = sns_reg->len;
	kvm->arch.sns.nr_eq_entries = (kvm->arch.sns.len - 2) / sizeof(uint16_t);
	kvm->arch.sns.next_eq_entry = 0;
	kvm->arch.sns.eq = kvm->arch.sns.hva + 2;
	kvm->arch.sns.eq_cntrl = kvm->arch.sns.hva;
	kvm->arch.sns.eq_state = kvm->arch.sns.hva + 1;
	kvm->arch.sns.exp_corr_nr = 1; /* Should be non-zero */

	*(kvm->arch.sns.eq_state) = SNS_EQ_STATE_OPERATIONAL;

	pr_info("%s: Registering SNS buffer for LPID %d sns_addr %llx eq %lx\n",
		__func__, kvm->arch.lpid, sns_reg->addr,
		(unsigned long)kvm->arch.sns.eq);

	return 0;
}
