// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021 Miroslav Benes <mbenes@suse.cz>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/completion.h>
#include <linux/workqueue.h>

/* Controls whether parent_function() waits for completion */
static bool block_transition;
module_param(block_transition, bool, 0644);
MODULE_PARM_DESC(block_transition, "block_transition (default=false)");

/*
 * work_started completion allows the _init function to make sure that the work
 *              (parent_function() is really scheduled and executed before
 *              returning. It solves a possible race.
 * finish completion causes parent_function() to wait (if block_transition is
 *        true) and thus it might block the live patching transition if
 *        parent_function() is specified as stack_only function.
 */
static DECLARE_COMPLETION(work_started);
static DECLARE_COMPLETION(finish);

static noinline void child_function(void)
{
	pr_info("%s\n", __func__);
}

static void parent_function(struct work_struct *work)
{
	pr_info("%s enter\n", __func__);

	complete(&work_started);

	child_function();

	if (block_transition)
		wait_for_completion(&finish);

	pr_info("%s exit\n", __func__);
}

static DECLARE_WORK(work, parent_function);

static int test_klp_func_stack_only_mod_init(void)
{
	pr_info("%s\n", __func__);

	schedule_work(&work);
	wait_for_completion(&work_started);

	return 0;
}

static void test_klp_func_stack_only_mod_exit(void)
{
	pr_info("%s\n", __func__);

	complete(&finish);
	flush_work(&work);
}

module_init(test_klp_func_stack_only_mod_init);
module_exit(test_klp_func_stack_only_mod_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Miroslav Benes <mbenes@suse.cz>");
MODULE_DESCRIPTION("Livepatch test: func_stack_only module");
