// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <net/addrconf.h>
#include <rdma/erdma-abi.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_hw.h"
#include "erdma_verbs.h"

#define DESC __stringify(ElasticRDMA(iWarp) Driver)

MODULE_AUTHOR("Alibaba");
MODULE_DESCRIPTION(DESC);
MODULE_LICENSE("GPL v2");

/*Common string that is matched to accept the device by the user library*/
#define ERDMA_NODE_DESC_COMMON "Elastic RDMA(iWARP) stack"

static struct list_head erdma_dev_list = LIST_HEAD_INIT(erdma_dev_list);
static DEFINE_MUTEX(erdma_dev_mutex);

static int erdma_res_cb_init(struct erdma_dev *dev)
{
	int i;

	for (i = 0; i < ERDMA_RES_CNT; i++) {
		dev->res_cb[i].next_alloc_idx = 1;
		spin_lock_init(&dev->res_cb[i].lock);
		dev->res_cb[i].bitmap = kcalloc(BITS_TO_LONGS(dev->res_cb[i].max_cap),
						sizeof(unsigned long), GFP_KERNEL);
		/* Free other memory in erdma_res_cb_free if failed. */
		if (!dev->res_cb[i].bitmap)
			return -ENOMEM;
	}

	return 0;
}

static void erdma_res_cb_free(struct erdma_dev *dev)
{
	int i;

	for (i = 0; i < ERDMA_RES_CNT; i++)
		kfree(dev->res_cb[i].bitmap);
}

static void erdma_release_handler(void *ptr)
{
	struct erdma_pci_drvdata *drvdata = (struct erdma_pci_drvdata *)ptr;

	drvdata->is_registered = 0;
}

static void erdma_dealloc_driver(struct ib_device *ibdev)
{
	struct erdma_dev *dev = to_edev(ibdev);

	erdma_res_cb_free(dev);
	xa_destroy(&dev->qp_xa);
	xa_destroy(&dev->cq_xa);

	if (dev->release_handler)
		dev->release_handler(dev->drvdata);
}

static int erdma_device_register(struct erdma_dev *dev, const char *dev_name)
{
	struct ib_device *ibdev = &dev->ibdev;
	int ret;

	ret = ib_register_device(ibdev, dev_name, dev->dmadev);
	if (ret) {
		dev_err(dev->dmadev, "ERROR: ib_register_device(%s) failed: ret = %d\n",
			dev_name, ret);
		return ret;
	}

	return 0;
}

static const struct ib_device_ops erdma_device_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_ERDMA,
	.uverbs_abi_ver = ERDMA_ABI_VERSION,

	.alloc_mr = erdma_ib_alloc_mr,
	.alloc_pd = erdma_alloc_pd,
	.alloc_ucontext = erdma_alloc_ucontext,
	.create_cq = erdma_create_cq,
	.create_qp = erdma_create_qp,
	.dealloc_driver = erdma_dealloc_driver,
	.dealloc_pd = erdma_dealloc_pd,
	.dealloc_ucontext = erdma_dealloc_ucontext,
	.dereg_mr = erdma_dereg_mr,
	.destroy_cq = erdma_destroy_cq,
	.destroy_qp = erdma_destroy_qp,
	.disassociate_ucontext = erdma_disassociate_ucontext,
	.get_dma_mr = erdma_get_dma_mr,
	.get_netdev = erdma_get_netdev,
	.get_port_immutable = erdma_get_port_immutable,
	.iw_accept = erdma_accept,
	.iw_add_ref = erdma_qp_get_ref,
	.iw_connect = erdma_connect,
	.iw_create_listen = erdma_create_listen,
	.iw_destroy_listen = erdma_destroy_listen,
	.iw_get_qp = erdma_get_ibqp,
	.iw_reject = erdma_reject,
	.iw_rem_ref = erdma_qp_put_ref,
	.map_mr_sg = erdma_map_mr_sg,
	.mmap = erdma_mmap,
	.modify_qp = erdma_modify_qp,
	.post_recv = erdma_post_recv,
	.post_send = erdma_post_send,
	.poll_cq = erdma_poll_cq,
	.query_device = erdma_query_device,
	.query_gid = erdma_query_gid,
	.query_port = erdma_query_port,
	.query_qp = erdma_query_qp,
	.req_notify_cq = erdma_req_notify_cq,
	.reg_user_mr = erdma_reg_user_mr,

	INIT_RDMA_OBJ_SIZE(ib_cq, erdma_cq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_pd, erdma_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, erdma_ucontext, ibucontext),
	INIT_RDMA_OBJ_SIZE(ib_qp, erdma_qp, ibqp),
};

static int erdma_dev_attrs_init(struct erdma_dev *dev)
{
	int err;
	u64 req_hdr, cap0, cap1;

	ERDMA_CMDQ_BUILD_REQ_HDR(&req_hdr, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_QUERY_DEVICE);

	err = erdma_post_cmd_wait(dev->cmdq, &req_hdr, sizeof(req_hdr), &cap0, &cap1);
	if (err)
		return err;

	dev->attrs.max_cqe = 1 << FIELD_GET(ERDMA_CMD_DEV_CAP0_MAX_CQE_MASK, cap0);
	dev->attrs.max_mr_size = 1 << FIELD_GET(ERDMA_CMD_DEV_CAP0_MAX_MR_SIZE_MASK, cap0);
	dev->attrs.max_mw = 1 << FIELD_GET(ERDMA_CMD_DEV_CAP1_MAX_MW_MASK, cap1);
	dev->attrs.max_recv_wr = 1 << FIELD_GET(ERDMA_CMD_DEV_CAP0_MAX_RECV_WR_MASK, cap0);
	dev->attrs.local_dma_key = FIELD_GET(ERDMA_CMD_DEV_CAP1_DMA_LOCAL_KEY_MASK, cap1);
	dev->cc_method = FIELD_GET(ERDMA_CMD_DEV_CAP1_DEFAULT_CC_MASK, cap1);
	dev->attrs.max_qp = ERDMA_NQP_PER_QBLOCK * FIELD_GET(ERDMA_CMD_DEV_CAP1_QBLOCK_MASK, cap1);
	dev->attrs.max_mr = 2 * dev->attrs.max_qp;
	dev->attrs.max_cq = 2 * dev->attrs.max_qp;

	dev->attrs.max_send_wr = ERDMA_MAX_SEND_WR;
	dev->attrs.vendor_id = PCI_VENDOR_ID_ALIBABA;
	dev->attrs.max_ord = ERDMA_MAX_ORD;
	dev->attrs.max_ird = ERDMA_MAX_IRD;
	dev->attrs.cap_flags = IB_DEVICE_LOCAL_DMA_LKEY | IB_DEVICE_MEM_MGT_EXTENSIONS;
	dev->attrs.max_send_sge = ERDMA_MAX_SEND_SGE;
	dev->attrs.max_recv_sge = ERDMA_MAX_RECV_SGE;
	dev->attrs.max_sge_rd = ERDMA_MAX_SGE_RD;
	dev->attrs.max_pd = ERDMA_MAX_PD;
	dev->attrs.max_srq = ERDMA_MAX_SRQ;
	dev->attrs.max_srq_wr = ERDMA_MAX_SRQ_WR;
	dev->attrs.max_srq_sge = ERDMA_MAX_SRQ_SGE;

	dev->res_cb[ERDMA_RES_TYPE_PD].max_cap = ERDMA_MAX_PD;
	dev->res_cb[ERDMA_RES_TYPE_STAG_IDX].max_cap = dev->attrs.max_mr;

	return 0;
}

static void __erdma_dwqe_resource_init(struct erdma_dev *dev, int grp_num)
{
	int total_pages, type0, type1;

	if (grp_num < 4)
		dev->disable_dwqe = 1;
	else
		dev->disable_dwqe = 0;

	/* One page contains 4 goups. */
	total_pages = grp_num * 4;
	if (grp_num >= ERDMA_DWQE_MAX_GRP_CNT) {
		grp_num = ERDMA_DWQE_MAX_GRP_CNT;
		type0 = ERDMA_DWQE_TYPE0_CNT;
		type1 = ERDMA_DWQE_TYPE1_CNT / ERDMA_DWQE_TYPE1_CNT_PER_PAGE;
	} else {
		type1 = total_pages / 3;
		type0 = total_pages - type1;
	}

	dev->dwqe_pages = type0;
	dev->dwqe_entries = type1 * ERDMA_DWQE_TYPE1_CNT_PER_PAGE;

	pr_info("grp_num:%d, total pages:%d, type0:%d, type1:%d, type1_db_cnt:%d\n",
		grp_num, total_pages, type0, type1, type1 * 16);
}

static struct erdma_dev *erdma_ib_device_create(struct erdma_pci_drvdata *drvdata,
						struct net_device *netdev)
{
	struct erdma_dev *dev;
	int ret = 0;

	dev = ib_alloc_device(erdma_dev, ibdev);
	if (!dev)
		return NULL;

	dev->drvdata = drvdata;
	dev->dmadev = &drvdata->pdev->dev;
	dev->cmdq = &drvdata->cmdq;

	dev->release_handler = erdma_release_handler;
	__erdma_dwqe_resource_init(dev, drvdata->grp_num);
	dev->db_space = drvdata->func_bar + ERDMA_BAR_DB_SPACE_BASE;
	dev->db_space_addr = drvdata->func_bar_addr + ERDMA_BAR_DB_SPACE_BASE;
	dev->netdev = netdev;

	ret = erdma_dev_attrs_init(dev);
	if (ret)
		goto release_ibdev;

	INIT_LIST_HEAD(&dev->cep_list);

	spin_lock_init(&dev->lock);
	xa_init_flags(&dev->qp_xa, XA_FLAGS_ALLOC1);
	xa_init_flags(&dev->cq_xa, XA_FLAGS_ALLOC1);
	dev->next_alloc_cqn = 1;
	dev->next_alloc_qpn = 1;

	ret = erdma_res_cb_init(dev);
	if (ret)
		goto err_out;

	spin_lock_init(&dev->db_bitmap_lock);
	bitmap_zero(dev->sdb_page, ERDMA_DWQE_TYPE0_CNT);
	bitmap_zero(dev->sdb_entry, ERDMA_DWQE_TYPE1_CNT);

	atomic_set(&dev->num_ctx, 0);
	atomic_set(&dev->num_qp, 0);
	atomic_set(&dev->num_cq, 0);
	atomic_set(&dev->num_mr, 0);
	atomic_set(&dev->num_pd, 0);

	dev->ibdev.node_type = RDMA_NODE_RNIC;
	memcpy(dev->ibdev.node_desc, ERDMA_NODE_DESC_COMMON, sizeof(ERDMA_NODE_DESC_COMMON));
	dev->ibdev.phys_port_cnt = 1;
	dev->ibdev.num_comp_vectors = drvdata->irq_num - 1;

	ib_set_device_ops(&dev->ibdev, &erdma_device_ops);
	addrconf_addr_eui48((u8 *)&dev->ibdev.node_guid, netdev->dev_addr);

	ret = ib_device_set_netdev(&dev->ibdev, dev->netdev, 1);
	if (ret)
		goto err_out;

	return dev;
err_out:
	erdma_res_cb_free(dev);
	xa_destroy(&dev->qp_xa);
	xa_destroy(&dev->cq_xa);

release_ibdev:
	ib_dealloc_device(&dev->ibdev);

	return ERR_PTR(ret);
}

static int erdma_netdev_matched_edev(struct net_device *netdev, struct erdma_pci_drvdata *drvdata)
{
	if (netdev->perm_addr[0] == drvdata->peer_addr[0] &&
	    netdev->perm_addr[1] == drvdata->peer_addr[1] &&
	    netdev->perm_addr[2] == drvdata->peer_addr[2] &&
	    netdev->perm_addr[3] == drvdata->peer_addr[3] &&
	    netdev->perm_addr[4] == drvdata->peer_addr[4] &&
	    netdev->perm_addr[5] == drvdata->peer_addr[5])
		return 1;

	return 0;
}

static int erdma_newlink(const char *dev_name, struct net_device *netdev)
{
	struct ib_device *ibdev;
	struct erdma_pci_drvdata *drvdata, *tmp;
	struct erdma_dev *dev;
	int ret;

	ibdev = ib_device_get_by_netdev(netdev, RDMA_DRIVER_ERDMA);
	if (ibdev) {
		ib_device_put(ibdev);
		return -EEXIST;
	}

	list_for_each_entry_safe(drvdata, tmp, &erdma_dev_list, list) {
		if (erdma_netdev_matched_edev(netdev, drvdata) && !drvdata->is_registered) {
			dev = erdma_ib_device_create(drvdata, netdev);
			if (IS_ERR(dev)) {
				pr_info("add device failed\n");
				return PTR_ERR(dev);
			}

			if (netif_running(netdev) && netif_carrier_ok(netdev))
				dev->state = IB_PORT_ACTIVE;
			else
				dev->state = IB_PORT_DOWN;

			ret = erdma_device_register(dev, dev_name);
			if (ret) {
				ib_dealloc_device(&dev->ibdev);
				return ret;
			}

			drvdata->is_registered = 1;
			drvdata->dev = dev;

			return 0;
		}
	}

	return -ENOENT;
}

static struct rdma_link_ops erdma_link_ops = {
	.type = "erdma",
	.newlink = erdma_newlink,
};

static int erdma_netdev_notifier(struct notifier_block *nb, unsigned long event, void *arg)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(arg);
	struct ib_device *ibdev;
	struct erdma_dev *dev;

	ibdev = ib_device_get_by_netdev(netdev, RDMA_DRIVER_ERDMA);
	if (!ibdev)
		return NOTIFY_OK;

	dev = to_edev(ibdev);

	switch (event) {
	case NETDEV_UP:
		dev->state = IB_PORT_ACTIVE;
		erdma_port_event(dev, IB_EVENT_PORT_ACTIVE);
		break;
	case NETDEV_DOWN:
		dev->state = IB_PORT_DOWN;
		erdma_port_event(dev, IB_EVENT_PORT_ERR);
		break;
	case NETDEV_UNREGISTER:
		ib_unregister_device_queued(ibdev);
		break;
	case NETDEV_REGISTER:
	case NETDEV_CHANGEADDR:
	case NETDEV_CHANGEMTU:
	case NETDEV_GOING_DOWN:
	case NETDEV_CHANGE:
	default:
		break;
	}

	ib_device_put(ibdev);

	return NOTIFY_DONE;
}

static irqreturn_t erdma_comm_irq_handler(int irq, void *data)
{
	struct erdma_pci_drvdata *drvdata = data;

	erdma_cmdq_completion_handler(&drvdata->cmdq);
	erdma_aeq_event_handler(drvdata);

	return IRQ_HANDLED;
}

static int erdma_request_vectors(struct erdma_pci_drvdata *drvdata)
{
	int irq_num;

	irq_num = pci_alloc_irq_vectors(drvdata->pdev, 1, ERDMA_NUM_MSIX_VEC, PCI_IRQ_MSIX);

	if (irq_num <= 0) {
		dev_err(&drvdata->pdev->dev, "request irq vectors failed(%d)\n", irq_num);
		return -ENOSPC;
	}

	dev_info(&drvdata->pdev->dev, "hardware return %d irqs.\n", irq_num);
	drvdata->irq_num = irq_num;

	return 0;
}

static int erdma_comm_irq_init(struct erdma_pci_drvdata *drvdata)
{
	u32 cpu = 0;
	int err;
	struct erdma_irq_info *irq_info = &drvdata->comm_irq;

	snprintf(irq_info->name, ERDMA_IRQNAME_SIZE,
		 "erdma-common@pci:%s", pci_name(drvdata->pdev));
	irq_info->handler = erdma_comm_irq_handler;
	irq_info->data = drvdata;
	irq_info->msix_vector = pci_irq_vector(drvdata->pdev, ERDMA_MSIX_VECTOR_CMDQ);

	if (drvdata->numa_node >= 0)
		cpu = cpumask_first(cpumask_of_node(drvdata->numa_node));

	irq_info->cpu = cpu;
	cpumask_set_cpu(cpu, &irq_info->affinity_hint_mask);
	dev_info(&drvdata->pdev->dev, "setup irq:%p vector:%d name:%s\n",
		 irq_info, irq_info->msix_vector, irq_info->name);

	err = request_irq(irq_info->msix_vector, irq_info->handler, 0,
			  irq_info->name, irq_info->data);
	if (err) {
		dev_err(&drvdata->pdev->dev, "failed to request_irq(%d)\n", err);
		return err;
	}

	irq_set_affinity_hint(irq_info->msix_vector, &irq_info->affinity_hint_mask);

	return 0;
}

static void erdma_comm_irq_uninit(struct erdma_pci_drvdata *drvdata)
{
	struct erdma_irq_info *irq_info = &drvdata->comm_irq;

	irq_set_affinity_hint(irq_info->msix_vector, NULL);
	free_irq(irq_info->msix_vector, irq_info->data);
}

static int erdma_device_init(struct erdma_pci_drvdata *drvdata)
{
	int err;
	struct pci_dev *pdev = drvdata->pdev;
	u32 mac_l, mac_h;

	drvdata->grp_num = erdma_reg_read32(drvdata, ERDMA_REGS_GRP_NUM_REG);
	mac_l = erdma_reg_read32(drvdata, ERDMA_REGS_NETDEV_MAC_L_REG);
	mac_h = erdma_reg_read32(drvdata, ERDMA_REGS_NETDEV_MAC_H_REG);

	pr_info("assoc netdev mac addr is 0x%x-0x%x.\n", mac_h, mac_l);

	drvdata->peer_addr[0] = (mac_h >> 8) & 0xFF;
	drvdata->peer_addr[1] = mac_h & 0xFF;
	drvdata->peer_addr[2] = (mac_l >> 24) & 0xFF;
	drvdata->peer_addr[3] = (mac_l >> 16) & 0xFF;
	drvdata->peer_addr[4] = (mac_l >> 8) & 0xFF;
	drvdata->peer_addr[5] = mac_l & 0xFF;

	dev_info(&pdev->dev, "hardware return grp_num:%d\n", drvdata->grp_num);

	/* force dma width to 64. */
	drvdata->dma_width = 64;

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(drvdata->dma_width));
	if (err) {
		dev_err(&pdev->dev, "pci_set_dma_mask failed(%d)\n", err);
		return err;
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(drvdata->dma_width));
	if (err) {
		dev_err(&pdev->dev, "pci_set_consistent_dma_mask failed(%d)\n", err);
		return err;
	}

	return err;
}

static void erdma_device_uninit(struct erdma_pci_drvdata *dev)
{
	u32 ctrl;

	ctrl = FIELD_PREP(ERDMA_REG_DEV_CTRL_RESET_MASK, 1);
	erdma_reg_write32(dev, ERDMA_REGS_DEV_CTRL_REG, ctrl);
}

static int erdma_probe_dev(struct pci_dev *pdev)
{
	int err;
	struct erdma_pci_drvdata *drvdata;
	u32 version;
	int bars;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device failed(%d)\n", err);
		return err;
	}

	pci_set_master(pdev);

	drvdata = kcalloc(1, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		err = -ENOMEM;
		goto err_disable_device;
	}

	pci_set_drvdata(pdev, drvdata);
	drvdata->pdev = pdev;
	drvdata->numa_node = pdev->dev.numa_node;

	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	err = pci_request_selected_regions(pdev, bars, DRV_MODULE_NAME);
	if (bars != ERDMA_BAR_MASK || err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed(bars:%d, err:%d)\n", bars, err);
		err = err == 0 ? -EINVAL : err;
		goto err_drvdata_release;
	}

	drvdata->func_bar_addr = pci_resource_start(pdev, ERDMA_FUNC_BAR);
	drvdata->func_bar_len = pci_resource_len(pdev, ERDMA_FUNC_BAR);

	drvdata->func_bar =
		devm_ioremap(&pdev->dev, drvdata->func_bar_addr, drvdata->func_bar_len);
	if (!drvdata->func_bar) {
		dev_err(&pdev->dev, "devm_ioremap failed.\n");
		err = -EFAULT;
		goto err_release_bars;
	}

	version = erdma_reg_read32(drvdata, ERDMA_REGS_VERSION_REG);
	if (version == 0) {
		/* we knows that it is a non-functional function. */
		err = -ENODEV;
		goto err_iounmap_func_bar;
	}

	err = erdma_device_init(drvdata);
	if (err)
		goto err_iounmap_func_bar;

	err = erdma_request_vectors(drvdata);
	if (err)
		goto err_iounmap_func_bar;

	err = erdma_comm_irq_init(drvdata);
	if (err)
		goto err_free_vectors;

	err = erdma_aeq_init(drvdata);
	if (err)
		goto err_uninit_comm_irq;

	err = erdma_cmdq_init(drvdata);
	if (err)
		goto err_uninit_aeq;

	err = erdma_ceqs_init(drvdata);
	if (err)
		goto err_uninit_cmdq;

	erdma_finish_cmdq_init(drvdata);

	mutex_lock(&erdma_dev_mutex);
	list_add_tail(&drvdata->list, &erdma_dev_list);
	mutex_unlock(&erdma_dev_mutex);

	return 0;

err_uninit_cmdq:
	erdma_device_uninit(drvdata);
	erdma_cmdq_destroy(drvdata);

err_uninit_aeq:
	erdma_aeq_destroy(drvdata);

err_uninit_comm_irq:
	erdma_comm_irq_uninit(drvdata);

err_free_vectors:
	pci_free_irq_vectors(drvdata->pdev);

err_iounmap_func_bar:
	devm_iounmap(&pdev->dev, drvdata->func_bar);

err_release_bars:
	pci_release_selected_regions(pdev, bars);

err_drvdata_release:
	kfree(drvdata);

err_disable_device:
	pci_disable_device(pdev);

	return err;
}

static void erdma_remove_dev(struct pci_dev *pdev)
{
	struct erdma_pci_drvdata *drvdata = pci_get_drvdata(pdev);

	mutex_lock(&erdma_dev_mutex);
	list_del(&drvdata->list);
	mutex_unlock(&erdma_dev_mutex);

	erdma_ceqs_uninit(drvdata);

	erdma_device_uninit(drvdata);

	erdma_cmdq_destroy(drvdata);
	erdma_aeq_destroy(drvdata);
	erdma_comm_irq_uninit(drvdata);
	pci_free_irq_vectors(drvdata->pdev);

	devm_iounmap(&pdev->dev, drvdata->func_bar);
	pci_release_selected_regions(pdev, ERDMA_BAR_MASK);

	kfree(drvdata);

	pci_disable_device(pdev);
}

static int erdma_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return erdma_probe_dev(pdev);
}

static void erdma_remove(struct pci_dev *pdev)
{
	struct erdma_pci_drvdata *drvdata = pci_get_drvdata(pdev);

	if (drvdata->is_registered) {
		ib_unregister_device(&drvdata->dev->ibdev);
		drvdata->is_registered = 0;
	}

	erdma_remove_dev(pdev);
}

static const struct pci_device_id erdma_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ALIBABA, 0x107f) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ALIBABA, 0x5007) },
	{ 0, }
};

static struct pci_driver erdma_pci_driver = {
	.name = DRV_MODULE_NAME,
	.id_table = erdma_pci_tbl,
	.probe = erdma_probe,
	.remove = erdma_remove
};

MODULE_DEVICE_TABLE(pci, erdma_pci_tbl);

static struct notifier_block erdma_netdev_nb = {
	.notifier_call = erdma_netdev_notifier,
};

static __init int erdma_init_module(void)
{
	int ret;

	ret = erdma_cm_init();
	if (ret)
		return ret;

	ret = pci_register_driver(&erdma_pci_driver);
	if (ret) {
		pr_err("Couldn't register erdma driver.\n");
		goto uninit_cm;
	}

	ret = register_netdevice_notifier(&erdma_netdev_nb);
	if (ret)
		goto unregister_driver;

	rdma_link_register(&erdma_link_ops);

	return ret;

unregister_driver:
	pci_unregister_driver(&erdma_pci_driver);

uninit_cm:
	erdma_cm_exit();

	return ret;
}

static void __exit erdma_exit_module(void)
{
	rdma_link_unregister(&erdma_link_ops);
	ib_unregister_driver(RDMA_DRIVER_ERDMA);

	unregister_netdevice_notifier(&erdma_netdev_nb);

	pci_unregister_driver(&erdma_pci_driver);

	erdma_cm_exit();
}

module_init(erdma_init_module);
module_exit(erdma_exit_module);
