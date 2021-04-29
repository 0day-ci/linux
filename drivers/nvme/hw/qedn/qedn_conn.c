// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

 /* Kernel includes */
#include <linux/kernel.h>
#include <linux/module.h>
#include <net/tcp.h>

/* Driver includes */
#include "qedn.h"

extern const struct qed_nvmetcp_ops *qed_ops;

static const char * const qedn_conn_state_str[] = {
	"CONN_IDLE",
	"CREATE_CONNECTION",
	"WAIT_FOR_CONNECT_DONE",
	"OFFLOAD_COMPLETE",
	"WAIT_FOR_UPDATE_EQE",
	"WAIT_FOR_IC_COMP",
	"NVMETCP_CONN_ESTABLISHED",
	"DESTROY_CONNECTION",
	"WAIT_FOR_DESTROY_DONE",
	"DESTROY_COMPLETE",
	NULL
};

inline int qedn_qid(struct nvme_tcp_ofld_queue *queue)
{
	return queue - queue->ctrl->queues;
}

void qedn_ring_doorbell(struct qedn_conn_ctx *conn_ctx)
{
	struct nvmetcp_db_data dbell = { 0 };
	u16 prod_idx;

	dbell.agg_flags = 0;
	dbell.params |= DB_DEST_XCM << NVMETCP_DB_DATA_DEST_SHIFT;
	dbell.params |= DB_AGG_CMD_SET << NVMETCP_DB_DATA_AGG_CMD_SHIFT;
	dbell.params |=
		DQ_XCM_ISCSI_SQ_PROD_CMD << NVMETCP_DB_DATA_AGG_VAL_SEL_SHIFT;
	dbell.params |= 1 << NVMETCP_DB_DATA_BYPASS_EN_SHIFT;
	prod_idx = qed_chain_get_prod_idx(&conn_ctx->ep.fw_sq_chain);
	dbell.sq_prod = cpu_to_le16(prod_idx);

	/* wmb - Make sure fw idx is coherent */
	wmb();
	writel(*(u32 *)&dbell, conn_ctx->ep.p_doorbell);
}

int qedn_set_con_state(struct qedn_conn_ctx *conn_ctx, enum qedn_conn_state new_state)
{
	spin_lock_bh(&conn_ctx->conn_state_lock);
	conn_ctx->state = new_state;
	spin_unlock_bh(&conn_ctx->conn_state_lock);

	return 0;
}

static void qedn_return_tcp_port(struct qedn_conn_ctx *conn_ctx)
{
	if (conn_ctx->sock && conn_ctx->sock->sk) {
		qed_return_tcp_port(conn_ctx->sock);
		conn_ctx->sock = NULL;
	}

	conn_ctx->ep.src_port = 0;
}

int qedn_wait_for_conn_est(struct qedn_conn_ctx *conn_ctx)
{
	int wrc, rc;

	wrc = wait_event_interruptible_timeout(conn_ctx->conn_waitq,
					       atomic_read(&conn_ctx->est_conn_indicator) > 0,
					       msecs_to_jiffies(QEDN_WAIT_CON_ESTABLSH_TMO));
	atomic_set(&conn_ctx->est_conn_indicator, 0);
	if (!wrc ||
	    conn_ctx->state != CONN_STATE_NVMETCP_CONN_ESTABLISHED) {
		rc = -ETIMEDOUT;

		/* If error was prior or during offload, conn_ctx was released.
		 * If the error was after offload sync has completed, we need to
		 * terminate the connection ourselves.
		 */
		if (conn_ctx &&
		    conn_ctx->state >= CONN_STATE_WAIT_FOR_CONNECT_DONE &&
		    conn_ctx->state <= CONN_STATE_NVMETCP_CONN_ESTABLISHED)
			qedn_terminate_connection(conn_ctx,
						  QEDN_ABORTIVE_TERMINATION);
	} else {
		rc = 0;
	}

	return rc;
}

int qedn_fill_ep_addr4(struct qedn_endpoint *ep,
		       struct nvme_tcp_ofld_ctrl_con_params *conn_params)
{
	struct sockaddr_in *raddr = (struct sockaddr_in *)&conn_params->remote_ip_addr;
	struct sockaddr_in *laddr = (struct sockaddr_in *)&conn_params->local_ip_addr;

	ep->ip_type = TCP_IPV4;
	ep->src_port = laddr->sin_port;
	ep->dst_port = ntohs(raddr->sin_port);

	ep->src_addr[0] = laddr->sin_addr.s_addr;
	ep->dst_addr[0] = raddr->sin_addr.s_addr;

	return 0;
}

int qedn_fill_ep_addr6(struct qedn_endpoint *ep,
		       struct nvme_tcp_ofld_ctrl_con_params *conn_params)
{
	struct sockaddr_in6 *raddr6 = (struct sockaddr_in6 *)&conn_params->remote_ip_addr;
	struct sockaddr_in6 *laddr6 = (struct sockaddr_in6 *)&conn_params->local_ip_addr;
	int i;

	ep->ip_type = TCP_IPV6;
	ep->src_port = laddr6->sin6_port;
	ep->dst_port = ntohs(raddr6->sin6_port);

	for (i = 0; i < 4; i++) {
		ep->src_addr[i] = laddr6->sin6_addr.in6_u.u6_addr32[i];
		ep->dst_addr[i] = raddr6->sin6_addr.in6_u.u6_addr32[i];
	}

	return 0;
}

int qedn_initialize_endpoint(struct qedn_endpoint *ep, u8 *local_mac_addr,
			     struct nvme_tcp_ofld_ctrl_con_params *conn_params)
{
	ether_addr_copy(ep->dst_mac, conn_params->remote_mac_addr.sa_data);
	ether_addr_copy(ep->src_mac, local_mac_addr);
	ep->vlan_id = conn_params->vlan_id;
	if (conn_params->remote_ip_addr.ss_family == AF_INET)
		qedn_fill_ep_addr4(ep, conn_params);
	else
		qedn_fill_ep_addr6(ep, conn_params);

	return -1;
}

static int qedn_alloc_icreq_pad(struct qedn_conn_ctx *conn_ctx)
{
	struct qedn_ctx *qedn = conn_ctx->qedn;
	struct qedn_icreq_padding *icreq_pad;
	u32 *buffer;
	int rc = 0;

	icreq_pad = kzalloc(sizeof(*icreq_pad), GFP_KERNEL);
	if (!icreq_pad)
		return -ENOMEM;

	conn_ctx->icreq_pad = icreq_pad;
	memset(&icreq_pad->sge, 0, sizeof(icreq_pad->sge));
	buffer = dma_alloc_coherent(&qedn->pdev->dev,
				    QEDN_ICREQ_FW_PAYLOAD,
				    &icreq_pad->pa,
				    GFP_KERNEL);
	if (!buffer) {
		pr_err("Could not allocate icreq_padding SGE buffer.\n");
		rc =  -ENOMEM;
		goto release_icreq_pad;
	}

	DMA_REGPAIR_LE(icreq_pad->sge.sge_addr, icreq_pad->pa);
	icreq_pad->sge.sge_len = cpu_to_le32(QEDN_ICREQ_FW_PAYLOAD);
	icreq_pad->buffer = buffer;
	set_bit(QEDN_CONN_RESRC_ICREQ_PAD, &conn_ctx->resrc_state);

	return 0;

release_icreq_pad:
	kfree(icreq_pad);
	conn_ctx->icreq_pad = NULL;

	return rc;
}

static void qedn_free_icreq_pad(struct qedn_conn_ctx *conn_ctx)
{
	struct qedn_ctx *qedn = conn_ctx->qedn;
	struct qedn_icreq_padding *icreq_pad;
	u32 *buffer;

	icreq_pad = conn_ctx->icreq_pad;
	if (unlikely(!icreq_pad)) {
		pr_err("null ptr in icreq_pad in conn_ctx\n");
		goto finally;
	}

	buffer = icreq_pad->buffer;
	if (buffer) {
		dma_free_coherent(&qedn->pdev->dev,
				  QEDN_ICREQ_FW_PAYLOAD,
				  (void *)buffer,
				  icreq_pad->pa);
		icreq_pad->buffer = NULL;
	}

	kfree(icreq_pad);
	conn_ctx->icreq_pad = NULL;

finally:
	clear_bit(QEDN_CONN_RESRC_ICREQ_PAD, &conn_ctx->resrc_state);
}

static void qedn_release_conn_ctx(struct qedn_conn_ctx *conn_ctx)
{
	struct qedn_ctx *qedn = conn_ctx->qedn;
	int rc = 0;

	if (test_bit(QEDN_CONN_RESRC_FW_SQ, &conn_ctx->resrc_state)) {
		qed_ops->common->chain_free(qedn->cdev,
					    &conn_ctx->ep.fw_sq_chain);
		clear_bit(QEDN_CONN_RESRC_FW_SQ, &conn_ctx->resrc_state);
	}

	if (test_bit(QEDN_CONN_RESRC_ACQUIRE_CONN, &conn_ctx->resrc_state)) {
		hash_del(&conn_ctx->hash_node);
		rc = qed_ops->release_conn(qedn->cdev, conn_ctx->conn_handle);
		if (rc)
			pr_warn("Release_conn returned with an error %u\n",
				rc);

		clear_bit(QEDN_CONN_RESRC_ACQUIRE_CONN, &conn_ctx->resrc_state);
	}

	if (test_bit(QEDN_CONN_RESRC_ICREQ_PAD, &conn_ctx->resrc_state))
		qedn_free_icreq_pad(conn_ctx);

	if (test_bit(QEDN_CONN_RESRC_TASKS, &conn_ctx->resrc_state)) {
		clear_bit(QEDN_CONN_RESRC_TASKS, &conn_ctx->resrc_state);
			qedn_return_active_tasks(conn_ctx);
	}

	if (test_bit(QEDN_CONN_RESRC_CCCID_ITID_MAP, &conn_ctx->resrc_state)) {
		dma_free_coherent(&qedn->pdev->dev,
				  conn_ctx->sq_depth *
				  sizeof(struct nvmetcp_host_cccid_itid_entry),
				  conn_ctx->host_cccid_itid,
				  conn_ctx->host_cccid_itid_phy_addr);
		clear_bit(QEDN_CONN_RESRC_CCCID_ITID_MAP,
			  &conn_ctx->resrc_state);
	}

	if (test_bit(QEDN_CONN_RESRC_TCP_PORT, &conn_ctx->resrc_state)) {
		qedn_return_tcp_port(conn_ctx);
		clear_bit(QEDN_CONN_RESRC_TCP_PORT,
			  &conn_ctx->resrc_state);
	}

	if (conn_ctx->resrc_state)
		pr_err("Conn resources state isn't 0 as expected 0x%lx\n",
		       conn_ctx->resrc_state);

	atomic_inc(&conn_ctx->destroy_conn_indicator);
	qedn_set_con_state(conn_ctx, CONN_STATE_DESTROY_COMPLETE);
	wake_up_interruptible(&conn_ctx->conn_waitq);
}

static int qedn_alloc_fw_sq(struct qedn_ctx *qedn,
			    struct qedn_endpoint *ep)
{
	struct qed_chain_init_params params = {
		.mode           = QED_CHAIN_MODE_PBL,
		.intended_use   = QED_CHAIN_USE_TO_PRODUCE,
		.cnt_type       = QED_CHAIN_CNT_TYPE_U16,
		.num_elems      = QEDN_SQ_SIZE,
		.elem_size      = sizeof(struct nvmetcp_wqe),
	};
	int rc;

	rc = qed_ops->common->chain_alloc(qedn->cdev,
					   &ep->fw_sq_chain,
					   &params);
	if (rc) {
		pr_err("Failed to allocate SQ chain\n");

		return -ENOMEM;
	}

	return 0;
}

static int qedn_nvmetcp_offload_conn(struct qedn_conn_ctx *conn_ctx)
{
	struct qed_nvmetcp_params_offload offld_prms = { 0 };
	struct qedn_endpoint *qedn_ep = &conn_ctx->ep;
	struct qedn_ctx *qedn = conn_ctx->qedn;
	u8 ts_hdr_size = 0;
	u32 hdr_size;
	int rc, i;

	ether_addr_copy(offld_prms.src.mac, qedn_ep->src_mac);
	ether_addr_copy(offld_prms.dst.mac, qedn_ep->dst_mac);
	offld_prms.vlan_id = qedn_ep->vlan_id;
	offld_prms.ecn_en = QEDN_TCP_ECN_EN;
	offld_prms.timestamp_en =  QEDN_TCP_TS_EN;
	offld_prms.delayed_ack_en = QEDN_TCP_DA_EN;
	offld_prms.tcp_keep_alive_en = QEDN_TCP_KA_EN;
	offld_prms.ip_version = qedn_ep->ip_type;

	offld_prms.src.ip[0] = ntohl(qedn_ep->src_addr[0]);
	offld_prms.dst.ip[0] = ntohl(qedn_ep->dst_addr[0]);
	if (qedn_ep->ip_type == TCP_IPV6) {
		for (i = 1; i < 4; i++) {
			offld_prms.src.ip[i] = ntohl(qedn_ep->src_addr[i]);
			offld_prms.dst.ip[i] = ntohl(qedn_ep->dst_addr[i]);
		}
	}

	offld_prms.ttl = QEDN_TCP_TTL;
	offld_prms.tos_or_tc = QEDN_TCP_TOS;
	offld_prms.dst.port = qedn_ep->dst_port;
	offld_prms.src.port = qedn_ep->src_port;
	offld_prms.nvmetcp_cccid_itid_table_addr =
		conn_ctx->host_cccid_itid_phy_addr;
	offld_prms.nvmetcp_cccid_max_range = conn_ctx->sq_depth;

	/* Calculate MSS */
	if (offld_prms.timestamp_en)
		ts_hdr_size = QEDN_TCP_TS_OPTION_LEN;

	hdr_size = qedn_ep->ip_type == TCP_IPV4 ?
		   sizeof(struct iphdr) : sizeof(struct ipv6hdr);
	hdr_size += sizeof(struct tcphdr) + ts_hdr_size;

	offld_prms.mss = qedn->mtu - hdr_size;
	offld_prms.rcv_wnd_scale = QEDN_TCP_RCV_WND_SCALE;
	offld_prms.cwnd = QEDN_TCP_MAX_CWND * offld_prms.mss;
	offld_prms.ka_max_probe_cnt = QEDN_TCP_KA_MAX_PROBE_COUNT;
	offld_prms.ka_timeout = QEDN_TCP_KA_TIMEOUT;
	offld_prms.ka_interval = QEDN_TCP_KA_INTERVAL;
	offld_prms.max_rt_time = QEDN_TCP_MAX_RT_TIME;
	offld_prms.sq_pbl_addr =
		(u64)qed_chain_get_pbl_phys(&qedn_ep->fw_sq_chain);
	offld_prms.default_cq = conn_ctx->default_cq;

	rc = qed_ops->offload_conn(qedn->cdev,
				   conn_ctx->conn_handle,
				   &offld_prms);
	if (rc)
		pr_err("offload_conn returned with an error\n");

	return rc;
}

static int qedn_fetch_tcp_port(struct qedn_conn_ctx *conn_ctx)
{
	struct nvme_tcp_ofld_ctrl *ctrl;
	struct qedn_ctrl *qctrl;
	int rc = 0;

	ctrl = conn_ctx->ctrl;
	qctrl = (struct qedn_ctrl *)ctrl->private_data;

	rc = qed_fetch_tcp_port(ctrl->conn_params.local_ip_addr,
				&conn_ctx->sock, &conn_ctx->ep.src_port);

	return rc;
}

static void qedn_decouple_conn(struct qedn_conn_ctx *conn_ctx)
{
	struct nvme_tcp_ofld_queue *queue;

	queue = conn_ctx->queue;
	queue->private_data = NULL;
}

void qedn_terminate_connection(struct qedn_conn_ctx *conn_ctx, int abrt_flag)
{
	struct qedn_ctrl *qctrl;

	if (!conn_ctx)
		return;

	qctrl = (struct qedn_ctrl *)conn_ctx->ctrl->private_data;

	if (test_and_set_bit(DESTROY_CONNECTION, &conn_ctx->agg_work_action))
		return;

	qedn_set_con_state(conn_ctx, CONN_STATE_DESTROY_CONNECTION);
	conn_ctx->abrt_flag = abrt_flag;

	queue_work(qctrl->sp_wq, &conn_ctx->sp_wq_entry);
}

static int qedn_nvmetcp_update_conn(struct qedn_ctx *qedn, struct qedn_conn_ctx *conn_ctx)
{
	struct qedn_negotiation_params *pdu_params = &conn_ctx->pdu_params;
	struct qed_nvmetcp_params_update *conn_info;
	int rc;

	conn_info = kzalloc(sizeof(*conn_info), GFP_KERNEL);
	if (!conn_info)
		return -ENOMEM;

	conn_info->hdr_digest_en = pdu_params->hdr_digest;
	conn_info->data_digest_en = pdu_params->data_digest;
	conn_info->max_recv_pdu_length = QEDN_MAX_PDU_SIZE;
	conn_info->max_io_size = QEDN_MAX_IO_SIZE;
	conn_info->max_send_pdu_length = pdu_params->maxh2cdata;

	rc = qed_ops->update_conn(qedn->cdev, conn_ctx->conn_handle, conn_info);
	if (rc) {
		pr_err("Could not update connection\n");
		rc = -ENXIO;
	}

	kfree(conn_info);

	return rc;
}

static int qedn_update_ramrod(struct qedn_conn_ctx *conn_ctx)
{
	struct qedn_ctx *qedn = conn_ctx->qedn;
	int rc = 0;

	rc = qedn_set_con_state(conn_ctx, CONN_STATE_WAIT_FOR_UPDATE_EQE);
	if (rc)
		return rc;

	rc = qedn_nvmetcp_update_conn(qedn, conn_ctx);
	if (rc)
		return rc;

	if (conn_ctx->state != CONN_STATE_WAIT_FOR_UPDATE_EQE) {
		pr_err("cid 0x%x: Unexpected state 0x%x after update ramrod\n",
		       conn_ctx->fw_cid, conn_ctx->state);

		return -EINVAL;
	}

	return rc;
}

static int qedn_send_icreq(struct qedn_conn_ctx *conn_ctx)
{
	struct nvmetcp_init_conn_req_hdr *icreq_ptr = NULL;
	struct storage_sgl_task_params *sgl_task_params;
	struct nvmetcp_task_params task_params;
	struct qedn_task_ctx *qedn_task = NULL;
	struct nvme_tcp_icreq_pdu icreq;
	struct nvmetcp_wqe *chain_sqe;
	struct nvmetcp_wqe local_sqe;

	qedn_task = qedn_get_task_from_pool_insist(conn_ctx, QEDN_ICREQ_CCCID);
	if (!qedn_task)
		return -EINVAL;

	memset(&icreq, 0, sizeof(icreq));
	memset(&local_sqe, 0, sizeof(local_sqe));

	/* Initialize ICReq */
	icreq.hdr.type = nvme_tcp_icreq;
	icreq.hdr.hlen = sizeof(icreq);
	icreq.hdr.pdo = 0;
	icreq.hdr.plen = cpu_to_le32(icreq.hdr.hlen);
	icreq.pfv = cpu_to_le16(conn_ctx->required_params.pfv);
	icreq.maxr2t = cpu_to_le32(conn_ctx->required_params.maxr2t);
	icreq.hpda = conn_ctx->required_params.hpda;
	if (conn_ctx->required_params.hdr_digest)
		icreq.digest |= NVME_TCP_HDR_DIGEST_ENABLE;
	if (conn_ctx->required_params.data_digest)
		icreq.digest |= NVME_TCP_DATA_DIGEST_ENABLE;

	qedn_swap_bytes((u32 *)&icreq,
			(sizeof(icreq) - QEDN_ICREQ_FW_PAYLOAD) /
			 sizeof(u32));

	/* Initialize task params */
	task_params.opq.lo = cpu_to_le32(((u64)(qedn_task)) & 0xffffffff);
	task_params.opq.hi = cpu_to_le32(((u64)(qedn_task)) >> 32);
	task_params.context = qedn_task->fw_task_ctx;
	task_params.sqe = &local_sqe;
	task_params.conn_icid = (u16)conn_ctx->conn_handle;
	task_params.itid = qedn_task->itid;
	task_params.cq_rss_number = conn_ctx->default_cq;
	task_params.tx_io_size = QEDN_ICREQ_FW_PAYLOAD;
	task_params.rx_io_size = 0; /* Rx doesn't use SGL for icresp */

	/* Init SGE for ICReq padding */
	sgl_task_params = &qedn_task->sgl_task_params;
	sgl_task_params->total_buffer_size = task_params.tx_io_size;
	sgl_task_params->small_mid_sge = false;
	sgl_task_params->num_sges = 1;
	memcpy(sgl_task_params->sgl, &conn_ctx->icreq_pad->sge,
	       sizeof(conn_ctx->icreq_pad->sge));
	icreq_ptr = (struct nvmetcp_init_conn_req_hdr *)&icreq;

	qed_ops->init_icreq_exchange(&task_params, icreq_ptr, sgl_task_params,  NULL);

	qedn_set_con_state(conn_ctx, CONN_STATE_WAIT_FOR_IC_COMP);
	atomic_inc(&conn_ctx->num_active_fw_tasks);

	/* spin_lock - doorbell is accessed  both Rx flow and response flow */
	spin_lock(&conn_ctx->ep.doorbell_lock);
	chain_sqe = qed_chain_produce(&conn_ctx->ep.fw_sq_chain);
	memcpy(chain_sqe, &local_sqe, sizeof(local_sqe));
	qedn_ring_doorbell(conn_ctx);
	spin_unlock(&conn_ctx->ep.doorbell_lock);

	return 0;
}

void qedn_prep_icresp(struct qedn_conn_ctx *conn_ctx, struct nvmetcp_fw_cqe *cqe)
{
	struct nvmetcp_icresp_hdr_psh *icresp_from_cqe =
		(struct nvmetcp_icresp_hdr_psh *)&cqe->nvme_cqe;
	struct nvme_tcp_ofld_ctrl *ctrl = conn_ctx->ctrl;
	struct qedn_ctrl *qctrl = NULL;

	qctrl = (struct qedn_ctrl *)ctrl->private_data;

	memcpy(&conn_ctx->icresp, icresp_from_cqe, sizeof(conn_ctx->icresp));
	qedn_set_sp_wa(conn_ctx, HANDLE_ICRESP);
	queue_work(qctrl->sp_wq, &conn_ctx->sp_wq_entry);
}

static int qedn_handle_icresp(struct qedn_conn_ctx *conn_ctx)
{
	struct nvmetcp_icresp_hdr_psh *icresp = &conn_ctx->icresp;
	u16 pfv = __swab16(le16_to_cpu(icresp->pfv_swapped));
	int rc = 0;

	qedn_free_icreq_pad(conn_ctx);

	/* Validate ICResp */
	if (pfv != conn_ctx->required_params.pfv) {
		pr_err("cid %u: unsupported pfv %u\n", conn_ctx->fw_cid, pfv);

		return -EINVAL;
	}

	if (icresp->cpda > conn_ctx->required_params.cpda) {
		pr_err("cid %u: unsupported cpda %u\n", conn_ctx->fw_cid, icresp->cpda);

		return -EINVAL;
	}

	if ((NVME_TCP_HDR_DIGEST_ENABLE & icresp->digest) !=
	    conn_ctx->required_params.hdr_digest) {
		if ((NVME_TCP_HDR_DIGEST_ENABLE & icresp->digest) >
		    conn_ctx->required_params.hdr_digest) {
			pr_err("cid 0x%x: invalid header digest bit\n", conn_ctx->fw_cid);
		}
	}

	if ((NVME_TCP_DATA_DIGEST_ENABLE & icresp->digest) !=
	    conn_ctx->required_params.data_digest) {
		if ((NVME_TCP_DATA_DIGEST_ENABLE & icresp->digest) >
		    conn_ctx->required_params.data_digest) {
			pr_err("cid 0x%x: invalid data digest bit\n", conn_ctx->fw_cid);
	}
	}

	memset(&conn_ctx->pdu_params, 0, sizeof(conn_ctx->pdu_params));
	conn_ctx->pdu_params.maxh2cdata =
		__swab32(le32_to_cpu(icresp->maxdata_swapped));
	conn_ctx->pdu_params.maxh2cdata = QEDN_MAX_PDU_SIZE;
	if (conn_ctx->pdu_params.maxh2cdata > QEDN_MAX_PDU_SIZE)
		conn_ctx->pdu_params.maxh2cdata = QEDN_MAX_PDU_SIZE;

	conn_ctx->pdu_params.pfv = pfv;
	conn_ctx->pdu_params.cpda = icresp->cpda;
	conn_ctx->pdu_params.hpda = conn_ctx->required_params.hpda;
	conn_ctx->pdu_params.hdr_digest = NVME_TCP_HDR_DIGEST_ENABLE & icresp->digest;
	conn_ctx->pdu_params.data_digest = NVME_TCP_DATA_DIGEST_ENABLE & icresp->digest;
	conn_ctx->pdu_params.maxr2t = conn_ctx->required_params.maxr2t;
	rc = qedn_update_ramrod(conn_ctx);

	return rc;
}

/* Slowpath EQ Callback */
int qedn_event_cb(void *context, u8 fw_event_code, void *event_ring_data)
{
	struct nvmetcp_connect_done_results *eqe_connect_done;
	struct nvmetcp_eqe_data *eqe_data;
	struct nvme_tcp_ofld_ctrl *ctrl;
	struct qedn_conn_ctx *conn_ctx;
	struct qedn_ctrl *qctrl;
	struct qedn_ctx *qedn;
	u16 icid;
	int rc;

	if (!context || !event_ring_data) {
		pr_err("Recv event with ctx NULL\n");

		return -EINVAL;
	}

	qedn = (struct qedn_ctx *)context;

	if (fw_event_code != NVMETCP_EVENT_TYPE_ASYN_CONNECT_COMPLETE) {
		eqe_data = (struct nvmetcp_eqe_data *)event_ring_data;
		icid = le16_to_cpu(eqe_data->icid);
		pr_err("EQE Info: icid=0x%x, conn_id=0x%x, err-code=0x%x, err-pdu-opcode-reserved=0x%x\n",
		       eqe_data->icid, eqe_data->conn_id,
		       eqe_data->error_code,
		       eqe_data->error_pdu_opcode_reserved);
	} else {
		eqe_connect_done =
			(struct nvmetcp_connect_done_results *)event_ring_data;
		icid = le16_to_cpu(eqe_connect_done->icid);
	}

	conn_ctx = qedn_get_conn_hash(qedn, icid);
	if (!conn_ctx) {
		pr_err("Connection with icid=0x%x doesn't exist in conn list\n", icid);

		return -EINVAL;
	}

	ctrl = conn_ctx->ctrl;
	qctrl = (struct qedn_ctrl *)ctrl->private_data;

	switch (fw_event_code) {
	case NVMETCP_EVENT_TYPE_ASYN_CONNECT_COMPLETE:
		if (conn_ctx->state != CONN_STATE_WAIT_FOR_CONNECT_DONE) {
			pr_err("CID=0x%x - ASYN_CONNECT_COMPLETE: Unexpected connection state %u\n",
			       conn_ctx->fw_cid, conn_ctx->state);
		} else {
			rc = qedn_set_con_state(conn_ctx, CONN_STATE_OFFLOAD_COMPLETE);

			if (rc)
				return rc;

			qedn_set_sp_wa(conn_ctx, SEND_ICREQ);
			queue_work(qctrl->sp_wq, &conn_ctx->sp_wq_entry);
		}

		break;
	case NVMETCP_EVENT_TYPE_ASYN_TERMINATE_DONE:
		if (conn_ctx->state != CONN_STATE_WAIT_FOR_DESTROY_DONE)
			pr_err("CID=0x%x - ASYN_TERMINATE_DONE: Unexpected connection state %u\n",
			       conn_ctx->fw_cid, conn_ctx->state);
		else
			queue_work(qctrl->sp_wq, &conn_ctx->sp_wq_entry);

		break;
	default:
		pr_err("CID=0x%x - Recv Unknown Event %u\n", conn_ctx->fw_cid, fw_event_code);
		break;
	}

	return 0;
}

static int qedn_prep_and_offload_queue(struct qedn_conn_ctx *conn_ctx)
{
	struct qedn_ctx *qedn = conn_ctx->qedn;
	struct qedn_io_resources *io_resrc;
	struct qedn_fp_queue *fp_q;
	u8 default_cq_idx, qid;
	size_t dma_size;
	int rc;

	rc = qedn_alloc_fw_sq(qedn, &conn_ctx->ep);
	if (rc) {
		pr_err("Failed to allocate FW SQ\n");
		goto rel_conn;
	}

	set_bit(QEDN_CONN_RESRC_FW_SQ, &conn_ctx->resrc_state);
	spin_lock_init(&conn_ctx->ep.doorbell_lock);
	INIT_LIST_HEAD(&conn_ctx->host_pend_req_list);
	spin_lock_init(&conn_ctx->nvme_req_lock);
	atomic_set(&conn_ctx->num_active_tasks, 0);
	atomic_set(&conn_ctx->num_active_fw_tasks, 0);

	rc = qed_ops->acquire_conn(qedn->cdev,
				   &conn_ctx->conn_handle,
				   &conn_ctx->fw_cid,
				   &conn_ctx->ep.p_doorbell);
	if (rc) {
		pr_err("Couldn't acquire connection\n");
		goto rel_conn;
	}

	hash_add(qedn->conn_ctx_hash, &conn_ctx->hash_node,
		 conn_ctx->conn_handle);
	set_bit(QEDN_CONN_RESRC_ACQUIRE_CONN, &conn_ctx->resrc_state);

	qid = qedn_qid(conn_ctx->queue);
	default_cq_idx = qid ? qid - 1 : 0; /* Offset adminq */

	conn_ctx->default_cq = (default_cq_idx % qedn->num_fw_cqs);
	fp_q = &qedn->fp_q_arr[conn_ctx->default_cq];
	conn_ctx->fp_q = fp_q;
	io_resrc = &fp_q->host_resrc;

	/* The first connection on each fp_q will fill task
	 * resources
	 */
	spin_lock(&io_resrc->resources_lock);
	if (io_resrc->num_alloc_tasks == 0) {
		rc = qedn_alloc_tasks(conn_ctx);
		if (rc) {
			pr_err("Failed allocating tasks: CID=0x%x\n",
			       conn_ctx->fw_cid);
			spin_unlock(&io_resrc->resources_lock);
			goto rel_conn;
		}
	}
	spin_unlock(&io_resrc->resources_lock);

	spin_lock_init(&conn_ctx->task_list_lock);
	INIT_LIST_HEAD(&conn_ctx->active_task_list);
	set_bit(QEDN_CONN_RESRC_TASKS, &conn_ctx->resrc_state);

	rc = qedn_fetch_tcp_port(conn_ctx);
	if (rc)
		goto rel_conn;

	set_bit(QEDN_CONN_RESRC_TCP_PORT, &conn_ctx->resrc_state);
	dma_size = conn_ctx->sq_depth *
			   sizeof(struct nvmetcp_host_cccid_itid_entry);
	conn_ctx->host_cccid_itid =
			dma_alloc_coherent(&qedn->pdev->dev,
					   dma_size,
					   &conn_ctx->host_cccid_itid_phy_addr,
					   GFP_ATOMIC);
	if (!conn_ctx->host_cccid_itid) {
		pr_err("CCCID-iTID Map allocation failed\n");
		goto rel_conn;
	}

	memset(conn_ctx->host_cccid_itid, 0xFF, dma_size);
	set_bit(QEDN_CONN_RESRC_CCCID_ITID_MAP, &conn_ctx->resrc_state);

	rc = qedn_alloc_icreq_pad(conn_ctx);
		if (rc)
			goto rel_conn;

	rc = qedn_set_con_state(conn_ctx, CONN_STATE_WAIT_FOR_CONNECT_DONE);
	if (rc)
		goto rel_conn;

	rc = qedn_nvmetcp_offload_conn(conn_ctx);
	if (rc) {
		pr_err("Offload error: CID=0x%x\n", conn_ctx->fw_cid);
		goto rel_conn;
	}

	return 0;

rel_conn:
	pr_err("qedn create queue ended with ERROR\n");
	qedn_release_conn_ctx(conn_ctx);

	return -EINVAL;
}

void qedn_destroy_connection(struct qedn_conn_ctx *conn_ctx)
{
	struct qedn_ctx *qedn = conn_ctx->qedn;
	int rc;

	qedn_decouple_conn(conn_ctx);

	if (qedn_set_con_state(conn_ctx, CONN_STATE_WAIT_FOR_DESTROY_DONE))
		return;

	/* Placeholder - task cleanup */

	rc = qed_ops->destroy_conn(qedn->cdev, conn_ctx->conn_handle,
				   conn_ctx->abrt_flag);
	if (rc)
		pr_warn("destroy_conn failed - rc %u\n", rc);
}

void qedn_sp_wq_handler(struct work_struct *work)
{
	struct qedn_conn_ctx *conn_ctx;
	struct qedn_ctx *qedn;
	int rc;

	conn_ctx = container_of(work, struct qedn_conn_ctx, sp_wq_entry);
	qedn = conn_ctx->qedn;

	if (conn_ctx->state == CONN_STATE_DESTROY_COMPLETE) {
		pr_err("Connection already released!\n");

		return;
	}

	if (conn_ctx->state == CONN_STATE_WAIT_FOR_DESTROY_DONE) {
		qedn_release_conn_ctx(conn_ctx);

		return;
	}

	qedn = conn_ctx->qedn;
	if (test_bit(DESTROY_CONNECTION, &conn_ctx->agg_work_action)) {
		if (test_bit(HANDLE_ICRESP, &conn_ctx->agg_work_action))
			qedn_clr_sp_wa(conn_ctx, HANDLE_ICRESP);

		qedn_destroy_connection(conn_ctx);

		return;
	}

	if (test_bit(CREATE_CONNECTION, &conn_ctx->agg_work_action)) {
		qedn_clr_sp_wa(conn_ctx, CREATE_CONNECTION);
		rc = qedn_prep_and_offload_queue(conn_ctx);
		if (rc) {
			pr_err("Error in queue prepare & firmware offload\n");

			return;
		}
	}

	if (test_bit(SEND_ICREQ, &conn_ctx->agg_work_action)) {
		qedn_clr_sp_wa(conn_ctx, SEND_ICREQ);
		rc = qedn_send_icreq(conn_ctx);
		if (rc)
			return;

		return;
	}

	if (test_bit(HANDLE_ICRESP, &conn_ctx->agg_work_action)) {
		rc = qedn_handle_icresp(conn_ctx);

		qedn_clr_sp_wa(conn_ctx, HANDLE_ICRESP);
		if (rc) {
			pr_err("IC handling returned with 0x%x\n", rc);
			if (test_and_set_bit(DESTROY_CONNECTION, &conn_ctx->agg_work_action))
				return;

			qedn_destroy_connection(conn_ctx);

			return;
		}

		atomic_inc(&conn_ctx->est_conn_indicator);
		qedn_set_con_state(conn_ctx, CONN_STATE_NVMETCP_CONN_ESTABLISHED);
		wake_up_interruptible(&conn_ctx->conn_waitq);

		return;
	}
}

/* Clear connection aggregative slowpath work action */
void qedn_clr_sp_wa(struct qedn_conn_ctx *conn_ctx, u32 bit)
{
	clear_bit(bit, &conn_ctx->agg_work_action);
}

/* Set connection aggregative slowpath work action */
void qedn_set_sp_wa(struct qedn_conn_ctx *conn_ctx, u32 bit)
{
	set_bit(bit, &conn_ctx->agg_work_action);
}
