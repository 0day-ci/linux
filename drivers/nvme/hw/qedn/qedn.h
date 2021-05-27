/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#ifndef _QEDN_H_
#define _QEDN_H_

#include <linux/qed/qed_if.h>
#include <linux/qed/qed_nvmetcp_if.h>

/* Driver includes */
#include "../../host/tcp-offload.h"

#define QEDN_MODULE_NAME "qedn"

#define QEDN_MAX_TASKS_PER_PF (16 * 1024)
#define QEDN_MAX_CONNS_PER_PF (4 * 1024)
#define QEDN_FW_CQ_SIZE (4 * 1024)
#define QEDN_PROTO_CQ_PROD_IDX	0
#define QEDN_NVMETCP_NUM_FW_CONN_QUEUE_PAGES 2

enum qedn_state {
	QEDN_STATE_CORE_PROBED = 0,
	QEDN_STATE_CORE_OPEN,
	QEDN_STATE_MFW_STATE,
	QEDN_STATE_REGISTERED_OFFLOAD_DEV,
	QEDN_STATE_MODULE_REMOVE_ONGOING,
};

struct qedn_ctx {
	struct pci_dev *pdev;
	struct qed_dev *cdev;
	struct qed_dev_nvmetcp_info dev_info;
	struct nvme_tcp_ofld_dev qedn_ofld_dev;
	struct qed_pf_params pf_params;

	/* Accessed with atomic bit ops, used with enum qedn_state */
	unsigned long state;

	/* Fast path queues */
	u8 num_fw_cqs;
};

#endif /* _QEDN_H_ */
