/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR Linux-OpenIB) */
/*
 * Copyright (c) 2020-2021, Alibaba Group.
 */

#ifndef __ERDMA_USER_H__
#define __ERDMA_USER_H__

#include <linux/types.h>

#define ERDMA_ABI_VERSION       1

struct erdma_ureq_create_cq {
	u64 db_record_va;
	u64 qbuf_va;
	u32 qbuf_len;
	u32 rsvd0;
};

struct erdma_uresp_create_cq {
	u32 cq_id;
	u32 num_cqe;
};

struct erdma_ureq_create_qp {
	u64 db_record_va;
	u64 qbuf_va;
	u32 qbuf_len;
	u32 rsvd0;
};

struct erdma_uresp_create_qp {
	u32 qp_id;
	u32 num_sqe;
	u32 num_rqe;
	u32 rq_offset;
};

struct erdma_uresp_alloc_ctx {
	u32 dev_id;
	u32 pad;
	u32 sdb_type;
	u32 sdb_offset;
	u64 sdb;
	u64 rdb;
	u64 cdb;
};

#endif
