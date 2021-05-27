/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#ifndef _QEDN_H_
#define _QEDN_H_

#include <linux/qed/common_hsi.h>
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

/* HW defines */

/* QEDN_MAX_LLH_PORTS will be extended in future */
#define QEDN_MAX_LLH_PORTS 16

/* Destroy connection defines */
#define QEDN_NON_ABORTIVE_TERMINATION 0
#define QEDN_ABORTIVE_TERMINATION 1

#define QEDN_FW_CQ_FP_WQ_WORKQUEUE "qedn_fw_cq_fp_wq"

/*
 * TCP offload stack default configurations and defines.
 * Future enhancements will allow controlling the configurable
 * parameters via devlink.
 */
#define QEDN_TCP_RTO_DEFAULT 280
#define QEDN_TCP_ECN_EN 0
#define QEDN_TCP_TS_EN 0
#define QEDN_TCP_DA_EN 0
#define QEDN_TCP_KA_EN 0
#define QEDN_TCP_TOS 0
#define QEDN_TCP_TTL 0xfe
#define QEDN_TCP_FLOW_LABEL 0
#define QEDN_TCP_KA_TIMEOUT 7200000
#define QEDN_TCP_KA_INTERVAL 10000
#define QEDN_TCP_KA_MAX_PROBE_COUNT 10
#define QEDN_TCP_MAX_RT_TIME 1200
#define QEDN_TCP_MAX_CWND 4
#define QEDN_TCP_RCV_WND_SCALE 2
#define QEDN_TCP_TS_OPTION_LEN 12

/* SP Work queue defines */
#define QEDN_SP_WORKQUEUE "qedn_sp_wq"
#define QEDN_SP_WORKQUEUE_MAX_ACTIVE 1

#define QEDN_HOST_MAX_SQ_SIZE (512)
#define QEDN_SQ_SIZE (2 * QEDN_HOST_MAX_SQ_SIZE)

/* Timeouts and delay constants */
#define QEDN_WAIT_CON_ESTABLSH_TMO 10000 /* 10 seconds */
#define QEDN_RLS_CONS_TMO 5000 /* 5 sec */

enum qedn_state {
	QEDN_STATE_CORE_PROBED = 0,
	QEDN_STATE_CORE_OPEN,
	QEDN_STATE_LLH_PORT_FILTER_SET,
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
	struct work_struct fw_cq_fp_wq_entry;
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

	u8 num_llh_filters;
	struct list_head llh_filter_list;
	u8 local_mac_addr[ETH_ALEN];
	u16 mtu;

	/* Connections */
	DECLARE_HASHTABLE(conn_ctx_hash, 16);

	/* Fast path queues */
	u8 num_fw_cqs;
	struct qedn_fp_queue *fp_q_arr;
	struct nvmetcp_glbl_queue_entry *fw_cq_array_virt;
	dma_addr_t fw_cq_array_phy; /* Physical address of fw_cq_array_virt */
	struct workqueue_struct *fw_cq_fp_wq;
};

struct qedn_endpoint {
	/* FW Params */
	struct qed_chain fw_sq_chain;
	struct nvmetcp_db_data db_data;
	void __iomem *p_doorbell;

	/* TCP Params */
	__be32 dst_addr[4]; /* In network order */
	__be32 src_addr[4]; /* In network order */
	u16 src_port;
	u16 dst_port;
	u16 vlan_id;
	u8 src_mac[ETH_ALEN];
	u8 dst_mac[ETH_ALEN];
	u8 ip_type;
};

enum sp_work_agg_action {
	CREATE_CONNECTION = 0,
	SEND_ICREQ,
	HANDLE_ICRESP,
	DESTROY_CONNECTION,
};

enum qedn_ctrl_agg_state {
	QEDN_CTRL_SET_TO_OFLD_CTRL = 0, /* CTRL set to OFLD_CTRL */
	QEDN_STATE_SP_WORK_THREAD_SET, /* slow patch WQ was created*/
	LLH_FILTER, /* LLH filter added */
	QEDN_RECOVERY,
	ADMINQ_CONNECTED, /* At least one connection has attempted offload */
	ERR_FLOW,
};

enum qedn_ctrl_sp_wq_state {
	QEDN_CTRL_STATE_UNINITIALIZED = 0,
	QEDN_CTRL_STATE_FREE_CTRL,
	QEDN_CTRL_STATE_CTRL_ERR,
};

/* Any change to this enum requires an update of qedn_conn_state_str */
enum qedn_conn_state {
	CONN_STATE_CONN_IDLE = 0,
	CONN_STATE_CREATE_CONNECTION,
	CONN_STATE_WAIT_FOR_CONNECT_DONE,
	CONN_STATE_OFFLOAD_COMPLETE,
	CONN_STATE_WAIT_FOR_UPDATE_EQE,
	CONN_STATE_WAIT_FOR_IC_COMP,
	CONN_STATE_NVMETCP_CONN_ESTABLISHED,
	CONN_STATE_DESTROY_CONNECTION,
	CONN_STATE_WAIT_FOR_DESTROY_DONE,
	CONN_STATE_DESTROY_COMPLETE
};

struct qedn_llh_filter {
	struct list_head entry;
	u16 port;
	u16 ref_cnt;
};

struct qedn_ctrl {
	struct list_head glb_entry;
	struct list_head pf_entry;

	struct qedn_ctx *qedn;
	struct nvme_tcp_ofld_queue *queue;
	struct nvme_tcp_ofld_ctrl *ctrl;

	struct sockaddr remote_mac_addr;
	u16 vlan_id;

	struct workqueue_struct *sp_wq;
	enum qedn_ctrl_sp_wq_state sp_wq_state;

	struct work_struct sp_wq_entry;

	struct qedn_llh_filter *llh_filter;

	unsigned long agg_state;

	atomic_t host_num_active_conns;
};

/* Connection level struct */
struct qedn_conn_ctx {
	/* IO path */
	struct qedn_fp_queue *fp_q;
	/* mutex for queueing request */
	struct mutex send_mutex;
	unsigned int cpu;
	int qid;

	struct qedn_ctx *qedn;
	struct nvme_tcp_ofld_queue *queue;
	struct nvme_tcp_ofld_ctrl *ctrl;
	u32 conn_handle;
	u32 fw_cid;

	atomic_t est_conn_indicator;
	atomic_t destroy_conn_indicator;
	wait_queue_head_t conn_waitq;

	struct work_struct sp_wq_entry;

	/* Connection aggregative state.
	 * Can have different states independently.
	 */
	unsigned long agg_work_action;

	struct hlist_node hash_node;
	struct nvmetcp_host_cccid_itid_entry *host_cccid_itid;
	dma_addr_t host_cccid_itid_phy_addr;
	struct qedn_endpoint ep;
	int abrt_flag;

	/* Connection resources - turned on to indicate what resource was
	 * allocated, to that it can later be released.
	 */
	unsigned long resrc_state;

	/* Connection state */
	spinlock_t conn_state_lock;
	enum qedn_conn_state state;

	size_t sq_depth;

	/* "dummy" socket */
	struct socket *sock;
};

enum qedn_conn_resources_state {
	QEDN_CONN_RESRC_FW_SQ,
	QEDN_CONN_RESRC_ACQUIRE_CONN,
	QEDN_CONN_RESRC_CCCID_ITID_MAP,
	QEDN_CONN_RESRC_TCP_PORT,
	QEDN_CONN_RESRC_DB_ADD,
	QEDN_CONN_RESRC_MAX = 64
};

struct qedn_conn_ctx *qedn_get_conn_hash(struct qedn_ctx *qedn, u16 icid);
int qedn_event_cb(void *context, u8 fw_event_code, void *event_ring_data);
void qedn_sp_wq_handler(struct work_struct *work);
void qedn_set_sp_wa(struct qedn_conn_ctx *conn_ctx, u32 bit);
void qedn_clr_sp_wa(struct qedn_conn_ctx *conn_ctx, u32 bit);
int qedn_initialize_endpoint(struct qedn_endpoint *ep, u8 *local_mac_addr,
			     struct nvme_tcp_ofld_ctrl *ctrl);
int qedn_wait_for_conn_est(struct qedn_conn_ctx *conn_ctx);
int qedn_set_con_state(struct qedn_conn_ctx *conn_ctx, enum qedn_conn_state new_state);
void qedn_terminate_connection(struct qedn_conn_ctx *conn_ctx);
void qedn_cleanp_fw(struct qedn_conn_ctx *conn_ctx);
__be16 qedn_get_in_port(struct sockaddr_storage *sa);
inline int qedn_validate_cccid_in_range(struct qedn_conn_ctx *conn_ctx, u16 cccid);
int qedn_queue_request(struct qedn_conn_ctx *qedn_conn, struct nvme_tcp_ofld_req *req);
void qedn_nvme_req_fp_wq_handler(struct work_struct *work);
void qedn_io_work_cq(struct qedn_ctx *qedn, struct nvmetcp_fw_cqe *cqe);

#endif /* _QEDN_H_ */
