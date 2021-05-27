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

static int
qedn_claim_dev(struct nvme_tcp_ofld_dev *dev,
	       struct nvme_tcp_ofld_ctrl_con_params *conn_params)
{
	/* Placeholder - qedn_claim_dev */

	return 0;
}

static int qedn_setup_ctrl(struct nvme_tcp_ofld_ctrl *ctrl)
{
	/* Placeholder - qedn_setup_ctrl */

	return 0;
}

static int qedn_release_ctrl(struct nvme_tcp_ofld_ctrl *ctrl)
{
	/* Placeholder - qedn_release_ctrl */

	return 0;
}

static int qedn_create_queue(struct nvme_tcp_ofld_queue *queue, int qid,
			     size_t queue_size)
{
	/* Placeholder - qedn_create_queue */

	return 0;
}

static void qedn_drain_queue(struct nvme_tcp_ofld_queue *queue)
{
	/* Placeholder - qedn_drain_queue */
}

static void qedn_destroy_queue(struct nvme_tcp_ofld_queue *queue)
{
	/* Placeholder - qedn_destroy_queue */
}

static int qedn_poll_queue(struct nvme_tcp_ofld_queue *queue)
{
	/*
	 * Poll queue support will be added as part of future
	 * enhancements.
	 */

	return 0;
}

static int qedn_send_req(struct nvme_tcp_ofld_req *req)
{
	/* Placeholder - qedn_send_req */

	return 0;
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

static inline void qedn_init_pf_struct(struct qedn_ctx *qedn)
{
	/* Placeholder - Initialize qedn fields */
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

static int qedn_set_nvmetcp_pf_param(struct qedn_ctx *qedn)
{
	u32 fw_conn_queue_pages = QEDN_NVMETCP_NUM_FW_CONN_QUEUE_PAGES;
	struct qed_nvmetcp_pf_params *pf_params;

	pf_params = &qedn->pf_params.nvmetcp_pf_params;
	memset(pf_params, 0, sizeof(*pf_params));
	qedn->num_fw_cqs = min_t(u8, qedn->dev_info.num_cqs, num_online_cpus());

	pf_params->num_cons = QEDN_MAX_CONNS_PER_PF;
	pf_params->num_tasks = QEDN_MAX_TASKS_PER_PF;

	/* Placeholder - Initialize function level queues */

	/* Placeholder - Initialize TCP params */

	/* Queues */
	pf_params->num_sq_pages_in_ring = fw_conn_queue_pages;
	pf_params->num_r2tq_pages_in_ring = fw_conn_queue_pages;
	pf_params->num_uhq_pages_in_ring = fw_conn_queue_pages;
	pf_params->num_queues = qedn->num_fw_cqs;
	pf_params->cq_num_entries = QEDN_FW_CQ_SIZE;

	/* the CQ SB pi */
	pf_params->gl_rq_pi = QEDN_PROTO_CQ_PROD_IDX;

	return 0;
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

	if (test_and_clear_bit(QEDN_STATE_REGISTERED_OFFLOAD_DEV, &qedn->state))
		nvme_tcp_ofld_unregister_dev(&qedn->qedn_ofld_dev);

	if (test_and_clear_bit(QEDN_STATE_MFW_STATE, &qedn->state)) {
		rc = qed_ops->common->update_drv_state(qedn->cdev, false);
		if (rc)
			pr_err("Failed to send drv state to MFW\n");
	}

	if (test_and_clear_bit(QEDN_STATE_CORE_OPEN, &qedn->state))
		qed_ops->common->slowpath_stop(qedn->cdev);

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

	rc = qed_ops->common->update_drv_state(qedn->cdev, true);
	if (rc) {
		pr_err("Failed to send drv state to MFW\n");
		goto exit_probe_and_release_mem;
	}

	set_bit(QEDN_STATE_MFW_STATE, &qedn->state);

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
