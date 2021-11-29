/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM64_KVM_NESTED_H
#define __ARM64_KVM_NESTED_H

#include <linux/bitfield.h>
#include <linux/kvm_host.h>

static inline bool nested_virt_in_use(const struct kvm_vcpu *vcpu)
{
	return (!__is_defined(__KVM_NVHE_HYPERVISOR__) &&
		cpus_have_final_cap(ARM64_HAS_NESTED_VIRT) &&
		test_bit(KVM_ARM_VCPU_HAS_EL2, vcpu->arch.features));
}

/* Translation helpers from non-VHE EL2 to EL1 */
static inline u64 tcr_el2_ips_to_tcr_el1_ps(u64 tcr_el2)
{
	return (u64)FIELD_GET(TCR_EL2_PS_MASK, tcr_el2) << TCR_IPS_SHIFT;
}

static inline u64 translate_tcr_el2_to_tcr_el1(u64 tcr)
{
	return TCR_EPD1_MASK |				/* disable TTBR1_EL1 */
	       ((tcr & TCR_EL2_TBI) ? TCR_TBI0 : 0) |
	       tcr_el2_ips_to_tcr_el1_ps(tcr) |
	       (tcr & TCR_EL2_TG0_MASK) |
	       (tcr & TCR_EL2_ORGN0_MASK) |
	       (tcr & TCR_EL2_IRGN0_MASK) |
	       (tcr & TCR_EL2_T0SZ_MASK);
}

static inline u64 translate_cptr_el2_to_cpacr_el1(u64 cptr_el2)
{
	u64 cpacr_el1 = 0;

	if (!(cptr_el2 & CPTR_EL2_TFP))
		cpacr_el1 |= CPACR_EL1_FPEN;
	if (cptr_el2 & CPTR_EL2_TTA)
		cpacr_el1 |= CPACR_EL1_TTA;
	if (!(cptr_el2 & CPTR_EL2_TZ))
		cpacr_el1 |= CPACR_EL1_ZEN;

	return cpacr_el1;
}

static inline u64 translate_sctlr_el2_to_sctlr_el1(u64 sctlr)
{
	/* Bit 20 is RES1 in SCTLR_EL1, but RES0 in SCTLR_EL2 */
	return sctlr | BIT(20);
}

static inline u64 translate_ttbr0_el2_to_ttbr0_el1(u64 ttbr0)
{
	/* Force ASID to 0 (ASID 0 or RES0) */
	return ttbr0 & ~GENMASK_ULL(63, 48);
}

static inline u64 translate_cnthctl_el2_to_cntkctl_el1(u64 cnthctl)
{
	return ((FIELD_GET(CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN, cnthctl) << 10) |
		(cnthctl & (CNTHCTL_EVNTI | CNTHCTL_EVNTDIR | CNTHCTL_EVNTEN)));
}

extern void kvm_init_nested(struct kvm *kvm);
extern int kvm_vcpu_init_nested(struct kvm_vcpu *vcpu);
extern void kvm_init_nested_s2_mmu(struct kvm_s2_mmu *mmu);
extern struct kvm_s2_mmu *lookup_s2_mmu(struct kvm *kvm, u64 vttbr, u64 hcr);
extern void kvm_vcpu_load_hw_mmu(struct kvm_vcpu *vcpu);
extern void kvm_vcpu_put_hw_mmu(struct kvm_vcpu *vcpu);

struct kvm_s2_trans {
	phys_addr_t output;
	unsigned long block_size;
	bool writable;
	bool readable;
	int level;
	u32 esr;
	u64 upper_attr;
};

static inline phys_addr_t kvm_s2_trans_output(struct kvm_s2_trans *trans)
{
	return trans->output;
}

static inline unsigned long kvm_s2_trans_size(struct kvm_s2_trans *trans)
{
	return trans->block_size;
}

static inline u32 kvm_s2_trans_esr(struct kvm_s2_trans *trans)
{
	return trans->esr;
}

static inline bool kvm_s2_trans_readable(struct kvm_s2_trans *trans)
{
	return trans->readable;
}

static inline bool kvm_s2_trans_writable(struct kvm_s2_trans *trans)
{
	return trans->writable;
}

static inline bool kvm_s2_trans_executable(struct kvm_s2_trans *trans)
{
	return !(trans->upper_attr & BIT(54));
}

extern int kvm_walk_nested_s2(struct kvm_vcpu *vcpu, phys_addr_t gipa,
			      struct kvm_s2_trans *result);

extern int kvm_s2_handle_perm_fault(struct kvm_vcpu *vcpu,
				    struct kvm_s2_trans *trans);
extern int kvm_inject_s2_fault(struct kvm_vcpu *vcpu, u64 esr_el2);
extern void kvm_nested_s2_wp(struct kvm *kvm);
extern void kvm_nested_s2_clear(struct kvm *kvm);
extern void kvm_nested_s2_flush(struct kvm *kvm);
int handle_wfx_nested(struct kvm_vcpu *vcpu, bool is_wfe);
extern bool __forward_traps(struct kvm_vcpu *vcpu, unsigned int reg,
			    u64 control_bit);
extern bool forward_traps(struct kvm_vcpu *vcpu, u64 control_bit);
extern bool forward_nv_traps(struct kvm_vcpu *vcpu);

struct sys_reg_params;
struct sys_reg_desc;

void access_nested_id_reg(struct kvm_vcpu *v, struct sys_reg_params *p,
			  const struct sys_reg_desc *r);

#endif /* __ARM64_KVM_NESTED_H */
