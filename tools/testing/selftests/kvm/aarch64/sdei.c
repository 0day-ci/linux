// SPDX-License-Identifier: GPL-2.0
/*
 * ARM SDEI test
 *
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * Author(s): Gavin Shan <gshan@redhat.com>
 */
#define _GNU_SOURCE
#include <stdio.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "asm/kvm_sdei.h"
#include "linux/arm_sdei.h"

#define NR_VCPUS	2

struct sdei_event {
	uint32_t	cpu;
	uint64_t	version;
	uint64_t	num;
	uint64_t	type;
	uint64_t	priority;
	uint64_t	signaled;
};

static struct sdei_event sdei_events[NR_VCPUS];

static int64_t smccc(uint32_t func, uint64_t arg0, uint64_t arg1,
		     uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
	int64_t ret;

	asm volatile(
		"mov    x0, %1\n"
		"mov    x1, %2\n"
		"mov    x2, %3\n"
		"mov    x3, %4\n"
		"mov    x4, %5\n"
		"mov    x5, %6\n"
		"hvc    #0\n"
		"mov    %0, x0\n"
	: "=r" (ret) : "r" (func), "r" (arg0), "r" (arg1),
	"r" (arg2), "r" (arg3), "r" (arg4) :
	"x0", "x1", "x2", "x3", "x4", "x5");

	return ret;
}

static inline bool is_error(int64_t ret)
{
	if (ret == SDEI_NOT_SUPPORTED      ||
	    ret == SDEI_INVALID_PARAMETERS ||
	    ret == SDEI_DENIED             ||
	    ret == SDEI_PENDING            ||
	    ret == SDEI_OUT_OF_RESOURCE)
		return true;

	return false;
}

static void guest_code(int cpu)
{
	struct sdei_event *event = &sdei_events[cpu];
	int64_t ret;

	/* CPU */
	event->cpu = cpu;
	event->num = KVM_SDEI_DEFAULT_NUM;
	GUEST_ASSERT(cpu < NR_VCPUS);

	/* Version */
	ret = smccc(SDEI_1_0_FN_SDEI_VERSION, 0, 0, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));
	GUEST_ASSERT(SDEI_VERSION_MAJOR(ret) == 1);
	GUEST_ASSERT(SDEI_VERSION_MINOR(ret) == 0);
	event->version = ret;

	/* CPU unmasking */
	ret = smccc(SDEI_1_0_FN_SDEI_PE_UNMASK, 0, 0, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));

	/* Reset */
	ret = smccc(SDEI_1_0_FN_SDEI_PRIVATE_RESET, 0, 0, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));
	ret = smccc(SDEI_1_0_FN_SDEI_SHARED_RESET, 0, 0, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));

	/* Event properties */
	ret = smccc(SDEI_1_0_FN_SDEI_EVENT_GET_INFO,
		     event->num, SDEI_EVENT_INFO_EV_TYPE, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));
	event->type = ret;

	ret = smccc(SDEI_1_0_FN_SDEI_EVENT_GET_INFO,
		    event->num, SDEI_EVENT_INFO_EV_PRIORITY, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));
	event->priority = ret;

	ret = smccc(SDEI_1_0_FN_SDEI_EVENT_GET_INFO,
		    event->num, SDEI_EVENT_INFO_EV_SIGNALED, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));
	event->signaled = ret;

	/* Event registration */
	ret = smccc(SDEI_1_0_FN_SDEI_EVENT_REGISTER,
		    event->num, 0, 0, SDEI_EVENT_REGISTER_RM_ANY, 0);
	GUEST_ASSERT(!is_error(ret));

	/* Event enablement */
	ret = smccc(SDEI_1_0_FN_SDEI_EVENT_ENABLE,
		    event->num, 0, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));

	/* Event disablement */
	ret = smccc(SDEI_1_0_FN_SDEI_EVENT_DISABLE,
		    event->num, 0, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));

	/* Event unregistration */
	ret = smccc(SDEI_1_0_FN_SDEI_EVENT_UNREGISTER,
		    event->num, 0, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));

	/* CPU masking */
	ret = smccc(SDEI_1_0_FN_SDEI_PE_MASK, 0, 0, 0, 0, 0);
	GUEST_ASSERT(!is_error(ret));

	GUEST_DONE();
}

int main(int argc, char **argv)
{
	struct kvm_vm *vm;
	int i;

	if (!kvm_check_cap(KVM_CAP_ARM_SDEI)) {
		pr_info("SDEI not supported\n");
		return 0;
	}

	vm = vm_create_default(0, 0, guest_code);
	ucall_init(vm, NULL);

	for (i = 1; i < NR_VCPUS; i++)
		vm_vcpu_add_default(vm, i, guest_code);

	for (i = 0; i < NR_VCPUS; i++) {
		vcpu_args_set(vm, i, 1, i);
		vcpu_run(vm, i);

		sync_global_from_guest(vm, sdei_events[i]);
		pr_info("--------------------------------\n");
		pr_info("CPU:      %d\n",
			sdei_events[i].cpu);
		pr_info("Version:  %ld.%ld (0x%lx)\n",
			SDEI_VERSION_MAJOR(sdei_events[i].version),
			SDEI_VERSION_MINOR(sdei_events[i].version),
			SDEI_VERSION_VENDOR(sdei_events[i].version));
		pr_info("Event:    0x%08lx\n",
			sdei_events[i].num);
		pr_info("Type:     %s\n",
			sdei_events[i].type ? "shared" : "private");
		pr_info("Signaled: %s\n",
			sdei_events[i].signaled ? "yes" : "no");
	}

	return 0;
}
