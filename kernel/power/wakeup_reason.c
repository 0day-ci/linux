/*
 * driver/base/power/wakeup_reason.c
 *
 * Logs the reasons which caused the kernel to resume from
 * the suspend mode.
 *
 * Copyright (C) 2021 Linaro, Inc.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/wakeup_reason.h>

static DEFINE_SPINLOCK(wakeup_reason_lock);

static bool capture_reasons;
static char wakeup_reason_str[MAX_WAKEUP_REASON_STR_LEN];

ssize_t log_ws_wakeup_reason(void)
{
	struct wakeup_source *ws, *last_active_ws = NULL;
	int idx, max, len = 0;
	bool active = false;
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	if (!capture_reasons) {
		goto out;
	}

	idx = wakeup_sources_read_lock();
	max = MAX_WAKEUP_REASON_STR_LEN;
	for_each_wakeup_source(ws) {
		if (ws->active && len < max) {
			if (!active)
				len += scnprintf(wakeup_reason_str, max,
						"Pending Wakeup Sources: ");
			len += scnprintf(wakeup_reason_str + len, max - len,
				"%s ", ws->name);
			active = true;
		} else if (!active &&
			   (!last_active_ws ||
			    ktime_to_ns(ws->last_time) >
			    ktime_to_ns(last_active_ws->last_time))) {
			last_active_ws = ws;
		}
	}
	if (!active && last_active_ws) {
		len = scnprintf(wakeup_reason_str, max,
				"Last active Wakeup Source: %s",
				last_active_ws->name);
	}
	len += scnprintf(wakeup_reason_str + len, max - len, "\n");
	wakeup_sources_read_unlock(idx);

out:
	spin_unlock_irqrestore(&wakeup_reason_lock, flags);

	return len;
}
EXPORT_SYMBOL(log_ws_wakeup_reason);

ssize_t log_irq_wakeup_reason(unsigned int irq_number)
{
	int len = 0;
	struct irq_desc *desc;
	const char *name = "null";
	unsigned long flags;

	desc = irq_to_desc(irq_number);
	if (desc == NULL)
		name = "stray irq";
	else if (desc->action && desc->action->name)
		name = desc->action->name;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	len = strnlen(wakeup_reason_str, MAX_WAKEUP_REASON_STR_LEN);
	len += scnprintf(wakeup_reason_str + len,
			MAX_WAKEUP_REASON_STR_LEN - len,
			"%d %s\n", irq_number, name);

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);

	return len;
}
EXPORT_SYMBOL(log_irq_wakeup_reason);

void clear_wakeup_reason(void)
{
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	memset(wakeup_reason_str, 0, sizeof(wakeup_reason_str));

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}
EXPORT_SYMBOL(clear_wakeup_reason);

ssize_t last_wakeup_reason_get(char *buf, ssize_t max)
{
	ssize_t len, size = 0;
	unsigned long flags;

	if (!buf) {
		return 0;
	}

	spin_lock_irqsave(&wakeup_reason_lock, flags);

	len = strnlen(wakeup_reason_str, MAX_WAKEUP_REASON_STR_LEN);
	if (len > 0) {
		size = scnprintf(buf, max, "%s", wakeup_reason_str);
	} else {
		size = -ENODATA;
	}

	spin_unlock_irqrestore(&wakeup_reason_lock, flags);

	return size;
}
EXPORT_SYMBOL(last_wakeup_reason_get);

static int wakeup_reason_pm_event(struct notifier_block *notifier,
		unsigned long pm_event, void *unused)
{
	unsigned long flags;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		spin_lock_irqsave(&wakeup_reason_lock, flags);
		capture_reasons = true;
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);

		clear_wakeup_reason();
		break;
	case PM_POST_SUSPEND:
		spin_lock_irqsave(&wakeup_reason_lock, flags);
		capture_reasons = false;
		if (!strnlen(wakeup_reason_str, MAX_WAKEUP_REASON_STR_LEN)) {
			scnprintf(wakeup_reason_str, MAX_WAKEUP_REASON_STR_LEN,
				"unknown wakeup reason, please check the kernel log\n");
		}
		spin_unlock_irqrestore(&wakeup_reason_lock, flags);

		pr_debug("Resume caused by %s\n", wakeup_reason_str);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block wakeup_reason_pm_notifier_block = {
	.notifier_call = wakeup_reason_pm_event,
};

static int __init wakeup_reason_init(void)
{
	if (register_pm_notifier(&wakeup_reason_pm_notifier_block)) {
		pr_warn("[%s] failed to register PM notifier\n", __func__);
		return -EPERM;
	}

	return 0;
}
late_initcall(wakeup_reason_init);
