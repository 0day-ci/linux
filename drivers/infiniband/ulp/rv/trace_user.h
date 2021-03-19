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

#define RV_USER_PRN "Inx %d rdma_mode %u attached %u dev_name %s " \
		    "cq_entries %u index %u"

#define RV_USER_MRS_PRN "rv_nx %d jdev %p total_size 0x%llx max_size 0x%llx " \
			"refcount %u"

#define RV_ATTACH_REQ_PRN "inx %d Device %s rdma_mode %u port_num %u " \
			  "loc_addr 0x%x jkey_len %u jkey 0x%s " \
			  " service_id 0x%llx cq_entries %u q_depth %u " \
			  " timeout %u hb_timeout %u"

DECLARE_EVENT_CLASS(/* user */
	rv_user_template,
	TP_PROTO(int inx, u8 rdma_mode, u8 attached, const char *dev_name,
		 u32 cq_entries, u16 index),
	TP_ARGS(inx, rdma_mode, attached, dev_name, cq_entries, index),
	TP_STRUCT__entry(/* entry */
		__field(int, inx)
		__field(u8, rdma_mode)
		__field(u8, attached)
		__string(name, dev_name)
		__field(u32, cq_entries)
		__field(u16, index)
	),
	TP_fast_assign(/* assign */
		__entry->inx = inx;
		__entry->rdma_mode = rdma_mode;
		__entry->attached = attached;
		__assign_str(name, dev_name);
		__entry->cq_entries = cq_entries;
		__entry->index = index;
	),
	TP_printk(/* print */
		RV_USER_PRN,
		__entry->inx,
		__entry->rdma_mode,
		__entry->attached,
		__get_str(name),
		__entry->cq_entries,
		__entry->index
	)
);

DEFINE_EVENT(/* event */
	rv_user_template, rv_user_open,
	TP_PROTO(int inx, u8 rdma_mode, u8 attached, const char *dev_name,
		 u32 cq_entries, u16 index),
	TP_ARGS(inx, rdma_mode, attached, dev_name, cq_entries, index)
);

DEFINE_EVENT(/* event */
	rv_user_template, rv_user_close,
	TP_PROTO(int inx, u8 rdma_mode, u8 attached, const char *dev_name,
		 u32 cq_entries, u16 index),
	TP_ARGS(inx, rdma_mode, attached, dev_name, cq_entries, index)
);

DEFINE_EVENT(/* event */
	rv_user_template, rv_user_attach,
	TP_PROTO(int inx, u8 rdma_mode, u8 attached, const char *dev_name,
		 u32 cq_entries, u16 index),
	TP_ARGS(inx, rdma_mode, attached, dev_name, cq_entries, index)
);

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
	rv_user_msg_template, rv_msg_detach_all,
	TP_PROTO(int inx, const char *msg, u64 d1, u64 d2),
	TP_ARGS(inx, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_user_msg_template, rv_msg_uconn_remove,
	TP_PROTO(int inx, const char *msg, u64 d1, u64 d2),
	TP_ARGS(inx, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_user_msg_template, rv_msg_cleanup,
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

DEFINE_EVENT(/* event */
	rv_user_msg_template, rv_msg_mmap,
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

TRACE_EVENT(/* event */
	rv_attach_req,
	TP_PROTO(int inx, char *dev_name, u8 rdma_mode, u8 port_num,
		 u32 loc_addr, u8 jkey_len, u8 *jkey, u64 service_id,
		 u32 cq_entries, u32 q_depth, u32 timeout, u32 hb_timeout),
	TP_ARGS(inx, dev_name, rdma_mode, port_num, loc_addr, jkey_len, jkey,
		service_id, cq_entries, q_depth, timeout, hb_timeout),
	TP_STRUCT__entry(/* entry */
		__field(int, inx)
		__string(device, dev_name)
		__field(u8, rdma_mode)
		__field(u8, port_num)
		__field(u32, loc_addr)
		__field(u8, jkey_len)
		__array(u8, jkey, RV_MAX_JOB_KEY_LEN)
		__field(u64, service_id)
		__field(u32, cq_entries)
		__field(u32, q_depth)
		__field(u32, timeout)
		__field(u32, hb_timeout)
	),
	TP_fast_assign(/* assign */
		__entry->inx = inx;
		__assign_str(device, dev_name);
		__entry->inx = inx;
		__entry->rdma_mode = rdma_mode;
		__entry->port_num = port_num;
		__entry->loc_addr = loc_addr;
		__entry->jkey_len = jkey_len;
		memcpy(__entry->jkey, jkey, RV_MAX_JOB_KEY_LEN);
		__entry->service_id = service_id;
		__entry->cq_entries = cq_entries;
		__entry->q_depth = q_depth;
		__entry->timeout = timeout;
		__entry->hb_timeout = hb_timeout;
	),
	TP_printk(/* print */
		RV_ATTACH_REQ_PRN,
		__entry->inx,
		__get_str(device),
		__entry->rdma_mode,
		__entry->port_num,
		__entry->loc_addr,
		__entry->jkey_len,
		__print_hex_str(__entry->jkey, RV_MAX_JOB_KEY_LEN),
		__entry->service_id,
		__entry->cq_entries,
		__entry->q_depth,
		__entry->timeout,
		__entry->hb_timeout
	)
);

DECLARE_EVENT_CLASS(/* user_ring */
	rv_user_ring_template,
	TP_PROTO(int rv_inx, u32 count, u32 hd, u32 tail),
	TP_ARGS(rv_inx, count, hd, tail),
	TP_STRUCT__entry(/* entry */
		__field(int, rv_inx)
		__field(u32, count)
		__field(u32, head)
		__field(u32, tail)
	),
	TP_fast_assign(/* assign */
		__entry->rv_inx = rv_inx;
		__entry->count = count;
		__entry->head = hd;
		__entry->tail = tail;
	),
	TP_printk(/* print */
		"rv_inx %d entries %u head %u tail %u",
		__entry->rv_inx,
		__entry->count,
		__entry->head,
		__entry->tail
	)
);

DEFINE_EVENT(/* event */
	rv_user_ring_template, rv_user_ring_alloc,
	TP_PROTO(int rv_inx, u32 count, u32 hd, u32 tail),
	TP_ARGS(rv_inx, count, hd, tail)
);

DEFINE_EVENT(/* event */
	rv_user_ring_template, rv_user_ring_free,
	TP_PROTO(int rv_inx, u32 count, u32 hd, u32 tail),
	TP_ARGS(rv_inx, count, hd, tail)
);

DEFINE_EVENT(/* event */
	rv_user_ring_template, rv_user_ring_post_event,
	TP_PROTO(int rv_inx, u32 count, u32 hd, u32 tail),
	TP_ARGS(rv_inx, count, hd, tail)
);

TRACE_EVENT(/* event */
	rv_ioctl,
	TP_PROTO(int inx, u32 cmd),
	TP_ARGS(inx, cmd),
	TP_STRUCT__entry(/* entry */
		__field(int, inx)
		__field(u32, cmd)
	),
	TP_fast_assign(/* assign */
		__entry->inx = inx;
		__entry->cmd = cmd;
	),
	TP_printk(/* print */
		"inx %d cmd 0x%x",
		__entry->inx,
		__entry->cmd
	)
);

#endif /* __RV_TRACE_USER_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_user
#include <trace/define_trace.h>
