/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 - ARM Ltd
 */

#ifndef __ARM64_KVM_SPE_H__
#define __ARM64_KVM_SPE_H__

#ifdef CONFIG_KVM_ARM_SPE
DECLARE_STATIC_KEY_FALSE(kvm_spe_available);

static __always_inline bool kvm_supports_spe(void)
{
	return static_branch_likely(&kvm_spe_available);
}

void kvm_spe_init_supported_cpus(void);
void kvm_spe_vm_init(struct kvm *kvm);
int kvm_spe_check_supported_cpus(struct kvm_vcpu *vcpu);
#else
#define kvm_supports_spe()	(false)

static inline void kvm_spe_init_supported_cpus(void) {}
static inline void kvm_spe_vm_init(struct kvm *kvm) {}
static inline int kvm_spe_check_supported_cpus(struct kvm_vcpu *vcpu) { return -ENOEXEC; }
#endif /* CONFIG_KVM_ARM_SPE */

#endif /* __ARM64_KVM_SPE_H__ */
