#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "wip"

/*
 * This is the self-generated part of the monitor. Generally, there is no need
 * to touch this section.
 */
#include "model.h"

/*
 * Declare the deterministic automata monitor.
 *
 * The rv monitor reference is needed for the monitor declaration.
 */
struct rv_monitor rv_wip;
DECLARE_DA_MON_PER_CPU(wip, char);

#define CREATE_TRACE_POINTS
#include "wip.h"

/*
 * This is the instrumentation part of the monitor.
 *
 * This is the section where manual work is required. Here the kernel events
 * are translated into model's event.
 *
 */

void handle_preempt_disable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_event_wip(preempt_disable);
}

void handle_preempt_enable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_init_event_wip(preempt_enable);
}

void handle_sched_waking(void *data, struct task_struct *task)
{
	da_handle_event_wip(sched_waking);
}

#define NR_TP	3
static struct tracepoint_hook_helper tracepoints_to_hook[NR_TP] = {
	{
		.probe = handle_preempt_disable,
		.name = "preempt_disable",
		.registered = 0
	},
	{
		.probe = handle_preempt_enable,
		.name = "preempt_enable",
		.registered = 0
	},
	{
		.probe = handle_sched_waking,
		.name = "sched_wakeup",
		.registered = 0
	},
};

static int start_wip(void)
{
	int retval;

	da_monitor_init_wip();

	retval = thh_hook_probes(tracepoints_to_hook, NR_TP);
	if (retval)
		goto out_err;

	return 0;

out_err:
	return -EINVAL;
}

static void stop_wip(void)
{
	rv_wip.enabled = 0;
	thh_unhook_probes(tracepoints_to_hook, NR_TP);
	return;
}

/*
 * This is the monitor register section.
 */
struct rv_monitor rv_wip = {
	.name = "wip",
	.description = "auto-generated wip",
	.start = start_wip,
	.stop = stop_wip,
	.reset = da_monitor_reset_all_wip,
	.enabled = 0,
};

int register_wip(void)
{
	rv_register_monitor(&rv_wip);
	return 0;
}

void unregister_wip(void)
{
	if (rv_wip.enabled)
		stop_wip();

	rv_unregister_monitor(&rv_wip);
}

module_init(register_wip);
module_exit(unregister_wip);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("dot2k: auto-generated");
MODULE_DESCRIPTION("wip");
