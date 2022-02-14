#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "MODEL_NAME"

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
struct rv_monitor rv_MODEL_NAME;
DECLARE_DA_MON_PER_TASK(MODEL_NAME, MIN_TYPE);

#define CREATE_TRACE_POINTS
#include "MODEL_NAME.h"

/*
 * This is the instrumentation part of the monitor.
 *
 * This is the section where manual work is required. Here the kernel events
 * are translated into model's event.
 *
 */

TRACEPOINT_HANDLERS_SKEL
#define NR_TP   NR_EVENTS
static struct tracepoint_hook_helper tracepoints_to_hook[NR_TP] = {
TRACEPOINT_HOOK_HELPERS
};

static int start_MODEL_NAME(void)
{
	int retval;

	da_monitor_init_MODEL_NAME();

	retval = thh_hook_probes(tracepoints_to_hook, NR_TP);
	if (retval)
		goto out_err;

	return 0;

out_err:
	return -EINVAL;
}

static void stop_MODEL_NAME(void)
{
	rv_MODEL_NAME.enabled = 0;
	thh_unhook_probes(tracepoints_to_hook, NR_TP);
	return;
}

/*
 * This is the monitor register section.
 */
struct rv_monitor rv_MODEL_NAME = {
	.name = "MODEL_NAME",
	.description = "auto-generated MODEL_NAME",
	.start = start_MODEL_NAME,
	.stop = stop_MODEL_NAME,
	.reset = da_monitor_reset_all_MODEL_NAME,
	.enabled = 0,
};

int register_MODEL_NAME(void)
{
	rv_register_monitor(&rv_MODEL_NAME);
	return 0;
}

void unregister_MODEL_NAME(void)
{
	if (rv_MODEL_NAME.enabled)
		stop_MODEL_NAME();

	rv_unregister_monitor(&rv_MODEL_NAME);
}

module_init(register_MODEL_NAME);
module_exit(unregister_MODEL_NAME);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("dot2k: auto-generated");
MODULE_DESCRIPTION("MODEL_NAME");
