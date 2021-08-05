/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_MSHV_H
#define _UAPI_LINUX_MSHV_H

/*
 * Userspace interface for /dev/mshv
 * Microsoft Hypervisor root partition APIs
 * NOTE: This API is not yet stable!
 */

#include <linux/types.h>

#define MSHV_CAP_CORE_API_STABLE    0x0

#define MSHV_IOCTL 0xB8

/* mshv device */
#define MSHV_CHECK_EXTENSION    _IOW(MSHV_IOCTL, 0x00, __u32)

#endif
