/*
 * Printk RV reactor:
 *   Prints the exception msg to the kernel message log.
 *
 * Copyright (C) 2019-2022 Daniel Bristot de Oliveira <bristot@kernel.org>
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>

static void rv_printk_reaction(char *msg)
{
	printk(msg);
}

struct rv_reactor rv_printk = {
	.name = "printk",
	.description = "prints the exception msg to the kernel message log",
	.react = rv_printk_reaction
};

int register_react_printk(void)
{
	rv_register_reactor(&rv_printk);
	return 0;
}

void unregister_react_printk(void)
{
	rv_unregister_reactor(&rv_printk);
}

module_init(register_react_printk);
module_exit(unregister_react_printk);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Daniel Bristot de Oliveira");
MODULE_DESCRIPTION("printk rv reactor: printk if an exception is hit");
