// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Joe Lawrence <joe.lawrence@redhat.com>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>
#include "test_klp_convert.h"

void print_saved_command_line(void)
{
	pr_info("saved_command_line (auto): %s\n", saved_command_line);
}

void print_driver_name(void)
{
	pr_info("driver_name, 0: %s\n", driver_name);
	pr_info("test_klp_get_driver_name(), (auto): %s\n", test_klp_get_driver_name());
}

void print_homonym_string(void)
{
	pr_info("homonym_string, 2: %s\n", homonym_string);
	pr_info("get_homonym_string(), 2: %s\n", get_homonym_string());
}

/* provide a sysfs handle to invoke debug functions */
static int print_debug;
static int print_debug_set(const char *val, const struct kernel_param *kp)
{
	print_saved_command_line();
	print_driver_name();
	print_homonym_string();

	return 0;
}
static const struct kernel_param_ops print_debug_ops = {
	.set = print_debug_set,
	.get = param_get_int,
};
module_param_cb(print_debug, &print_debug_ops, &print_debug, 0200);
MODULE_PARM_DESC(print_debug, "print klp-convert debugging info");

/*
 * saved_command_line is a uniquely named symbol, so the sympos
 * annotation is optional.  Skip it and test that klp-convert can
 * resolve the symbol on its own.
 */

/*
 * driver_name symbols can be found in vmlinux (multiple) and also
 * test_klp_convert_mod, therefore the annotation is required to
 * clarify that we want the one from test_klp_convert_mod.
 *
 * test_klp_convert_mod contains multiple homonym_string symbols,
 * test_klp_convert1.c resolved to the first one, resolve to the
 * second one here.
 *
 * test_klp_get_driver_name is a uniquely named symbol, test klp-convert can
 * resolve it automatically.
 */
KLP_MODULE_RELOC(test_klp_convert_mod) test_klp_convert_mod_relocs_a[] = {
	KLP_SYMPOS(driver_name, 0),
	KLP_SYMPOS(homonym_string, 2),
	KLP_SYMPOS(get_homonym_string, 2),
};

static struct klp_func funcs[] = {
	{
	}, { }
};

static struct klp_object objs[] = {
	{
		/* name being NULL means vmlinux */
		.funcs = funcs,
	},
	{
		.name = "test_klp_convert_mod",
		.funcs = funcs,
	}, { }
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

static int test_klp_convert_init(void)
{
	int ret;

	ret = klp_enable_patch(&patch);
	if (ret)
		return ret;

	return 0;
}

static void test_klp_convert_exit(void)
{
}

module_init(test_klp_convert_init);
module_exit(test_klp_convert_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: klp-convert2");
MODULE_INFO(livepatch, "Y");
