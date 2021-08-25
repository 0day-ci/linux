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

int kvm_spe_check_supported_cpus(struct kvm_vcpu *vcpu)
{
	/* SPE is supported on all CPUs, we don't care about the VCPU mask */
	if (cpumask_equal(supported_cpus, cpu_possible_mask))
		return 0;

	if (!cpumask_subset(&vcpu->arch.supported_cpus, supported_cpus))
		return -ENOEXEC;

	return 0;
}
