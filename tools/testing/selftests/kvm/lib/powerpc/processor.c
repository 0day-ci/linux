// SPDX-License-Identifier: GPL-2.0-only
/*
 * powerpc specific code
 */
#include "kvm_util.h"
#include "../kvm_util_internal.h"
#include "processor.h"


void virt_pg_map(struct kvm_vm *vm, uint64_t gva, uint64_t gpa, uint32_t memslot)
{
	TEST_FAIL("%s not implemented", __func__);
}

vm_paddr_t addr_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva)
{
	TEST_FAIL("%s not implemented", __func__);
	return 0;
}

void virt_pgd_alloc(struct kvm_vm *vm, uint32_t pgd_memslot)
{
	TEST_FAIL("%s not implemented", __func__);
}

void vm_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid, void *guest_code)
{
	TEST_FAIL("%s not implemented", __func__);
}

void virt_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
{
	TEST_FAIL("%s not implemented", __func__);
}

void vcpu_dump(FILE *stream, struct kvm_vm *vm, uint32_t vcpuid, uint8_t indent)
{
	TEST_FAIL("%s not implemented", __func__);
}

void assert_on_unhandled_exception(struct kvm_vm *vm, uint32_t vcpuid)
{
	TEST_ASSERT(false, "Unhandled exception");
}
