#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/da_monitor.h>

#include <linux/watchdog.h>
#include <linux/moduleparam.h>

#define MODULE_NAME "safe_wtd_nwo"

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
struct rv_monitor rv_safe_wtd_nwo;
DECLARE_DA_MON_GLOBAL(safe_wtd_nwo, char);

#define CREATE_TRACE_POINTS
#include "safe_wtd_nwo.h"

/*
 * custom: safe_timeout is the maximum value a watchdog monitor
 * can set. This value is registered here to duplicate the information.
 * In this way, a miss-behaving monitor can be detected.
 */
static int safe_timeout = ~0;
module_param(safe_timeout, int, 0444);

/*
 * custom: if check_timeout is set, the monitor will check if the time left
 * in the watchdog is less than or equals to the last safe timeout set by
 * user-space. This check is done after each ping. In this way, if any
 * code by-passed the watchdog dev interface setting a higher (so unsafe)
 * timeout, this monitor will catch the side effect and react.
 */
static int last_timeout_set = 0;
static int check_timeout = 0;
module_param(check_timeout, int, 0444);

/*
 * custom: if dont_stop is set the monitor will react if stopped.
 */
static int dont_stop = 0;
module_param(dont_stop, int, 0444);

/*
 * custom: there are some states that are kept after the watchdog is closed.
 * For example, the nowayout state.
 *
 * Thus, the RV monitor needs to keep track of these states after a start/stop
 * of the RV monitor itself, and should not reset after each restart - keeping the
 * know state until the system shutdown.
 *
 * If for an unknown reason an RV monitor would like to reset the RV monitor at each
 * RV monitor start, set it to one.
 */
static int reset_on_restart = 0;
module_param(reset_on_restart, int, 0444);

/*
 * open_pid takes note of the first thread that opened the watchdog.
 *
 * Any other thread that generates an event will cause an "other_threads"
 * event in the monitor.
 */
static int open_pid = 0;

/*
 * watchdog_id: the watchdog to monitor
 */
static int watchdog_id = 0;
module_param(watchdog_id, int, 0444);

static void handle_nowayout(void *data, struct watchdog_device *wdd)
{
	if (wdd->id != watchdog_id)
		return;

	da_handle_init_run_event_safe_wtd_nwo(nowayout);
}

static void handle_close(void *data, struct watchdog_device *wdd)
{
	if (wdd->id != watchdog_id)
		return;

	if (open_pid && current->pid != open_pid) {
		da_handle_init_run_event_safe_wtd_nwo(other_threads);
	} else {
		da_handle_event_safe_wtd_nwo(close);
		open_pid = 0;
	}
}

static void handle_open(void *data, struct watchdog_device *wdd)
{
	if (wdd->id != watchdog_id)
		return;

	if (open_pid && current->pid != open_pid) {
		da_handle_init_run_event_safe_wtd_nwo(other_threads);
	} else {
		da_handle_init_run_event_safe_wtd_nwo(open);
		open_pid = current->pid;
	}
}

static void blocked_events(void *data, struct watchdog_device *wdd)
{
	if (wdd->id != watchdog_id)
		return;

	if (open_pid && current->pid != open_pid) {
		da_handle_init_run_event_safe_wtd_nwo(other_threads);
		return;
	}
	da_handle_event_safe_wtd_nwo(other_threads);
}

static void handle_ping(void *data, struct watchdog_device *wdd)
{
	char msg[128];
	unsigned int timeout;

	if (wdd->id != watchdog_id)
		return;

	if (open_pid && current->pid != open_pid) {
		da_handle_init_run_event_safe_wtd_nwo(other_threads);
		return;
	}

	da_handle_event_safe_wtd_nwo(ping);

	if (!check_timeout)
		return;

	if (wdd->ops->get_timeleft) {
		timeout = wdd->ops->get_timeleft(wdd);
		if (timeout > last_timeout_set) {
			snprintf(msg, 128,
				 "watchdog timout is %u > than previously set (%d)\n",
				 timeout, last_timeout_set);
			cond_react(msg);
		}
	} else {
		snprintf(msg, 128, "error getting timeout: option not supported\n");
		cond_react(msg);
	}
}

static void handle_set_safe_timeout(void *data, struct watchdog_device *wdd, unsigned long timeout)
{
	char msg[128];

	if (wdd->id != watchdog_id)
		return;

	if (open_pid && current->pid != open_pid) {
		da_handle_init_run_event_safe_wtd_nwo(other_threads);
		return;
	}

	da_handle_event_safe_wtd_nwo(set_safe_timeout);

	if (timeout > safe_timeout) {
		snprintf(msg, 128, "set safety timeout is too high: %d", (int) timeout);
		cond_react(msg);
	}

	if (check_timeout)
		last_timeout_set = timeout;
}

static void handle_start(void *data, struct watchdog_device *wdd)
{
	if (wdd->id != watchdog_id)
		return;

	if (open_pid && current->pid != open_pid) {
		da_handle_init_run_event_safe_wtd_nwo(other_threads);
		return;
	}

	da_handle_event_safe_wtd_nwo(start);
}

#define NR_TP	9
static struct tracepoint_hook_helper tracepoints_to_hook[NR_TP] = {
	{
		.probe = handle_close,
		.name = "watchdog_close",
		.registered = 0
	},
	{
		.probe = handle_nowayout,
		.name = "watchdog_nowayout",
		.registered = 0
	},
	{
		.probe = handle_open,
		.name = "watchdog_open",
		.registered = 0
	},
	{
		.probe = handle_ping,
		.name = "watchdog_ping",
		.registered = 0
	},
	{
		.probe = handle_set_safe_timeout,
		.name = "watchdog_set_timeout",
		.registered = 0
	},
	{
		.probe = handle_start,
		.name = "watchdog_start",
		.registered = 0
	},
	{
		.probe = blocked_events,
		.name = "watchdog_stop",
		.registered = 0
	},
	{
		.probe = blocked_events,
		.name = "watchdog_set_keep_alive",
		.registered = 0
	},
	{
		.probe = blocked_events,
		.name = "watchdog_keep_alive",
		.registered = 0
	},
};

static int mon_started = 0;

static int start_safe_wtd_nwo(void)
{
	int retval;

	if (!mon_started || reset_on_restart) {
		da_monitor_init_safe_wtd_nwo();
		mon_started = 1;
	}

	retval = thh_hook_probes(tracepoints_to_hook, NR_TP);
	if (retval)
		goto out_err;

	return 0;

out_err:
	return -EINVAL;
}

static void stop_safe_wtd_nwo(void)
{
	if (dont_stop)
		cond_react("dont_stop safe_wtd_nwo is set.");

	rv_safe_wtd_nwo.enabled = 0;
	thh_unhook_probes(tracepoints_to_hook, NR_TP);
	return;
}

/*
 * This is the monitor register section.
 */
struct rv_monitor rv_safe_wtd_nwo = {
	.name = "safe_wtd_nwo",
	.description = "A watchdog monitor guarding a safety monitor actions, nowayout required.",
	.start = start_safe_wtd_nwo,
	.stop = stop_safe_wtd_nwo,
	.reset = da_monitor_reset_all_safe_wtd_nwo,
	.enabled = 0,
};

int register_safe_wtd_nwo(void)
{
	rv_register_monitor(&rv_safe_wtd_nwo);
	return 0;
}

void unregister_safe_wtd_nwo(void)
{
	if (rv_safe_wtd_nwo.enabled)
		stop_safe_wtd_nwo();

	rv_unregister_monitor(&rv_safe_wtd_nwo);
}

module_init(register_safe_wtd_nwo);
module_exit(unregister_safe_wtd_nwo);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Daniel Bristot de Oliveira <bristot@kernel.org>");
MODULE_DESCRIPTION("Safe watchdog RV monitor nowayout");
