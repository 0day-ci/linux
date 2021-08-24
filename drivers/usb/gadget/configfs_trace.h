// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM configfs_gadget

#if !defined(__GADGET_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __GADGET_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(gadget_dev_desc_UDC_store,
	TP_PROTO(char *name, char *udc),
	TP_ARGS(name, udc),
	TP_STRUCT__entry(
		__string(group_name, name)
		__string(udc_name, udc)
	),
	TP_fast_assign(
		__assign_str(group_name, name);
		__assign_str(udc_name, udc);
	),
	TP_printk("gadget:%s UDC:%s", __get_str(group_name),
		__get_str(udc_name))
);

#endif /* __GADGET_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/usb/gadget

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE configfs_trace

#include <trace/define_trace.h>
