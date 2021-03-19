/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */
#if !defined(__RV_TRACE_DEV_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RV_TRACE_DEV_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rv_dev

DECLARE_EVENT_CLASS(/* dev */
	rv_dev_template,
	TP_PROTO(const char *dev_name, u32 refcount),
	TP_ARGS(dev_name, refcount),
	TP_STRUCT__entry(/* entry */
		__string(name, dev_name)
		__field(u32, refcount)
	),
	TP_fast_assign(/* assign */
		__assign_str(name, dev_name);
		__entry->refcount = refcount;
	),
	TP_printk(/* print */
		"name %s, refcount %u",
		__get_str(name),
		__entry->refcount
	)
);

DEFINE_EVENT(/* event */
	rv_dev_template, rv_dev_add,
	TP_PROTO(const char *dev_name, u32 refcount),
	TP_ARGS(dev_name, refcount)
);

DEFINE_EVENT(/* event */
	rv_dev_template, rv_dev_remove,
	TP_PROTO(const char *dev_name, u32 refcount),
	TP_ARGS(dev_name, refcount)
);

DEFINE_EVENT(/* event */
	rv_dev_template, rv_dev_get,
	TP_PROTO(const char *dev_name, u32 refcount),
	TP_ARGS(dev_name, refcount)
);

DEFINE_EVENT(/* event */
	rv_dev_template, rv_dev_put,
	TP_PROTO(const char *dev_name, u32 refcount),
	TP_ARGS(dev_name, refcount)
);

TRACE_EVENT(/* event */
	rv_device_event,
	TP_PROTO(const char *dev_name, const char *evt_name),
	TP_ARGS(dev_name, evt_name),
	TP_STRUCT__entry(/* entry */
		__string(device, dev_name)
		__string(event, evt_name)
	),
	TP_fast_assign(/* assign */
		__assign_str(device, dev_name);
		__assign_str(event, evt_name);
	),
	TP_printk(/* print */
		"Device %s Event %s",
		__get_str(device),
		__get_str(event)
	)
);

#endif /* __RV_TRACE_DEV_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_dev
#include <trace/define_trace.h>
