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

struct mshv_create_partition {
	__u64 flags;
	struct hv_partition_creation_properties partition_creation_properties;
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

#define MSHV_IOCTL 0xB8

/* mshv device */
#define MSHV_CHECK_EXTENSION    _IOW(MSHV_IOCTL, 0x00, __u32)
#define MSHV_CREATE_PARTITION	_IOW(MSHV_IOCTL, 0x01, struct mshv_create_partition)

/* partition device */
#define MSHV_MAP_GUEST_MEMORY	_IOW(MSHV_IOCTL, 0x02, struct mshv_user_mem_region)
#define MSHV_UNMAP_GUEST_MEMORY	_IOW(MSHV_IOCTL, 0x03, struct mshv_user_mem_region)

#endif
