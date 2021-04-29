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

#define QEDN_MAJOR_VERSION		8
#define QEDN_MINOR_VERSION		62
#define QEDN_REVISION_VERSION		10
#define QEDN_ENGINEERING_VERSION	0
#define DRV_MODULE_VERSION __stringify(QEDE_MAJOR_VERSION) "."	\
		__stringify(QEDE_MINOR_VERSION) "."		\
		__stringify(QEDE_REVISION_VERSION) "."		\
		__stringify(QEDE_ENGINEERING_VERSION)

#define QEDN_MODULE_NAME "qedn"

#define QEDN_MAX_TASKS_PER_PF (16 * 1024)
#define QEDN_MAX_CONNS_PER_PF (4 * 1024)
#define QEDN_FW_CQ_SIZE (4 * 1024)
#define QEDN_PROTO_CQ_PROD_IDX	0
#define QEDN_NVMETCP_NUM_FW_CONN_QUEUE_PAGES 2

enum qedn_state {
	QEDN_STATE_CORE_PROBED = 0,
	QEDN_STATE_CORE_OPEN,
	QEDN_STATE_GL_PF_LIST_ADDED,
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

	/* Global PF list entry */
	struct list_head gl_pf_entry;

	/* Accessed with atomic bit ops, used with enum qedn_state */
	unsigned long state;

	/* Fast path queues */
	u8 num_fw_cqs;
};

struct qedn_global {
	struct list_head qedn_pf_list;

	/* Host mode */
	struct list_head ctrl_list;

	/* Mutex for accessing the global struct */
	struct mutex glb_mutex;
};

#endif /* _QEDN_H_ */
