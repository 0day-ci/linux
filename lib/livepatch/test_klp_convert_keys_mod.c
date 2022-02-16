// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Joe Lawrence <joe.lawrence@redhat.com>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/jump_label.h>

static DEFINE_STATIC_KEY_TRUE(test_klp_true_key);
static DEFINE_STATIC_KEY_FALSE(test_klp_false_key);

static void print_key_status(char *msg)
{
	pr_info("%s: %s\n", __func__, msg);

	/* static_key_enable() only tests the key value */
	pr_info("static_key_enabled(&test_klp_true_key) is %s\n",
		static_key_enabled(&test_klp_true_key) ? "true" : "false");
	pr_info("static_key_enabled(&test_klp_false_key) is %s\n",
		static_key_enabled(&test_klp_false_key) ? "true" : "false");

	/*
	 * static_branch_(un)likely() requires code patching when the
	 * key value changes
	 */
	pr_info("static_branch_likely(&test_klp_true_key) is %s\n",
		static_branch_likely(&test_klp_true_key) ? "true" : "false");
	pr_info("static_branch_unlikely(&test_klp_false_key) is %s\n",
		static_branch_unlikely(&test_klp_false_key) ? "true" : "false");
}

static int test_klp_keys_mod_init(void)
{
	print_key_status("initial conditions");
	static_branch_disable(&test_klp_true_key);
	print_key_status("disabled test_klp_true_key");

	return 0;
}

static void test_klp_keys_mod_exit(void)
{
	print_key_status("unloading conditions");
}

module_init(test_klp_keys_mod_init);
module_exit(test_klp_keys_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: static keys target module");
