// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Joe Lawrence <joe.lawrence@redhat.com>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

/* Unique symbols that don't need sympos annotation */
static const char driver_name[] = KBUILD_MODNAME;
__used static const char *test_klp_get_driver_name(void)
{
	return driver_name;
}

/* Common symbol names that need sympos */
static const char homonym_string[] = "homonym string A";
__used static const char *get_homonym_string(void)
{
	return homonym_string;
}

__used static void static_string_function(void)
{
	__used static char klp_string[] __asm__("klp_string.12345") =
		__FILE__ " static string";
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: klp-convert module");
