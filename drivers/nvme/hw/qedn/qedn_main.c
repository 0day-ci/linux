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

static void __qedn_remove(struct pci_dev *pdev)
{
	struct qedn_ctx *qedn = pci_get_drvdata(pdev);

	pr_notice("Starting qedn_remove\n");
	nvme_tcp_ofld_unregister_dev(&qedn->qedn_ofld_dev);
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

	qedn->qedn_ofld_dev.ops = &qedn_ofld_ops;
	INIT_LIST_HEAD(&qedn->qedn_ofld_dev.entry);
	rc = nvme_tcp_ofld_register_dev(&qedn->qedn_ofld_dev);
	if (rc)
		goto release_qedn;

	return 0;
release_qedn:
	kfree(qedn);

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
	pr_notice("Unloading qedn ended\n");
}

module_init(qedn_init);
module_exit(qedn_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: qede nvme-fabrics nvme-tcp-offload");
MODULE_DESCRIPTION("Marvell 25/50/100G NVMe-TCP Offload Host Driver");
MODULE_AUTHOR("Marvell");
