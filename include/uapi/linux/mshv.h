/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_MSHV_H
#define _UAPI_LINUX_MSHV_H

/*
 * Userspace interface for /dev/mshv
 * Microsoft Hypervisor root partition APIs
 * NOTE: This API is not yet stable!
 */

#include <linux/types.h>
#include <asm/hyperv-tlfs.h>
#include <asm-generic/hyperv-tlfs.h>

#define MSHV_CAP_CORE_API_STABLE    0x0

#define MSHV_VP_MMAP_REGISTERS_OFFSET (HV_VP_STATE_PAGE_REGISTERS * 0x1000)

struct mshv_create_partition {
	__u64 flags;
	struct hv_partition_creation_properties partition_creation_properties;
	union hv_partition_synthetic_processor_features synthetic_processor_features;
};

/*
 * Mappings can't overlap in GPA space or userspace
 * To unmap, these fields must match an existing mapping
 */
struct mshv_user_mem_region {
	__u64 size;		/* bytes */
	__u64 guest_pfn;
	__u64 userspace_addr;	/* start of the userspace allocated memory */
	__u32 flags;		/* ignored on unmap */
};

struct mshv_create_vp {
	__u32 vp_index;
};

#define MSHV_VP_MAX_REGISTERS	128

struct mshv_vp_registers {
	int count; /* at most MSHV_VP_MAX_REGISTERS */
	struct hv_register_assoc *regs;
};

struct mshv_install_intercept {
	__u32 access_type_mask;
	enum hv_intercept_type intercept_type;
	union hv_intercept_parameters intercept_parameter;
};

struct mshv_assert_interrupt {
	union hv_interrupt_control control;
	__u64 dest_addr;
	__u32 vector;
};

struct mshv_vp_state {
	enum hv_get_set_vp_state_type type;
	struct hv_vp_state_data_xsave xsave; /* only for xsave request */

	__u64 buf_size; /* If xsave, must be page-aligned */
	union {
		struct hv_local_interrupt_controller_state *lapic;
		__u8 *bytes; /* Xsave data. must be page-aligned */
	} buf;
};

struct mshv_partition_property {
	enum hv_partition_property_code property_code;
	__u64 property_value;
};

#define MSHV_IOCTL 0xB8

/* mshv device */
#define MSHV_CHECK_EXTENSION    _IOW(MSHV_IOCTL, 0x00, __u32)
#define MSHV_CREATE_PARTITION	_IOW(MSHV_IOCTL, 0x01, struct mshv_create_partition)

/* partition device */
#define MSHV_MAP_GUEST_MEMORY	_IOW(MSHV_IOCTL, 0x02, struct mshv_user_mem_region)
#define MSHV_UNMAP_GUEST_MEMORY	_IOW(MSHV_IOCTL, 0x03, struct mshv_user_mem_region)
#define MSHV_CREATE_VP		_IOW(MSHV_IOCTL, 0x04, struct mshv_create_vp)
#define MSHV_INSTALL_INTERCEPT	_IOW(MSHV_IOCTL, 0x08, struct mshv_install_intercept)
#define MSHV_ASSERT_INTERRUPT	_IOW(MSHV_IOCTL, 0x09, struct mshv_assert_interrupt)
#define MSHV_SET_PARTITION_PROPERTY \
				_IOW(MSHV_IOCTL, 0xC, struct mshv_partition_property)
#define MSHV_GET_PARTITION_PROPERTY \
				_IOWR(MSHV_IOCTL, 0xD, struct mshv_partition_property)

/* vp device */
#define MSHV_GET_VP_REGISTERS   _IOWR(MSHV_IOCTL, 0x05, struct mshv_vp_registers)
#define MSHV_SET_VP_REGISTERS   _IOW(MSHV_IOCTL, 0x06, struct mshv_vp_registers)
#define MSHV_RUN_VP		_IOR(MSHV_IOCTL, 0x07, struct hv_message)
#define MSHV_GET_VP_STATE	_IOWR(MSHV_IOCTL, 0x0A, struct mshv_vp_state)
#define MSHV_SET_VP_STATE	_IOWR(MSHV_IOCTL, 0x0B, struct mshv_vp_state)

/* register page mapping example:
 * struct hv_vp_register_page *regs = mmap(NULL,
 *					   4096,
 *					   PROT_READ | PROT_WRITE,
 *					   MAP_SHARED,
 *					   vp_fd,
 *					   HV_VP_MMAP_REGISTERS_OFFSET);
 * munmap(regs, 4096);
 */

#endif
