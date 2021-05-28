/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_MSHV_H
#define _LINUX_MSHV_H

/*
 * Microsoft Hypervisor root partition driver for /dev/mshv
 */

#include <linux/spinlock.h>
#include <uapi/linux/mshv.h>

#define MSHV_MAX_PARTITIONS		128

struct mshv_partition {
	u64 id;
	refcount_t ref_count;
};

struct mshv {
	struct {
		spinlock_t lock;
		u64 count;
		struct mshv_partition *array[MSHV_MAX_PARTITIONS];
	} partitions;
};

#endif
