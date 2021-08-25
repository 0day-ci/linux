/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 - ARM Ltd
 */

#ifndef __ARM64_KVM_SPE_H__
#define __ARM64_KVM_SPE_H__

#include <linux/kvm.h>

#ifdef CONFIG_KVM_ARM_SPE
DECLARE_STATIC_KEY_FALSE(kvm_spe_available);

static __always_inline bool kvm_supports_spe(void)
{
	return static_branch_likely(&kvm_spe_available);
}

struct kvm_vcpu_spe {
	bool initialized;	/* SPE initialized for the VCPU */
	int irq_num;		/* Buffer management interrut number */
};

void kvm_spe_init_supported_cpus(void);
void kvm_spe_vm_init(struct kvm *kvm);
int kvm_spe_vcpu_first_run_init(struct kvm_vcpu *vcpu);

void kvm_spe_write_sysreg(struct kvm_vcpu *vcpu, int reg, u64 val);
u64 kvm_spe_read_sysreg(struct kvm_vcpu *vcpu, int reg);

int kvm_spe_set_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr);
int kvm_spe_get_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr);
int kvm_spe_has_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr);

#else
#define kvm_supports_spe()	(false)

struct kvm_vcpu_spe {
};

static inline void kvm_spe_init_supported_cpus(void) {}
static inline void kvm_spe_vm_init(struct kvm *kvm) {}
static inline int kvm_spe_vcpu_first_run_init(struct kvm_vcpu *vcpu) { return -ENOEXEC; }

static inline void kvm_spe_write_sysreg(struct kvm_vcpu *vcpu, int reg, u64 val) {}
static inline u64 kvm_spe_read_sysreg(struct kvm_vcpu *vcpu, int reg) { return 0; }

static inline int kvm_spe_set_attr(struct kvm_vcpu *vcpu,
				   struct kvm_device_attr *attr)
{
	return -ENXIO;
}
static inline int kvm_spe_get_attr(struct kvm_vcpu *vcpu,
				   struct kvm_device_attr *attr)
{
	return -ENXIO;
}
static inline int kvm_spe_has_attr(struct kvm_vcpu *vcpu,
				   struct kvm_device_attr *attr)
{
	return -ENXIO;
}
#endif /* CONFIG_KVM_ARM_SPE */

#endif /* __ARM64_KVM_SPE_H__ */
