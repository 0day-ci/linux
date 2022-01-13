// SPDX-License-Identifier: GPL-2.0
/*
 * Test for x86 KVM_SET_PMU_EVENT_FILTER.
 *
 * Copyright (C) 2022, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Verifies the expected behavior of allow lists and deny lists for
 * virtual PMU events.
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

/*
 * In lieue of copying perf_event.h into tools...
 */
#define ARCH_PERFMON_EVENTSEL_ENABLE	BIT(22)
#define ARCH_PERFMON_EVENTSEL_OS	BIT(17)

#define VCPU_ID 0
#define NUM_BRANCHES 42

/*
 * This is how the event selector and unit mask are stored in an AMD
 * core performance event-select register. Intel's format is similar,
 * but the event selector is only 8 bits.
 */
#define EVENT(select, umask) ((select & 0xf00UL) << 24 | (select & 0xff) | \
			      (umask & 0xff) << 8)

/*
 * "Branch instructions retired", from the Intel SDM, volume 3,
 * "Pre-defined Architectural Performance Events."
 */

#define INTEL_BR_RETIRED EVENT(0xc4, 0)

/*
 * "Retired branch instructions", from Processor Programming Reference
 * (PPR) for AMD Family 17h Model 01h, Revision B1 Processors,
 * Preliminary Processor Programming Reference (PPR) for AMD Family
 * 17h Model 31h, Revision B0 Processors, and Preliminary Processor
 * Programming Reference (PPR) for AMD Family 19h Model 01h, Revision
 * B1 Processors Volume 1 of 2
 */

#define AMD_ZEN_BR_RETIRED EVENT(0xc2, 0)

/*
 * This event list comprises Intel's eight architectural events plus
 * AMD's "branch instructions retired" for Zen[123].
 */
static const uint64_t event_list[] = {
	EVENT(0x3c, 0),
	EVENT(0xc0, 0),
	EVENT(0x3c, 1),
	EVENT(0x2e, 0x4f),
	EVENT(0x2e, 0x41),
	EVENT(0xc4, 0),
	EVENT(0xc5, 0),
	EVENT(0xa4, 1),
	AMD_ZEN_BR_RETIRED,
};

static void intel_guest_code(void)
{
	uint64_t br0, br1;

	for (;;) {
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
		wrmsr(MSR_P6_EVNTSEL0, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | INTEL_BR_RETIRED);
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 1);
		br0 = rdmsr(MSR_IA32_PMC0);
		__asm__ __volatile__("loop ."
				     : "=c"((int){0})
				     : "0"(NUM_BRANCHES));
		br1 = rdmsr(MSR_IA32_PMC0);
		GUEST_SYNC(br1 - br0);
	}
}

/*
 * To avoid needing a check for CPUID.80000001:ECX.PerfCtrExtCore[bit
 * 23], this code uses the always-available, legacy K7 PMU MSRs, which
 * alias to the first four of the six extended core PMU MSRs.
 */
static void amd_guest_code(void)
{
	uint64_t br0, br1;

	for (;;) {
		wrmsr(MSR_K7_EVNTSEL0, 0);
		wrmsr(MSR_K7_EVNTSEL0, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | AMD_ZEN_BR_RETIRED);
		br0 = rdmsr(MSR_K7_PERFCTR0);
		__asm__ __volatile__("loop ."
				     : "=c"((int){0})
				     : "0"(NUM_BRANCHES));
		br1 = rdmsr(MSR_K7_PERFCTR0);
		GUEST_SYNC(br1 - br0);
	}
}

static uint64_t test_branches_retired(struct kvm_vm *vm)
{
	struct kvm_run *run = vcpu_state(vm, VCPU_ID);
	struct ucall uc;

	vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Exit_reason other than KVM_EXIT_IO: %u (%s)\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	get_ucall(vm, VCPU_ID, &uc);
	TEST_ASSERT(uc.cmd == UCALL_SYNC,
		    "Received ucall other than UCALL_SYNC: %lu", uc.cmd);
	return uc.args[1];
}

static struct kvm_pmu_event_filter *make_pmu_event_filter(uint32_t nevents)
{
	struct kvm_pmu_event_filter *f;
	int size = sizeof(*f) + nevents * sizeof(f->events[0]);

	f = malloc(size);
	TEST_ASSERT(f, "Out of memory");
	memset(f, 0, size);
	f->nevents = nevents;
	return f;
}

static struct kvm_pmu_event_filter *event_filter(uint32_t action)
{
	struct kvm_pmu_event_filter *f;
	int i;

	f = make_pmu_event_filter(ARRAY_SIZE(event_list));
	f->action = action;
	for (i = 0; i < ARRAY_SIZE(event_list); i++)
		f->events[i] = event_list[i];

	return f;
}

static struct kvm_pmu_event_filter *remove_event(struct kvm_pmu_event_filter *f,
						 uint64_t event)
{
	bool found = false;
	int i;

	for (i = 0; i < f->nevents; i++) {
		if (found)
			f->events[i - 1] = f->events[i];
		else
			found = f->events[i] == event;
	}
	if (found)
		f->nevents--;
	return f;
}

static void test_no_filter(struct kvm_vm *vm)
{
	uint64_t count = test_branches_retired(vm);

	if (count != NUM_BRANCHES)
		pr_info("%s: Branch instructions retired = %lu (expected %u)\n",
			__func__, count, NUM_BRANCHES);
	TEST_ASSERT(count, "Allowed PMU event is not counting");
}

static uint64_t test_with_filter(struct kvm_vm *vm,
				 struct kvm_pmu_event_filter *f)
{
	vm_ioctl(vm, KVM_SET_PMU_EVENT_FILTER, (void *)f);
	return test_branches_retired(vm);
}

static void test_member_deny_list(struct kvm_vm *vm)
{
	struct kvm_pmu_event_filter *f = event_filter(KVM_PMU_EVENT_DENY);
	uint64_t count = test_with_filter(vm, f);

	free(f);
	if (count)
		pr_info("%s: Branch instructions retired = %lu (expected 0)\n",
			__func__, count);
	TEST_ASSERT(!count, "Disallowed PMU Event is counting");
}

static void test_member_allow_list(struct kvm_vm *vm)
{
	struct kvm_pmu_event_filter *f = event_filter(KVM_PMU_EVENT_ALLOW);
	uint64_t count = test_with_filter(vm, f);

	free(f);
	if (count != NUM_BRANCHES)
		pr_info("%s: Branch instructions retired = %lu (expected %u)\n",
			__func__, count, NUM_BRANCHES);
	TEST_ASSERT(count, "Allowed PMU event is not counting");
}

static void test_not_member_deny_list(struct kvm_vm *vm)
{
	struct kvm_pmu_event_filter *f = event_filter(KVM_PMU_EVENT_DENY);
	uint64_t count;

	remove_event(f, INTEL_BR_RETIRED);
	remove_event(f, AMD_ZEN_BR_RETIRED);
	count = test_with_filter(vm, f);
	free(f);
	if (count != NUM_BRANCHES)
		pr_info("%s: Branch instructions retired = %lu (expected %u)\n",
			__func__, count, NUM_BRANCHES);
	TEST_ASSERT(count, "Allowed PMU event is not counting");
}

static void test_not_member_allow_list(struct kvm_vm *vm)
{
	struct kvm_pmu_event_filter *f = event_filter(KVM_PMU_EVENT_ALLOW);
	uint64_t count;

	remove_event(f, INTEL_BR_RETIRED);
	remove_event(f, AMD_ZEN_BR_RETIRED);
	count = test_with_filter(vm, f);
	free(f);
	if (count)
		pr_info("%s: Branch instructions retired = %lu (expected 0)\n",
			__func__, count);
	TEST_ASSERT(!count, "Disallowed PMU Event is counting");
}

/*
 * Note that CPUID leaf 0xa is Intel-specific. This leaf should be
 * clear on AMD hardware.
 */
static bool vcpu_supports_intel_br_retired(void)
{
	struct kvm_cpuid_entry2 *entry;
	struct kvm_cpuid2 *cpuid;

	cpuid = kvm_get_supported_cpuid();
	entry = kvm_get_supported_cpuid_index(0xa, 0);
	return entry &&
		(entry->eax & 0xff) &&
		(entry->eax >> 24) > 5 &&
		!(entry->ebx & BIT(5));
}

/*
 * Determining AMD support for a PMU event requires consulting the AMD
 * PPR for the CPU or reference material derived therefrom.
 */
static bool vcpu_supports_amd_zen_br_retired(void)
{
	struct kvm_cpuid_entry2 *entry;
	struct kvm_cpuid2 *cpuid;

	cpuid = kvm_get_supported_cpuid();
	entry = kvm_get_supported_cpuid_index(1, 0);
	return entry &&
		((x86_family(entry->eax) == 0x17 &&
		  (x86_model(entry->eax) == 1 ||
		   x86_model(entry->eax) == 0x31)) ||
		 (x86_family(entry->eax) == 0x19 &&
		  x86_model(entry->eax) == 1));
}

int main(int argc, char *argv[])
{
	void (*guest_code)(void) = NULL;
	struct kvm_vm *vm;
	int r;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	r = kvm_check_cap(KVM_CAP_PMU_EVENT_FILTER);
	if (!r) {
		print_skip("KVM_CAP_PMU_EVENT_FILTER not supported");
		exit(KSFT_SKIP);
	}

	if (vcpu_supports_intel_br_retired())
		guest_code = intel_guest_code;
	else if (vcpu_supports_amd_zen_br_retired())
		guest_code = amd_guest_code;

	if (!guest_code) {
		print_skip("Branch instructions retired not supported");
		exit(KSFT_SKIP);
	}

	vm = vm_create_default(VCPU_ID, 0, guest_code);

	test_no_filter(vm);
	test_member_deny_list(vm);
	test_member_allow_list(vm);
	test_not_member_deny_list(vm);
	test_not_member_allow_list(vm);

	kvm_vm_free(vm);

	return 0;
}
