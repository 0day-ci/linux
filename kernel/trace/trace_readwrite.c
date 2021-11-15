// SPDX-License-Identifier: GPL-2.0-only
/*
 * Register read and write tracepoints
 *
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/ftrace.h>
#include <linux/mmio-instrumented.h>
#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/rwmmio.h>

#ifdef CONFIG_TRACE_MMIO_ACCESS
void log_write_mmio(const char *width, volatile void __iomem *addr)
{
	trace_rwmmio_write(CALLER_ADDR0, width, addr);
}
EXPORT_SYMBOL_GPL(log_write_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwmmio_write);

void log_read_mmio(const char *width, const volatile void __iomem *addr)
{
	trace_rwmmio_read(CALLER_ADDR0, width, addr);
}
EXPORT_SYMBOL_GPL(log_read_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwmmio_read);
#endif /* CONFIG_TRACE_MMIO_ACCESS */
