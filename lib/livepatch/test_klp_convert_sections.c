// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Joe Lawrence <joe.lawrence@redhat.com>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>
#include "test_klp_convert.h"

/* klp-convert symbols - vmlinux */
extern char *saved_command_line;

/*
 * Scatter references to the same symbol (saved_command_line) across a
 * few different ELF sections.  At the same time, include multiple
 * references within the same function.
 */
__section(".text.print_saved_command_line")
void print_saved_command_line(void)
{
	pr_info("saved_command_line (1): %s\n", saved_command_line);
}

__section(".text.print_saved_command_line2")
void print_saved_command_line2(void)
{
	pr_info("saved_command_line (1): %s\n", saved_command_line);
	pr_info("saved_command_line (2): %s\n", saved_command_line);
}

__section(".text.print_saved_command_line3")
void print_saved_command_line3(void)
{
	pr_info("saved_command_line (1): %s\n", saved_command_line);
	pr_info("saved_command_line (2): %s\n", saved_command_line);
	pr_info("saved_command_line (3): %s\n", saved_command_line);
}

/*
 * Create relocations in .rela.data that need conversion, sharing
 * symbols with ordinary .text relas.
 */
const char *(*p_test_klp_get_driver_name)(void) = test_klp_get_driver_name;
const char *(*p_get_homonym_string)(void) = get_homonym_string;

void print_via_function_pointers(void)
{
	pr_info("test_klp_get_driver_name(): %s\n", test_klp_get_driver_name());
	pr_info("p_test_klp_get_driver_name(): %s\n", p_test_klp_get_driver_name());
	pr_info("get_homonym_string(): %s\n", get_homonym_string());
	pr_info("p_get_homonym_string(): %s\n", p_get_homonym_string());
}

/* provide a sysfs handle to invoke debug functions */
static int print_debug;
static int print_debug_set(const char *val, const struct kernel_param *kp)
{
	print_saved_command_line();
	print_saved_command_line2();
	print_saved_command_line3();
	print_via_function_pointers();

	return 0;
}
static const struct kernel_param_ops print_debug_ops = {
	.set = print_debug_set,
	.get = param_get_int,
};

module_param_cb(print_debug, &print_debug_ops, &print_debug, 0200);
MODULE_PARM_DESC(print_debug, "print klp-convert debugging info");


KLP_MODULE_RELOC(test_klp_convert_mod) test_klp_convert_mod_relocs_a[] = {
	KLP_SYMPOS(get_homonym_string, 1),
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

static int test_klp_convert_sections_init(void)
{
	int ret;

	ret = klp_enable_patch(&patch);
	if (ret)
		return ret;

	return 0;
}

static void test_klp_convert_sections_exit(void)
{
}

module_init(test_klp_convert_sections_init);
module_exit(test_klp_convert_sections_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: klp-convert-sections");
MODULE_INFO(livepatch, "Y");
