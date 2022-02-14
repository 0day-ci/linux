#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "wwnr"

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
struct rv_monitor rv_wwnr;
DECLARE_DA_MON_PER_TASK(wwnr, char);

#define CREATE_TRACE_POINTS
#include "wwnr.h"

/*
 * This is the instrumentation part of the monitor.
 *
 * This is the section where manual work is required. Here the kernel events
 * are translated into model's event.
 *
 */

void handle_switch_in(void *data, /* XXX: fill header */)
{
	pid_t pid = /* XXX how do I get the pid? */;
	da_handle_event_wwnr(pid, switch_in);
}

void handle_switch_out(void *data, /* XXX: fill header */)
{
	pid_t pid = /* XXX how do I get the pid? */;
	da_handle_event_wwnr(pid, switch_out);
}

void handle_wakeup(void *data, /* XXX: fill header */)
{
	pid_t pid = /* XXX how do I get the pid? */;
	da_handle_event_wwnr(pid, wakeup);
}

#define NR_TP   3
static struct tracepoint_hook_helper tracepoints_to_hook[NR_TP] = {
	{
		.probe = handle_switch_in,
		.name = /* XXX: tracepoint name here */,
		.registered = 0
	},
	{
		.probe = handle_switch_out,
		.name = /* XXX: tracepoint name here */,
		.registered = 0
	},
	{
		.probe = handle_wakeup,
		.name = /* XXX: tracepoint name here */,
		.registered = 0
	},
};

static int start_wwnr(void)
{
	int retval;

	da_monitor_init_wwnr();

	retval = thh_hook_probes(tracepoints_to_hook, NR_TP);
	if (retval)
		goto out_err;

	return 0;

out_err:
	return -EINVAL;
}

static void stop_wwnr(void)
{
	rv_wwnr.enabled = 0;
	thh_unhook_probes(tracepoints_to_hook, NR_TP);
	return;
}

/*
 * This is the monitor register section.
 */
struct rv_monitor rv_wwnr = {
	.name = "wwnr",
	.description = "auto-generated wwnr",
	.start = start_wwnr,
	.stop = stop_wwnr,
	.reset = da_monitor_reset_all_wwnr,
	.enabled = 0,
};

int register_wwnr(void)
{
	rv_register_monitor(&rv_wwnr);
	return 0;
}

void unregister_wwnr(void)
{
	if (rv_wwnr.enabled)
		stop_wwnr();

	rv_unregister_monitor(&rv_wwnr);
}

module_init(register_wwnr);
module_exit(unregister_wwnr);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("dot2k: auto-generated");
MODULE_DESCRIPTION("wwnr");
