// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * poll_source.c - cpuidle busy waiting API
 */

#include <linux/lockdep.h>
#include <linux/percpu.h>
#include <linux/poll_source.h>

/* The per-cpu list of registered poll sources */
DEFINE_PER_CPU(struct list_head, poll_source_list);

/* Called from idle task with TIF_POLLING_NRFLAG set and irqs enabled */
void poll_source_start(void)
{
	struct poll_source *src;

	list_for_each_entry(src, this_cpu_ptr(&poll_source_list), node)
		src->ops->start(src);
}

/* Called from idle task with TIF_POLLING_NRFLAG set and irqs enabled */
void poll_source_run_once(void)
{
	struct poll_source *src;

	list_for_each_entry(src, this_cpu_ptr(&poll_source_list), node)
		src->ops->poll(src);
}

/* Called from idle task with TIF_POLLING_NRFLAG set and irqs enabled */
void poll_source_stop(void)
{
	struct poll_source *src;

	list_for_each_entry(src, this_cpu_ptr(&poll_source_list), node)
		src->ops->stop(src);
}

static void poll_source_register_this_cpu(void *opaque)
{
	struct poll_source *src = opaque;

	lockdep_assert_irqs_disabled();

	list_add_tail(&src->node, this_cpu_ptr(&poll_source_list));
}

int poll_source_register(struct poll_source *src)
{
	if (!list_empty(&src->node))
		return -EBUSY;

	/*
	 * There is no race with src->cpu iterating over poll_source_list
	 * because smp_call_function_single() just sets TIF_NEED_RESCHED
	 * instead of sending an IPI during idle.
	 */
	/* TODO but what happens if the flag isn't set yet when smp_call_function_single() is invoked? */
	return smp_call_function_single(src->cpu,
					poll_source_register_this_cpu,
					src,
					1);
}
EXPORT_SYMBOL_GPL(poll_source_register);

static void poll_source_unregister_this_cpu(void *opaque)
{
	struct poll_source *src = opaque;

	lockdep_assert_irqs_disabled();

	/*
	 * See comment in poll_source_register() about why this does not race
	 * with the idle CPU iterating over poll_source_list.
	 */
	list_del_init(&src->node);
}

int poll_source_unregister(struct poll_source *src)
{
	return smp_call_function_single(src->cpu,
					poll_source_unregister_this_cpu,
					src,
					1);
}
EXPORT_SYMBOL_GPL(poll_source_unregister);

/* TODO what happens when a CPU goes offline? */
static int __init poll_source_init(void)
{
	int i;

	for_each_possible_cpu(i)
		INIT_LIST_HEAD(&per_cpu(poll_source_list, i));

	return 0;
}
core_initcall(poll_source_init);
