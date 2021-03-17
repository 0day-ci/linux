// SPDX-License-Identifier: GPL-2.0
/*
 * Test that KVM_SET_BOOT_CPU_ID works as intended
 *
 * Copyright (C) 2020, Red Hat, Inc.
 */
#define _GNU_SOURCE /* for program_invocation_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#define N_VCPU 2
#define VCPU_ID0 0
#define VCPU_ID1 1

#define WRONG_BSP 2

static uint32_t get_bsp_flag(void)
{
	return rdmsr(MSR_IA32_APICBASE) & MSR_IA32_APICBASE_BSP;
}

static void guest_bsp_vcpu(void *arg)
{
	GUEST_SYNC(1);

	GUEST_ASSERT(get_bsp_flag() != 0);

	GUEST_DONE();
}

static void guest_not_bsp_vcpu(void *arg)
{
	GUEST_SYNC(1);

	GUEST_ASSERT(get_bsp_flag() == 0);

	GUEST_DONE();
}

static void run_vcpu(struct kvm_vm *vm, uint32_t vcpuid, int stage)
{
	struct ucall uc;

	printf("vcpu executing...\n");
	vcpu_run(vm, vcpuid);
	printf("vcpu executed\n");

	switch (get_ucall(vm, vcpuid, &uc)) {
	case UCALL_SYNC:
		printf("stage %d sync %ld\n", stage, uc.args[1]);
		TEST_ASSERT(!strcmp((const char *)uc.args[0], "hello") &&
			    uc.args[1] == stage + 1,
			    "Stage %d: Unexpected register values vmexit, got %lx",
			    stage + 1, (ulong)uc.args[1]);
		return;
	case UCALL_DONE:
		printf("got done\n");
		return;
	case UCALL_ABORT:
		TEST_ASSERT(false, "%s at %s:%ld\n\tvalues: %#lx, %#lx", (const char *)uc.args[0],
			    __FILE__, uc.args[1], uc.args[2], uc.args[3]);
	default:
		TEST_ASSERT(false, "Unexpected exit: %s",
			    exit_reason_str(vcpu_state(vm, vcpuid)->exit_reason));
	}
}

static void check_wrong_bsp(void)
{
	struct kvm_vm *vm;
	int res;

	vm = vm_create_default(VCPU_ID0, 0, guest_bsp_vcpu);

	res = _kvm_ioctl(vm, KVM_SET_BOOT_CPU_ID, (void *) WRONG_BSP);
	TEST_ASSERT(res == -1, "KVM_SET_BOOT_CPU_ID set to a non-existent vcpu %d", WRONG_BSP);

	kvm_vm_free(vm);
}

static struct kvm_vm *create_vm(void)
{
	struct kvm_vm *vm;
	uint64_t vcpu_pages = (DEFAULT_STACK_PGS) * 2;
	uint64_t extra_pg_pages = vcpu_pages / PTES_PER_MIN_PAGE * N_VCPU;
	uint64_t pages = DEFAULT_GUEST_PHY_PAGES + vcpu_pages + extra_pg_pages;

	pages = vm_adjust_num_guest_pages(VM_MODE_DEFAULT, pages);
	vm = vm_create(VM_MODE_DEFAULT, pages, O_RDWR);

	kvm_vm_elf_load(vm, program_invocation_name, 0, 0);
	vm_create_irqchip(vm);

	return vm;
}

static void add_x86_vcpu(struct kvm_vm *vm, uint32_t vcpuid, void *code)
{
	vm_vcpu_add_default(vm, vcpuid, code);
	vcpu_set_cpuid(vm, vcpuid, kvm_get_supported_cpuid());
}

static void run_vm_bsp(uint32_t bsp_vcpu)
{
	struct kvm_vm *vm;
	int stage;
	void *vcpu0_code, *vcpu1_code;

	vm = create_vm();

	vcpu0_code = guest_bsp_vcpu;
	vcpu1_code = guest_not_bsp_vcpu;

	if (bsp_vcpu == VCPU_ID1) {
		vcpu0_code = guest_not_bsp_vcpu;
		vcpu1_code = guest_bsp_vcpu;

		vm_ioctl(vm, KVM_SET_BOOT_CPU_ID, (void *) VCPU_ID1);
	}

	add_x86_vcpu(vm, VCPU_ID0, vcpu0_code);
	add_x86_vcpu(vm, VCPU_ID1, vcpu1_code);

	for (stage = 0; stage < 2; stage++) {
		run_vcpu(vm, VCPU_ID0, stage);
		run_vcpu(vm, VCPU_ID1, stage);
	}

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	if (!kvm_check_cap(KVM_CAP_SET_BOOT_CPU_ID)) {
		print_skip("set_boot_cpu_id not available");
		return 0;
	}

	run_vm_bsp(VCPU_ID0);
	run_vm_bsp(VCPU_ID1);
	run_vm_bsp(VCPU_ID0);

	check_wrong_bsp();
}
