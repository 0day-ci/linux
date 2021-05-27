// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

 /* Kernel includes */
#include <linux/kernel.h>
#include <linux/module.h>

/* Driver includes */
#include "qedn.h"

#define CHIP_NUM_AHP_NVMETCP 0x8194

const struct qed_nvmetcp_ops *qed_ops;

/* Global context instance */
static struct pci_device_id qedn_pci_tbl[] = {
	{ PCI_VDEVICE(QLOGIC, CHIP_NUM_AHP_NVMETCP), 0 },
	{0, 0},
};

__be16 qedn_get_in_port(struct sockaddr_storage *sa)
{
	return sa->ss_family == AF_INET
		? ((struct sockaddr_in *)sa)->sin_port
		: ((struct sockaddr_in6 *)sa)->sin6_port;
}

struct qedn_llh_filter *qedn_add_llh_filter(struct qedn_ctx *qedn, u16 tcp_port)
{
	struct qedn_llh_filter *llh_filter = NULL;
	struct qedn_llh_filter *llh_tmp = NULL;
	bool new_filter = 1;
	int rc = 0;

	/* Check if LLH filter already defined */
	list_for_each_entry_safe(llh_filter, llh_tmp, &qedn->llh_filter_list, entry) {
		if (llh_filter->port == tcp_port) {
			new_filter = 0;
			llh_filter->ref_cnt++;
			break;
		}
	}

	if (new_filter) {
		if (qedn->num_llh_filters >= QEDN_MAX_LLH_PORTS) {
			pr_err("PF reached the max target ports limit %u. %u\n",
			       qedn->dev_info.common.abs_pf_id,
			       qedn->num_llh_filters);

			return NULL;
		}

		rc = qed_ops->add_src_tcp_port_filter(qedn->cdev, tcp_port);
		if (rc) {
			pr_err("LLH port configuration failed. port:%u; rc:%u\n", tcp_port, rc);

			return NULL;
		}

		llh_filter = kzalloc(sizeof(*llh_filter), GFP_KERNEL);
		if (!llh_filter) {
			qed_ops->remove_src_tcp_port_filter(qedn->cdev, tcp_port);

			return NULL;
		}

		llh_filter->port = tcp_port;
		llh_filter->ref_cnt = 1;
		++qedn->num_llh_filters;
		list_add_tail(&llh_filter->entry, &qedn->llh_filter_list);
		set_bit(QEDN_STATE_LLH_PORT_FILTER_SET, &qedn->state);
	}

	return llh_filter;
}

void qedn_dec_llh_filter(struct qedn_ctx *qedn, struct qedn_llh_filter *llh_filter)
{
	if (!llh_filter)
		return;

	llh_filter->ref_cnt--;
	if (!llh_filter->ref_cnt) {
		list_del(&llh_filter->entry);

		/* Remove LLH protocol port filter */
		qed_ops->remove_src_tcp_port_filter(qedn->cdev, llh_filter->port);

		--qedn->num_llh_filters;
		kfree(llh_filter);
		if (!qedn->num_llh_filters)
			clear_bit(QEDN_STATE_LLH_PORT_FILTER_SET, &qedn->state);
	}
}

static bool qedn_matches_qede(struct qedn_ctx *qedn, struct pci_dev *qede_pdev)
{
	struct pci_dev *qedn_pdev = qedn->pdev;

	return (qede_pdev->bus->number == qedn_pdev->bus->number &&
		PCI_SLOT(qede_pdev->devfn) == PCI_SLOT(qedn_pdev->devfn) &&
		PCI_FUNC(qede_pdev->devfn) == qedn->dev_info.port_id);
}

static int
qedn_find_dev(struct nvme_tcp_ofld_dev *dev,
	      struct nvme_tcp_ofld_ctrl_con_params *conn_params,
	      struct qedn_ctrl *qctrl)
{
	struct pci_dev *qede_pdev = NULL;
	struct sockaddr remote_mac_addr;
	struct net_device *ndev = NULL;
	struct qedn_ctx *qedn = NULL;
	u16 vlan_id = 0;
	int rc = 0;

	/* qedn utilizes host network stack through paired qede device for
	 * non-offload traffic. First we verify there is valid route to remote
	 * peer.
	 */
	if (conn_params->remote_ip_addr.ss_family == AF_INET) {
		rc = qed_route_ipv4(&conn_params->local_ip_addr,
				    &conn_params->remote_ip_addr,
				    &remote_mac_addr, &ndev);
	} else if (conn_params->remote_ip_addr.ss_family == AF_INET6) {
		rc = qed_route_ipv6(&conn_params->local_ip_addr,
				    &conn_params->remote_ip_addr,
				    &remote_mac_addr, &ndev);
	} else {
		pr_err("address family %d not supported\n",
		       conn_params->remote_ip_addr.ss_family);

		return false;
	}

	if (rc)
		return false;

	qed_vlan_get_ndev(&ndev, &vlan_id);

	if (qctrl) {
		qctrl->remote_mac_addr = remote_mac_addr;
		qctrl->vlan_id = vlan_id;
	}

	dev->ndev = ndev;

	/* route found through ndev - validate this is qede*/
	qede_pdev = qed_validate_ndev(ndev);
	if (!qede_pdev)
		return false;

	qedn = container_of(dev, struct qedn_ctx, qedn_ofld_dev);
	if (!qedn)
		return false;

	if (!qedn_matches_qede(qedn, qede_pdev))
		return false;

	return true;
}

static int
qedn_claim_dev(struct nvme_tcp_ofld_dev *dev,
	       struct nvme_tcp_ofld_ctrl_con_params *conn_params)
{
	return qedn_find_dev(dev, conn_params, NULL);
}

static int qedn_setup_ctrl(struct nvme_tcp_ofld_ctrl *ctrl)
{
	struct nvme_tcp_ofld_dev *dev = ctrl->dev;
	struct qedn_llh_filter *llh_filter = NULL;
	struct qedn_ctrl *qctrl = NULL;
	struct qedn_ctx *qedn = NULL;
	__be16 remote_port;
	bool new = true;
	int rc = 0;

	if (ctrl->private_data) {
		qctrl = (struct qedn_ctrl *)ctrl->private_data;
		new = false;
	}

	if (new) {
		qctrl = kzalloc(sizeof(*qctrl), GFP_KERNEL);
		if (!qctrl)
			return -ENOMEM;

		ctrl->private_data = (void *)qctrl;
		set_bit(QEDN_CTRL_SET_TO_OFLD_CTRL, &qctrl->agg_state);

		qctrl->sp_wq = alloc_workqueue(QEDN_SP_WORKQUEUE, WQ_MEM_RECLAIM,
					       QEDN_SP_WORKQUEUE_MAX_ACTIVE);
		if (!qctrl->sp_wq) {
			rc = -ENODEV;
			pr_err("Unable to create slowpath work queue!\n");
			kfree(qctrl);

			return rc;
		}

		set_bit(QEDN_STATE_SP_WORK_THREAD_SET, &qctrl->agg_state);
	}

	if (!qedn_find_dev(dev, &ctrl->conn_params, qctrl)) {
		rc = -ENODEV;
		goto err_out;
	}

	qedn = container_of(dev, struct qedn_ctx, qedn_ofld_dev);
	qctrl->qedn = qedn;

	if (qedn->num_llh_filters == 0) {
		qedn->mtu = dev->ndev->mtu;
		memcpy(qedn->local_mac_addr, dev->ndev->dev_addr, ETH_ALEN);
	}

	remote_port = qedn_get_in_port(&ctrl->conn_params.remote_ip_addr);
	if (new) {
		llh_filter = qedn_add_llh_filter(qedn, ntohs(remote_port));
		if (!llh_filter) {
			rc = -EFAULT;
			goto err_out;
		}

		qctrl->llh_filter = llh_filter;
		set_bit(LLH_FILTER, &qctrl->agg_state);
	}

	return 0;
err_out:
	flush_workqueue(qctrl->sp_wq);
	kfree(qctrl);

	return rc;
}

static int qedn_release_ctrl(struct nvme_tcp_ofld_ctrl *ctrl)
{
	struct qedn_ctrl *qctrl = (struct qedn_ctrl *)ctrl->private_data;

	if (test_and_clear_bit(LLH_FILTER, &qctrl->agg_state) &&
	    qctrl->llh_filter) {
		qedn_dec_llh_filter(qctrl->qedn, qctrl->llh_filter);
		qctrl->llh_filter = NULL;
	}

	if (test_and_clear_bit(QEDN_STATE_SP_WORK_THREAD_SET, &qctrl->agg_state))
		flush_workqueue(qctrl->sp_wq);

	if (test_and_clear_bit(QEDN_CTRL_SET_TO_OFLD_CTRL, &qctrl->agg_state)) {
		kfree(qctrl);
		ctrl->private_data = NULL;
	}

	return 0;
}

static void qedn_set_ctrl_io_cpus(struct qedn_conn_ctx *conn_ctx, int qid)
{
	struct qedn_ctx *qedn = conn_ctx->qedn;
	struct qedn_fp_queue *fp_q = NULL;
	int index;

	index = qid ? (qid - 1) % qedn->num_fw_cqs : 0;
	fp_q = &qedn->fp_q_arr[index];

	conn_ctx->cpu = fp_q->cpu;
}

static int qedn_create_queue(struct nvme_tcp_ofld_queue *queue, int qid,
			     size_t queue_size)
{
	struct nvme_tcp_ofld_ctrl *ctrl = queue->ctrl;
	struct nvme_ctrl *nctrl = &ctrl->nctrl;
	struct qedn_conn_ctx *conn_ctx;
	struct qedn_ctrl *qctrl;
	struct qedn_ctx *qedn;
	int rc;

	qctrl = (struct qedn_ctrl *)ctrl->private_data;
	qedn = qctrl->qedn;

	/* Allocate qedn connection context */
	conn_ctx = kzalloc(sizeof(*conn_ctx), GFP_KERNEL);
	if (!conn_ctx)
		return -ENOMEM;

	queue->private_data = conn_ctx;
	queue->hdr_digest = nctrl->opts->hdr_digest;
	queue->data_digest = nctrl->opts->data_digest;
	queue->tos = nctrl->opts->tos;

	conn_ctx->qedn = qedn;
	conn_ctx->queue = queue;
	conn_ctx->ctrl = ctrl;
	conn_ctx->sq_depth = queue_size;
	mutex_init(&conn_ctx->send_mutex);
	qedn_set_ctrl_io_cpus(conn_ctx, qid);

	init_waitqueue_head(&conn_ctx->conn_waitq);
	atomic_set(&conn_ctx->est_conn_indicator, 0);
	atomic_set(&conn_ctx->destroy_conn_indicator, 0);

	spin_lock_init(&conn_ctx->conn_state_lock);

	conn_ctx->qid = qid;

	qedn_initialize_endpoint(&conn_ctx->ep, qedn->local_mac_addr, ctrl);

	atomic_inc(&qctrl->host_num_active_conns);

	qedn_set_sp_wa(conn_ctx, CREATE_CONNECTION);
	qedn_set_con_state(conn_ctx, CONN_STATE_CREATE_CONNECTION);
	INIT_WORK(&conn_ctx->sp_wq_entry, qedn_sp_wq_handler);
	queue_work(qctrl->sp_wq, &conn_ctx->sp_wq_entry);

	/* Wait for the connection establishment to complete - this includes the
	 * FW TCP connection establishment and the NVMeTCP ICReq & ICResp
	 */
	rc = qedn_wait_for_conn_est(conn_ctx);
	if (rc)
		return -ENXIO;

	return 0;
}

static void qedn_drain_queue(struct nvme_tcp_ofld_queue *queue)
{
	struct qedn_conn_ctx *conn_ctx;

	if (!queue) {
		pr_err("ctrl has no queues\n");

		return;
	}

	conn_ctx = (struct qedn_conn_ctx *)queue->private_data;
	if (!conn_ctx)
		return;

	qedn_cleanp_fw(conn_ctx);
}

#define ATOMIC_READ_DESTROY_IND atomic_read(&conn_ctx->destroy_conn_indicator)
#define TERMINATE_TIMEOUT msecs_to_jiffies(QEDN_RLS_CONS_TMO)
static inline void
qedn_queue_wait_for_terminate_complete(struct qedn_conn_ctx *conn_ctx)
{
	/* Returns valid non-0 */
	int wrc, state;

	wrc = wait_event_interruptible_timeout(conn_ctx->conn_waitq,
					       ATOMIC_READ_DESTROY_IND > 0,
					       TERMINATE_TIMEOUT);

	atomic_set(&conn_ctx->destroy_conn_indicator, 0);

	spin_lock_bh(&conn_ctx->conn_state_lock);
	state = conn_ctx->state;
	spin_unlock_bh(&conn_ctx->conn_state_lock);

	if (!wrc  || state != CONN_STATE_DESTROY_COMPLETE)
		pr_warn("Timed out waiting for clear-SQ on FW conns");
}

static void qedn_destroy_queue(struct nvme_tcp_ofld_queue *queue)
{
	struct qedn_conn_ctx *conn_ctx;

	if (!queue) {
		pr_err("ctrl has no queues\n");

		return;
	}

	conn_ctx = (struct qedn_conn_ctx *)queue->private_data;
	if (!conn_ctx)
		return;

	qedn_terminate_connection(conn_ctx);

	qedn_queue_wait_for_terminate_complete(conn_ctx);

	kfree(conn_ctx);
}

static int qedn_poll_queue(struct nvme_tcp_ofld_queue *queue)
{
	/*
	 * Poll queue support will be added as part of future
	 * enhancements.
	 */

	return 0;
}

int qedn_process_request(struct qedn_conn_ctx *qedn_conn,
			 struct nvme_tcp_ofld_req *req)
{
	int rc = 0;

	mutex_lock(&qedn_conn->send_mutex);
	rc = qedn_queue_request(qedn_conn, req);
	mutex_unlock(&qedn_conn->send_mutex);

	return rc;
}

static int qedn_send_req(struct nvme_tcp_ofld_req *req)
{
	struct qedn_conn_ctx *qedn_conn = (struct qedn_conn_ctx *)req->queue->private_data;
	struct request *rq;

	rq = blk_mq_rq_from_pdu(req);

	/* Under the assumption that the cccid/tag will be in the range of 0 to sq_depth-1. */
	if (!req->async && qedn_validate_cccid_in_range(qedn_conn, rq->tag))
		return BLK_STS_NOTSUPP;

	return qedn_process_request(qedn_conn, req);
}

static struct nvme_tcp_ofld_ops qedn_ofld_ops = {
	.name = "qedn",
	.module = THIS_MODULE,
	.required_opts = NVMF_OPT_TRADDR,
	.allowed_opts = NVMF_OPT_TRSVCID | NVMF_OPT_NR_WRITE_QUEUES |
			NVMF_OPT_HOST_TRADDR | NVMF_OPT_CTRL_LOSS_TMO |
			NVMF_OPT_RECONNECT_DELAY,
		/* These flags will be as part of future enhancements
		 *	NVMF_OPT_HDR_DIGEST | NVMF_OPT_DATA_DIGEST |
		 *	NVMF_OPT_NR_POLL_QUEUES | NVMF_OPT_TOS
		 */
	.claim_dev = qedn_claim_dev,
	.setup_ctrl = qedn_setup_ctrl,
	.release_ctrl = qedn_release_ctrl,
	.create_queue = qedn_create_queue,
	.drain_queue = qedn_drain_queue,
	.destroy_queue = qedn_destroy_queue,
	.poll_queue = qedn_poll_queue,
	.send_req = qedn_send_req,
};

struct qedn_conn_ctx *qedn_get_conn_hash(struct qedn_ctx *qedn, u16 icid)
{
	struct qedn_conn_ctx *conn = NULL;

	hash_for_each_possible(qedn->conn_ctx_hash, conn, hash_node, icid) {
		if (conn->conn_handle == icid)
			break;
	}

	if (!conn || conn->conn_handle != icid)
		return NULL;

	return conn;
}

/* Fastpath IRQ handler */
void qedn_fw_cq_fp_handler(struct qedn_fp_queue *fp_q)
{
	u16 sb_id, cq_prod_idx, cq_cons_idx;
	struct qedn_ctx *qedn = fp_q->qedn;
	struct nvmetcp_fw_cqe *cqe = NULL;

	sb_id = fp_q->sb_id;
	qed_sb_update_sb_idx(fp_q->sb_info);

	/* rmb - to prevent missing new cqes */
	rmb();

	/* Read the latest cq_prod from the SB */
	cq_prod_idx = *fp_q->cq_prod;
	cq_cons_idx = qed_chain_get_cons_idx(&fp_q->cq_chain);

	while (cq_cons_idx != cq_prod_idx) {
		cqe = qed_chain_consume(&fp_q->cq_chain);
		if (likely(cqe))
			qedn_io_work_cq(qedn, cqe);
		else
			pr_err("Failed consuming cqe\n");

		cq_cons_idx = qed_chain_get_cons_idx(&fp_q->cq_chain);

		/* Check if new completions were posted */
		if (unlikely(cq_prod_idx == cq_cons_idx)) {
			/* rmb - to prevent missing new cqes */
			rmb();

			/* Update the latest cq_prod from the SB */
			cq_prod_idx = *fp_q->cq_prod;
		}
	}
}

static void qedn_fw_cq_fq_wq_handler(struct work_struct *work)
{
	struct qedn_fp_queue *fp_q = container_of(work, struct qedn_fp_queue, fw_cq_fp_wq_entry);

	qedn_fw_cq_fp_handler(fp_q);
	qed_sb_ack(fp_q->sb_info, IGU_INT_ENABLE, 1);
}

static irqreturn_t qedn_irq_handler(int irq, void *dev_id)
{
	struct qedn_fp_queue *fp_q = dev_id;
	struct qedn_ctx *qedn = fp_q->qedn;

	fp_q->cpu = smp_processor_id();

	qed_sb_ack(fp_q->sb_info, IGU_INT_DISABLE, 0);
	queue_work_on(fp_q->cpu, qedn->fw_cq_fp_wq, &fp_q->fw_cq_fp_wq_entry);

	return IRQ_HANDLED;
}

static void qedn_sync_free_irqs(struct qedn_ctx *qedn)
{
	u16 vector_idx;
	int i;

	for (i = 0; i < qedn->num_fw_cqs; i++) {
		vector_idx = i * qedn->dev_info.common.num_hwfns +
			     qed_ops->common->get_affin_hwfn_idx(qedn->cdev);
		synchronize_irq(qedn->int_info.msix[vector_idx].vector);
		irq_set_affinity_hint(qedn->int_info.msix[vector_idx].vector,
				      NULL);
		free_irq(qedn->int_info.msix[vector_idx].vector,
			 &qedn->fp_q_arr[i]);
	}

	qedn->int_info.used_cnt = 0;
	qed_ops->common->set_fp_int(qedn->cdev, 0);
}

static int qedn_request_msix_irq(struct qedn_ctx *qedn)
{
	struct pci_dev *pdev = qedn->pdev;
	struct qedn_fp_queue *fp_q = NULL;
	int i, rc, cpu;
	u16 vector_idx;
	u32 vector;

	/* numa-awareness will be added in future enhancements */
	cpu = cpumask_first(cpu_online_mask);
	for (i = 0; i < qedn->num_fw_cqs; i++) {
		fp_q = &qedn->fp_q_arr[i];
		vector_idx = i * qedn->dev_info.common.num_hwfns +
			     qed_ops->common->get_affin_hwfn_idx(qedn->cdev);
		vector = qedn->int_info.msix[vector_idx].vector;
		sprintf(fp_q->irqname, "qedn_queue_%x.%x.%x_%d",
			pdev->bus->number, PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn), i);
		rc = request_irq(vector, qedn_irq_handler, QEDN_IRQ_NO_FLAGS,
				 fp_q->irqname, fp_q);
		if (rc) {
			pr_err("request_irq failed.\n");
			qedn_sync_free_irqs(qedn);

			return rc;
		}

		fp_q->cpu = cpu;
		qedn->int_info.used_cnt++;
		rc = irq_set_affinity_hint(vector, get_cpu_mask(cpu));
		cpu = cpumask_next_wrap(cpu, cpu_online_mask, -1, false);
	}

	return 0;
}

static int qedn_setup_irq(struct qedn_ctx *qedn)
{
	int rc = 0;
	u8 rval;

	rval = qed_ops->common->set_fp_int(qedn->cdev, qedn->num_fw_cqs);
	if (rval < qedn->num_fw_cqs) {
		qedn->num_fw_cqs = rval;
		if (rval == 0) {
			pr_err("set_fp_int return 0 IRQs\n");

			return -ENODEV;
		}
	}

	rc = qed_ops->common->get_fp_int(qedn->cdev, &qedn->int_info);
	if (rc) {
		pr_err("get_fp_int failed\n");
		goto exit_setup_int;
	}

	if (qedn->int_info.msix_cnt) {
		rc = qedn_request_msix_irq(qedn);
		goto exit_setup_int;
	} else {
		pr_err("msix_cnt = 0\n");
		rc = -EINVAL;
		goto exit_setup_int;
	}

exit_setup_int:

	return rc;
}

static inline void qedn_init_pf_struct(struct qedn_ctx *qedn)
{
	INIT_LIST_HEAD(&qedn->llh_filter_list);
	qedn->num_llh_filters = 0;
	hash_init(qedn->conn_ctx_hash);
}

static inline void
qedn_init_core_probe_params(struct qed_probe_params *probe_params)
{
	memset(probe_params, 0, sizeof(*probe_params));
	probe_params->protocol = QED_PROTOCOL_NVMETCP;
	probe_params->is_vf = false;
	probe_params->recov_in_prog = 0;
}

static inline int qedn_core_probe(struct qedn_ctx *qedn)
{
	struct qed_probe_params probe_params;
	int rc = 0;

	qedn_init_core_probe_params(&probe_params);
	pr_info("Starting QED probe\n");
	qedn->cdev = qed_ops->common->probe(qedn->pdev, &probe_params);
	if (!qedn->cdev) {
		rc = -ENODEV;
		pr_err("QED probe failed\n");
	}

	return rc;
}

static void qedn_free_function_queues(struct qedn_ctx *qedn)
{
	struct qed_sb_info *sb_info = NULL;
	struct qedn_fp_queue *fp_q;
	int i;

	/* Free workqueues */
	destroy_workqueue(qedn->fw_cq_fp_wq);
	qedn->fw_cq_fp_wq = NULL;

	/* Free the fast path queues*/
	for (i = 0; i < qedn->num_fw_cqs; i++) {
		fp_q = &qedn->fp_q_arr[i];

		/* Free SB */
		sb_info = fp_q->sb_info;
		if (sb_info->sb_virt) {
			qed_ops->common->sb_release(qedn->cdev, sb_info,
						    fp_q->sb_id,
						    QED_SB_TYPE_STORAGE);
			dma_free_coherent(&qedn->pdev->dev,
					  sizeof(*sb_info->sb_virt),
					  (void *)sb_info->sb_virt,
					  sb_info->sb_phys);
			memset(sb_info, 0, sizeof(*sb_info));
			kfree(sb_info);
			fp_q->sb_info = NULL;
		}

		qed_ops->common->chain_free(qedn->cdev, &fp_q->cq_chain);
	}

	if (qedn->fw_cq_array_virt)
		dma_free_coherent(&qedn->pdev->dev,
				  qedn->num_fw_cqs * sizeof(u64),
				  qedn->fw_cq_array_virt,
				  qedn->fw_cq_array_phy);
	kfree(qedn->fp_q_arr);
	qedn->fp_q_arr = NULL;
}

static int qedn_alloc_and_init_sb(struct qedn_ctx *qedn,
				  struct qed_sb_info *sb_info, u16 sb_id)
{
	int rc = 0;

	sb_info->sb_virt = dma_alloc_coherent(&qedn->pdev->dev,
					      sizeof(struct status_block_e4),
					      &sb_info->sb_phys, GFP_KERNEL);
	if (!sb_info->sb_virt) {
		pr_err("Status block allocation failed\n");

		return -ENOMEM;
	}

	rc = qed_ops->common->sb_init(qedn->cdev, sb_info, sb_info->sb_virt,
				      sb_info->sb_phys, sb_id,
				      QED_SB_TYPE_STORAGE);
	if (rc) {
		pr_err("Status block initialization failed\n");

		return rc;
	}

	return 0;
}

static int qedn_alloc_function_queues(struct qedn_ctx *qedn)
{
	struct qed_chain_init_params chain_params = {};
	struct status_block_e4 *sb = NULL;
	struct qedn_fp_queue *fp_q = NULL;
	int rc = 0, arr_size;
	u64 cq_phy_addr;
	int i;

	qedn->fw_cq_fp_wq = alloc_workqueue(QEDN_FW_CQ_FP_WQ_WORKQUEUE,
					    WQ_HIGHPRI | WQ_MEM_RECLAIM, 0);
	if (!qedn->fw_cq_fp_wq) {
		rc = -ENODEV;
		pr_err("Unable to create fastpath FW CQ workqueue!\n");

		return rc;
	}

	qedn->fp_q_arr = kcalloc(qedn->num_fw_cqs,
				 sizeof(struct qedn_fp_queue), GFP_KERNEL);
	if (!qedn->fp_q_arr)
		return -ENOMEM;

	arr_size = qedn->num_fw_cqs * sizeof(struct nvmetcp_glbl_queue_entry);
	qedn->fw_cq_array_virt = dma_alloc_coherent(&qedn->pdev->dev,
						    arr_size,
						    &qedn->fw_cq_array_phy,
						    GFP_KERNEL);
	if (!qedn->fw_cq_array_virt) {
		rc = -ENOMEM;
		goto mem_alloc_failure;
	}

	/* placeholder - create task pools */

	for (i = 0; i < qedn->num_fw_cqs; i++) {
		fp_q = &qedn->fp_q_arr[i];
		mutex_init(&fp_q->cq_mutex);

		/* FW CQ */
		chain_params.intended_use = QED_CHAIN_USE_TO_CONSUME,
		chain_params.mode = QED_CHAIN_MODE_PBL,
		chain_params.cnt_type = QED_CHAIN_CNT_TYPE_U16,
		chain_params.num_elems = QEDN_FW_CQ_SIZE;
		chain_params.elem_size = sizeof(struct nvmetcp_fw_cqe);

		rc = qed_ops->common->chain_alloc(qedn->cdev,
						  &fp_q->cq_chain,
						  &chain_params);
		if (rc) {
			pr_err("CQ chain pci_alloc_consistent fail\n");
			goto mem_alloc_failure;
		}

		cq_phy_addr = qed_chain_get_pbl_phys(&fp_q->cq_chain);
		qedn->fw_cq_array_virt[i].cq_pbl_addr.hi = PTR_HI(cq_phy_addr);
		qedn->fw_cq_array_virt[i].cq_pbl_addr.lo = PTR_LO(cq_phy_addr);

		/* SB */
		fp_q->sb_info = kzalloc(sizeof(*fp_q->sb_info), GFP_KERNEL);
		if (!fp_q->sb_info)
			goto mem_alloc_failure;

		fp_q->sb_id = i;
		rc = qedn_alloc_and_init_sb(qedn, fp_q->sb_info, fp_q->sb_id);
		if (rc) {
			pr_err("SB allocation and initialization failed.\n");
			goto mem_alloc_failure;
		}

		sb = fp_q->sb_info->sb_virt;
		fp_q->cq_prod = (u16 *)&sb->pi_array[QEDN_PROTO_CQ_PROD_IDX];
		fp_q->qedn = qedn;
		INIT_WORK(&fp_q->fw_cq_fp_wq_entry, qedn_fw_cq_fq_wq_handler);

		/* Placeholder - Init IO-path resources */
	}

	return 0;

mem_alloc_failure:
	pr_err("Function allocation failed\n");
	qedn_free_function_queues(qedn);

	return rc;
}

static int qedn_set_nvmetcp_pf_param(struct qedn_ctx *qedn)
{
	u32 fw_conn_queue_pages = QEDN_NVMETCP_NUM_FW_CONN_QUEUE_PAGES;
	struct qed_nvmetcp_pf_params *pf_params;
	int rc;

	pf_params = &qedn->pf_params.nvmetcp_pf_params;
	memset(pf_params, 0, sizeof(*pf_params));
	qedn->num_fw_cqs = min_t(u8, qedn->dev_info.num_cqs, num_online_cpus());
	pr_info("Num qedn FW CQs %u\n", qedn->num_fw_cqs);

	pf_params->num_cons = QEDN_MAX_CONNS_PER_PF;
	pf_params->num_tasks = QEDN_MAX_TASKS_PER_PF;

	rc = qedn_alloc_function_queues(qedn);
	if (rc) {
		pr_err("Global queue allocation failed.\n");
		goto err_alloc_mem;
	}

	set_bit(QEDN_STATE_FP_WORK_THREAD_SET, &qedn->state);

	/* Queues */
	pf_params->num_sq_pages_in_ring = fw_conn_queue_pages;
	pf_params->num_r2tq_pages_in_ring = fw_conn_queue_pages;
	pf_params->num_uhq_pages_in_ring = fw_conn_queue_pages;
	pf_params->num_queues = qedn->num_fw_cqs;
	pf_params->cq_num_entries = QEDN_FW_CQ_SIZE;
	pf_params->glbl_q_params_addr = qedn->fw_cq_array_phy;

	/* the CQ SB pi */
	pf_params->gl_rq_pi = QEDN_PROTO_CQ_PROD_IDX;

err_alloc_mem:

	return rc;
}

static inline int qedn_slowpath_start(struct qedn_ctx *qedn)
{
	struct qed_slowpath_params sp_params = {};
	int rc = 0;

	/* Start the Slowpath-process */
	sp_params.int_mode = QED_INT_MODE_MSIX;
	strscpy(sp_params.name, "qedn NVMeTCP", QED_DRV_VER_STR_SIZE);
	rc = qed_ops->common->slowpath_start(qedn->cdev, &sp_params);
	if (rc)
		pr_err("Cannot start slowpath\n");

	return rc;
}

static void __qedn_remove(struct pci_dev *pdev)
{
	struct qedn_ctx *qedn = pci_get_drvdata(pdev);
	int rc;

	pr_notice("Starting qedn_remove: abs PF id=%u\n",
		  qedn->dev_info.common.abs_pf_id);

	if (test_and_set_bit(QEDN_STATE_MODULE_REMOVE_ONGOING, &qedn->state)) {
		pr_err("Remove already ongoing\n");

		return;
	}

	if (test_and_clear_bit(QEDN_STATE_LLH_PORT_FILTER_SET, &qedn->state)) {
		pr_err("LLH port configuration removal. %d filters still set\n",
		       qedn->num_llh_filters);
		qed_ops->clear_all_filters(qedn->cdev);
	}

	if (test_and_clear_bit(QEDN_STATE_REGISTERED_OFFLOAD_DEV, &qedn->state))
		nvme_tcp_ofld_unregister_dev(&qedn->qedn_ofld_dev);

	if (test_and_clear_bit(QEDN_STATE_IRQ_SET, &qedn->state))
		qedn_sync_free_irqs(qedn);

	if (test_and_clear_bit(QEDN_STATE_NVMETCP_OPEN, &qedn->state))
		qed_ops->stop(qedn->cdev);

	if (test_and_clear_bit(QEDN_STATE_MFW_STATE, &qedn->state)) {
		rc = qed_ops->common->update_drv_state(qedn->cdev, false);
		if (rc)
			pr_err("Failed to send drv state to MFW\n");
	}

	if (test_and_clear_bit(QEDN_STATE_CORE_OPEN, &qedn->state))
		qed_ops->common->slowpath_stop(qedn->cdev);

	if (test_and_clear_bit(QEDN_STATE_FP_WORK_THREAD_SET, &qedn->state))
		qedn_free_function_queues(qedn);

	if (test_and_clear_bit(QEDN_STATE_CORE_PROBED, &qedn->state))
		qed_ops->common->remove(qedn->cdev);

	kfree(qedn);
	pr_notice("Ending qedn_remove successfully\n");
}

static void qedn_remove(struct pci_dev *pdev)
{
	__qedn_remove(pdev);
}

static void qedn_shutdown(struct pci_dev *pdev)
{
	__qedn_remove(pdev);
}

static struct qedn_ctx *qedn_alloc_ctx(struct pci_dev *pdev)
{
	struct qedn_ctx *qedn = NULL;

	qedn = kzalloc(sizeof(*qedn), GFP_KERNEL);
	if (!qedn)
		return NULL;

	qedn->pdev = pdev;
	pci_set_drvdata(pdev, qedn);

	return qedn;
}

static int __qedn_probe(struct pci_dev *pdev)
{
	struct qedn_ctx *qedn;
	int rc;

	pr_notice("Starting qedn probe\n");

	qedn = qedn_alloc_ctx(pdev);
	if (!qedn)
		return -ENODEV;

	qedn_init_pf_struct(qedn);

	/* QED probe */
	rc = qedn_core_probe(qedn);
	if (rc)
		goto exit_probe_and_release_mem;

	set_bit(QEDN_STATE_CORE_PROBED, &qedn->state);

	rc = qed_ops->fill_dev_info(qedn->cdev, &qedn->dev_info);
	if (rc) {
		pr_err("fill_dev_info failed\n");
		goto exit_probe_and_release_mem;
	}

	rc = qedn_set_nvmetcp_pf_param(qedn);
	if (rc)
		goto exit_probe_and_release_mem;

	qed_ops->common->update_pf_params(qedn->cdev, &qedn->pf_params);
	rc = qedn_slowpath_start(qedn);
	if (rc)
		goto exit_probe_and_release_mem;

	set_bit(QEDN_STATE_CORE_OPEN, &qedn->state);

	rc = qedn_setup_irq(qedn);
	if (rc)
		goto exit_probe_and_release_mem;

	set_bit(QEDN_STATE_IRQ_SET, &qedn->state);

	/* NVMeTCP start HW PF */
	rc = qed_ops->start(qedn->cdev,
			    NULL /* Placeholder for FW IO-path resources */,
			    qedn,
			    qedn_event_cb);
	if (rc) {
		rc = -ENODEV;
		pr_err("Cannot start NVMeTCP Function\n");
		goto exit_probe_and_release_mem;
	}

	set_bit(QEDN_STATE_NVMETCP_OPEN, &qedn->state);

	rc = qed_ops->common->update_drv_state(qedn->cdev, true);
	if (rc) {
		pr_err("Failed to send drv state to MFW\n");
		goto exit_probe_and_release_mem;
	}

	set_bit(QEDN_STATE_MFW_STATE, &qedn->state);

	qedn->qedn_ofld_dev.num_hw_vectors = qedn->num_fw_cqs;
	qedn->qedn_ofld_dev.ops = &qedn_ofld_ops;
	INIT_LIST_HEAD(&qedn->qedn_ofld_dev.entry);
	rc = nvme_tcp_ofld_register_dev(&qedn->qedn_ofld_dev);
	if (rc)
		goto exit_probe_and_release_mem;

	set_bit(QEDN_STATE_REGISTERED_OFFLOAD_DEV, &qedn->state);

	return 0;
exit_probe_and_release_mem:
	__qedn_remove(pdev);
	pr_err("probe ended with error\n");

	return rc;
}

static int qedn_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return __qedn_probe(pdev);
}

static struct pci_driver qedn_pci_driver = {
	.name     = QEDN_MODULE_NAME,
	.id_table = qedn_pci_tbl,
	.probe    = qedn_probe,
	.remove   = qedn_remove,
	.shutdown = qedn_shutdown,
};

static int __init qedn_init(void)
{
	int rc;

	qed_ops = qed_get_nvmetcp_ops();
	if (!qed_ops) {
		pr_err("Failed to get QED NVMeTCP ops\n");

		return -EINVAL;
	}

	rc = pci_register_driver(&qedn_pci_driver);
	if (rc) {
		pr_err("Failed to register pci driver\n");

		return -EINVAL;
	}

	pr_notice("driver loaded successfully\n");

	return 0;
}

static void __exit qedn_cleanup(void)
{
	pci_unregister_driver(&qedn_pci_driver);
	qed_put_nvmetcp_ops();
	pr_notice("Unloading qedn ended\n");
}

module_init(qedn_init);
module_exit(qedn_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: qede nvme-fabrics nvme-tcp-offload");
MODULE_DESCRIPTION("Marvell 25/50/100G NVMe-TCP Offload Host Driver");
MODULE_AUTHOR("Marvell");
