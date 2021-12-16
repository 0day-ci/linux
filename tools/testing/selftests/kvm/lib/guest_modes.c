// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, Red Hat, Inc.
 */
#include "guest_modes.h"

#ifdef __aarch64__
#include "processor.h"
enum vm_guest_mode vm_mode_default;
static void get_supported_psz(uint32_t ipa,
			      bool *ps4k, bool *ps16k, bool *ps64k)
{
	struct kvm_vcpu_init preferred_init;
	int kvm_fd, vm_fd, vcpu_fd, err;
	uint64_t val;
	struct kvm_one_reg reg = {
		.id	= KVM_ARM64_SYS_REG(SYS_ID_AA64MMFR0_EL1),
		.addr	= (uint64_t)&val,
	};

	kvm_fd = open_kvm_dev_path_or_exit();
	vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, ipa);
	TEST_ASSERT(vm_fd >= 0, "Can't create VM");

	vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
	TEST_ASSERT(vcpu_fd >= 0, "Can't create vcpu");

	err = ioctl(vm_fd, KVM_ARM_PREFERRED_TARGET, &preferred_init);
	TEST_ASSERT(err == 0, "Can't get target");
	err = ioctl(vcpu_fd, KVM_ARM_VCPU_INIT, &preferred_init);
	TEST_ASSERT(err == 0, "Can't get init vcpu");

	err = ioctl(vcpu_fd, KVM_GET_ONE_REG, &reg);
	TEST_ASSERT(err == 0, "Can't get MMFR0");

	*ps4k = ((val >> 28) & 0xf) != 0xf;
	*ps64k = ((val >> 24) & 0xf) == 0;
	*ps16k = ((val >> 20) & 0xf) != 0;

	close(vcpu_fd);
	close(vm_fd);
	close(kvm_fd);
}
#endif

struct guest_mode guest_modes[NUM_VM_MODES];

void guest_modes_append_default(void)
{
#ifndef __aarch64__
	guest_mode_append(VM_MODE_DEFAULT, true, true);
#endif
#ifdef __aarch64__
	{
		unsigned int limit = kvm_check_cap(KVM_CAP_ARM_VM_IPA_SIZE);
		bool ps4k, ps16k, ps64k;
		int i;

		get_supported_psz(limit, &ps4k, &ps16k, &ps64k);

		vm_mode_default = NUM_VM_MODES;

		if (limit >= 52)
			guest_mode_append(VM_MODE_P52V48_64K, ps64k, ps64k);
		if (limit >= 48) {
			guest_mode_append(VM_MODE_P48V48_4K, ps4k, ps4k);
			guest_mode_append(VM_MODE_P48V48_64K, ps64k, ps64k);
		}
		if (limit >= 40) {
			guest_mode_append(VM_MODE_P40V48_4K, ps4k, ps4k);
			guest_mode_append(VM_MODE_P40V48_64K, ps64k, ps64k);
			if (ps4k)
				vm_mode_default = VM_MODE_P40V48_4K;
		}

		/* Pick the largest supported IPA size */
		for (i = 0;
		     vm_mode_default == NUM_VM_MODES && i < NUM_VM_MODES;
		     i++) {
			if (guest_modes[i].supported)
				vm_mode_default = i;
		}

		TEST_ASSERT(vm_mode_default != NUM_VM_MODES,
			    "No supported mode!");
	}
#endif
#ifdef __s390x__
	{
		int kvm_fd, vm_fd;
		struct kvm_s390_vm_cpu_processor info;

		kvm_fd = open_kvm_dev_path_or_exit();
		vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
		kvm_device_access(vm_fd, KVM_S390_VM_CPU_MODEL,
				  KVM_S390_VM_CPU_PROCESSOR, &info, false);
		close(vm_fd);
		close(kvm_fd);
		/* Starting with z13 we have 47bits of physical address */
		if (info.ibc >= 0x30)
			guest_mode_append(VM_MODE_P47V64_4K, true, true);
	}
#endif
}

void for_each_guest_mode(void (*func)(enum vm_guest_mode, void *), void *arg)
{
	int i;

	for (i = 0; i < NUM_VM_MODES; ++i) {
		if (!guest_modes[i].enabled)
			continue;
		TEST_ASSERT(guest_modes[i].supported,
			    "Guest mode ID %d (%s) not supported.",
			    i, vm_guest_mode_string(i));
		func(i, arg);
	}
}

void guest_modes_help(void)
{
	int i;

	printf(" -m: specify the guest mode ID to test\n"
	       "     (default: test all supported modes)\n"
	       "     This option may be used multiple times.\n"
	       "     Guest mode IDs:\n");
	for (i = 0; i < NUM_VM_MODES; ++i) {
		printf("         %d:    %s%s\n", i, vm_guest_mode_string(i),
		       guest_modes[i].supported ? " (supported)" : "");
	}
}

void guest_modes_cmdline(const char *arg)
{
	static bool mode_selected;
	unsigned int mode;
	int i;

	if (!mode_selected) {
		for (i = 0; i < NUM_VM_MODES; ++i)
			guest_modes[i].enabled = false;
		mode_selected = true;
	}

	mode = strtoul(optarg, NULL, 10);
	TEST_ASSERT(mode < NUM_VM_MODES, "Guest mode ID %d too big", mode);
	guest_modes[mode].enabled = true;
}
