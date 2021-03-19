/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */
#if !defined(__RV_TRACE_USER_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RV_TRACE_USER_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rv_user

#define RV_USER_MRS_PRN "rv_nx %d jdev %p total_size 0x%llx max_size 0x%llx " \
			"refcount %u"

DECLARE_EVENT_CLASS(/* user msg */
	rv_user_msg_template,
	TP_PROTO(int inx, const char *msg, u64 d1, u64 d2),
	TP_ARGS(inx, msg, d1, d2),
	TP_STRUCT__entry(/* entry */
		__field(int, inx)
		__string(msg, msg)
		__field(u64, d1)
		__field(u64, d2)
	),
	TP_fast_assign(/* assign */
		__entry->inx = inx;
		__assign_str(msg, msg);
		__entry->d1 = d1;
		__entry->d2 = d2;
	),
	TP_printk(/* print */
		"rv_nx %d: %s 0x%llx 0x%llx",
		__entry->inx,
		__get_str(msg),
		__entry->d1,
		__entry->d2
	)
);

DEFINE_EVENT(/* event */
	rv_user_msg_template, rv_msg_uconn_create,
	TP_PROTO(int inx, const char *msg, u64 d1, u64 d2),
	TP_ARGS(inx, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_user_msg_template, rv_msg_uconn_connect,
	TP_PROTO(int inx, const char *msg, u64 d1, u64 d2),
	TP_ARGS(inx, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_user_msg_template, rv_msg_cmp_params,
	TP_PROTO(int inx, const char *msg, u64 d1, u64 d2),
	TP_ARGS(inx, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_user_msg_template, rv_msg_conn_exist,
	TP_PROTO(int inx, const char *msg, u64 d1, u64 d2),
	TP_ARGS(inx, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_user_msg_template, rv_msg_conn_create,
	TP_PROTO(int inx, const char *msg, u64 d1, u64 d2),
	TP_ARGS(inx, msg, d1, d2)
);

DECLARE_EVENT_CLASS(/* user_mrs */
	rv_user_mrs_template,
	TP_PROTO(int rv_inx, void *jdev, u64 total_size, u64 max_size,
		 u32 refcount),
	TP_ARGS(rv_inx, jdev, total_size, max_size, refcount),
	TP_STRUCT__entry(/* entry */
		__field(int, rv_inx)
		__field(void *, jdev)
		__field(u64, total_size)
		__field(u64, max_size)
		__field(u32, refcount)
	),
	TP_fast_assign(/* assign */
		__entry->rv_inx = rv_inx;
		__entry->jdev = jdev;
		__entry->total_size = total_size;
		__entry->max_size = max_size;
		__entry->refcount = refcount;
	),
	TP_printk(/* print */
		RV_USER_MRS_PRN,
		__entry->rv_inx,
		__entry->jdev,
		__entry->total_size,
		__entry->max_size,
		__entry->refcount
	)
);

DEFINE_EVENT(/* event */
	rv_user_mrs_template, rv_user_mrs_attach,
	TP_PROTO(int rv_inx, void *jdev, u64 total_size, u64 max_size,
		 u32 refcount),
	TP_ARGS(rv_inx, jdev, total_size, max_size, refcount)
);

DEFINE_EVENT(/* event */
	rv_user_mrs_template, rv_user_mrs_release,
	TP_PROTO(int rv_inx, void *jdev, u64 total_size, u64 max_size,
		 u32 refcount),
	TP_ARGS(rv_inx, jdev, total_size, max_size, refcount)
);

#endif /* __RV_TRACE_USER_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_user
#include <trace/define_trace.h>
