/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#ifndef _QEDN_H_
#define _QEDN_H_

#include <linux/qed/qed_if.h>
#include <linux/qed/qed_nvmetcp_if.h>
#include <linux/qed/qed_nvmetcp_ip_services_if.h>
#include <linux/qed/qed_chain.h>
#include <linux/qed/storage_common.h>
#include <linux/qed/nvmetcp_common.h>

/* Driver includes */
#include "../../host/tcp-offload.h"

#define QEDN_MODULE_NAME "qedn"

#define QEDN_MAX_TASKS_PER_PF (16 * 1024)
#define QEDN_MAX_CONNS_PER_PF (4 * 1024)
#define QEDN_FW_CQ_SIZE (4 * 1024)
#define QEDN_PROTO_CQ_PROD_IDX	0
#define QEDN_NVMETCP_NUM_FW_CONN_QUEUE_PAGES 2

#define QEDN_PAGE_SIZE	4096 /* FW page size - Configurable */
#define QEDN_IRQ_NAME_LEN 24
#define QEDN_IRQ_NO_FLAGS 0

#define QEDN_TCP_RTO_DEFAULT 280

enum qedn_state {
	QEDN_STATE_CORE_PROBED = 0,
	QEDN_STATE_CORE_OPEN,
	QEDN_STATE_MFW_STATE,
	QEDN_STATE_NVMETCP_OPEN,
	QEDN_STATE_IRQ_SET,
	QEDN_STATE_FP_WORK_THREAD_SET,
	QEDN_STATE_REGISTERED_OFFLOAD_DEV,
	QEDN_STATE_MODULE_REMOVE_ONGOING,
};

/* Per CPU core params */
struct qedn_fp_queue {
	struct qed_chain cq_chain;
	u16 *cq_prod;
	struct mutex cq_mutex; /* cq handler mutex */
	struct qedn_ctx	*qedn;
	struct qed_sb_info *sb_info;
	unsigned int cpu;
	u16 sb_id;
	char irqname[QEDN_IRQ_NAME_LEN];
};

struct qedn_ctx {
	struct pci_dev *pdev;
	struct qed_dev *cdev;
	struct qed_int_info int_info;
	struct qed_dev_nvmetcp_info dev_info;
	struct nvme_tcp_ofld_dev qedn_ofld_dev;
	struct qed_pf_params pf_params;

	/* Accessed with atomic bit ops, used with enum qedn_state */
	unsigned long state;

	/* Fast path queues */
	u8 num_fw_cqs;
	struct qedn_fp_queue *fp_q_arr;
	struct nvmetcp_glbl_queue_entry *fw_cq_array_virt;
	dma_addr_t fw_cq_array_phy; /* Physical address of fw_cq_array_virt */
};

#endif /* _QEDN_H_ */
