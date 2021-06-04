// SPDX-License-Identifier: GPL-2.0
/*
 * vmx_pi_mmio_test
 *
 * Copyright (C) 2021, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Test that an L2 vCPU can be launched with an unbacked posted
 * interrupt descriptor, but that any attempt to send that vCPU its
 * posted interrupt notification vector will result in an exit to
 * userspace with KVM_INTERNAL_ERROR.
 *
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include <pthread.h>
#include <inttypes.h>
#include <unistd.h>

#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"
#include "vmx.h"

#include "kselftest.h"

#define RECEIVER_VCPU_ID	0
#define SENDER_VCPU_ID		1

#define L2_GUEST_STACK_SIZE	64

#define TIMEOUT_SECS		10

#define L1_PI_VECTOR		33

static struct kvm_vm *vm;

static bool l2_active;

static void l2_guest_code(void)
{
	l2_active = true;
	__asm__ __volatile__("hlt");
	/* NOT REACHED */
}

static void l1_receiver_code(struct vmx_pages *vmx_pages,
			     unsigned long high_gpa)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	uint32_t control;

	x2apic_enable();

	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);
	control = vmreadz(PIN_BASED_VM_EXEC_CONTROL);
	control |= PIN_BASED_EXT_INTR_MASK |
		PIN_BASED_POSTED_INTR;
	vmwrite(PIN_BASED_VM_EXEC_CONTROL, control);

	control = vmreadz(CPU_BASED_VM_EXEC_CONTROL);
	control |= CPU_BASED_TPR_SHADOW |
		CPU_BASED_ACTIVATE_SECONDARY_CONTROLS;
	vmwrite(CPU_BASED_VM_EXEC_CONTROL, control);

	control = vmreadz(SECONDARY_VM_EXEC_CONTROL);
	control |= SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY;
	vmwrite(SECONDARY_VM_EXEC_CONTROL, control);

	control = vmreadz(VM_EXIT_CONTROLS);
	control |= VM_EXIT_ACK_INTR_ON_EXIT;
	vmwrite(VM_EXIT_CONTROLS, control);

	vmwrite(VIRTUAL_APIC_PAGE_ADDR, vmx_pages->virtual_apic_gpa);
	vmwrite(POSTED_INTR_NV, L1_PI_VECTOR);
	vmwrite(POSTED_INTR_DESC_ADDR, high_gpa);

	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	GUEST_DONE();
}

static void l1_sender_code(void *arg)
{
	x2apic_enable();

	x2apic_write_reg(APIC_ICR,
			 APIC_INT_ASSERT | APIC_DEST_PHYSICAL |
			 APIC_DM_FIXED | L1_PI_VECTOR |
			 ((uint64_t)RECEIVER_VCPU_ID << 32));

	GUEST_DONE();
}

static bool vcpu_run_loop(int vcpu_id)
{
	volatile struct kvm_run *run = vcpu_state(vm, vcpu_id);
	bool done = false;
	struct ucall uc;

	while (!done) {
		vcpu_run(vm, vcpu_id);

		if (run->exit_reason != KVM_EXIT_IO)
			break;

		switch (get_ucall(vm, vcpu_id, &uc)) {
		case UCALL_ABORT:
			TEST_FAIL("vCPU  %d: %s at %s:%ld", vcpu_id,
				  (const char *)uc.args[0], __FILE__,
				  uc.args[1]);
			/* NOT REACHED */
		case UCALL_SYNC:
			break;
		case UCALL_DONE:
			done = true;
			break;
		default:
			TEST_FAIL("vCPU %d: Unknown ucall %lu",
				  vcpu_id, uc.cmd);
			/* NOT REACHED */
		}
	}

	return done;
}

static void *receiver(void *arg)
{
	volatile struct kvm_run *run = vcpu_state(vm, RECEIVER_VCPU_ID);
	unsigned long high_gpa = *(unsigned long *)arg;
	vm_vaddr_t vmx_pages_gva;
	struct vmx_pages *vmx;
	bool success;

	vmx = vcpu_alloc_vmx(vm, &vmx_pages_gva);
	prepare_tpr_shadow(vmx, vm);
	vcpu_args_set(vm, RECEIVER_VCPU_ID, 2, vmx_pages_gva, high_gpa);

	success = vcpu_run_loop(RECEIVER_VCPU_ID);
	TEST_ASSERT(!success, "Receiver didn't fail as expected.\n");
	TEST_ASSERT(run->exit_reason ==
		    KVM_EXIT_INTERNAL_ERROR,
		    "Exit reason isn't KVM_EXIT_INTERNAL_ERROR: %u (%s).\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->internal.suberror ==
		    KVM_INTERNAL_ERROR_EMULATION,
		    "Internal suberror isn't KVM_INTERNAL_ERROR_EMULATION: %u.\n",
		    run->internal.suberror);

	return NULL;
}

static void sender(void)
{
	volatile struct kvm_run *run = vcpu_state(vm, SENDER_VCPU_ID);
	bool success;

	success = vcpu_run_loop(SENDER_VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Sender didn't exit with KVM_EXIT_IO: %u (%s).\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(success, "Sender didn't complete successfully.\n");
}

void check_constraints(void)
{
	uint64_t msr;

	nested_vmx_check_supported();

	msr = kvm_get_feature_msr(MSR_IA32_VMX_PINBASED_CTLS) >> 32;
	if (!(msr & PIN_BASED_EXT_INTR_MASK)) {
		print_skip("Cannot enable \"external-interrupt exiting\"");
		exit(KSFT_SKIP);
	}
	if (!(msr & PIN_BASED_POSTED_INTR)) {
		print_skip("Cannot enable \"process posted interrupts\"");
		exit(KSFT_SKIP);
	}

	msr = kvm_get_feature_msr(MSR_IA32_VMX_PROCBASED_CTLS) >> 32;
	if (!(msr & CPU_BASED_TPR_SHADOW)) {
		print_skip("Cannot enable \"use TPR shadow\"");
		exit(KSFT_SKIP);
	}
	if (!(msr & CPU_BASED_ACTIVATE_SECONDARY_CONTROLS)) {
		print_skip("Cannot enable \"activate secondary controls\"");
		exit(KSFT_SKIP);
	}

	msr = kvm_get_feature_msr(MSR_IA32_VMX_PROCBASED_CTLS2) >> 32;
	if (!(msr & SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY)) {
		print_skip("Cannot enable \"virtual-interrupt delivery\"");
		exit(KSFT_SKIP);
	}

	msr = kvm_get_feature_msr(MSR_IA32_VMX_EXIT_CTLS) >> 32;
	if (!(msr & VM_EXIT_ACK_INTR_ON_EXIT)) {
		print_skip("Cannot enable \"acknowledge interrupt on exit\"");
		exit(KSFT_SKIP);
	}
}

int main(int argc, char *argv[])
{
	unsigned int paddr_width;
	unsigned int vaddr_width;
	unsigned long high_gpa;
	pthread_t thread;
	bool *l2_active_hva;
	int r;

	kvm_get_cpu_address_width(&paddr_width, &vaddr_width);
	high_gpa = (1ul << paddr_width) - getpagesize();
	if ((unsigned long)DEFAULT_GUEST_PHY_PAGES * getpagesize() > high_gpa) {
		print_skip("No unbacked physical page available");
		exit(KSFT_SKIP);
	}

	check_constraints();

	vm = vm_create_default(RECEIVER_VCPU_ID, 0, (void *)l1_receiver_code);
	vm_vcpu_add_default(vm, SENDER_VCPU_ID, (void *)l1_sender_code);
	vcpu_set_cpuid(vm, SENDER_VCPU_ID, kvm_get_supported_cpuid());

	r = pthread_create(&thread, NULL, receiver, &high_gpa);
	TEST_ASSERT(r == 0,
		    "pthread_create failed errno=%d", errno);

	alarm(TIMEOUT_SECS);
	l2_active_hva = (bool *)addr_gva2hva(vm, (vm_vaddr_t)&l2_active);
	while (!*l2_active_hva)
		pthread_yield();

	sender();

	r = pthread_join(thread, NULL);
	TEST_ASSERT(r == 0, "pthread_join failed with errno=%d", r);

	kvm_vm_free(vm);

	return 0;
}
