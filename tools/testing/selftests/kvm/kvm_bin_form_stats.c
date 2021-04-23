// SPDX-License-Identifier: GPL-2.0-only
/*
 * kvm_bin_form_stats
 *
 * Copyright (C) 2021, Google LLC.
 *
 * Test the fd-based interface for KVM statistics.
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "test_util.h"

#include "kvm_util.h"
#include "asm/kvm.h"
#include "linux/kvm.h"

int vm_stats_test(struct kvm_vm *vm)
{
	ssize_t ret;
	int i, stats_fd, err = -1;
	size_t size_desc, size_data = 0;
	struct kvm_stats_header header;
	struct kvm_stats_desc *stats_desc, *pdesc;
	struct kvm_vm_stats_data *stats_data;

	/* Get fd for VM stats */
	stats_fd = vm_get_statsfd(vm);
	if (stats_fd < 0) {
		perror("Get VM stats fd");
		return err;
	}
	/* Read kvm vm stats header */
	ret = read(stats_fd, &header, sizeof(header));
	if (ret != sizeof(header)) {
		perror("Read VM stats header");
		goto out_close_fd;
	}
	size_desc = sizeof(*stats_desc) + header.name_size;
	/* Check id string in header, that should start with "kvm" */
	if (strncmp(header.id, "kvm", 3) ||
			strlen(header.id) >= KVM_STATS_ID_MAXLEN) {
		printf("Invalid KVM VM stats type!\n");
		goto out_close_fd;
	}
	/* Sanity check for other fields in header */
	if (header.count == 0) {
		err = 0;
		goto out_close_fd;
	}
	/* Check overlap */
	if (header.desc_offset == 0 || header.data_offset == 0 ||
			header.desc_offset < sizeof(header) ||
			header.data_offset < sizeof(header)) {
		printf("Invalid offset fields in header!\n");
		goto out_close_fd;
	}
	if (header.desc_offset < header.data_offset &&
			(header.desc_offset + size_desc * header.count >
			header.data_offset)) {
		printf("Descriptor block is overlapped with data block!\n");
		goto out_close_fd;
	}

	/* Allocate memory for stats descriptors */
	stats_desc = calloc(header.count, size_desc);
	if (!stats_desc) {
		perror("Allocate memory for VM stats descriptors");
		goto out_close_fd;
	}
	/* Read kvm vm stats descriptors */
	ret = pread(stats_fd, stats_desc,
			size_desc * header.count, header.desc_offset);
	if (ret != size_desc * header.count) {
		perror("Read KVM VM stats descriptors");
		goto out_free_desc;
	}
	/* Sanity check for fields in descriptors */
	for (i = 0; i < header.count; ++i) {
		pdesc = (void *)stats_desc + i * size_desc;
		/* Check type,unit,scale boundaries */
		if ((pdesc->flags & KVM_STATS_TYPE_MASK) > KVM_STATS_TYPE_MAX) {
			printf("Unknown KVM stats type!\n");
			goto out_free_desc;
		}
		if ((pdesc->flags & KVM_STATS_UNIT_MASK) > KVM_STATS_UNIT_MAX) {
			printf("Unknown KVM stats unit!\n");
			goto out_free_desc;
		}
		if ((pdesc->flags & KVM_STATS_SCALE_MASK) >
				KVM_STATS_SCALE_MAX) {
			printf("Unknown KVM stats scale!\n");
			goto out_free_desc;
		}
		/* Check exponent for stats unit
		 * Exponent for counter should be greater than or equal to 0
		 * Exponent for unit bytes should be greater than or equal to 0
		 * Exponent for unit seconds should be less than or equal to 0
		 * Exponent for unit clock cycles should be greater than or
		 * equal to 0
		 */
		switch (pdesc->flags & KVM_STATS_UNIT_MASK) {
		case KVM_STATS_UNIT_NONE:
		case KVM_STATS_UNIT_BYTES:
		case KVM_STATS_UNIT_CYCLES:
			if (pdesc->exponent < 0) {
				printf("Unsupported KVM stats unit!\n");
				goto out_free_desc;
			}
			break;
		case KVM_STATS_UNIT_SECONDS:
			if (pdesc->exponent > 0) {
				printf("Unsupported KVM stats unit!\n");
				goto out_free_desc;
			}
			break;
		}
		/* Check name string */
		if (strlen(pdesc->name) >= header.name_size) {
			printf("KVM stats name(%s) too long!\n", pdesc->name);
			goto out_free_desc;
		}
		/* Check size field, which should not be zero */
		if (pdesc->size == 0) {
			printf("KVM descriptor(%s) with size of 0!\n",
					pdesc->name);
			goto out_free_desc;
		}
		size_data = pdesc->size * sizeof(stats_data->value[0]);
	}
	/* Check overlap */
	if (header.data_offset < header.desc_offset &&
		header.data_offset + size_data > header.desc_offset) {
		printf("Data block is overlapped with Descriptor block!\n");
		goto out_free_desc;
	}

	/* Allocate memory for stats data */
	stats_data = malloc(size_data);
	if (!stats_data) {
		perror("Allocate memory for VM stats data");
		goto out_free_desc;
	}
	/* Read kvm vm stats data */
	ret = pread(stats_fd, stats_data, size_data, header.data_offset);
	if (ret != size_data) {
		perror("Read KVM VM stats data");
		goto out_free_data;
	}

	err = 0;
out_free_data:
	free(stats_data);
out_free_desc:
	free(stats_desc);
out_close_fd:
	close(stats_fd);
	return err;
}

int vcpu_stats_test(struct kvm_vm *vm, int vcpu_id)
{
	ssize_t ret;
	int i, stats_fd, err = -1;
	size_t size_desc, size_data = 0;
	struct kvm_stats_header header;
	struct kvm_stats_desc *stats_desc, *pdesc;
	struct kvm_vcpu_stats_data *stats_data;

	/* Get fd for VCPU stats */
	stats_fd = vcpu_get_statsfd(vm, vcpu_id);
	if (stats_fd < 0) {
		perror("Get VCPU stats fd");
		return err;
	}
	/* Read kvm vcpu stats header */
	ret = read(stats_fd, &header, sizeof(header));
	if (ret != sizeof(header)) {
		perror("Read VCPU stats header");
		goto out_close_fd;
	}
	size_desc = sizeof(*stats_desc) + header.name_size;
	/* Check id string in header, that should start with "kvm" */
	if (strncmp(header.id, "kvm", 3) ||
			strlen(header.id) >= KVM_STATS_ID_MAXLEN) {
		printf("Invalid KVM VCPU stats type!\n");
		goto out_close_fd;
	}
	/* Sanity check for other fields in header */
	if (header.count == 0) {
		err = 0;
		goto out_close_fd;
	}
	/* Check overlap */
	if (header.desc_offset == 0 || header.data_offset == 0 ||
			header.desc_offset < sizeof(header) ||
			header.data_offset < sizeof(header)) {
		printf("Invalid offset fields in header!\n");
		goto out_close_fd;
	}
	if (header.desc_offset < header.data_offset &&
			(header.desc_offset + size_desc * header.count >
			header.data_offset)) {
		printf("Descriptor block is overlapped with data block!\n");
		goto out_close_fd;
	}

	/* Allocate memory for stats descriptors */
	stats_desc = calloc(header.count, size_desc);
	if (!stats_desc) {
		perror("Allocate memory for VCPU stats descriptors");
		goto out_close_fd;
	}
	/* Read kvm vcpu stats descriptors */
	ret = pread(stats_fd, stats_desc,
			size_desc * header.count, header.desc_offset);
	if (ret != size_desc * header.count) {
		perror("Read KVM VCPU stats descriptors");
		goto out_free_desc;
	}
	/* Sanity check for fields in descriptors */
	for (i = 0; i < header.count; ++i) {
		pdesc = (void *)stats_desc + i * size_desc;
		/* Check boundaries */
		if ((pdesc->flags & KVM_STATS_TYPE_MASK) > KVM_STATS_TYPE_MAX) {
			printf("Unknown KVM stats type!\n");
			goto out_free_desc;
		}
		if ((pdesc->flags & KVM_STATS_UNIT_MASK) > KVM_STATS_UNIT_MAX) {
			printf("Unknown KVM stats unit!\n");
			goto out_free_desc;
		}
		if ((pdesc->flags & KVM_STATS_SCALE_MASK) >
				KVM_STATS_SCALE_MAX) {
			printf("Unknown KVM stats scale!\n");
			goto out_free_desc;
		}
		/* Check exponent for stats unit
		 * Exponent for counter should be greater than or equal to 0
		 * Exponent for unit bytes should be greater than or equal to 0
		 * Exponent for unit seconds should be less than or equal to 0
		 * Exponent for unit clock cycles should be greater than or
		 * equal to 0
		 */
		switch (pdesc->flags & KVM_STATS_UNIT_MASK) {
		case KVM_STATS_UNIT_NONE:
		case KVM_STATS_UNIT_BYTES:
		case KVM_STATS_UNIT_CYCLES:
			if (pdesc->exponent < 0) {
				printf("Unsupported KVM stats unit!\n");
				goto out_free_desc;
			}
			break;
		case KVM_STATS_UNIT_SECONDS:
			if (pdesc->exponent > 0) {
				printf("Unsupported KVM stats unit!\n");
				goto out_free_desc;
			}
			break;
		}
		/* Check name string */
		if (strlen(pdesc->name) >= header.name_size) {
			printf("KVM stats name(%s) too long!\n", pdesc->name);
			goto out_free_desc;
		}
		/* Check size field, which should not be zero */
		if (pdesc->size == 0) {
			printf("KVM descriptor(%s) with size of 0!\n",
					pdesc->name);
			goto out_free_desc;
		}
		size_data = pdesc->size * sizeof(stats_data->value[0]);
	}
	/* Check overlap */
	if (header.data_offset < header.desc_offset &&
		header.data_offset + size_data > header.desc_offset) {
		printf("Data block is overlapped with Descriptor block!\n");
		goto out_free_desc;
	}

	/* Allocate memory for stats data */
	stats_data = malloc(size_data);
	if (!stats_data) {
		perror("Allocate memory for VCPU stats data");
		goto out_free_desc;
	}
	/* Read kvm vcpu stats data */
	ret = pread(stats_fd, stats_data, size_data, header.data_offset);
	if (ret != size_data) {
		perror("Read KVM VCPU stats data");
		goto out_free_data;
	}

	err = 0;
out_free_data:
	free(stats_data);
out_free_desc:
	free(stats_desc);
out_close_fd:
	close(stats_fd);
	return err;
}

/*
 * Usage: kvm_bin_form_stats [#vm] [#vcpu]
 * The first parameter #vm set the number of VMs being created.
 * The second parameter #vcpu set the number of VCPUs being created.
 * By default, 1 VM and 1 VCPU for the VM would be created for testing.
 */

int main(int argc, char *argv[])
{
	int max_vm = 1, max_vcpu = 1, ret, i, j, err = -1;
	struct kvm_vm **vms;

	/* Get the number of VMs and VCPUs that would be created for testing. */
	if (argc > 1) {
		max_vm = strtol(argv[1], NULL, 0);
		if (max_vm <= 0)
			max_vm = 1;
	}
	if (argc > 2 ) {
		max_vcpu = strtol(argv[2], NULL, 0);
		if (max_vcpu <= 0)
			max_vcpu = 1;
	}

	/* Check the extension for binary stats */
	ret = kvm_check_cap(KVM_CAP_STATS_BINARY_FD);
	if (ret < 0) {
		printf("Binary form statistics interface is not supported!\n");
		return err;
	}

	/* Create VMs and VCPUs */
	vms = malloc(sizeof(vms[0]) * max_vm);
	if (!vms) {
		perror("Allocate memory for storing VM pointers");
		return err;
	}
	for (i = 0; i < max_vm; ++i) {
		vms[i] = vm_create(VM_MODE_DEFAULT,
				DEFAULT_GUEST_PHY_PAGES, O_RDWR);
		for (j = 0; j < max_vcpu; ++j) {
			vm_vcpu_add(vms[i], j);
		}
	}

	/* Check stats read for every VM and VCPU */
	for (i = 0; i < max_vm; ++i) {
		if (vm_stats_test(vms[i]))
			goto out_free_vm;
		for (j = 0; j < max_vcpu; ++j) {
			if (vcpu_stats_test(vms[i], j))
				goto out_free_vm;
		}
	}

	err = 0;
out_free_vm:
	for (i = 0; i < max_vm; ++i)
		kvm_vm_free(vms[i]);
	free(vms);
	return err;
}
