// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Changbin Du <changbin.du@gmail.com>
 */

#include <linux/irqflags.h>
#include <linux/kprobes.h>
#include "trace_irq.h"

/**
 * trace_hardirqs_on/off requires at least two parent call frames.
 * Here we add one extra level so they can be safely called by low
 * level entry code.
 */

void __trace_hardirqs_on(void)
{
	trace_hardirqs_on();
}
NOKPROBE_SYMBOL(__trace_hardirqs_on);

void __trace_hardirqs_off(void)
{
	trace_hardirqs_off();
}
NOKPROBE_SYMBOL(__trace_hardirqs_off);
