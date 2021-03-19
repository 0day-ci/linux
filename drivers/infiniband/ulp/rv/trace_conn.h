/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */
#if !defined(__RV_TRACE_CONN_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RV_TRACE_CONN_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rv_conn

#define RV_CONN_REQ_PRN  "rem_addr 0x%x global %u sgid_inx %u port_num %u " \
			 "dlid 0x%x dgid 0x%llx %llx"

#define RV_CONN_PRN  "Conn 0x%p rem_addr 0x%x global %u dlid 0x%x " \
		     "dgid 0x%llx %llx num_conn %u next %u jdev 0x%p " \
		     "refcount %u"

#define RV_JDEV_PRN "jdev 0x%p dev %p num_conn %u index_bits %u " \
		    "loc_gid_index %u loc addr 0x%x jkey_len %u " \
		    "jkey 0x%s sid 0x%llx q_depth %u ua_next %u "\
		    "refcount %u"

#define RV_SCONN_PRN "sconn %p index %u qp 0x%x conn %p flags 0x%x state %u " \
		     "cm_id %p retry %u"

DECLARE_EVENT_CLASS(/* listener */
	rv_listener_template,
	TP_PROTO(const char *dev_name, u64 svc_id, u32 refcount),
	TP_ARGS(dev_name, svc_id, refcount),
	TP_STRUCT__entry(/* entry */
		__string(name, dev_name)
		__field(u64, sid)
		__field(u32, count)
	),
	TP_fast_assign(/* assign */
		__assign_str(name, dev_name);
		__entry->sid = svc_id;
		__entry->count = refcount;
	),
	TP_printk(/* print */
		"Device %s sid 0x%llx refcount %u",
		__get_str(name),
		__entry->sid,
		__entry->count
	)
);

DEFINE_EVENT(/* event */
	rv_listener_template, rv_listener_get,
	TP_PROTO(const char *dev_name, u64 svc_id, u32 refcount),
	TP_ARGS(dev_name, svc_id, refcount)
);

DEFINE_EVENT(/* event */
	rv_listener_template, rv_listener_put,
	TP_PROTO(const char *dev_name, u64 svc_id, u32 refcount),
	TP_ARGS(dev_name, svc_id, refcount)
);

TRACE_EVENT(/* event */
	rv_conn_create_req,
	TP_PROTO(u32 rem_addr, u8 global, u8 sgid_inx, u8 port_num, u16 dlid,
		 u64 dgid1, u64 dgid2),
	TP_ARGS(rem_addr, global, sgid_inx, port_num, dlid, dgid1, dgid2),
	TP_STRUCT__entry(/* entry */
		__field(u32, rem_addr)
		__field(u8, global)
		__field(u8, sgid_inx)
		__field(u8, port_num)
		__field(u16, dlid)
		__field(u64, dgid1)
		__field(u64, dgid2)
	),
	TP_fast_assign(/* assign */
		__entry->rem_addr = rem_addr;
		__entry->global = global;
		__entry->sgid_inx = sgid_inx;
		__entry->port_num = port_num;
		__entry->dlid = dlid;
		__entry->dgid1 = dgid1;
		__entry->dgid2 = dgid2;
	),
	TP_printk(/* print */
		RV_CONN_REQ_PRN,
		__entry->rem_addr,
		__entry->global,
		__entry->sgid_inx,
		__entry->port_num,
		__entry->dlid,
		__entry->dgid1,
		__entry->dgid2
	)
);

DECLARE_EVENT_CLASS(/* conn */
	rv_conn_template,
	TP_PROTO(void *ptr, u32 rem_addr, u8 global, u16 dlid, u64 dgid1,
		 u64 dgid2, u8 num_conn, u32 next, void *jdev, u32 refcount),
	TP_ARGS(ptr, rem_addr, global, dlid, dgid1, dgid2, num_conn, next,
		jdev, refcount),
	TP_STRUCT__entry(/* entry */
		__field(void *, ptr)
		__field(u32, rem_addr)
		__field(u8, global)
		__field(u16, dlid)
		__field(u64, dgid1)
		__field(u64, dgid2)
		__field(u8, num_conn)
		__field(u32, next)
		__field(void *, jdev)
		__field(u32, refcount)
	),
	TP_fast_assign(/* assign */
		__entry->ptr = ptr;
		__entry->rem_addr = rem_addr;
		__entry->global = global;
		__entry->dlid = dlid;
		__entry->dgid1 = dgid1;
		__entry->dgid2 = dgid2;
		__entry->num_conn = num_conn;
		__entry->next = next;
		__entry->jdev = jdev;
		__entry->refcount = refcount;
	),
	TP_printk(/* print */
		RV_CONN_PRN,
		__entry->ptr,
		__entry->rem_addr,
		__entry->global,
		__entry->dlid,
		__entry->dgid1,
		__entry->dgid2,
		__entry->num_conn,
		__entry->next,
		__entry->jdev,
		__entry->refcount
	)
);

DEFINE_EVENT(/* event */
	rv_conn_template, rv_conn_create,
	TP_PROTO(void *ptr, u32 rem_addr, u8 global, u16 dlid, u64 dgid1,
		 u64 dgid2, u8 num_conn, u32 next, void *jdev, u32 refcount),
	TP_ARGS(ptr, rem_addr, global, dlid, dgid1, dgid2, num_conn, next,
		jdev, refcount)
);

DEFINE_EVENT(/* event */
	rv_conn_template, rv_conn_alloc,
	TP_PROTO(void *ptr, u32 rem_addr, u8 global, u16 dlid, u64 dgid1,
		 u64 dgid2, u8 num_conn, u32 next, void *jdev, u32 refcount),
	TP_ARGS(ptr, rem_addr, global, dlid, dgid1, dgid2, num_conn, next,
		jdev, refcount)
);

DEFINE_EVENT(/* event */
	rv_conn_template, rv_conn_release,
	TP_PROTO(void *ptr, u32 rem_addr, u8 global, u16 dlid, u64 dgid1,
		 u64 dgid2, u8 num_conn, u32 next, void *jdev, u32 refcount),
	TP_ARGS(ptr, rem_addr, global, dlid, dgid1, dgid2, num_conn, next,
		jdev, refcount)
);

DEFINE_EVENT(/* event */
	rv_conn_template, rv_conn_connect,
	TP_PROTO(void *ptr, u32 rem_addr, u8 global, u16 dlid, u64 dgid1,
		 u64 dgid2, u8 num_conn, u32 next, void *jdev, u32 refcount),
	TP_ARGS(ptr, rem_addr, global, dlid, dgid1, dgid2, num_conn, next,
		jdev, refcount)
);

DECLARE_EVENT_CLASS(/* jdev */
	rv_jdev_template,
	TP_PROTO(void *ptr, const char *dev_name, u8 num_conn, u8 index_bits,
		 u16 loc_gid_index, u32 loc_addr, u8 jkey_len, u8 *jkey,
		 u64 sid, u32 q_depth, u32 ua_next, u32 refcount),
	TP_ARGS(ptr, dev_name, num_conn, index_bits, loc_gid_index, loc_addr,
		jkey_len, jkey, sid, q_depth, ua_next, refcount),
	TP_STRUCT__entry(/* entry */
		__field(void *, ptr)
		__string(name, dev_name)
		__field(u8, num_conn)
		__field(u8, index_bits)
		__field(u16, loc_gid_index)
		__field(u32, loc_addr)
		__field(u8, jkey_len)
		__array(u8, jkey, RV_MAX_JOB_KEY_LEN)
		__field(u64, sid)
		__field(u32, q_depth)
		__field(u32, ua_next)
		__field(u32, refcount)
	),
	TP_fast_assign(/* assign */
		__entry->ptr = ptr;
		__assign_str(name, dev_name);
		__entry->num_conn = num_conn;
		__entry->index_bits = index_bits;
		__entry->loc_gid_index = loc_gid_index;
		__entry->loc_addr = loc_addr;
		__entry->jkey_len = jkey_len;
		memcpy(__entry->jkey, jkey, RV_MAX_JOB_KEY_LEN);
		__entry->sid = sid;
		__entry->q_depth = q_depth;
		__entry->ua_next = ua_next;
		__entry->refcount = refcount;
	),
	TP_printk(/* print */
		RV_JDEV_PRN,
		__entry->ptr,
		__get_str(name),
		__entry->num_conn,
		__entry->index_bits,
		__entry->loc_gid_index,
		__entry->loc_addr,
		__entry->jkey_len,
		__print_hex_str(__entry->jkey, RV_MAX_JOB_KEY_LEN),
		__entry->sid,
		__entry->q_depth,
		__entry->ua_next,
		__entry->refcount
	)
);

DEFINE_EVENT(/* event */
	rv_jdev_template, rv_jdev_conn_create,
	TP_PROTO(void *ptr, const char *dev_name, u8 num_conn, u8 index_bits,
		 u16 loc_gid_index, u32 loc_addr, u8 jkey_len, u8 *jkey,
		 u64 sid, u32 q_depth, u32 ua_next, u32 refcount),
	TP_ARGS(ptr, dev_name, num_conn, index_bits, loc_gid_index, loc_addr,
		jkey_len, jkey, sid, q_depth, ua_next, refcount)
);

DECLARE_EVENT_CLASS(/* sconn */
	rv_sconn_template,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry),
	TP_STRUCT__entry(/* entry */
		__field(void *, ptr)
		__field(u8, index)
		__field(u32, qp_num)
		__field(void *, conn)
		__field(u32, flags)
		__field(u32, state)
		__field(void *, cm_id)
		__field(u32, retry)
	),
	TP_fast_assign(/* assign */
		__entry->ptr = ptr;
		__entry->index = index;
		__entry->qp_num = qp_num;
		__entry->conn = conn;
		__entry->flags = flags;
		__entry->state = state;
		__entry->cm_id = cm_id;
		__entry->retry = retry;
	),
	TP_printk(/* print */
		 RV_SCONN_PRN,
		__entry->ptr,
		__entry->index,
		__entry->qp_num,
		__entry->conn,
		__entry->flags,
		__entry->state,
		__entry->cm_id,
		__entry->retry
	)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_init,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_deinit,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_resolve,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_resolve_cb,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_cm_handler,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_set_state,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_req_handler,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_done_discon,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_drain_done,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_cq_event,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_qp_event,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_timeout_work,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DEFINE_EVENT(/* event */
	rv_sconn_template, rv_sconn_delay_work,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, void *cm_id, u32 retry),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, cm_id, retry)
);

DECLARE_EVENT_CLASS(/* cm_event */
	rv_cm_event_template,
	TP_PROTO(u32 evt, void *cm_id, void *sconn),
	TP_ARGS(evt, cm_id, sconn),
	TP_STRUCT__entry(/* entry */
		__field(u32, event)
		__field(void *, cm_id)
		__field(void *, sconn)
	),
	TP_fast_assign(/* assign */
		__entry->event = evt;
		__entry->cm_id = cm_id;
		__entry->sconn = sconn;
	),
	TP_printk(/* print */
		"Event %u cm_id %p sconn %p",
		__entry->event,
		__entry->cm_id,
		__entry->sconn
	)
);

DEFINE_EVENT(/* event */
	rv_cm_event_template, rv_cm_event_handler,
	TP_PROTO(u32 evt, void *cm_id, void *sconn),
	TP_ARGS(evt, cm_id, sconn)
);

DEFINE_EVENT(/* event */
	rv_cm_event_template, rv_cm_event_server_handler,
	TP_PROTO(u32 evt, void *cm_id, void *sconn),
	TP_ARGS(evt, cm_id, sconn)
);

DECLARE_EVENT_CLASS(/* msg */
	rv_sconn_msg_template,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2),
	TP_STRUCT__entry(/* entry */
		__field(void *, ptr)
		__field(u8, index)
		__string(msg, msg)
		__field(u64, d1)
		__field(u64, d2)
	),
	TP_fast_assign(/* assign */
		__entry->ptr = ptr;
		__entry->index = index;
		__assign_str(msg, msg);
		__entry->d1 = d1;
		__entry->d2 = d2;
	),
	TP_printk(/* print */
		"sconn %p index %u: %s 0x%llx 0x%llx",
		__entry->ptr,
		__entry->index,
		__get_str(msg),
		__entry->d1,
		__entry->d2
	)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_destroy_qp,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_send_req,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_qp_rtr,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_cm_handler,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_cm_rep_handler,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_enter_disconnect,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_cm_req_handler,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_sconn_timeout_work,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_cq_event,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_qp_event,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DEFINE_EVENT(/* event */
	rv_sconn_msg_template, rv_msg_prepost_recv,
	TP_PROTO(void *ptr, u8 index, const char *msg, u64 d1, u64 d2),
	TP_ARGS(ptr, index, msg, d1, d2)
);

DECLARE_EVENT_CLASS(/* msg */
	rv_msg_template,
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
		"inx %u: %s 0x%llx 0x%llx",
		__entry->inx,
		__get_str(msg),
		__entry->d1,
		__entry->d2
	)
);

DEFINE_EVENT(/* event */
	rv_msg_template, rv_msg_err_qp,
	TP_PROTO(int inx, const char *msg, u64 d1, u64 d2),
	TP_ARGS(inx, msg, d1, d2)
);
#endif /* __RV_TRACE_CONN_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_conn
#include <trace/define_trace.h>
