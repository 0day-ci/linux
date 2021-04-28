/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Adapted from /arch/x86/kvm/cpuid.h
 */

#ifndef SELFTEST_KVM_CPUID_FEATURE_H
#define SELFTEST_KVM_CPUID_FEATURE_H

#include <stdint.h>
#include <asm/cpufeatures.h>
#include <asm/kvm_para.h>
#include "reverse_cpuid.h"

static __always_inline u32 *kvm_cpuid_get_register(unsigned int x86_feature)
{
	struct kvm_cpuid_entry2 *entry;
	const struct cpuid_reg cpuid = x86_feature_cpuid(x86_feature);

	entry = kvm_get_supported_cpuid_index(cpuid.function, cpuid.index);
	if (!entry)
		return NULL;

	return __cpuid_entry_get_reg(entry, cpuid.reg);
}

static __always_inline bool kvm_cpuid_has(unsigned int x86_feature)
{
	u32 *reg;

	reg = kvm_cpuid_get_register(x86_feature);
	if (!reg)
		return false;

	return *reg & __feature_bit(x86_feature);
}

static __always_inline bool kvm_pv_has(unsigned int kvm_feature)
{
	u32 reg;

	reg = kvm_get_supported_cpuid_entry(KVM_CPUID_FEATURES)->eax;
	return reg & __feature_bit(kvm_feature);
}

static __always_inline bool this_cpu_has(unsigned int x86_feature)
{
	struct kvm_cpuid_entry2 entry;
	const struct cpuid_reg cpuid = x86_feature_cpuid(x86_feature);
	u32 *reg;

	entry.eax = cpuid.function;
	entry.ecx = cpuid.index;
	__asm__ __volatile__("cpuid"
			     : "+a"(entry.eax), "=b"(entry.ebx),
			       "+c"(entry.ecx), "=d"(entry.edx));

	reg = __cpuid_entry_get_reg(&entry, cpuid.reg);
	return *reg &  __feature_bit(x86_feature);
}

#endif /* SELFTEST_KVM_CPUID_FEATURE_H */
