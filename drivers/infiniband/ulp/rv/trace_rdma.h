/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */
#if !defined(__RV_TRACE_RDMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RV_TRACE_RDMA_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rv_rdma

#define RV_PEND_WRITE_PRN "user_inx %d sconn %p pend_wr %p loc_addr 0x%llx" \
			  " rkey 0x%x rem_addr 0x%llx len 0x%llx immed 0x%x" \
			  " wr_id 0x%llx"

#define RV_SCONN_RECV_PRN "sconn %p index %u qp 0x%x conn %p flags 0x%x " \
			  " state %u immed 0x%x"

#define RV_EVENT_PRN "type 0x%x status 0x%x immed 0x%x wr_id 0x%llx " \
		     "conn_handle 0x%llx len 0x%x"

DECLARE_EVENT_CLASS(/* pend_write */
	rv_pend_write_template,
	TP_PROTO(int user_inx, void *sconn, void *pend_wr, u64 loc_addr,
		 u32 rkey, u64 rem_addr, u64 len, u32 immed, u64 wr_id),
	TP_ARGS(user_inx, sconn, pend_wr, loc_addr, rkey, rem_addr, len, immed,
		wr_id),
	TP_STRUCT__entry(/* entry */
		__field(int, user_inx)
		__field(void *, sconn)
		__field(void *, pend_wr)
		__field(u64, loc_addr)
		__field(u32, rkey)
		__field(u64, rem_addr)
		__field(u64, len)
		__field(u32, immed)
		__field(u64, wr_id)
	),
	TP_fast_assign(/* assign */
		__entry->user_inx = user_inx;
		__entry->sconn = sconn;
		__entry->pend_wr = pend_wr;
		__entry->loc_addr = loc_addr;
		__entry->rkey = rkey;
		__entry->rem_addr = rem_addr;
		__entry->len = len;
		__entry->immed = immed;
		__entry->wr_id = wr_id;
	),
	TP_printk(/* print */
		RV_PEND_WRITE_PRN,
		__entry->user_inx,
		__entry->sconn,
		__entry->pend_wr,
		__entry->loc_addr,
		__entry->rkey,
		__entry->rem_addr,
		__entry->len,
		__entry->immed,
		__entry->wr_id
	)
);

DEFINE_EVENT(/* event */
	rv_pend_write_template, rv_pend_write_post,
	TP_PROTO(int user_inx, void *sconn, void *pend_wr, u64 loc_addr,
		 u32 rkey, u64 rem_addr, u64 len, u32 immed, u64 wr_id),
	TP_ARGS(user_inx, sconn, pend_wr, loc_addr, rkey, rem_addr, len, immed,
		wr_id)
);

DEFINE_EVENT(/* event */
	rv_pend_write_template, rv_pend_write_done,
	TP_PROTO(int user_inx, void *sconn, void *pend_wr, u64 loc_addr,
		 u32 rkey, u64 rem_addr, u64 len, u32 immed, u64 wr_id),
	TP_ARGS(user_inx, sconn, pend_wr, loc_addr, rkey, rem_addr, len, immed,
		wr_id)
);

DECLARE_EVENT_CLASS(/* recv */
	rv_sconn_recv_template,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, u32 immed),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, immed),
	TP_STRUCT__entry(/* entry */
		__field(void *, ptr)
		__field(u8, index)
		__field(u32, qp_num)
		__field(void *, conn)
		__field(u32, flags)
		__field(u32, state)
		__field(u32, immed)
	),
	TP_fast_assign(/* assign */
		__entry->ptr = ptr;
		__entry->index = index;
		__entry->qp_num = qp_num;
		__entry->conn = conn;
		__entry->flags = flags;
		__entry->state = state;
		__entry->immed = immed;
	),
	TP_printk(/* print */
		 RV_SCONN_RECV_PRN,
		__entry->ptr,
		__entry->index,
		__entry->qp_num,
		__entry->conn,
		__entry->flags,
		__entry->state,
		__entry->immed
	)
);

DEFINE_EVENT(/* event */
	rv_sconn_recv_template, rv_sconn_recv_done,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, u32 immed),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, immed)
);

DEFINE_EVENT(/* event */
	rv_sconn_recv_template, rv_sconn_recv_post,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, u32 immed),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, immed)
);

DEFINE_EVENT(/* event */
	rv_sconn_recv_template, rv_sconn_hb_done,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, u32 immed),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, immed)
);

DEFINE_EVENT(/* event */
	rv_sconn_recv_template, rv_sconn_hb_post,
	TP_PROTO(void *ptr, u8 index, u32 qp_num, void *conn, u32 flags,
		 u32 state, u32 immed),
	TP_ARGS(ptr, index, qp_num, conn, flags, state, immed)
);

DECLARE_EVENT_CLASS(/* event */
	rv_event_template,
	TP_PROTO(u8 type, u8 status, u32 immed, u64 wr_id, u64 conn_handle,
		 u32 len),
	TP_ARGS(type, status, immed, wr_id, conn_handle, len),
	TP_STRUCT__entry(/* entry */
		__field(u8, type)
		__field(u8, status)
		__field(u32, immed)
		__field(u64, wr_id)
		__field(u64, conn_handle)
		__field(u32, len)
	),
	TP_fast_assign(/* assign */
		__entry->type = type;
		__entry->status = status;
		__entry->immed = immed;
		__entry->wr_id = wr_id;
		__entry->conn_handle = conn_handle;
		__entry->len = len;
	),
	TP_printk(/* print */
		RV_EVENT_PRN,
		__entry->type,
		__entry->status,
		__entry->immed,
		__entry->wr_id,
		__entry->conn_handle,
		__entry->len
	)
);

DEFINE_EVENT(/* event */
	rv_event_template, rv_event_write_done,
	TP_PROTO(u8 type, u8 status, u32 immed, u64 wr_id, u64 conn_handle,
		 u32 len),
	TP_ARGS(type, status, immed, wr_id, conn_handle, len)
);

DEFINE_EVENT(/* event */
	rv_event_template, rv_event_post,
	TP_PROTO(u8 type, u8 status, u32 immed, u64 wr_id, u64 conn_handle,
		 u32 len),
	TP_ARGS(type, status, immed, wr_id, conn_handle, len)
);

DEFINE_EVENT(/* event */
	rv_event_template, rv_event_recv_write,
	TP_PROTO(u8 type, u8 status, u32 immed, u64 wr_id, u64 conn_handle,
		 u32 len),
	TP_ARGS(type, status, immed, wr_id, conn_handle, len)
);

DECLARE_EVENT_CLASS(/* wc */
	rv_wc_template,
	TP_PROTO(u64 wr_id, u32 status, u32 opcode, u32 byte_len,
		 u32 imm_data),
	TP_ARGS(wr_id, status, opcode, byte_len, imm_data),
	TP_STRUCT__entry(/* entry */
		__field(u64, wr_id)
		__field(u32, status)
		__field(u32, opcode)
		__field(u32, byte_len)
		__field(u32, imm_data)
	),
	TP_fast_assign(/* assign */
		__entry->wr_id = wr_id;
		__entry->status = status;
		__entry->opcode = opcode;
		__entry->byte_len = byte_len;
		__entry->imm_data = imm_data;
	),
	TP_printk(/* print */
		"wr_id 0x%llx status 0x%x opcode 0x%x byte_len 0x%x immed 0x%x",
		__entry->wr_id,
		__entry->status,
		__entry->opcode,
		__entry->byte_len,
		__entry->imm_data
	)
);

DEFINE_EVENT(/* event */
	rv_wc_template, rv_wc_recv_done,
	TP_PROTO(u64 wr_id, u32 status, u32 opcode, u32 byte_len,
		 u32 imm_data),
	TP_ARGS(wr_id, status, opcode, byte_len, imm_data)
);

DEFINE_EVENT(/* event */
	rv_wc_template, rv_wc_write_done,
	TP_PROTO(u64 wr_id, u32 status, u32 opcode, u32 byte_len,
		 u32 imm_data),
	TP_ARGS(wr_id, status, opcode, byte_len, imm_data)
);

DEFINE_EVENT(/* event */
	rv_wc_template, rv_wc_hb_done,
	TP_PROTO(u64 wr_id, u32 status, u32 opcode, u32 byte_len,
		 u32 imm_data),
	TP_ARGS(wr_id, status, opcode, byte_len, imm_data)
);

#endif /* __RV_TRACE_RDMA_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_rdma
#include <trace/define_trace.h>
