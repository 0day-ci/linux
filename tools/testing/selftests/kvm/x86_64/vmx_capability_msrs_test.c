// SPDX-License-Identifier: GPL-2.0-only
/*
 * VMX capability MSRs test
 *
 * Copyright (C) 2022 Google LLC
 *
 * Regression tests to check that updates to guest CPUID do not affect the
 * values of VMX capability MSRs.
 */

#include "kvm_util.h"
#include "vmx.h"

#define VCPU_ID 0

static void get_vmx_capability_msr(struct kvm_vm *vm, uint32_t msr_index,
				   uint32_t *low, uint32_t *high)
{
	uint64_t val;

	val = vcpu_get_msr(vm, VCPU_ID, msr_index);
	*low = val;
	*high = val >> 32;
}

static void set_vmx_capability_msr(struct kvm_vm *vm, uint32_t msr_index,
				   uint32_t low, uint32_t high)
{
	uint64_t val = (((uint64_t) high) << 32) | low;

	vcpu_set_msr(vm, VCPU_ID, msr_index, val);
}

/*
 * Test to assert that clearing the "load IA32_PERF_GLOBAL_CTRL" VM-{Entry,Exit}
 * control capability bits is preserved across a KVM_SET_CPUID2.
 */
static void load_perf_global_ctrl_test(struct kvm_vm *vm)
{
	uint32_t entry_low, entry_high, exit_low, exit_high;
	struct kvm_cpuid2 *cpuid;

	get_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, &entry_low, &entry_high);
	get_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, &exit_low, &exit_high);

	if (!(entry_high & VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL) ||
	    !(exit_high & VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL)) {
		print_skip("\"load IA32_PERF_GLOBAL_CTRL\" VM-{Entry,Exit} control not supported");
		return;
	}

	entry_high &= ~VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL;
	exit_high &= ~VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL;

	set_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, entry_low, entry_high);
	set_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, exit_low, exit_high);

	cpuid = kvm_get_supported_cpuid();
	vcpu_set_cpuid(vm, VCPU_ID, cpuid);

	get_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, &entry_low, &entry_high);
	get_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, &exit_low, &exit_high);

	TEST_ASSERT(!(entry_high & VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL),
		    "\"load IA32_PERF_GLOBAL_CTRL\" VM-Entry bit set");
	TEST_ASSERT(!(exit_high & VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL),
		    "\"load IA32_PERF_GLOBAL_CTRL\" VM-Exit bit set");
}

/*
 * Test to assert that clearing the "load IA32_BNDCFGS" and "clear IA32_BNDCFGS"
 * control capability bits is preserved across a KVM_SET_CPUID2.
 */
static void bndcfgs_ctrl_test(struct kvm_vm *vm)
{
	uint32_t entry_low, entry_high, exit_low, exit_high;
	struct kvm_cpuid2 *cpuid;

	get_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, &entry_low, &entry_high);
	get_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, &exit_low, &exit_high);

	if (!(entry_high & VM_ENTRY_LOAD_BNDCFGS) || !(exit_high & VM_EXIT_CLEAR_BNDCFGS)) {
		print_skip("\"{load,clear} IA32_BNDCFGS\" controls not supported");
		return;
	}

	entry_high &= ~VM_ENTRY_LOAD_BNDCFGS;
	exit_high &= ~VM_EXIT_CLEAR_BNDCFGS;

	set_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, entry_low, entry_high);
	set_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, exit_low, exit_high);

	cpuid = kvm_get_supported_cpuid();
	vcpu_set_cpuid(vm, VCPU_ID, cpuid);

	get_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, &entry_low, &entry_high);
	get_vmx_capability_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, &exit_low, &exit_high);

	TEST_ASSERT(!(entry_high & VM_ENTRY_LOAD_BNDCFGS),
		    "\"load IA32_BNDCFGS\" VM-Entry bit set");
	TEST_ASSERT(!(exit_high & VM_EXIT_CLEAR_BNDCFGS),
		    "\"clear IA32_BNDCFGS\" VM-Exit bit set");
}


int main(void)
{
	struct kvm_vm *vm;

	nested_vmx_check_supported();

	/* No need to run a guest for these tests */
	vm = vm_create_default(VCPU_ID, 0, NULL);

	load_perf_global_ctrl_test(vm);
	bndcfgs_ctrl_test(vm);

	kvm_vm_free(vm);
}
