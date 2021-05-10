// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 - Columbia University and Linaro Ltd.
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

#include <asm/kvm_arm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_nested.h>
#include <asm/sysreg.h>

#include "sys_regs.h"

void kvm_init_nested(struct kvm *kvm)
{
	kvm->arch.nested_mmus = NULL;
	kvm->arch.nested_mmus_size = 0;
}

int kvm_vcpu_init_nested(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_s2_mmu *tmp;
	int num_mmus;
	int ret = -ENOMEM;

	if (!test_bit(KVM_ARM_VCPU_HAS_EL2, vcpu->arch.features))
		return 0;

	if (!cpus_have_final_cap(ARM64_HAS_NESTED_VIRT))
		return -EINVAL;

	mutex_lock(&kvm->lock);

	/*
	 * Let's treat memory allocation failures as benign: If we fail to
	 * allocate anything, return an error and keep the allocated array
	 * alive. Userspace may try to recover by intializing the vcpu
	 * again, and there is no reason to affect the whole VM for this.
	 */
	num_mmus = atomic_read(&kvm->online_vcpus) * 2;
	tmp = krealloc(kvm->arch.nested_mmus,
		       num_mmus * sizeof(*kvm->arch.nested_mmus),
		       GFP_KERNEL | __GFP_ZERO);
	if (tmp) {
		if (kvm_init_stage2_mmu(kvm, &tmp[num_mmus - 1]) ||
		    kvm_init_stage2_mmu(kvm, &tmp[num_mmus - 2])) {
			kvm_free_stage2_pgd(&tmp[num_mmus - 1]);
			kvm_free_stage2_pgd(&tmp[num_mmus - 2]);
		} else {
			kvm->arch.nested_mmus_size = num_mmus;
			ret = 0;
		}

		kvm->arch.nested_mmus = tmp;
	}

	mutex_unlock(&kvm->lock);
	return ret;
}

/* Must be called with kvm->lock held */
struct kvm_s2_mmu *lookup_s2_mmu(struct kvm *kvm, u64 vttbr, u64 hcr)
{
	bool nested_stage2_enabled = hcr & HCR_VM;
	int i;

	/* Don't consider the CnP bit for the vttbr match */
	vttbr = vttbr & ~VTTBR_CNP_BIT;

	/*
	 * Two possibilities when looking up a S2 MMU context:
	 *
	 * - either S2 is enabled in the guest, and we need a context that
         *   is S2-enabled and matches the full VTTBR (VMID+BADDR), which
         *   makes it safe from a TLB conflict perspective (a broken guest
         *   won't be able to generate them),
	 *
	 * - or S2 is disabled, and we need a context that is S2-disabled
         *   and matches the VMID only, as all TLBs are tagged by VMID even
         *   if S2 translation is enabled.
	 */
	for (i = 0; i < kvm->arch.nested_mmus_size; i++) {
		struct kvm_s2_mmu *mmu = &kvm->arch.nested_mmus[i];

		if (!kvm_s2_mmu_valid(mmu))
			continue;

		if (nested_stage2_enabled &&
		    mmu->nested_stage2_enabled &&
		    vttbr == mmu->vttbr)
			return mmu;

		if (!nested_stage2_enabled &&
		    !mmu->nested_stage2_enabled &&
		    get_vmid(vttbr) == get_vmid(mmu->vttbr))
			return mmu;
	}
	return NULL;
}

static struct kvm_s2_mmu *get_s2_mmu_nested(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	u64 vttbr = vcpu_read_sys_reg(vcpu, VTTBR_EL2);
	u64 hcr= vcpu_read_sys_reg(vcpu, HCR_EL2);
	struct kvm_s2_mmu *s2_mmu;
	int i;

	s2_mmu = lookup_s2_mmu(kvm, vttbr, hcr);
	if (s2_mmu)
		goto out;

	/*
	 * Make sure we don't always search from the same point, or we
	 * will always reuse a potentially active context, leaving
	 * free contexts unused.
	 */
	for (i = kvm->arch.nested_mmus_next;
	     i < (kvm->arch.nested_mmus_size + kvm->arch.nested_mmus_next);
	     i++) {
		s2_mmu = &kvm->arch.nested_mmus[i % kvm->arch.nested_mmus_size];

		if (atomic_read(&s2_mmu->refcnt) == 0)
			break;
	}
	BUG_ON(atomic_read(&s2_mmu->refcnt)); /* We have struct MMUs to spare */

	/* Set the scene for the next search */
	kvm->arch.nested_mmus_next = (i + 1) % kvm->arch.nested_mmus_size;

	if (kvm_s2_mmu_valid(s2_mmu)) {
		/* Clear the old state */
		kvm_unmap_stage2_range(s2_mmu, 0, kvm_phys_size(kvm));
		if (s2_mmu->vmid.vmid_gen)
			kvm_call_hyp(__kvm_tlb_flush_vmid, s2_mmu);
	}

	/*
	 * The virtual VMID (modulo CnP) will be used as a key when matching
	 * an existing kvm_s2_mmu.
	 */
	s2_mmu->vttbr = vttbr & ~VTTBR_CNP_BIT;
	s2_mmu->nested_stage2_enabled = hcr & HCR_VM;

out:
	atomic_inc(&s2_mmu->refcnt);
	return s2_mmu;
}

void kvm_init_nested_s2_mmu(struct kvm_s2_mmu *mmu)
{
	mmu->vttbr = 1;
	mmu->nested_stage2_enabled = false;
	atomic_set(&mmu->refcnt, 0);
}

void kvm_vcpu_load_hw_mmu(struct kvm_vcpu *vcpu)
{
	if (is_hyp_ctxt(vcpu)) {
		vcpu->arch.hw_mmu = &vcpu->kvm->arch.mmu;
	} else {
		spin_lock(&vcpu->kvm->mmu_lock);
		vcpu->arch.hw_mmu = get_s2_mmu_nested(vcpu);
		spin_unlock(&vcpu->kvm->mmu_lock);
	}
}

void kvm_vcpu_put_hw_mmu(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.hw_mmu != &vcpu->kvm->arch.mmu) {
		atomic_dec(&vcpu->arch.hw_mmu->refcnt);
		vcpu->arch.hw_mmu = NULL;
	}
}

/*
 * Inject wfx to the virtual EL2 if this is not from the virtual EL2 and
 * the virtual HCR_EL2.TWX is set. Otherwise, let the host hypervisor
 * handle this.
 */
int handle_wfx_nested(struct kvm_vcpu *vcpu, bool is_wfe)
{
	u64 hcr_el2 = __vcpu_sys_reg(vcpu, HCR_EL2);

	if (vcpu_mode_el2(vcpu))
		return -EINVAL;

	if ((is_wfe && (hcr_el2 & HCR_TWE)) || (!is_wfe && (hcr_el2 & HCR_TWI)))
		return kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));

	return -EINVAL;
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	int i;

	for (i = 0; i < kvm->arch.nested_mmus_size; i++) {
		struct kvm_s2_mmu *mmu = &kvm->arch.nested_mmus[i];

		WARN_ON(atomic_read(&mmu->refcnt));

		if (!atomic_read(&mmu->refcnt))
			kvm_free_stage2_pgd(mmu);
	}
	kfree(kvm->arch.nested_mmus);
	kvm->arch.nested_mmus = NULL;
	kvm->arch.nested_mmus_size = 0;
	kvm_free_stage2_pgd(&kvm->arch.mmu);
}

/*
 * Our emulated CPU doesn't support all the possible features. For the
 * sake of simplicity (and probably mental sanity), wipe out a number
 * of feature bits we don't intend to support for the time being.
 * This list should get updated as new features get added to the NV
 * support, and new extension to the architecture.
 */
void access_nested_id_reg(struct kvm_vcpu *v, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	u32 id = sys_reg((u32)r->Op0, (u32)r->Op1,
			 (u32)r->CRn, (u32)r->CRm, (u32)r->Op2);
	u64 val, tmp;

	if (!nested_virt_in_use(v))
		return;

	val = p->regval;

	switch (id) {
	case SYS_ID_AA64ISAR0_EL1:
		/* Support everything but O.S. and Range TLBIs */
		val &= ~(FEATURE(ID_AA64ISAR0_TLB)	|
			 GENMASK_ULL(27, 24)		|
			 GENMASK_ULL(3, 0));
		break;

	case SYS_ID_AA64ISAR1_EL1:
		/* Support everything but PtrAuth and Spec Invalidation */
		val &= ~(GENMASK_ULL(63, 56)		|
			 FEATURE(ID_AA64ISAR1_SPECRES)	|
			 FEATURE(ID_AA64ISAR1_GPI)	|
			 FEATURE(ID_AA64ISAR1_GPA)	|
			 FEATURE(ID_AA64ISAR1_API)	|
			 FEATURE(ID_AA64ISAR1_APA));
		break;

	case SYS_ID_AA64PFR0_EL1:
		/* No AMU, MPAM, S-EL2, RAS or SVE */
		val &= ~(GENMASK_ULL(55, 52)		|
			 FEATURE(ID_AA64PFR0_AMU)	|
			 FEATURE(ID_AA64PFR0_MPAM)	|
			 FEATURE(ID_AA64PFR0_SEL2)	|
			 FEATURE(ID_AA64PFR0_RAS)	|
			 FEATURE(ID_AA64PFR0_SVE)	|
			 FEATURE(ID_AA64PFR0_EL3)	|
			 FEATURE(ID_AA64PFR0_EL2));
		/* 64bit EL2/EL3 only */
		val |= FIELD_PREP(FEATURE(ID_AA64PFR0_EL2), 0b0001);
		val |= FIELD_PREP(FEATURE(ID_AA64PFR0_EL3), 0b0001);
		break;

	case SYS_ID_AA64PFR1_EL1:
		/* Only support SSBS */
		val &= FEATURE(ID_AA64PFR1_SSBS);
		break;

	case SYS_ID_AA64MMFR0_EL1:
		/* Hide ECV, FGT, ExS, Secure Memory */
		val &= ~(GENMASK_ULL(63, 43)			|
			 FEATURE(ID_AA64MMFR0_TGRAN4_2)		|
			 FEATURE(ID_AA64MMFR0_TGRAN16_2)	|
			 FEATURE(ID_AA64MMFR0_TGRAN64_2)	|
			 FEATURE(ID_AA64MMFR0_SNSMEM));

		/* Disallow unsupported S2 page sizes */
		switch (PAGE_SIZE) {
		case SZ_64K:
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_TGRAN16_2), 0b0001);
			/* Fall through */
		case SZ_16K:
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_TGRAN4_2), 0b0001);
			/* Fall through */
		case SZ_4K:
			/* Support everything */
			break;
		}
		/* Advertize supported S2 page sizes */
		switch (PAGE_SIZE) {
		case SZ_4K:
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_TGRAN4_2), 0b0010);
			/* Fall through */
		case SZ_16K:
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_TGRAN16_2), 0b0010);
			/* Fall through */
		case SZ_64K:
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_TGRAN64_2), 0b0010);
			break;
		}
		/* Cap PARange to 40bits */
		tmp = FIELD_GET(FEATURE(ID_AA64MMFR0_PARANGE), val);
		if (tmp > 0b0010) {
			val &= ~FEATURE(ID_AA64MMFR0_PARANGE);
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_PARANGE), 0b0010);
		}
		break;

	case SYS_ID_AA64MMFR1_EL1:
		val &= (FEATURE(ID_AA64MMFR1_PAN)	|
			FEATURE(ID_AA64MMFR1_LOR)	|
			FEATURE(ID_AA64MMFR1_HPD)	|
			FEATURE(ID_AA64MMFR1_VHE)	|
			FEATURE(ID_AA64MMFR1_VMIDBITS));
		break;

	case SYS_ID_AA64MMFR2_EL1:
		val &= ~(FEATURE(ID_AA64MMFR2_EVT)	|
			 FEATURE(ID_AA64MMFR2_BBM)	|
			 FEATURE(ID_AA64MMFR2_TTL)	|
			 GENMASK_ULL(47, 44)		|
			 FEATURE(ID_AA64MMFR2_ST)	|
			 FEATURE(ID_AA64MMFR2_CCIDX)	|
			 FEATURE(ID_AA64MMFR2_LVA));

		/* Force TTL support */
		val |= FIELD_PREP(FEATURE(ID_AA64MMFR2_TTL), 0b0001);
		break;

	case SYS_ID_AA64DFR0_EL1:
		/* Only limited support for PMU, Debug, BPs and WPs */
		val &= (FEATURE(ID_AA64DFR0_PMSVER)	|
			FEATURE(ID_AA64DFR0_WRPS)	|
			FEATURE(ID_AA64DFR0_BRPS)	|
			FEATURE(ID_AA64DFR0_DEBUGVER));

		/* Cap PMU to ARMv8.1 */
		tmp = FIELD_GET(FEATURE(ID_AA64DFR0_PMUVER), val);
		if (tmp > 0b0100) {
			val &= ~FEATURE(ID_AA64DFR0_PMUVER);
			val |= FIELD_PREP(FEATURE(ID_AA64DFR0_PMUVER), 0b0100);
		}
		/* Cap Debug to ARMv8.1 */
		tmp = FIELD_GET(FEATURE(ID_AA64DFR0_DEBUGVER), val);
		if (tmp > 0b0111) {
			val &= ~FEATURE(ID_AA64DFR0_DEBUGVER);
			val |= FIELD_PREP(FEATURE(ID_AA64DFR0_DEBUGVER), 0b0111);
		}
		break;

	default:
		/* Unknown register, just wipe it clean */
		val = 0;
		break;
	}

	p->regval = val;
}
