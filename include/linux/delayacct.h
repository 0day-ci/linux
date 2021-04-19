/* SPDX-License-Identifier: GPL-2.0-or-later */
/* delayacct.h - per-task delay accounting
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 * Copyright (C) Chunguang Xu, Tencent Corp. 2021
 */

#ifndef _LINUX_DELAYACCT_H
#define _LINUX_DELAYACCT_H

#include <uapi/linux/taskstats.h>
#include <linux/sched.h>
#include <linux/slab.h>

/*
 * Per-task flags relevant to delay accounting
 * maintained privately to avoid exhausting similar flags in sched.h:PF_*
 * Used to set current->delays->flags
 */
#define DELAYACCT_PF_SWAPIN	0x00000001	/* I am doing a swapin */

static inline int delayacct_is_task_waiting_on_io(struct task_struct *p)
{
	return p->in_iowait;
}

#ifdef CONFIG_TASK_DELAY_ACCT

enum delayacct_item {
	DELAYACCT_BLKIO,     /* block IO latency */
	DELAYACCT_SWAPIN,    /* swapin IO latency*/
	DELAYACCT_THRASHING, /* pagecache thrashing IO latency*/
	DELAYACCT_FREEPAGES, /* memory reclaim latency*/
	DELAYACCT_NR_ITEMS
};

struct delayacct_count {
	u64 start;  /* start timestamp of XXX operation */
	u32 count;  /* incremented on every XXX operation */
	u64 delay;  /* accumulated delay time in nanoseconds */
	u64 max;    /* maximum latency of XXX operation */
};

struct task_delay_info {
	raw_spinlock_t	lock;
	unsigned int	flags;	/* Private per-task flags */
	struct delayacct_count delays[DELAYACCT_NR_ITEMS];
};

extern int delayacct_on;	/* Delay accounting turned on/off */
extern struct kmem_cache *delayacct_cache;
extern void delayacct_init(void);
extern void __delayacct_tsk_init(struct task_struct *);
extern int  __delayacct_add_tsk(struct taskstats *d, struct task_struct *tsk);
extern u64  __delayacct_blkio_ticks(struct task_struct *tsk);
extern void __delayacct_end(struct task_delay_info *delays, int item);

extern int  proc_delayacct_show(struct seq_file *m, struct pid_namespace *ns,
			struct pid *pid, struct task_struct *task);

static inline void __delayacct_start(struct task_delay_info *delays, int item)
{
	delays->delays[item].start = ktime_get_ns();
}

static inline void delayacct_set_flag(int flag)
{
	if (current->delays)
		current->delays->flags |= flag;
}

static inline void delayacct_clear_flag(int flag)
{
	if (current->delays)
		current->delays->flags &= ~flag;
}

static inline void delayacct_tsk_init(struct task_struct *tsk)
{
	/* reinitialize in case parent's non-null pointer was dup'ed*/
	tsk->delays = NULL;
	if (delayacct_on)
		__delayacct_tsk_init(tsk);
}

/* Free tsk->delays. Called from bad fork and __put_task_struct
 * where there's no risk of tsk->delays being accessed elsewhere
 */
static inline void delayacct_tsk_free(struct task_struct *tsk)
{
	if (tsk->delays)
		kmem_cache_free(delayacct_cache, tsk->delays);
	tsk->delays = NULL;
}

static inline int delayacct_add_tsk(struct taskstats *d, struct task_struct *tsk)
{
	if (!delayacct_on || !tsk->delays)
		return 0;
	return __delayacct_add_tsk(d, tsk);
}

static inline __u64 delayacct_blkio_ticks(struct task_struct *tsk)
{
	if (tsk->delays)
		return __delayacct_blkio_ticks(tsk);
	return 0;
}

static inline void delayacct_blkio_start(void)
{
	if (current->delays) {
		if (current->delays->flags & DELAYACCT_PF_SWAPIN)
			__delayacct_start(current->delays, DELAYACCT_SWAPIN);
		else
			__delayacct_start(current->delays, DELAYACCT_BLKIO);
	}
}

static inline void delayacct_blkio_end(struct task_struct *p)
{
	if (p->delays) {
		if (p->delays->flags & DELAYACCT_PF_SWAPIN)
			__delayacct_end(p->delays, DELAYACCT_SWAPIN);
		else
			__delayacct_end(p->delays, DELAYACCT_BLKIO);
	}
}

static inline void delayacct_freepages_start(void)
{
	if (current->delays)
		__delayacct_start(current->delays, DELAYACCT_FREEPAGES);
}

static inline void delayacct_freepages_end(void)
{
	if (current->delays)
		__delayacct_end(current->delays, DELAYACCT_FREEPAGES);
}

static inline void delayacct_thrashing_start(void)
{
	if (current->delays)
		__delayacct_start(current->delays, DELAYACCT_THRASHING);
}

static inline void delayacct_thrashing_end(void)
{
	if (current->delays)
		__delayacct_end(current->delays, DELAYACCT_THRASHING);
}

#else

static inline void delayacct_set_flag(int flag)
{}
static inline void delayacct_clear_flag(int flag)
{}
static inline void delayacct_init(void)
{}
static inline void delayacct_tsk_init(struct task_struct *tsk)
{}
static inline void delayacct_tsk_free(struct task_struct *tsk)
{}
static inline int  delayacct_add_tsk(struct taskstats *d, struct task_struct *tsk)
{ return 0; }
static inline u64  delayacct_blkio_ticks(struct task_struct *tsk)
{ return 0; }
static inline void delayacct_blkio_start(void)
{}
static inline void delayacct_blkio_end(struct task_struct *p)
{}
static inline void delayacct_freepages_start(void)
{}
static inline void delayacct_freepages_end(void)
{}
static inline void delayacct_thrashing_start(void)
{}
static inline void delayacct_thrashing_end(void)
{}

#endif /* CONFIG_TASK_DELAY_ACCT */

#endif
