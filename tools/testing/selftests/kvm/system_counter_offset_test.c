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
};

static void check_preconditions(struct kvm_vm *vm)
{
	if (!_vcpu_has_device_attr(vm, VCPU_ID, KVM_ARM_VCPU_TIMER_CTRL,
				   KVM_ARM_VCPU_TIMER_OFFSET_VTIMER) &&
	    !_vcpu_has_device_attr(vm, VCPU_ID, KVM_ARM_VCPU_TIMER_CTRL,
				   KVM_ARM_VCPU_TIMER_OFFSET_PTIMER))
		return;

	print_skip("KVM_ARM_VCPU_TIMER_OFFSET_{VTIMER,PTIMER} not supported; skipping test");
	exit(KSFT_SKIP);
}

static void setup_system_counter(struct kvm_vm *vm, struct test_case *test)
{
	u64 attr = 0;

	switch (test->counter) {
	case VIRTUAL:
		attr = KVM_ARM_VCPU_TIMER_OFFSET_VTIMER;
		break;
	case PHYSICAL:
		attr = KVM_ARM_VCPU_TIMER_OFFSET_PTIMER;
		break;
	default:
		TEST_ASSERT(false, "unrecognized counter index %u",
			    test->counter);
	}

	vcpu_access_device_attr(vm, VCPU_ID, KVM_ARM_VCPU_TIMER_CTRL,
				attr, &test->offset, true);
}

static uint64_t guest_read_system_counter(struct test_case *test)
{
	switch (test->counter) {
	case VIRTUAL:
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

	GUEST_DONE();
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
		case UCALL_DONE:
			return;
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
