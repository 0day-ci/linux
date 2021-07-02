// SPDX-License-Identifier: GPL-2.0

#include <linux/fault-inject.h>
#include <linux/delay.h>

#include "kernfs-internal.h"

static DECLARE_FAULT_ATTR(fail_kernfs_fop_write_iter);
struct kernfs_config_fail kernfs_config_fail;

#define kernfs_config_fail(when) \
	kernfs_config_fail.kernfs_fop_write_iter_fail.wait_ ## when

#define kernfs_config_fail(when) \
	kernfs_config_fail.kernfs_fop_write_iter_fail.wait_ ## when

static int __init setup_fail_kernfs_fop_write_iter(char *str)
{
	return setup_fault_attr(&fail_kernfs_fop_write_iter, str);
}

__setup("fail_kernfs_fop_write_iter=", setup_fail_kernfs_fop_write_iter);

struct dentry *kernfs_debugfs_root;
struct dentry *config_fail_kernfs_fop_write_iter;

void __init kernfs_init_failure_injection(void)
{
	kernfs_config_fail.sleep_after_wait_ms = 100;
	kernfs_debugfs_root = debugfs_create_dir("kernfs", NULL);

	fault_create_debugfs_attr("fail_kernfs_fop_write_iter",
				  kernfs_debugfs_root, &fail_kernfs_fop_write_iter);

	config_fail_kernfs_fop_write_iter =
		debugfs_create_dir("config_fail_kernfs_fop_write_iter",
				   kernfs_debugfs_root);

	debugfs_create_u32("sleep_after_wait_ms", 0600,
			   kernfs_debugfs_root,
			   &kernfs_config_fail.sleep_after_wait_ms);

	debugfs_create_bool("wait_at_start", 0600,
			    config_fail_kernfs_fop_write_iter,
			    &kernfs_config_fail(at_start));
	debugfs_create_bool("wait_before_mutex", 0600,
			    config_fail_kernfs_fop_write_iter,
			    &kernfs_config_fail(before_mutex));
	debugfs_create_bool("wait_after_mutex", 0600,
			    config_fail_kernfs_fop_write_iter,
			    &kernfs_config_fail(after_mutex));
	debugfs_create_bool("wait_after_active", 0600,
			    config_fail_kernfs_fop_write_iter,
			    &kernfs_config_fail(after_active));
}
late_initcall(kernfs_init_failure_injection);

int __kernfs_debug_should_wait_kernfs_fop_write_iter(bool evaluate)
{
	if (!evaluate)
		return 0;

	return should_fail(&fail_kernfs_fop_write_iter, 0);
}

DECLARE_COMPLETION(kernfs_debug_wait_completion);
EXPORT_SYMBOL_NS_GPL(kernfs_debug_wait_completion, KERNFS_DEBUG_PRIVATE);

void kernfs_debug_wait(void)
{
	wait_for_completion(&kernfs_debug_wait_completion);
	pr_info("%s received completion\n", __func__);

	/**
	 * The goal is wait for an event, and *then* once we have
	 * reached it, the other side will try to do something which
	 * it thinks will break. So we must give it some time to do
	 * that. The amount of time is configurable.
	 */
	msleep(kernfs_config_fail.sleep_after_wait_ms);
	pr_info("%s ended\n", __func__);
}
