/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

#ifndef __ERDMA_H__
#define __ERDMA_H__

#include <linux/bitfield.h>
#include <linux/netdevice.h>
#include <linux/xarray.h>
#include <rdma/ib_verbs.h>

#include "erdma_hw.h"

#define DRV_MODULE_NAME "erdma"

struct erdma_eq {
	void *qbuf;
	dma_addr_t qbuf_dma_addr;

	u32 depth;
	u64 __iomem *db_addr;

	spinlock_t lock;

	u16 ci;
	u16 owner;

	atomic64_t event_num;
	atomic64_t notify_num;

	void *db_info;
};

struct erdma_cmdq_sq {
	void *qbuf;
	dma_addr_t qbuf_dma_addr;

	spinlock_t lock;
	u64 __iomem *db_addr;

	u16 ci;
	u16 pi;

	u16 depth;
	u16 wqebb_cnt;

	void *db_info;

	u64 total_cmds;
	u64 total_comp_cmds;
};

struct erdma_cmdq_cq {
	void *qbuf;

	dma_addr_t qbuf_dma_addr;

	u64 __iomem *db_addr;
	spinlock_t lock;

	u32 ci;
	u16 owner;
	u16 depth;

	void *db_info;

	atomic64_t cq_armed_num;
};

enum {
	ERDMA_CMD_STATUS_INIT,
	ERDMA_CMD_STATUS_ISSUED,
	ERDMA_CMD_STATUS_FINISHED,
	ERDMA_CMD_STATUS_TIMEOUT
};

struct erdma_comp_wait {
	struct completion wait_event;
	u32 cmd_status;
	u32 ctx_id;
	u16 sq_pi;
	u8 comp_status;
	u8 rsvd;
	u32 comp_data[4];
};

enum {
	ERDMA_CMDQ_STATE_OK_BIT = 0,
	ERDMA_CMDQ_STATE_TIMEOUT_BIT = 1,
	ERDMA_CMDQ_STATE_CTX_ERR_BIT = 2,
};

#define ERDMA_CMDQ_TIMEOUT_MS       15000
#define ERDMA_REG_ACCESS_WAIT_MS    20
#define ERDMA_WAIT_DEV_DONE_CNT     500

struct erdma_cmdq {
	void *dev;

	unsigned long *comp_wait_bitmap;
	struct erdma_comp_wait *wait_pool;
	spinlock_t lock;

	u8 use_event;

	struct erdma_cmdq_sq sq;
	struct erdma_cmdq_cq cq;
	struct erdma_eq eq;

	unsigned long state;

	struct semaphore credits;
	u16 max_outstandings;
};

struct erdma_devattr {
	unsigned int device;
	unsigned int version;

	u32 vendor_id;
	u32 vendor_part_id;
	u32 sw_version;
	u32 max_qp;
	u32 max_send_wr;
	u32 max_recv_wr;
	u32 max_ord;
	u32 max_ird;

	enum ib_device_cap_flags cap_flags;
	u32 max_send_sge;
	u32 max_recv_sge;
	u32 max_sge_rd;
	u32 max_cq;
	u32 max_cqe;
	u64 max_mr_size;
	u32 max_mr;
	u32 max_pd;
	u32 max_mw;
	u32 max_srq;
	u32 max_srq_wr;
	u32 max_srq_sge;
	u32 local_dma_key;
};

#define ERDMA_IRQNAME_SIZE 50
struct erdma_irq_info {
	char name[ERDMA_IRQNAME_SIZE];
	irq_handler_t handler;
	u32 msix_vector;
	void *data;
	int cpu;
	cpumask_t affinity_hint_mask;
};

struct erdma_eq_cb {
	u8 ready;
	u8 rsvd[3];
	void *dev;
	struct erdma_irq_info irq_info;
	struct erdma_eq eq;
	struct tasklet_struct tasklet;
};

#define COMPROMISE_CC ERDMA_CC_CUBIC
enum erdma_cc_method {
	ERDMA_CC_NEWRENO = 0,
	ERDMA_CC_CUBIC,
	ERDMA_CC_HPCC_RTT,
	ERDMA_CC_HPCC_ECN,
	ERDMA_CC_HPCC_INT,
	ERDMA_CC_METHODS_NUM
};

struct erdma_resource_cb {
	unsigned long *bitmap;
	spinlock_t lock;
	u32 next_alloc_idx;
	u32 max_cap;
};

enum {
	ERDMA_RES_TYPE_PD = 0,
	ERDMA_RES_TYPE_STAG_IDX = 1,
	ERDMA_RES_CNT = 2,
};

static inline int erdma_alloc_idx(struct erdma_resource_cb *res_cb)
{
	int idx;
	unsigned long flags;
	u32 start_idx = res_cb->next_alloc_idx;

	spin_lock_irqsave(&res_cb->lock, flags);
	idx = find_next_zero_bit(res_cb->bitmap, res_cb->max_cap, start_idx);
	if (idx == res_cb->max_cap) {
		idx = find_first_zero_bit(res_cb->bitmap, res_cb->max_cap);
		if (idx == res_cb->max_cap) {
			res_cb->next_alloc_idx = 1;
			spin_unlock_irqrestore(&res_cb->lock, flags);
			return -ENOSPC;
		}
	}

	set_bit(idx, res_cb->bitmap);
	spin_unlock_irqrestore(&res_cb->lock, flags);
	res_cb->next_alloc_idx = idx + 1;
	return idx;
}

static inline void erdma_free_idx(struct erdma_resource_cb *res_cb, u32 idx)
{
	unsigned long flags;
	u32 used;

	spin_lock_irqsave(&res_cb->lock, flags);
	used = test_and_clear_bit(idx, res_cb->bitmap);
	spin_unlock_irqrestore(&res_cb->lock, flags);
	WARN_ON(!used);
}

#define ERDMA_EXTRA_BUFFER_SIZE 8

struct erdma_dev {
	struct ib_device ibdev;
	struct net_device *netdev;
	void *dmadev;
	void *drvdata;
	/* reference to drvdata->cmdq */
	struct erdma_cmdq *cmdq;

	void (*release_handler)(void *drvdata);

	/* physical port state (only one port per device) */
	enum ib_port_state state;

	struct erdma_devattr attrs;

	spinlock_t lock;

	struct erdma_resource_cb res_cb[ERDMA_RES_CNT];
	struct xarray qp_xa;
	struct xarray cq_xa;

	u32 next_alloc_qpn;
	u32 next_alloc_cqn;

	spinlock_t db_bitmap_lock;

	/* We provide 64 uContexts that each has one SQ doorbell Page. */
	DECLARE_BITMAP(sdb_page, ERDMA_DWQE_TYPE0_CNT);
	/* We provide 496 uContexts that each has one SQ normal Db, and one directWQE db */
	DECLARE_BITMAP(sdb_entry, ERDMA_DWQE_TYPE1_CNT);

	u8 __iomem *db_space;
	resource_size_t db_space_addr;

	atomic_t num_pd;
	atomic_t num_qp;
	atomic_t num_cq;
	atomic_t num_mr;
	atomic_t num_ctx;

	struct list_head cep_list;

	int cc_method;
	int disable_dwqe;
	int dwqe_pages;
	int dwqe_entries;
};

struct erdma_pci_drvdata {
	struct pci_dev *pdev;
	struct erdma_dev *dev;
	struct list_head list;

	u32 is_registered;
	unsigned char peer_addr[MAX_ADDR_LEN];

	u8 __iomem *func_bar;

	resource_size_t func_bar_addr;
	resource_size_t func_bar_len;

	u32 dma_width;

	u16 irq_num;
	u16 rsvd;

	struct erdma_irq_info comm_irq;
	struct erdma_cmdq cmdq;

	struct erdma_eq_cb aeq;
	struct erdma_eq_cb ceqs[31];

	int numa_node;
	int grp_num;
};

static inline struct erdma_dev *to_edev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct erdma_dev, ibdev);
}

static inline u32 erdma_reg_read32(struct erdma_pci_drvdata *drvdata, u32 reg)
{
	return readl(drvdata->func_bar + reg);
}

static inline u64 erdma_reg_read64(struct erdma_pci_drvdata *drvdata, u32 reg)
{
	return readq(drvdata->func_bar + reg);
}

static inline void erdma_reg_write32(struct erdma_pci_drvdata *drvdata, u32 reg, u32 value)
{
	writel(value, drvdata->func_bar + reg);
}

static inline void erdma_reg_write64(struct erdma_pci_drvdata *drvdata, u32 reg, u64 value)
{
	writeq(value, drvdata->func_bar + reg);
}

static inline u32 erdma_reg_read32_filed(struct erdma_pci_drvdata *drvdata, u32 reg,
					 u32 filed_mask)
{
	u32 val = erdma_reg_read32(drvdata, reg);

	return FIELD_GET(filed_mask, val);
}

static inline int erdma_poll_ceq_event(struct erdma_eq *ceq)
{
	__le64 *ceqe;
	u16 queue_size_mask = ceq->depth - 1;
	u64 val;

	ceqe = ceq->qbuf + ((ceq->ci & queue_size_mask) << EQE_SHIFT);

	val = READ_ONCE(*ceqe);
	if (FIELD_GET(ERDMA_CEQE_HDR_O_MASK, val) == ceq->owner) {
		dma_rmb();
		ceq->ci++;

		if ((ceq->ci & queue_size_mask) == 0)
			ceq->owner = !ceq->owner;

		atomic64_inc(&ceq->event_num);

		return FIELD_GET(ERDMA_CEQE_HDR_CQN_MASK, val);
	}

	return -1;
}

static inline void notify_eq(struct erdma_eq *eq)
{
	u64 db_data = FIELD_PREP(ERDMA_EQDB_CI_MASK, eq->ci) |
		      FIELD_PREP(ERDMA_EQDB_ARM_MASK, 1);

	*(u64 *)eq->db_info = db_data;
	writeq(db_data, eq->db_addr);

	atomic64_inc(&eq->notify_num);
}

int erdma_cmdq_init(struct erdma_pci_drvdata *drvdata);
void erdma_finish_cmdq_init(struct erdma_pci_drvdata *drvdata);
void erdma_cmdq_destroy(struct erdma_pci_drvdata *drvdata);

#define ERDMA_CMDQ_BUILD_REQ_HDR(hdr, mod, op)\
do { \
	*(u64 *)(hdr) = FIELD_PREP(ERDMA_CMD_HDR_SUB_MOD_MASK, mod);\
	*(u64 *)(hdr) |= FIELD_PREP(ERDMA_CMD_HDR_OPCODE_MASK, op);\
} while (0)

int erdma_post_cmd_wait(struct erdma_cmdq *cmdq, u64 *req, u32 req_size, u64 *resp0, u64 *resp1);
void erdma_cmdq_completion_handler(struct erdma_cmdq *cmdq);

int erdma_ceqs_init(struct erdma_pci_drvdata *drvdata);
void erdma_ceqs_uninit(struct erdma_pci_drvdata *drvdata);

int erdma_aeq_init(struct erdma_pci_drvdata *drvdata);
void erdma_aeq_destroy(struct erdma_pci_drvdata *drvdata);

void erdma_aeq_event_handler(struct erdma_pci_drvdata *drvdata);
void erdma_ceq_completion_handler(struct erdma_eq_cb *ceq_cb);

#endif
