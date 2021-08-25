// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - ARM Ltd
 */

#include <linux/cpumask.h>
#include <linux/kvm_host.h>
#include <linux/perf/arm_pmu.h>

#include <asm/kvm_spe.h>

DEFINE_STATIC_KEY_FALSE(kvm_spe_available);

static const cpumask_t *supported_cpus;

void kvm_spe_init_supported_cpus(void)
{
	if (likely(supported_cpus))
		return;

	supported_cpus = arm_spe_pmu_supported_cpus();
	BUG_ON(!supported_cpus);

	if (!cpumask_empty(supported_cpus))
		static_branch_enable(&kvm_spe_available);
}

void kvm_spe_vm_init(struct kvm *kvm)
{
	/* Set supported_cpus if it isn't already initialized. */
	kvm_spe_init_supported_cpus();
}

static int kvm_spe_check_supported_cpus(struct kvm_vcpu *vcpu)
{
	/* SPE is supported on all CPUs, we don't care about the VCPU mask */
	if (cpumask_equal(supported_cpus, cpu_possible_mask))
		return 0;

	if (!cpumask_subset(&vcpu->arch.supported_cpus, supported_cpus))
		return -ENOEXEC;

	return 0;
}

int kvm_spe_vcpu_first_run_init(struct kvm_vcpu *vcpu)
{
	int ret;

	ret = kvm_spe_check_supported_cpus(vcpu);
	if (ret)
		return ret;

	if (!vcpu->arch.spe.initialized)
		return -EPERM;

	return 0;
}

void kvm_spe_write_sysreg(struct kvm_vcpu *vcpu, int reg, u64 val)
{
	__vcpu_sys_reg(vcpu, reg) = val;
}

u64 kvm_spe_read_sysreg(struct kvm_vcpu *vcpu, int reg)
{
	return __vcpu_sys_reg(vcpu, reg);
}

static bool kvm_vcpu_supports_spe(struct kvm_vcpu *vcpu)
{
	if (!kvm_supports_spe())
		return false;

	if (!kvm_vcpu_has_spe(vcpu))
		return false;

	if (!irqchip_in_kernel(vcpu->kvm))
		return false;

	return true;
}

static bool kvm_spe_irq_is_valid(struct kvm *kvm, int irq)
{
	struct kvm_vcpu *vcpu;
	int i;

	if (!irq_is_ppi(irq))
		return -EINVAL;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!vcpu->arch.spe.irq_num)
			continue;

		if (vcpu->arch.spe.irq_num != irq)
			return false;
	}

	return true;
}

int kvm_spe_set_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	if (!kvm_vcpu_supports_spe(vcpu))
		return -ENXIO;

	if (vcpu->arch.spe.initialized)
		return -EBUSY;

	switch (attr->attr) {
	case KVM_ARM_VCPU_SPE_IRQ: {
		int __user *uaddr = (int __user *)(long)attr->addr;
		int irq;

		if (vcpu->arch.spe.irq_num)
			return -EBUSY;

		if (get_user(irq, uaddr))
			return -EFAULT;

		if (!kvm_spe_irq_is_valid(vcpu->kvm, irq))
			return -EINVAL;

		kvm_debug("Set KVM_ARM_VCPU_SPE_IRQ: %d\n", irq);
		vcpu->arch.spe.irq_num = irq;
		return 0;
	}
	case KVM_ARM_VCPU_SPE_INIT:
		if (!vcpu->arch.spe.irq_num)
			return -ENXIO;

		if (!vgic_initialized(vcpu->kvm))
			return -ENXIO;

		if (kvm_vgic_set_owner(vcpu, vcpu->arch.spe.irq_num, &vcpu->arch.spe))
			return -ENXIO;

		vcpu->arch.spe.initialized = true;
		return 0;
	}

	return -ENXIO;
}

int kvm_spe_get_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	if (!kvm_vcpu_supports_spe(vcpu))
		return -ENXIO;

	switch (attr->attr) {
	case KVM_ARM_VCPU_SPE_IRQ: {
		int __user *uaddr = (int __user *)(long)attr->addr;
		int irq;

		if (!vcpu->arch.spe.irq_num)
			return -ENXIO;

		irq = vcpu->arch.spe.irq_num;
		if (put_user(irq, uaddr))
			return -EFAULT;

		return 0;
	}
	}

	return -ENXIO;
}

int kvm_spe_has_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	if (!kvm_vcpu_supports_spe(vcpu))
		return -ENXIO;

	switch(attr->attr) {
	case KVM_ARM_VCPU_SPE_IRQ:
	case KVM_ARM_VCPU_SPE_INIT:
		return 0;
	}

	return -ENXIO;
}
