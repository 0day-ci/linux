// SPDX-License-Identifier: GPL-2.0-only
/*
 * VMX control MSR test
 *
 * Copyright (C) 2022 Google LLC.
 *
 * Tests for KVM ownership of bits in the VMX entry/exit control MSRs. Checks
 * that KVM will set owned bits where appropriate, and will not if
 * KVM_X86_QUIRK_TWEAK_VMX_CTRL_MSRS is disabled.
 */

#include "kvm_util.h"
#include "vmx.h"

#define VCPU_ID 0

static void get_vmx_control_msr(struct kvm_vm *vm, uint32_t msr_index,
				uint32_t *low, uint32_t *high)
{
	uint64_t val;

	val = vcpu_get_msr(vm, VCPU_ID, msr_index);
	*low = val;
	*high = val >> 32;
}

static void set_vmx_control_msr(struct kvm_vm *vm, uint32_t msr_index,
				uint32_t low, uint32_t high)
{
	uint64_t val = (((uint64_t) high) << 32) | low;

	vcpu_set_msr(vm, VCPU_ID, msr_index, val);
}

static void test_vmx_control_msr(struct kvm_vm *vm, uint32_t msr_index, uint32_t set,
				 uint32_t clear, uint32_t exp_set, uint32_t exp_clear)
{
	uint32_t low, high;

	get_vmx_control_msr(vm, msr_index, &low, &high);

	high &= ~clear;
	high |= set;

	set_vmx_control_msr(vm, msr_index, low, high);

	get_vmx_control_msr(vm, msr_index, &low, &high);
	ASSERT_EQ(high & exp_set, exp_set);
	ASSERT_EQ(~high & exp_clear, exp_clear);
}

static void load_perf_global_ctrl_test(struct kvm_vm *vm)
{
	uint32_t entry_low, entry_high, exit_low, exit_high;
	struct kvm_enable_cap cap = {0};

	get_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, &entry_low, &entry_high);
	get_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, &exit_low, &exit_high);

	if (!(entry_high & VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL) ||
	    !(exit_high & VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL)) {
		print_skip("\"load IA32_PERF_GLOBAL_CTRL\" VM-{Entry,Exit} controls not supported");
		return;
	}

	/*
	 * Test that KVM will set these bits regardless of userspace if the
	 * guest CPUID exposes a supporting vPMU.
	 */
	test_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, 0,
			     VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL,
			     VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL,
			     0);
	test_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, 0,
			     VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL,
			     VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL,
			     0);

	/*
	 * Disable the quirk, giving userspace control of the VMX capability
	 * MSRs.
	 */
	cap.cap = KVM_CAP_DISABLE_QUIRKS;
	cap.args[0] = KVM_X86_QUIRK_TWEAK_VMX_CTRL_MSRS;
	vm_enable_cap(vm, &cap);

	/*
	 * Test that userspace can clear these bits, even if it exposes a vPMU
	 * that supports IA32_PERF_GLOBAL_CTRL.
	 */
	test_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, 0,
			     VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL,
			     0,
			     VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL);
	test_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, 0,
			     VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL,
			     0,
			     VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL);

	/* cleanup, enable the quirk again */
	cap.args[0] = 0;
	vm_enable_cap(vm, &cap);
}

static void bndcfgs_test(struct kvm_vm *vm)
{
	uint32_t entry_low, entry_high, exit_low, exit_high;
	struct kvm_enable_cap cap = {0};

	get_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, &entry_low, &entry_high);
	get_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, &exit_low, &exit_high);

	if (!(entry_high & VM_ENTRY_LOAD_BNDCFGS) ||
	    !(exit_high & VM_EXIT_CLEAR_BNDCFGS)) {
		print_skip("\"load/clear IA32_BNDCFGS\" VM-{Entry,Exit} controls not supported");
		return;
	}

	/*
	 * Test that KVM will set these bits regardless of userspace if the
	 * guest CPUID exposes MPX.
	 */
	test_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, 0,
			     VM_ENTRY_LOAD_BNDCFGS,
			     VM_ENTRY_LOAD_BNDCFGS,
			     0);
	test_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, 0,
			     VM_EXIT_CLEAR_BNDCFGS,
			     VM_EXIT_CLEAR_BNDCFGS,
			     0);

	/*
	 * Disable the quirk, giving userspace control of the VMX capability
	 * MSRs.
	 */
	cap.cap = KVM_CAP_DISABLE_QUIRKS;
	cap.args[0] = KVM_X86_QUIRK_TWEAK_VMX_CTRL_MSRS;
	vm_enable_cap(vm, &cap);

	/*
	 * Test that userspace can clear these bits, even if it exposes MPX.
	 */
	test_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_ENTRY_CTLS, 0,
			     VM_ENTRY_LOAD_BNDCFGS,
			     0,
			     VM_ENTRY_LOAD_BNDCFGS);
	test_vmx_control_msr(vm, MSR_IA32_VMX_TRUE_EXIT_CTLS, 0,
			     VM_EXIT_CLEAR_BNDCFGS,
			     0,
			     VM_EXIT_CLEAR_BNDCFGS);
}

int main(void)
{
	struct kvm_vm *vm;

	nested_vmx_check_supported();

	/* No need to run a guest for these tests */
	vm = vm_create_default(VCPU_ID, 0, NULL);

	load_perf_global_ctrl_test(vm);
	bndcfgs_test(vm);

	kvm_vm_free(vm);
}
