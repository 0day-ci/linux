// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Google LLC.
 *
 * Tests for adjusting the system counter from userspace
 */
#include <asm/kvm_para.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#define VCPU_ID 0

#ifdef __x86_64__

struct test_case {
	uint64_t tsc_offset;
};

static struct test_case test_cases[] = {
	{ 0 },
	{ 180 * NSEC_PER_SEC },
	{ -180 * NSEC_PER_SEC },
};

static void check_preconditions(struct kvm_vm *vm)
{
	if (!_vcpu_has_device_attr(vm, VCPU_ID, KVM_VCPU_TSC_CTRL, KVM_VCPU_TSC_OFFSET))
		return;

	print_skip("KVM_VCPU_TSC_OFFSET not supported; skipping test");
	exit(KSFT_SKIP);
}

static void setup_system_counter(struct kvm_vm *vm, struct test_case *test)
{
	vcpu_access_device_attr(vm, VCPU_ID, KVM_VCPU_TSC_CTRL,
				KVM_VCPU_TSC_OFFSET, &test->tsc_offset, true);
}

static uint64_t guest_read_system_counter(struct test_case *test)
{
	return rdtsc();
}

static uint64_t host_read_guest_system_counter(struct test_case *test)
{
	return rdtsc() + test->tsc_offset;
}

#elif __aarch64__ /* __x86_64__ */

enum arch_counter {
	VIRTUAL,
	PHYSICAL,
	/* offset physical, read virtual */
	PHYSICAL_READ_VIRTUAL,
};

struct test_case {
	enum arch_counter counter;
	uint64_t offset;
};

static struct test_case test_cases[] = {
	{ .counter = VIRTUAL, .offset = 0 },
	{ .counter = VIRTUAL, .offset = 180 * NSEC_PER_SEC },
	{ .counter = VIRTUAL, .offset = -180 * NSEC_PER_SEC },
	{ .counter = PHYSICAL, .offset = 0 },
	{ .counter = PHYSICAL, .offset = 180 * NSEC_PER_SEC },
	{ .counter = PHYSICAL, .offset = -180 * NSEC_PER_SEC },
	{ .counter = PHYSICAL_READ_VIRTUAL, .offset = 0 },
	{ .counter = PHYSICAL_READ_VIRTUAL, .offset = 180 * NSEC_PER_SEC },
	{ .counter = PHYSICAL_READ_VIRTUAL, .offset = -180 * NSEC_PER_SEC },
};

static void check_preconditions(struct kvm_vm *vm)
{
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_ARM_VTIMER_OFFSET,
	};

	if (!kvm_check_cap(KVM_CAP_ARM_VTIMER_OFFSET)) {
		print_skip("KVM_REG_ARM_TIMER_OFFSET not supported");
		exit(KSFT_SKIP);
	}

	vm_enable_cap(vm, &cap);

	if (_vcpu_has_device_attr(vm, VCPU_ID, KVM_ARM_VCPU_TIMER_CTRL,
				  KVM_ARM_VCPU_TIMER_PHYS_OFFSET)) {
		print_skip("KVM_ARM_VCPU_TIMER_PHYS_OFFSET not supported");
		exit(KSFT_SKIP);
	}
}

static void setup_system_counter(struct kvm_vm *vm, struct test_case *test)
{
	uint64_t cntvoff, cntpoff;
	struct kvm_one_reg reg = {
		.id = KVM_REG_ARM_TIMER_OFFSET,
		.addr = (__u64)&cntvoff,
	};

	if (test->counter == VIRTUAL) {
		cntvoff = test->offset;
		cntpoff = 0;
	} else {
		cntvoff = 0;
		cntpoff = test->offset;
	}

	vcpu_set_reg(vm, VCPU_ID, &reg);
	vcpu_access_device_attr(vm, VCPU_ID, KVM_ARM_VCPU_TIMER_CTRL,
				KVM_ARM_VCPU_TIMER_PHYS_OFFSET, &cntpoff, true);
}

static uint64_t guest_read_system_counter(struct test_case *test)
{
	switch (test->counter) {
	case VIRTUAL:
	case PHYSICAL_READ_VIRTUAL:
		return read_cntvct_ordered();
	case PHYSICAL:
		return read_cntpct_ordered();
	default:
		GUEST_ASSERT(0);
	}

	/* unreachable */
	return 0;
}

static uint64_t host_read_guest_system_counter(struct test_case *test)
{
	return read_cntvct_ordered() - test->offset;
}

#else /* __aarch64__ */

#error test not implemented for this architecture!

#endif

#define GUEST_SYNC_CLOCK(__stage, __val)			\
		GUEST_SYNC_ARGS(__stage, __val, 0, 0, 0)

static void guest_main(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		struct test_case *test = &test_cases[i];

		GUEST_SYNC_CLOCK(i, guest_read_system_counter(test));
	}
}

static void handle_sync(struct ucall *uc, uint64_t start, uint64_t end)
{
	uint64_t obs = uc->args[2];

	TEST_ASSERT(start <= obs && obs <= end,
		    "unexpected system counter value: %"PRIu64" expected range: [%"PRIu64", %"PRIu64"]",
		    obs, start, end);

	pr_info("system counter value: %"PRIu64" expected range [%"PRIu64", %"PRIu64"]\n",
		obs, start, end);
}

static void handle_abort(struct ucall *uc)
{
	TEST_FAIL("%s at %s:%ld", (const char *)uc->args[0],
		  __FILE__, uc->args[1]);
}

static void enter_guest(struct kvm_vm *vm)
{
	uint64_t start, end;
	struct ucall uc;
	int i;

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		struct test_case *test = &test_cases[i];

		setup_system_counter(vm, test);
		start = host_read_guest_system_counter(test);
		vcpu_run(vm, VCPU_ID);
		end = host_read_guest_system_counter(test);

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_SYNC:
			handle_sync(&uc, start, end);
			break;
		case UCALL_ABORT:
			handle_abort(&uc);
			return;
		default:
			TEST_ASSERT(0, "unhandled ucall %ld\n",
				    get_ucall(vm, VCPU_ID, &uc));
		}
	}
}

int main(void)
{
	struct kvm_vm *vm;

	vm = vm_create_default(VCPU_ID, 0, guest_main);
	check_preconditions(vm);
	ucall_init(vm, NULL);

	enter_guest(vm);
	kvm_vm_free(vm);
}
