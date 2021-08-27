/* SPDX-License-Identifier: GPL-2.0 */
#ifndef	__TASK_SHARED_H__
#define	__TASK_SHARED_H__

#include <linux/mm_types.h>
#include <uapi/linux/task_shared.h>

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
	struct task_schedstat ts;
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


#ifdef CONFIG_SCHED_INFO

#define task_update_exec_runtime(t)					\
	do {								\
		struct task_ushrd_struct *shrdp = t->task_ushrd;	\
		if (shrdp != NULL && shrdp->kaddr != NULL)		\
			shrdp->kaddr->ts.sum_exec_runtime =		\
				 t->se.sum_exec_runtime;		\
	} while (0)

#define task_update_runq_stat(t, p)					\
	do {								\
		struct task_ushrd_struct *shrdp = t->task_ushrd;	\
		if (shrdp != NULL && shrdp->kaddr != NULL) {		\
			shrdp->kaddr->ts.run_delay =			\
				 t->sched_info.run_delay;		\
			if (p) {					\
				shrdp->kaddr->ts.pcount =		\
					 t->sched_info.pcount;		\
			}						\
		}							\
	} while (0)
#else

#define task_update_exec_runtime(t)	do { } while (0)
#define task_update_runq_stat(t, p)	do { } while (0)

#endif



extern void task_ushared_free(struct task_struct *t);
extern void mm_ushared_clear(struct mm_struct *mm);
#endif /* __TASK_SHARED_H__ */
