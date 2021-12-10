// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021 Miroslav Benes <mbenes@suse.cz>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>

static int func_stack_only;
module_param(func_stack_only, int, 0644);
MODULE_PARM_DESC(func_stack_only, "func_stack_only (default=0)");

static void livepatch_child_function(void)
{
	pr_info("%s\n", __func__);
}

static struct klp_func funcs[] = {
	{
		.old_name = "child_function",
		.new_func = livepatch_child_function,
	}, {}
};

/* Used if func_stack_only module parameter is true */
static struct klp_func funcs_stack_only[] = {
	{
		.old_name = "child_function",
		.new_func = livepatch_child_function,
	}, {
		.old_name = "parent_function",
		.stack_only = true,
	}, {}
};

static struct klp_object objs[] = {
	{
		.name = "test_klp_func_stack_only_mod",
		.funcs = funcs,
	}, {}
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

static int test_klp_func_stack_only_demo_init(void)
{
	if (func_stack_only)
		objs[0].funcs = funcs_stack_only;

	return klp_enable_patch(&patch);
}

static void test_klp_func_stack_only_demo_exit(void)
{
}

module_init(test_klp_func_stack_only_demo_init);
module_exit(test_klp_func_stack_only_demo_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
MODULE_AUTHOR("Miroslav Benes <mbenes@suse.cz>");
MODULE_DESCRIPTION("Livepatch test: func_stack_only demo");
