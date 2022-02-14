/*
 * Panic RV reactor:
 *   Prints the exception msg to the kernel message log and panic().
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

static void rv_panic_reaction(char *msg)
{
	panic(msg);
}

struct rv_reactor rv_panic = {
	.name = "panic",
	.description = "panic the system if an exception is found.",
	.react = rv_panic_reaction
};

int register_react_panic(void)
{
	rv_register_reactor(&rv_panic);
	return 0;
}

void unregister_react_panic(void)
{
	rv_unregister_reactor(&rv_panic);
}

module_init(register_react_panic);
module_exit(unregister_react_panic);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Daniel Bristot de Oliveira");
MODULE_DESCRIPTION("panic rv reactor: panic if an exception is found");
