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

#define RV_SCONN_RECV_PRN "sconn %p index %u qp 0x%x conn %p flags 0x%x " \
			  " state %u immed 0x%x"

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
