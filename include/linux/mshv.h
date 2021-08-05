/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_MSHV_H
#define _LINUX_MSHV_H

/*
 * Microsoft Hypervisor root partition driver for /dev/mshv
 */

#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <uapi/linux/mshv.h>

#define MSHV_MAX_PARTITIONS		128
#define MSHV_MAX_MEM_REGIONS		64
#define MSHV_MAX_VPS			256

struct mshv_vp {
	u32 index;
	struct mshv_partition *partition;
	struct mutex mutex;
	struct page *register_page;
	struct {
		struct semaphore sem;
		struct task_struct *task;
		struct hv_message *intercept_message;
	} run;
};

struct mshv_mem_region {
	u64 size; /* bytes */
	u64 guest_pfn;
	u64 userspace_addr; /* start of the userspace allocated memory */
	struct page **pages;
};

struct mshv_partition {
	u64 id;
	refcount_t ref_count;
	struct mutex mutex;
	struct {
		u32 count;
		struct mshv_mem_region slots[MSHV_MAX_MEM_REGIONS];
	} regions;
	struct {
		u32 count;
		struct mshv_vp *array[MSHV_MAX_VPS];
	} vps;
};

struct mshv {
	struct hv_message_page __percpu **synic_message_page;
	struct {
		spinlock_t lock;
		u64 count;
		struct mshv_partition *array[MSHV_MAX_PARTITIONS];
	} partitions;
};

#endif
