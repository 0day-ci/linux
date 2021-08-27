/* SPDX-License-Identifier: GPL-2.0 */
#ifndef	__TASK_SHARED_H__
#define	__TASK_SHARED_H__

#include <linux/mm_types.h>

/*
 * Track user-kernel shared pages referred by mm_struct
 */
struct ushared_pages {
	struct list_head plist;
	struct list_head frlist;
	unsigned long pcount;
};

/*
 * Following is the per task struct shared with kernel for
 * fast communication.
 */
struct task_ushared {
	long version;
};

/*
 * Following is used for cacheline aligned allocations in a page.
 */
union  task_shared {
	struct task_ushared tu;
	char    s[128];
};

/*
 * Struct to track per page slots
 */
struct ushared_pg {
	struct list_head list;
	struct list_head fr_list;
	struct page *pages[2];
	u64 bitmap; /* free slots */
	int slot_count;
	unsigned long kaddr;
	unsigned long vaddr; /* user address */
	struct vm_special_mapping ushrd_mapping;
};

/*
 * Following struct is referred by tast_struct
 */
struct task_ushrd_struct {
	struct task_ushared *kaddr; /* kernel address */
	struct task_ushared *uaddr; /* user address */
	struct ushared_pg *upg;
};

extern void task_ushared_free(struct task_struct *t);
extern void mm_ushared_clear(struct mm_struct *mm);
#endif /* __TASK_SHARED_H__ */
