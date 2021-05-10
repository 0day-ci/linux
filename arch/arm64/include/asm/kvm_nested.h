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

int handle_wfx_nested(struct kvm_vcpu *vcpu, bool is_wfe);

#endif /* __ARM64_KVM_NESTED_H */
