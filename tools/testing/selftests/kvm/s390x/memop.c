// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test for s390x KVM_S390_MEM_OP
 *
 * Copyright (C) 2019, Red Hat, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define CR0_FETCH_PROTECTION_OVERRIDE	(1UL << (63 - 38))
#define CR0_STORAGE_PROTECTION_OVERRIDE	(1UL << (63 - 39))

#define VCPU_ID 1

const uint64_t last_page_addr = UINT64_MAX - PAGE_SIZE + 1;

static uint8_t mem1[65536];
static uint8_t mem2[65536];

static void set_storage_key_range(void *addr, size_t len, u8 key)
{
	uintptr_t _addr, abs, i;

	_addr = (uintptr_t)addr;
	for (i = _addr & PAGE_MASK; i < _addr + len; i += PAGE_SIZE) {
		abs = i;
		asm volatile (
			       "lra	%[abs], 0(0,%[abs])\n"
			"	sske	%[key], %[abs]\n"
			: [abs] "+&a" (abs)
			: [key] "r" (key)
			: "cc"
		);
	}
}

static void guest_code(void)
{
	/* Set storage key */
	set_storage_key_range(mem1, sizeof(mem1), 0x90);
	set_storage_key_range(mem2, sizeof(mem2), 0x90);
	GUEST_SYNC(0);

	/* Write, read back, without keys */
	memcpy(mem2, mem1, sizeof(mem2));
	GUEST_SYNC(10);

	/* Write, read back, key 0 */
	memcpy(mem2, mem1, sizeof(mem2));
	GUEST_SYNC(20);

	/* Write, read back, matching key, 1 page */
	memcpy(mem2, mem1, sizeof(mem2));
	GUEST_SYNC(30);

	/* Write, read back, matching key, all pages */
	memcpy(mem2, mem1, sizeof(mem2));
	GUEST_SYNC(40);

	/* Set fetch protection */
	set_storage_key_range(0, 1, 0x18);
	GUEST_SYNC(50);

	/* Enable fetch protection override */
	GUEST_SYNC(60);

	/* Enable storage protection override, set fetch protection*/
	set_storage_key_range(mem1, sizeof(mem1), 0x98);
	set_storage_key_range(mem2, sizeof(mem2), 0x98);
	GUEST_SYNC(70);

	/* Write, read back, mismatching key,
	 * storage protection override, all pages
	 */
	memcpy(mem2, mem1, sizeof(mem2));
	GUEST_SYNC(80);

	/* VM memop, write, read back, matching key */
	memcpy(mem2, mem1, sizeof(mem2));
	GUEST_SYNC(90);

	/* VM memop, write, read back, key 0 */
	memcpy(mem2, mem1, sizeof(mem2));
	/* VM memop, fail to read from 0 absolute/virtual, mismatching key,
	 * fetch protection override does not apply to VM memops
	 */
	asm volatile ("sske %1,%0\n"
		: : "r"(0), "r"(0x18) : "cc"
	);
	GUEST_SYNC(100);

	/* Enable AR mode */
	GUEST_SYNC(110);

	/* Disable AR mode */
	GUEST_SYNC(120);
}

static void reroll_mem1(void)
{
	int i;

	for (i = 0; i < sizeof(mem1); i++)
		mem1[i] = rand();
}

static int _vcpu_read_guest(struct kvm_vm *vm, void *host_addr,
			    uintptr_t guest_addr, size_t len)
{
	struct kvm_s390_mem_op ksmo = {
		.gaddr = guest_addr,
		.flags = 0,
		.size = len,
		.op = KVM_S390_MEMOP_LOGICAL_READ,
		.buf = (uintptr_t)host_addr,
		.ar = 0,
	};

	return _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
}

static void vcpu_read_guest(struct kvm_vm *vm, void *host_addr,
			    uintptr_t guest_addr, size_t len)
{
	int rv;

	rv = _vcpu_read_guest(vm, host_addr, guest_addr, len);
	TEST_ASSERT(rv == 0, "vcpu memop read failed: reason = %d\n", rv);
}

static int _vcpu_read_guest_key(struct kvm_vm *vm, void *host_addr,
				uintptr_t guest_addr, size_t len, u8 access_key)
{
	struct kvm_s390_mem_op ksmo = {0};

	ksmo.gaddr = guest_addr;
	ksmo.flags = KVM_S390_MEMOP_F_SKEY_PROTECTION;
	ksmo.size = len;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_READ;
	ksmo.buf = (uintptr_t)host_addr;
	ksmo.ar = 0;
	ksmo.key = access_key;

	return _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
}

static void vcpu_read_guest_key(struct kvm_vm *vm, void *host_addr,
				uintptr_t guest_addr, size_t len, u8 access_key)
{
	int rv;

	rv = _vcpu_read_guest_key(vm, host_addr, guest_addr, len, access_key);
	TEST_ASSERT(rv == 0, "vcpu memop read failed: reason = %d\n", rv);
}

static int _vcpu_write_guest(struct kvm_vm *vm, uintptr_t guest_addr,
			     void *host_addr, size_t len)
{
	struct kvm_s390_mem_op ksmo = {
		.gaddr = guest_addr,
		.flags = 0,
		.size = len,
		.op = KVM_S390_MEMOP_LOGICAL_WRITE,
		.buf = (uintptr_t)host_addr,
		.ar = 0,
	};
	return _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
}

static void vcpu_write_guest(struct kvm_vm *vm, uintptr_t guest_addr,
			     void *host_addr, size_t len)
{
	int rv;

	rv = _vcpu_write_guest(vm, guest_addr, host_addr, len);
	TEST_ASSERT(rv == 0, "vcpu memop write failed: reason = %d\n", rv);
}

static int _vcpu_write_guest_key(struct kvm_vm *vm, uintptr_t guest_addr,
				 void *host_addr, size_t len, u8 access_key)
{
	struct kvm_s390_mem_op ksmo = {0};

	ksmo.gaddr = guest_addr;
	ksmo.flags = KVM_S390_MEMOP_F_SKEY_PROTECTION;
	ksmo.size = len;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)host_addr;
	ksmo.ar = 0;
	ksmo.key = access_key;

	return _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
}

static void vcpu_write_guest_key(struct kvm_vm *vm, uintptr_t guest_addr,
				 void *host_addr, size_t len, u8 access_key)
{
	int rv;

	rv = _vcpu_write_guest_key(vm, guest_addr, host_addr, len, access_key);
	TEST_ASSERT(rv == 0, "vcpu memop write failed: reason = %d\n", rv);
}

static int _vm_read_guest_key(struct kvm_vm *vm, void *host_addr,
			      uintptr_t guest_addr, size_t len, u8 access_key)
{
	struct kvm_s390_mem_op ksmo = {0};

	ksmo.gaddr = guest_addr;
	ksmo.flags = KVM_S390_MEMOP_F_SKEY_PROTECTION;
	ksmo.size = len;
	ksmo.op = KVM_S390_MEMOP_ABSOLUTE_READ;
	ksmo.buf = (uintptr_t)host_addr;
	ksmo.key = access_key;

	return _vm_ioctl(vm, KVM_S390_MEM_OP, &ksmo);
}

static void vm_read_guest_key(struct kvm_vm *vm, void *host_addr,
			      uintptr_t guest_addr, size_t len, u8 access_key)
{
	int rv;

	rv = _vm_read_guest_key(vm, host_addr, guest_addr, len, access_key);
	TEST_ASSERT(rv == 0, "vm memop read failed: reason = %d\n", rv);
}

static int _vm_write_guest_key(struct kvm_vm *vm, uintptr_t guest_addr,
			       void *host_addr, size_t len, u8 access_key)
{
	struct kvm_s390_mem_op ksmo = {0};

	ksmo.gaddr = guest_addr;
	ksmo.flags = KVM_S390_MEMOP_F_SKEY_PROTECTION;
	ksmo.size = len;
	ksmo.op = KVM_S390_MEMOP_ABSOLUTE_WRITE;
	ksmo.buf = (uintptr_t)host_addr;
	ksmo.key = access_key;

	return _vm_ioctl(vm, KVM_S390_MEM_OP, &ksmo);
}

static void vm_write_guest_key(struct kvm_vm *vm, uintptr_t guest_addr,
			       void *host_addr, size_t len, u8 access_key)
{
	int rv;

	rv = _vm_write_guest_key(vm, guest_addr, host_addr, len, access_key);
	TEST_ASSERT(rv == 0, "vm memop write failed: reason = %d\n", rv);
}

enum access_mode {
	ACCESS_READ,
	ACCESS_WRITE
};

static int _vm_check_guest_key(struct kvm_vm *vm, enum access_mode mode,
			       uintptr_t guest_addr, size_t len, u8 access_key)
{
	struct kvm_s390_mem_op ksmo = {0};

	ksmo.gaddr = guest_addr;
	ksmo.flags = KVM_S390_MEMOP_F_CHECK_ONLY | KVM_S390_MEMOP_F_SKEY_PROTECTION;
	ksmo.size = len;
	if (mode == ACCESS_READ)
		ksmo.op = KVM_S390_MEMOP_ABSOLUTE_READ;
	else
		ksmo.op = KVM_S390_MEMOP_ABSOLUTE_WRITE;
	ksmo.key = access_key;

	return _vm_ioctl(vm, KVM_S390_MEM_OP, &ksmo);
}

static void vm_check_guest_key(struct kvm_vm *vm, enum access_mode mode,
			       uintptr_t guest_addr, size_t len, u8 access_key)
{
	int rv;

	rv = _vm_check_guest_key(vm, mode, guest_addr, len, access_key);
	TEST_ASSERT(rv == 0, "vm memop write failed: reason = %d\n", rv);
}

#define HOST_SYNC(vmp, stage)						\
({									\
	struct kvm_vm *__vm = (vmp);					\
	struct ucall uc;						\
	int __stage = (stage);						\
									\
	vcpu_run(__vm, VCPU_ID);					\
	get_ucall(__vm, VCPU_ID, &uc);					\
	ASSERT_EQ(uc.cmd, UCALL_SYNC);					\
	ASSERT_EQ(uc.args[1], __stage);					\
})									\

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_s390_mem_op ksmo;
	bool has_skey_ext;
	vm_vaddr_t guest_mem1;
	vm_vaddr_t guest_mem2;
	vm_paddr_t guest_mem1_abs;
	int rv, maxsize;

	setbuf(stdout, NULL);	/* Tell stdout not to buffer its content */

	maxsize = kvm_check_cap(KVM_CAP_S390_MEM_OP);
	if (!maxsize) {
		print_skip("CAP_S390_MEM_OP not supported");
		exit(KSFT_SKIP);
	}
	if (maxsize > sizeof(mem1))
		maxsize = sizeof(mem1);
	has_skey_ext = kvm_check_cap(KVM_CAP_S390_MEM_OP_EXTENSION);
	if (!has_skey_ext)
		print_skip("Storage key extension not supported");

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);
	run = vcpu_state(vm, VCPU_ID);
	guest_mem1 = (uintptr_t)mem1;
	guest_mem2 = (uintptr_t)mem2;
	guest_mem1_abs = addr_gva2gpa(vm, guest_mem1);

	/* Set storage key */
	HOST_SYNC(vm, 0);

	/* Write, read back, without keys */
	reroll_mem1();
	vcpu_write_guest(vm, guest_mem1, mem1, maxsize);
	HOST_SYNC(vm, 10); // Copy in vm
	memset(mem2, 0xaa, sizeof(mem2));
	vcpu_read_guest(vm, mem2, guest_mem2, maxsize);
	TEST_ASSERT(!memcmp(mem1, mem2, maxsize),
		    "Memory contents do not match!");

	if (has_skey_ext) {
		vm_vaddr_t guest_0_page = vm_vaddr_alloc(vm, PAGE_SIZE, 0);
		vm_vaddr_t guest_last_page = vm_vaddr_alloc(vm, PAGE_SIZE, last_page_addr);
		vm_paddr_t guest_mem2_abs = addr_gva2gpa(vm, guest_mem2);

		/* Write, read back, key 0 */
		reroll_mem1();
		vcpu_write_guest_key(vm, guest_mem1, mem1, maxsize, 0);
		HOST_SYNC(vm, 20); // Copy in vm
		memset(mem2, 0xaa, sizeof(mem2));
		vcpu_read_guest_key(vm, mem2, guest_mem2, maxsize, 0);
		TEST_ASSERT(!memcmp(mem1, mem2, maxsize),
			    "Memory contents do not match!");

		/* Write, read back, matching key, 1 page */
		reroll_mem1();
		vcpu_write_guest_key(vm, guest_mem1, mem1, PAGE_SIZE, 9);
		HOST_SYNC(vm, 30); // Copy in vm
		memset(mem2, 0xaa, sizeof(mem2));
		vcpu_read_guest_key(vm, mem2, guest_mem2, PAGE_SIZE, 9);
		TEST_ASSERT(!memcmp(mem1, mem2, PAGE_SIZE),
			    "Memory contents do not match!");

		/* Write, read back, matching key, all pages */
		reroll_mem1();
		vcpu_write_guest_key(vm, guest_mem1, mem1, maxsize, 9);
		HOST_SYNC(vm, 40); // Copy in vm
		memset(mem2, 0xaa, sizeof(mem2));
		vcpu_read_guest_key(vm, mem2, guest_mem2, maxsize, 9);
		TEST_ASSERT(!memcmp(mem1, mem2, maxsize),
			    "Memory contents do not match!");

		/* Fail to write, read back old value, mismatching key */
		rv = _vcpu_write_guest_key(vm, guest_mem1, mem1, maxsize, 2);
		TEST_ASSERT(rv == 4, "Store should result in protection exception");
		memset(mem2, 0xaa, sizeof(mem2));
		vcpu_read_guest_key(vm, mem2, guest_mem2, maxsize, 2);
		TEST_ASSERT(!memcmp(mem1, mem2, maxsize),
			    "Memory contents do not match!");

		/* Set fetch protection */
		HOST_SYNC(vm, 50);

		/* Write without key, read back, matching key, fetch protection */
		reroll_mem1();
		vcpu_write_guest(vm, guest_0_page, mem1, PAGE_SIZE);
		memset(mem2, 0xaa, sizeof(mem2));
		/* Lets not copy in the guest, in case guest_0_page != 0 */
		vcpu_read_guest_key(vm, mem2, guest_0_page, PAGE_SIZE, 1);
		TEST_ASSERT(!memcmp(mem1, mem2, PAGE_SIZE),
			    "Memory contents do not match!");

		/* Fail to read,  mismatching key, fetch protection */
		rv = _vcpu_read_guest_key(vm, mem2, guest_0_page, PAGE_SIZE, 2);
		TEST_ASSERT(rv == 4, "Fetch should result in protection exception");

		/* Enable fetch protection override */
		run->s.regs.crs[0] |= CR0_FETCH_PROTECTION_OVERRIDE;
		run->kvm_dirty_regs = KVM_SYNC_CRS;
		HOST_SYNC(vm, 60);

		if (guest_0_page != 0)
			print_skip("Did not allocate page at 0 for fetch protection override test");

		/* Write without key, read back, mismatching key,
		 * fetch protection override, 1 page
		 */
		if (guest_0_page == 0) {
			reroll_mem1();
			vcpu_write_guest(vm, guest_0_page, mem1, PAGE_SIZE);
			memset(mem2, 0xaa, sizeof(mem2));
			/* Lets not copy in the guest, in case guest_0_page != 0 */
			vcpu_read_guest_key(vm, mem2, guest_0_page, 2048, 2);
			TEST_ASSERT(!memcmp(mem1, mem2, 2048),
				    "Memory contents do not match!");
		}

		/* Fail to read, mismatching key,
		 * fetch protection override address exceeded, 1 page
		 */
		if (guest_0_page == 0) {
			rv = _vcpu_read_guest_key(vm, mem2, 0, 2048 + 1, 2);
			TEST_ASSERT(rv == 4,
				    "Fetch should result in protection exception");
		}

		if (guest_last_page != last_page_addr)
			print_skip("Did not allocate last page for fetch protection override test");

		/* Write without key, read back, mismatching key,
		 * fetch protection override, 2 pages, last page not fetch protected
		 */
		reroll_mem1();
		vcpu_write_guest(vm, guest_last_page, mem1, PAGE_SIZE);
		vcpu_write_guest(vm, guest_0_page, mem1 + PAGE_SIZE, PAGE_SIZE);
		if (guest_0_page == 0 && guest_last_page == last_page_addr) {
			memset(mem2, 0xaa, sizeof(mem2));
			/* Lets not copy in the guest, in case guest_0_page != 0 */
			vcpu_read_guest_key(vm, mem2, last_page_addr,
					    PAGE_SIZE + 2048, 2);
			TEST_ASSERT(!memcmp(mem1, mem2, PAGE_SIZE + 2048),
				    "Memory contents do not match!");
		}

		/* Fail to read, mismatching key, fetch protection override address
		 * exceeded, 2 pages, last page not fetch protected
		 */
		if (guest_0_page == 0 && guest_last_page == last_page_addr) {
			rv = _vcpu_read_guest_key(vm, mem2, last_page_addr,
						  PAGE_SIZE + 2048 + 1, 2);
			TEST_ASSERT(rv == 4,
				    "Fetch should result in protection exception");
		}

		/* Enable storage protection override, set fetch protection*/
		run->s.regs.crs[0] |= CR0_STORAGE_PROTECTION_OVERRIDE;
		run->kvm_dirty_regs = KVM_SYNC_CRS;
		HOST_SYNC(vm, 70);

		/* Write, read back, mismatching key,
		 * storage protection override, all pages
		 */
		reroll_mem1();
		vcpu_write_guest_key(vm, guest_mem1, mem1, maxsize, 2);
		HOST_SYNC(vm, 80); // Copy in vm
		memset(mem2, 0xaa, sizeof(mem2));
		vcpu_read_guest_key(vm, mem2, guest_mem2, maxsize, 2);
		TEST_ASSERT(!memcmp(mem1, mem2, maxsize),
			    "Memory contents do not match!");

		/* VM memop, write, read back, matching key */
		reroll_mem1();
		vm_write_guest_key(vm, guest_mem1_abs, mem1, maxsize, 9);
		HOST_SYNC(vm, 90); // Copy in vm
		memset(mem2, 0xaa, sizeof(mem2));
		vm_read_guest_key(vm, mem2, guest_mem2_abs, maxsize, 9);
		TEST_ASSERT(!memcmp(mem1, mem2, maxsize),
			    "Memory contents do not match!");
		vm_check_guest_key(vm, ACCESS_WRITE, guest_mem1_abs, maxsize, 9);
		vm_check_guest_key(vm, ACCESS_READ, guest_mem2_abs, maxsize, 9);

		/* VM memop, write, read back, key 0 */
		reroll_mem1();
		vm_write_guest_key(vm, guest_mem1_abs, mem1, maxsize, 0);
		HOST_SYNC(vm, 100); // Copy in vm
		memset(mem2, 0xaa, sizeof(mem2));
		vm_read_guest_key(vm, mem2, guest_mem2_abs, maxsize, 0);
		TEST_ASSERT(!memcmp(mem1, mem2, maxsize),
			    "Memory contents do not match!");
		rv = _vm_check_guest_key(vm, ACCESS_READ, guest_mem1_abs, maxsize, 9);
		TEST_ASSERT(rv == 0, "Check should succeed");
		vm_check_guest_key(vm, ACCESS_WRITE, guest_mem1_abs, maxsize, 0);
		vm_check_guest_key(vm, ACCESS_READ, guest_mem2_abs, maxsize, 0);

		/* VM memop, fail to write, fail to read, mismatching key,
		 * storage protection override does not apply to VM memops
		 */
		rv = _vm_write_guest_key(vm, guest_mem1_abs, mem1, maxsize, 2);
		TEST_ASSERT(rv == 4, "Store should result in protection exception");
		rv = _vm_read_guest_key(vm, mem2, guest_mem2_abs, maxsize, 2);
		TEST_ASSERT(rv == 4, "Fetch should result in protection exception");
		rv = _vm_check_guest_key(vm, ACCESS_WRITE, guest_mem1_abs, maxsize, 2);
		TEST_ASSERT(rv == 4, "Check should indicate protection exception");
		rv = _vm_check_guest_key(vm, ACCESS_READ, guest_mem2_abs, maxsize, 2);
		TEST_ASSERT(rv == 4, "Check should indicate protection exception");

		/* VM memop, fail to read from 0 absolute/virtual, mismatching key,
		 * fetch protection override does not apply to VM memops
		 */
		rv = _vm_read_guest_key(vm, mem2, 0, 2048, 2);
		TEST_ASSERT(rv != 0, "Fetch should result in exception");
		rv = _vm_read_guest_key(vm, mem2, addr_gva2gpa(vm, 0), 2048, 2);
		TEST_ASSERT(rv == 4, "Fetch should result in protection exception");
	} else {
		struct ucall uc;

		do {
			vcpu_run(vm, VCPU_ID);
			get_ucall(vm, VCPU_ID, &uc);
			ASSERT_EQ(uc.cmd, UCALL_SYNC);
		} while (uc.args[1] < 100);
	}

	/* Check error conditions */

	/* Bad size: */
	rv = _vcpu_write_guest(vm, (uintptr_t)mem1, mem1, -1);
	TEST_ASSERT(rv == -1 && errno == E2BIG, "ioctl allows insane sizes");

	/* Zero size: */
	rv = _vcpu_write_guest(vm, (uintptr_t)mem1, mem1, 0);
	TEST_ASSERT(rv == -1 && (errno == EINVAL || errno == ENOMEM),
		    "ioctl allows 0 as size");

	/* Bad flags: */
	ksmo.gaddr = guest_mem1;
	ksmo.flags = -1;
	ksmo.size = maxsize;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows all flags");

	/* Bad operation: */
	ksmo.gaddr = guest_mem1;
	ksmo.flags = 0;
	ksmo.size = maxsize;
	ksmo.op = -1;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows bad operations");

	/* Bad guest address: */
	ksmo.gaddr = ~0xfffUL;
	ksmo.flags = KVM_S390_MEMOP_F_CHECK_ONLY;
	ksmo.size = maxsize;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 0;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv > 0, "ioctl does not report bad guest memory access");

	/* Bad host address: */
	rv = _vcpu_write_guest(vm, guest_mem1, 0, maxsize);
	TEST_ASSERT(rv == -1 && errno == EFAULT,
		    "ioctl does not report bad host memory address");

	/* Enable AR mode */
	run->psw_mask &= ~(3UL << (63 - 17));
	run->psw_mask |= 1UL << (63 - 17);
	HOST_SYNC(vm, 110);

	/* Bad access register: */
	ksmo.gaddr = guest_mem1;
	ksmo.flags = 0;
	ksmo.size = maxsize;
	ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
	ksmo.buf = (uintptr_t)mem1;
	ksmo.ar = 17;
	rv = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_MEM_OP, &ksmo);
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows ARs > 15");

	/* Disable AR mode */
	run->psw_mask &= ~(3UL << (63 - 17));
	HOST_SYNC(vm, 120);

	kvm_vm_free(vm);

	return 0;
}
