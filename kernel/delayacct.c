// SPDX-License-Identifier: GPL-2.0-or-later
/* delayacct.c - per-task delay accounting
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 * Copyright (C) Chunguang Xu, Tencent Corp. 2021
 */

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/cputime.h>
#include <linux/slab.h>
#include <linux/taskstats.h>
#include <linux/time.h>
#include <linux/sysctl.h>
#include <linux/delayacct.h>
#include <linux/module.h>

int delayacct_on __read_mostly = 1;	/* Delay accounting turned on/off */
EXPORT_SYMBOL_GPL(delayacct_on);
struct kmem_cache *delayacct_cache;

static int __init delayacct_setup_disable(char *str)
{
	delayacct_on = 0;
	return 1;
}
__setup("nodelayacct", delayacct_setup_disable);

void delayacct_init(void)
{
	delayacct_cache = KMEM_CACHE(task_delay_info, SLAB_PANIC|SLAB_ACCOUNT);
	delayacct_tsk_init(&init_task);
}

void __delayacct_tsk_init(struct task_struct *tsk)
{
	tsk->delays = kmem_cache_zalloc(delayacct_cache, GFP_KERNEL);
	if (tsk->delays)
		raw_spin_lock_init(&tsk->delays->lock);
}

/*
 * Finish delay accounting for a statistic using its timestamps (@start),
 * accumalator (@total) and @count
 */
void __delayacct_end(struct task_delay_info *delays, int item)
{
	struct delayacct_count *delay = &delays->delays[item];
	u64 ns = ktime_get_ns() - delay->start;
	unsigned long flags;

	if (ns > 0) {
		raw_spin_lock_irqsave(&delays->lock, flags);
		delay->max = max(delay->max, ns);
		delay->delay += ns;
		delay->count++;
		raw_spin_unlock_irqrestore(&delays->lock, flags);
	}
}

int __delayacct_add_tsk(struct taskstats *d, struct task_struct *tsk)
{
	struct delayacct_count *delays = tsk->delays->delays;
	u64 utime, stime, stimescaled, utimescaled;
	unsigned long long t2, t3;
	unsigned long flags, t1;
	s64 tmp;

	task_cputime(tsk, &utime, &stime);
	tmp = (s64)d->cpu_run_real_total;
	tmp += utime + stime;
	d->cpu_run_real_total = (tmp < (s64)d->cpu_run_real_total) ? 0 : tmp;

	task_cputime_scaled(tsk, &utimescaled, &stimescaled);
	tmp = (s64)d->cpu_scaled_run_real_total;
	tmp += utimescaled + stimescaled;
	d->cpu_scaled_run_real_total =
		(tmp < (s64)d->cpu_scaled_run_real_total) ? 0 : tmp;

	/*
	 * No locking available for sched_info (and too expensive to add one)
	 * Mitigate by taking snapshot of values
	 */
	t1 = tsk->sched_info.pcount;
	t2 = tsk->sched_info.run_delay;
	t3 = tsk->se.sum_exec_runtime;

	d->cpu_count += t1;

	tmp = (s64)d->cpu_delay_total + t2;
	d->cpu_delay_total = (tmp < (s64)d->cpu_delay_total) ? 0 : tmp;

	tmp = (s64)d->cpu_run_virtual_total + t3;
	d->cpu_run_virtual_total =
		(tmp < (s64)d->cpu_run_virtual_total) ?	0 : tmp;

	/* zero XXX_total, non-zero XXX_count implies XXX stat overflowed */

	raw_spin_lock_irqsave(&tsk->delays->lock, flags);
	tmp = d->blkio_delay_total + delays[DELAYACCT_BLKIO].delay;
	d->blkio_delay_total = (tmp < d->blkio_delay_total) ? 0 : tmp;
	tmp = d->swapin_delay_total + delays[DELAYACCT_SWAPIN].delay;
	d->swapin_delay_total = (tmp < d->swapin_delay_total) ? 0 : tmp;
	tmp = d->freepages_delay_total + delays[DELAYACCT_FREEPAGES].delay;
	d->freepages_delay_total = (tmp < d->freepages_delay_total) ? 0 : tmp;
	tmp = d->thrashing_delay_total + delays[DELAYACCT_THRASHING].delay;
	d->thrashing_delay_total = (tmp < d->thrashing_delay_total) ? 0 : tmp;
	d->blkio_count += delays[DELAYACCT_BLKIO].count;
	d->swapin_count += delays[DELAYACCT_SWAPIN].count;
	d->freepages_count += delays[DELAYACCT_FREEPAGES].count;
	d->thrashing_count += delays[DELAYACCT_THRASHING].count;
	raw_spin_unlock_irqrestore(&tsk->delays->lock, flags);

	return 0;
}

u64 __delayacct_blkio_ticks(struct task_struct *tsk)
{
	u64 ret;
	unsigned long flags;

	raw_spin_lock_irqsave(&tsk->delays->lock, flags);
	ret = nsec_to_clock_t(tsk->delays->delays[DELAYACCT_BLKIO].delay +
			      tsk->delays->delays[DELAYACCT_SWAPIN].delay);
	raw_spin_unlock_irqrestore(&tsk->delays->lock, flags);
	return ret;
}

