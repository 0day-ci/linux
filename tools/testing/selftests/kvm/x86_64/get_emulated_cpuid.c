// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Red Hat Inc.
 *
 * Generic tests for KVM CPUID set/get ioctls
 */
#include <asm/kvm_para.h>
#include <linux/kvm_para.h>
#include <stdint.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#define VCPU_ID 0
#define MAX_NENT 1000

/* CPUIDs known to differ */
struct {
	u32 function;
	u32 index;
} mangled_cpuids[] = {
	{.function = 0xd, .index = 0},
};

static void guest_main(void)
{

}

static bool is_cpuid_mangled(struct kvm_cpuid_entry2 *entrie)
{
	int i;

	for (i = 0; i < sizeof(mangled_cpuids); i++) {
		if (mangled_cpuids[i].function == entrie->function &&
		    mangled_cpuids[i].index == entrie->index)
			return true;
	}

	return false;
}

static void check_cpuid(struct kvm_cpuid2 *cpuid, struct kvm_cpuid_entry2 *entrie)
{
	int i;

	for (i = 0; i < cpuid->nent; i++) {
		if (cpuid->entries[i].function == entrie->function &&
		    cpuid->entries[i].index == entrie->index) {
			if (is_cpuid_mangled(entrie))
				return;

			TEST_ASSERT(cpuid->entries[i].eax == entrie->eax &&
				    cpuid->entries[i].ebx == entrie->ebx &&
				    cpuid->entries[i].ecx == entrie->ecx &&
				    cpuid->entries[i].edx == entrie->edx,
				    "CPUID 0x%x.%x differ: 0x%x:0x%x:0x%x:0x%x vs 0x%x:0x%x:0x%x:0x%x",
				    entrie->function, entrie->index,
				    cpuid->entries[i].eax, cpuid->entries[i].ebx,
				    cpuid->entries[i].ecx, cpuid->entries[i].edx,
				    entrie->eax, entrie->ebx, entrie->ecx, entrie->edx);
			return;
		}
	}

	TEST_ASSERT(false, "CPUID 0x%x.%x not found", entrie->function, entrie->index);
}

static void compare_cpuids(struct kvm_cpuid2 *cpuid1,
						   struct kvm_cpuid2 *cpuid2)
{
	int i;

	for (i = 0; i < cpuid1->nent; i++)
		check_cpuid(cpuid2, &cpuid1->entries[i]);

	for (i = 0; i < cpuid2->nent; i++)
		check_cpuid(cpuid1, &cpuid2->entries[i]);
}

struct kvm_cpuid2 *vcpu_alloc_cpuid(struct kvm_vm *vm, vm_vaddr_t *p_gva, struct kvm_cpuid2 *cpuid)
{
	int size = sizeof(*cpuid) + cpuid->nent * sizeof(cpuid->entries[0]);
	vm_vaddr_t gva = vm_vaddr_alloc(vm, size,
					getpagesize(), 0, 0);
	struct kvm_cpuid2 *guest_cpuids = addr_gva2hva(vm, gva);

	memcpy(guest_cpuids, cpuid, size);

	*p_gva = gva;
	return guest_cpuids;
}

static struct kvm_cpuid2 *alloc_custom_kvm_cpuid2(int nent)
{
	struct kvm_cpuid2 *cpuid;
	size_t size;

	size = sizeof(*cpuid);
	size += nent * sizeof(struct kvm_cpuid_entry2);
	cpuid = calloc(1, size);
	if (!cpuid) {
		perror("malloc");
		abort();
	}

	cpuid->nent = nent;

	return cpuid;
}

static void clean_entries_kvm_cpuid2(struct kvm_cpuid2 *cpuid, int nent)
{
	size_t size;
	int old_nent;

	size = sizeof(*cpuid);
	size += nent * sizeof(struct kvm_cpuid_entry2);

	old_nent = cpuid->nent;
	memset(cpuid, 0, size);
	cpuid->nent = old_nent;
}

static void test_emulated_entries(struct kvm_vm *vm)
{
	int res, right_nent;
	struct kvm_cpuid2 *cpuid;

	cpuid = alloc_custom_kvm_cpuid2(MAX_NENT);

	/* 0 nent, return E2BIG */
	cpuid->nent = 0;
	res = _kvm_ioctl(vm, KVM_GET_EMULATED_CPUID, cpuid);
	TEST_ASSERT(res == -1 && errno == E2BIG,
				"KVM_GET_EMULATED_CPUID should fail E2BIG with nent=0");
	clean_entries_kvm_cpuid2(cpuid, MAX_NENT);

	/* high nent, set the entries and adjust */
	cpuid->nent = MAX_NENT;
	res = _kvm_ioctl(vm, KVM_GET_EMULATED_CPUID, cpuid);
	TEST_ASSERT(res == 0,
			"KVM_GET_EMULATED_CPUID should not fail with nent > actual nent");
	right_nent = cpuid->nent;
	clean_entries_kvm_cpuid2(cpuid, MAX_NENT);

	/* high nent, set the entries and adjust */
	cpuid->nent++;
	res = _kvm_ioctl(vm, KVM_GET_EMULATED_CPUID, cpuid);
	TEST_ASSERT(res == 0,
			"KVM_GET_EMULATED_CPUID should not fail with nent > actual nent");
	TEST_ASSERT(right_nent == cpuid->nent,
				"KVM_GET_EMULATED_CPUID nent should be always the same");
	clean_entries_kvm_cpuid2(cpuid, MAX_NENT);

	/* low nent, return E2BIG */
	if (right_nent > 1) {
		cpuid->nent = 1;
		res = _kvm_ioctl(vm, KVM_GET_EMULATED_CPUID, cpuid);
		TEST_ASSERT(res == -1 && errno == E2BIG,
					"KVM_GET_EMULATED_CPUID should fail with nent=1");
		clean_entries_kvm_cpuid2(cpuid, MAX_NENT);
	}

	/* exact nent */
	cpuid->nent = right_nent;
	res = _kvm_ioctl(vm, KVM_GET_EMULATED_CPUID, cpuid);
	TEST_ASSERT(res == 0,
			"KVM_GET_EMULATED_CPUID should not fail with nent == actual nent");
	TEST_ASSERT(cpuid->nent == right_nent,
			"KVM_GET_EMULATED_CPUID should be invaried when nent is exact");
	clean_entries_kvm_cpuid2(cpuid, MAX_NENT);

	free(cpuid);
}

int main(void)
{
	struct kvm_cpuid2 *emul_cpuid, *cpuid2;
	struct kvm_vm *vm;

	if (!kvm_check_cap(KVM_CAP_EXT_EMUL_CPUID)) {
		print_skip("KVM_GET_EMULATED_CPUID not available");
		return 0;
	}

	vm = vm_create_default(VCPU_ID, 0, guest_main);

	emul_cpuid = kvm_get_emulated_cpuid();
	vcpu_set_cpuid(vm, VCPU_ID, emul_cpuid);
	cpuid2 = vcpu_get_cpuid(vm, VCPU_ID);

	test_emulated_entries(vm);
	compare_cpuids(emul_cpuid, cpuid2);

	kvm_vm_free(vm);
}
