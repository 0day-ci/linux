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

#define MSHV_IOCTL 0xB8

/* mshv device */
#define MSHV_CHECK_EXTENSION    _IOW(MSHV_IOCTL, 0x00, __u32)
#define MSHV_CREATE_PARTITION	_IOW(MSHV_IOCTL, 0x01, struct mshv_create_partition)

#endif
