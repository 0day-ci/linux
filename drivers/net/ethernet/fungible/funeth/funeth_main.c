// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

#include <linux/bpf.h>
#include <linux/crash_dump.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/filter.h>
#include <linux/idr.h>
#include <linux/if_vlan.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>
#include <linux/inetdevice.h>

#include "funeth.h"
#include "funeth_devlink.h"
#include "funeth_ktls.h"
#include "fun_port.h"
#include "fun_queue.h"
#include "funeth_txrx.h"

#define ADMIN_SQ_DEPTH 32
#define ADMIN_CQ_DEPTH 64
#define ADMIN_RQ_DEPTH 16

/* Default number of Tx/Rx queues. */
#define FUN_DFLT_QUEUES 16U

enum {
	FUN_SERV_RES_CHANGE = FUN_SERV_FIRST_AVAIL,
	FUN_SERV_DEL_PORTS,
};

static const struct pci_device_id funeth_id_table[] = {
	{ PCI_VDEVICE(FUNGIBLE, 0x0101) },
	{ PCI_VDEVICE(FUNGIBLE, 0x0181) },
	{ 0, }
};

/* Issue a port write admin command with @n key/value pairs. */
static int fun_port_write_cmds(struct funeth_priv *fp, unsigned int n,
			       const int *keys, const u64 *data)
{
	unsigned int cmd_size, i;
	union {
		struct fun_admin_port_req req;
		struct fun_admin_port_rsp rsp;
		u8 v[ADMIN_SQE_SIZE];
	} cmd;

	cmd_size = offsetof(struct fun_admin_port_req, u.write.write48) +
		n * sizeof(struct fun_admin_write48_req);
	if (cmd_size > sizeof(cmd) || cmd_size > ADMIN_RSP_MAX_LEN)
		return -EINVAL;

	cmd.req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_PORT,
						    cmd_size);
	cmd.req.u.write =
		FUN_ADMIN_PORT_WRITE_REQ_INIT(FUN_ADMIN_SUBOP_WRITE, 0,
					      fp->netdev->dev_port);
	for (i = 0; i < n; i++)
		cmd.req.u.write.write48[i] =
			FUN_ADMIN_WRITE48_REQ_INIT(keys[i], data[i]);

	return fun_submit_admin_sync_cmd(fp->fdev, &cmd.req.common,
					 &cmd.rsp, cmd_size, 0);
}

int fun_port_write_cmd(struct funeth_priv *fp, int key, u64 data)
{
	return fun_port_write_cmds(fp, 1, &key, &data);
}

/* Issue a port read admin command with @n key/value pairs. */
static int fun_port_read_cmds(struct funeth_priv *fp, unsigned int n,
			      const int *keys, u64 *data)
{
	const struct fun_admin_read48_rsp *r48rsp;
	unsigned int cmd_size, i;
	int rc;
	union {
		struct fun_admin_port_req req;
		struct fun_admin_port_rsp rsp;
		u8 v[ADMIN_SQE_SIZE];
	} cmd;

	cmd_size = offsetof(struct fun_admin_port_req, u.read.read48) +
		n * sizeof(struct fun_admin_read48_req);
	if (cmd_size > sizeof(cmd) || cmd_size > ADMIN_RSP_MAX_LEN)
		return -EINVAL;

	cmd.req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_PORT,
						    cmd_size);
	cmd.req.u.read =
		FUN_ADMIN_PORT_READ_REQ_INIT(FUN_ADMIN_SUBOP_READ, 0,
					     fp->netdev->dev_port);
	for (i = 0; i < n; i++)
		cmd.req.u.read.read48[i] = FUN_ADMIN_READ48_REQ_INIT(keys[i]);

	rc = fun_submit_admin_sync_cmd(fp->fdev, &cmd.req.common,
				       &cmd.rsp, cmd_size, 0);
	if (rc)
		return rc;

	for (r48rsp = cmd.rsp.u.read.read48, i = 0; i < n; i++, r48rsp++) {
		data[i] = FUN_ADMIN_READ48_RSP_DATA_G(r48rsp->key_to_data);
		dev_dbg(fp->fdev->dev,
			"port_read_rsp lport=%u (key_to_data=0x%llx) key=%d data:%lld retval:%lld",
			fp->lport, r48rsp->key_to_data, keys[i], data[i],
			FUN_ADMIN_READ48_RSP_RET_G(r48rsp->key_to_data));
	}
	return 0;
}

int fun_port_read_cmd(struct funeth_priv *fp, int key, u64 *data)
{
	return fun_port_read_cmds(fp, 1, &key, data);
}

static void fun_report_link(struct net_device *netdev)
{
	if (netif_carrier_ok(netdev)) {
		const struct funeth_priv *fp = netdev_priv(netdev);
		const char *fec = "", *pause = "";
		int speed = fp->link_speed;
		char unit = 'M';

		if (fp->link_speed >= SPEED_1000) {
			speed /= 1000;
			unit = 'G';
		}

		if (fp->active_fec & FUN_PORT_FEC_RS)
			fec = ", RS-FEC";
		else if (fp->active_fec & FUN_PORT_FEC_FC)
			fec = ", BASER-FEC";

		if ((fp->active_fc & FUN_PORT_CAP_PAUSE_MASK) == FUN_PORT_CAP_PAUSE_MASK)
			pause = ", Tx/Rx PAUSE";
		else if (fp->active_fc & FUN_PORT_CAP_RX_PAUSE)
			pause = ", Rx PAUSE";
		else if (fp->active_fc & FUN_PORT_CAP_TX_PAUSE)
			pause = ", Tx PAUSE";

		netdev_info(netdev, "Link up at %d %cb/s full-duplex%s%s\n",
			    speed, unit, pause, fec);
	} else {
		netdev_info(netdev, "Link down\n");
	}
}

static int fun_adi_write(struct fun_dev *fdev, enum fun_admin_adi_attr attr,
			 unsigned int adi_id, const struct fun_adi_param *param)
{
	struct fun_admin_adi_req req = {
		.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_ADI,
						     sizeof(req)),
		.u.write.subop = FUN_ADMIN_SUBOP_WRITE,
		.u.write.attribute = attr,
		.u.write.id = cpu_to_be32(adi_id),
		.u.write.param = *param
	};

	return fun_submit_admin_sync_cmd(fdev, &req.common, NULL, 0, 0);
}

/* Configure RSS for the given port. @op determines whether a new RSS context
 * is to be created or whether an existing one should be reconfigured. The
 * remaining parameters specify the hashing algorithm, key, and indirection
 * table.
 *
 * This initiates packet delivery to the Rx queues set in the indirection
 * table.
 */
int fun_config_rss(struct net_device *dev, int algo, const u8 *key,
		   const u32 *qtable, u8 op)
{
	struct funeth_priv *fp = netdev_priv(dev);
	unsigned int table_len = fp->indir_table_nentries;
	unsigned int len = FUN_ETH_RSS_MAX_KEY_SIZE + sizeof(u32) * table_len;
	struct funeth_rxq **rxqs = rtnl_dereference(fp->rxqs);
	union {
		struct {
			struct fun_admin_rss_req req;
			struct fun_dataop_gl gl;
		};
		struct fun_admin_generic_create_rsp rsp;
	} cmd;
	__be32 *indir_tab;
	u16 flags;
	int rc;

	if (op != FUN_ADMIN_SUBOP_CREATE && fp->rss_hw_id == FUN_HCI_ID_INVALID)
		return -EINVAL;

	flags = op == FUN_ADMIN_SUBOP_CREATE ?
			FUN_ADMIN_RES_CREATE_FLAG_ALLOCATOR : 0;
	cmd.req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_RSS,
						    sizeof(cmd));
	cmd.req.u.create =
		FUN_ADMIN_RSS_CREATE_REQ_INIT(op, flags, fp->rss_hw_id,
					      dev->dev_port, algo,
					      FUN_ETH_RSS_MAX_KEY_SIZE,
					      table_len, 0,
					      FUN_ETH_RSS_MAX_KEY_SIZE);
	cmd.req.u.create.dataop = FUN_DATAOP_HDR_INIT(1, 0, 1, 0, len);
	fun_dataop_gl_init(&cmd.gl, 0, 0, len, fp->rss_dma_addr);

	/* write the key and indirection table into the RSS DMA area */
	memcpy(fp->rss_cfg, key, FUN_ETH_RSS_MAX_KEY_SIZE);
	indir_tab = fp->rss_cfg + FUN_ETH_RSS_MAX_KEY_SIZE;
	for (rc = 0; rc < table_len; rc++)
		*indir_tab++ = cpu_to_be32(rxqs[*qtable++]->hw_cqid);

	rc = fun_submit_admin_sync_cmd(fp->fdev, &cmd.req.common,
				       &cmd.rsp, sizeof(cmd.rsp), 0);
	if (!rc && op == FUN_ADMIN_SUBOP_CREATE)
		fp->rss_hw_id = be32_to_cpu(cmd.rsp.id);
	return rc;
}

/* Destroy the HW RSS conntext associated with the given port. This also stops
 * all packet delivery to our Rx queues.
 */
static int fun_destroy_rss(struct funeth_priv *fp)
{
	int rc;

	if (fp->rss_hw_id == FUN_HCI_ID_INVALID)
		return 0;

	rc = fun_res_destroy(fp->fdev, FUN_ADMIN_OP_RSS, 0, fp->rss_hw_id);
	fp->rss_hw_id = FUN_HCI_ID_INVALID;
	return rc;
}

static void fun_irq_aff_notify(struct irq_affinity_notify *notify,
			       const cpumask_t *mask)
{
	struct fun_irq *p = container_of(notify, struct fun_irq, aff_notify);

	cpumask_copy(&p->affinity_mask, mask);
}

static void fun_irq_aff_release(struct kref __always_unused *ref)
{
}

static void fun_init_irq(struct fun_irq *p, int node, int idx)
{
	cpumask_set_cpu(cpumask_local_spread(idx, node), &p->affinity_mask);
	p->aff_notify.notify = fun_irq_aff_notify;
	p->aff_notify.release = fun_irq_aff_release;
}

static void fun_free_irqs_from(struct funeth_priv *fp, unsigned int start)
{
	struct fun_irq *p = fp->irqs + start;

	for ( ; start < fp->num_irqs; start++, p++) {
		netif_napi_del(&p->napi);
		fun_release_irqs(fp->fdev, 1, &p->irq_idx);
	}
}

/* Release the IRQ vectors reserved for Tx/Rx queues. */
static void fun_free_queue_irqs(struct net_device *dev)
{
	struct funeth_priv *fp = netdev_priv(dev);

	if (fp->num_irqs) {
		netif_info(fp, intr, dev, "Releasing %u queue IRQs\n",
			   fp->num_irqs);
		fun_free_irqs_from(fp, 0);
		kfree(fp->irqs);
		fp->irqs = NULL;
		fp->num_irqs = 0;
		fp->num_tx_irqs = 0;
	}
}

/* Reserve IRQ vectors, one per queue. We hold on to allocated vectors until
 * the total number of queues changes.
 */
static int fun_alloc_queue_irqs(struct net_device *dev, unsigned int ntx,
				unsigned int nrx)
{
	struct funeth_priv *fp = netdev_priv(dev);
	unsigned int i, copy, irqs_needed;
	struct fun_irq *irqs, *p;
	int node, res = -ENOMEM;
	u16 *irq_idx;

	irqs_needed = nrx + ntx;
	if (irqs_needed == fp->num_irqs && fp->num_tx_irqs == ntx)
		return 0;

	/* IRQ needs have changed, reallocate. */
	irqs = kcalloc(irqs_needed, sizeof(*irqs), GFP_KERNEL);
	if (!irqs)
		return -ENOMEM;

	irq_idx = kcalloc(irqs_needed, sizeof(u16), GFP_KERNEL);
	if (!irq_idx)
		goto free;

	/* keep as many existing IRQs as possible */
	copy = min(irqs_needed, fp->num_irqs);
	for (i = 0; i < copy; i++)
		irq_idx[i] = fp->irqs[i].irq_idx;

	/* get additional IRQs */
	if (irqs_needed > fp->num_irqs) {
		unsigned int addl_irqs = irqs_needed - fp->num_irqs;

		res = fun_reserve_irqs(fp->fdev, addl_irqs, irq_idx + copy);
		if (res != addl_irqs)
			goto free;
	}

	/* release excess IRQs */
	fun_free_irqs_from(fp, copy);

	for (i = 0; i < copy; i++)
		netif_napi_del(&fp->irqs[i].napi);

	/* new Tx IRQs */
	copy = min(ntx, fp->num_tx_irqs);
	memcpy(irqs, fp->irqs, copy * sizeof(*p));

	node = dev_to_node(&fp->pdev->dev);
	for (p = irqs + copy, i = copy; i < ntx; i++, p++)
		fun_init_irq(p, node, i);

	/* new Rx IRQs */
	copy = min(nrx, fp->num_irqs - fp->num_tx_irqs);
	memcpy(p, fp->irqs + fp->num_tx_irqs, copy * sizeof(*p));
	p += copy;

	for (i = copy; i < nrx; i++, p++)
		fun_init_irq(p, node, i);

	/* assign IRQ vectors and register NAPI */
	for (i = 0; i < irqs_needed; i++) {
		irqs[i].irq_idx = irq_idx[i];
		irqs[i].irq = pci_irq_vector(fp->pdev, irq_idx[i]);
	}

	for (p = irqs, i = 0; i < ntx; i++, p++)
		netif_tx_napi_add(dev, &p->napi, fun_txq_napi_poll,
				  NAPI_POLL_WEIGHT);

	for (i = 0; i < nrx; i++, p++)
		netif_napi_add(dev, &p->napi, fun_rxq_napi_poll,
			       NAPI_POLL_WEIGHT);

	kfree(irq_idx);
	kfree(fp->irqs);

	fp->irqs = irqs;
	fp->num_irqs = irqs_needed;
	fp->num_tx_irqs = ntx;
	netif_info(fp, intr, dev, "Reserved %u IRQs for Tx/Rx queues\n",
		   irqs_needed);
	return 0;

free:
	kfree(irq_idx);
	kfree(irqs);
	return res;
}

static void free_txqs(struct funeth_txq **txqs, unsigned int nqs,
		      struct fun_irq *irqs, int state)
{
	unsigned int i;

	for (i = 0; i < nqs && txqs[i]; i++) {
		if (txqs[i]->init_state >= FUN_QSTATE_INIT_FULL)
			irqs[i].txq = NULL;
		txqs[i] = funeth_txq_free(txqs[i], state);
	}
}

static int alloc_txqs(struct net_device *dev, struct funeth_txq **txqs,
		      unsigned int nqs, unsigned int depth,
		      struct fun_irq *irqs, int state)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct funeth_txq *q;
	unsigned int i;

	for (i = 0; i < nqs; i++) {
		q = funeth_txq_create(dev, i, depth, &irqs[i % fp->num_tx_irqs],
				      state);
		if (IS_ERR(q)) {
			free_txqs(txqs, nqs, irqs, FUN_QSTATE_DESTROYED);
			return PTR_ERR(q);
		}
		txqs[i] = q;
	}
	return 0;
}

static void free_rxqs(struct funeth_rxq **rxqs, unsigned int nqs,
		      struct fun_irq *irqs, int state)
{
	unsigned int i;

	for (i = 0; i < nqs && rxqs[i]; i++) {
		if (rxqs[i]->init_state >= FUN_QSTATE_INIT_FULL)
			irqs[i].rxq = NULL;
		rxqs[i] = funeth_rxq_free(rxqs[i], state);
	}
}

static int alloc_rxqs(struct net_device *dev, struct funeth_rxq **rxqs,
		      unsigned int nqs, unsigned int ncqe, unsigned int nrqe,
		      struct fun_irq *irqs, int state)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct funeth_rxq *q;
	unsigned int i, nirq;

	nirq = fp->num_irqs - fp->num_tx_irqs;
	for (i = 0; i < nqs; i++) {
		q = funeth_rxq_create(dev, i, ncqe, nrqe, &irqs[i % nirq],
				      state);
		if (IS_ERR(q)) {
			free_rxqs(rxqs, nqs, irqs, FUN_QSTATE_DESTROYED);
			return PTR_ERR(q);
		}
		rxqs[i] = q;
	}
	return 0;
}

static void free_xdpqs(struct funeth_txq **xdpqs, unsigned int nqs, int state)
{
	unsigned int i;

	for (i = 0; i < nqs && xdpqs[i]; i++)
		xdpqs[i] = funeth_txq_free(xdpqs[i], state);

	if (state == FUN_QSTATE_DESTROYED)
		kfree(xdpqs);
}

static struct funeth_txq **alloc_xdpqs(struct net_device *dev, unsigned int nqs,
				       unsigned int depth, int state)
{
	struct funeth_txq **xdpqs, *q;
	unsigned int i;

	xdpqs = kcalloc(nqs, sizeof(*xdpqs), GFP_KERNEL);
	if (!xdpqs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nqs; i++) {
		q = funeth_txq_create(dev, i, depth, NULL, state);
		if (IS_ERR(q)) {
			free_xdpqs(xdpqs, nqs, FUN_QSTATE_DESTROYED);
			return (struct funeth_txq **)q;
		}
		xdpqs[i] = q;
	}
	return xdpqs;
}

static void fun_free_rings(struct net_device *netdev, struct fun_qset *qset)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	struct funeth_txq **xdpqs = qset->xdpqs;
	struct funeth_rxq **rxqs = qset->rxqs;

	/* qset may not specify any queues to operate on. In that case the
	 * currently installed queues are implied.
	 */
	if (!rxqs) {
		rxqs = rtnl_dereference(fp->rxqs);
		xdpqs = rtnl_dereference(fp->xdpqs);
		qset->txqs = fp->txqs;
		qset->nrxqs = netdev->real_num_rx_queues;
		qset->ntxqs = netdev->real_num_tx_queues;
		qset->nxdpqs = fp->num_xdpqs;
	}
	if (!rxqs)
		return;

	if (rxqs == rtnl_dereference(fp->rxqs)) {
		rcu_assign_pointer(fp->rxqs, NULL);
		rcu_assign_pointer(fp->xdpqs, NULL);
		synchronize_net();
		fp->txqs = NULL;
	}

	free_rxqs(rxqs, qset->nrxqs, &fp->irqs[fp->num_tx_irqs], qset->state);
	free_txqs(qset->txqs, qset->ntxqs, fp->irqs, qset->state);
	free_xdpqs(xdpqs, qset->nxdpqs, qset->state);
	if (qset->state == FUN_QSTATE_DESTROYED)
		kfree(rxqs);

	/* Tell the caller which queues were operated on. */
	qset->rxqs = rxqs;
	qset->xdpqs = xdpqs;
}

static int fun_alloc_rings(struct net_device *netdev, struct fun_qset *qset)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	struct funeth_txq **xdpqs = NULL, **txqs;
	struct funeth_rxq **rxqs;
	int err;

	if (qset->state >= FUN_QSTATE_INIT_FULL || !fp->irqs) {
		err = fun_alloc_queue_irqs(netdev, qset->ntxqs, qset->nrxqs);
		if (err)
			return err;
	}

	rxqs = kcalloc(qset->ntxqs + qset->nrxqs, sizeof(*rxqs), GFP_KERNEL);
	if (!rxqs)
		return -ENOMEM;

	if (qset->nxdpqs) {
		xdpqs = alloc_xdpqs(netdev, qset->nxdpqs, qset->sq_depth,
				    qset->state);
		if (IS_ERR(xdpqs)) {
			err = PTR_ERR(xdpqs);
			goto free_qvec;
		}
	}

	txqs = (struct funeth_txq **)&rxqs[qset->nrxqs];
	err = alloc_txqs(netdev, txqs, qset->ntxqs, qset->sq_depth,
			 fp->irqs, qset->state);
	if (err)
		goto free_xdpqs;

	err = alloc_rxqs(netdev, rxqs, qset->nrxqs, qset->cq_depth,
			 qset->rq_depth, &fp->irqs[fp->num_tx_irqs],
			 qset->state);
	if (err)
		goto free_txqs;

	qset->rxqs = rxqs;
	qset->txqs = txqs;
	qset->xdpqs = xdpqs;
	return 0;

free_txqs:
	free_txqs(txqs, qset->ntxqs, fp->irqs, FUN_QSTATE_DESTROYED);
free_xdpqs:
	free_xdpqs(xdpqs, qset->nxdpqs, FUN_QSTATE_DESTROYED);
free_qvec:
	kfree(rxqs);
	return err;
}

/* Take queues to the next level. Presently this means creating them on the
 * device.
 */
static int fun_advance_ring_state(struct net_device *dev, struct fun_qset *qset)
{
	struct funeth_priv *fp = netdev_priv(dev);
	int i, err;

	err = fun_alloc_queue_irqs(dev, qset->ntxqs, qset->nrxqs);
	if (err)
		return err;

	for (i = 0; i < qset->nrxqs; i++) {
		err = fun_rxq_create_dev(qset->rxqs[i],
					 &fp->irqs[qset->ntxqs + i]);
		if (err)
			goto out;
	}

	for (i = 0; i < qset->ntxqs; i++) {
		err = fun_txq_create_dev(qset->txqs[i], &fp->irqs[i]);
		if (err)
			goto out;
	}

	for (i = 0; i < qset->nxdpqs; i++) {
		err = fun_txq_create_dev(qset->xdpqs[i], NULL);
		if (err)
			goto out;
	}

	return 0;

out:
	fun_free_rings(dev, qset);
	return err;
}

static int fun_port_create(struct net_device *netdev)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	union {
		struct fun_admin_port_req req;
		struct fun_admin_port_rsp rsp;
	} cmd;
	int rc;

	if (fp->lport != INVALID_LPORT)
		return 0;

	cmd.req.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_PORT,
						    sizeof(cmd.req));
	cmd.req.u.create =
		FUN_ADMIN_PORT_CREATE_REQ_INIT(FUN_ADMIN_SUBOP_CREATE, 0,
					       netdev->dev_port);

	rc = fun_submit_admin_sync_cmd(fp->fdev, &cmd.req.common, &cmd.rsp,
				       sizeof(cmd.rsp), 0);

	if (!rc)
		fp->lport = be16_to_cpu(cmd.rsp.u.create.lport);
	return rc;
}

static int fun_port_destroy(struct net_device *netdev)
{
	struct funeth_priv *fp = netdev_priv(netdev);

	if (fp->lport == INVALID_LPORT)
		return 0;

	fp->lport = INVALID_LPORT;
	return fun_res_destroy(fp->fdev, FUN_ADMIN_OP_PORT, 0,
			       netdev->dev_port);
}

static int fun_eth_create(struct funeth_priv *fp, u32 ethid)
{
	struct fun_admin_eth_req req = {
		.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_ETH,
						     sizeof(req)),
		.u.create =
			FUN_ADMIN_ETH_CREATE_REQ_INIT(FUN_ADMIN_SUBOP_CREATE, 0,
						      ethid,
						      fp->netdev->dev_port)
	};

	return fun_submit_admin_sync_cmd(fp->fdev, &req.common, NULL, 0, 0);
}

static int fun_vi_create(struct funeth_priv *fp)
{
	struct fun_admin_vi_req req = {
		.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_VI,
						     sizeof(req)),
		.u.create = FUN_ADMIN_VI_CREATE_REQ_INIT(FUN_ADMIN_SUBOP_CREATE,
							 0,
							 fp->netdev->dev_port,
							 fp->netdev->dev_port)
	};

	return fun_submit_admin_sync_cmd(fp->fdev, &req.common, NULL, 0, 0);
}

/* helper to create an ETH flow and bind an SQ to it */
int fun_create_and_bind_tx(struct funeth_priv *fp, u32 ethid, u32 sqid)
{
	int rc;

	rc = fun_eth_create(fp, ethid);
	if (!rc) {
		rc = fun_bind(fp->fdev, FUN_ADMIN_BIND_TYPE_EPSQ, sqid,
			      FUN_ADMIN_BIND_TYPE_ETH, ethid);
		if (rc)
			fun_res_destroy(fp->fdev, FUN_ADMIN_OP_ETH, 0, ethid);
	}
	return rc;
}

static irqreturn_t fun_queue_irq_handler(int irq, void *data)
{
	struct fun_irq *p = data;

	if (p->rxq) {
		prefetch(p->rxq->next_cqe_info);
		p->rxq->irq_cnt++;
	}
	napi_schedule_irqoff(&p->napi);
	return IRQ_HANDLED;
}

static int fun_enable_irqs(struct net_device *dev)
{
	struct funeth_priv *fp = netdev_priv(dev);
	unsigned int i, qidx;
	struct fun_irq *p;
	const char *qtype;
	int err;

	for (p = fp->irqs, i = 0; i < fp->num_irqs; i++, p++) {
		if (p->txq) {
			qtype = "tx";
			qidx = p->txq->qidx;
		} else if (p->rxq) {
			qtype = "rx";
			qidx = p->rxq->qidx;
		} else {
			continue;
		}

		snprintf(p->name, sizeof(p->name) - 1, "%s-%s-%u", dev->name,
			 qtype, qidx);
		err = request_irq(p->irq, fun_queue_irq_handler, 0, p->name, p);
		if (err) {
			netdev_err(dev, "Failed to allocate IRQ %u, err %d\n",
				   p->irq, err);
			goto unroll;
		}
	}

	for (p = fp->irqs, i = 0; i < fp->num_irqs; i++, p++) {
		if (!p->txq && !p->rxq)
			continue;
		irq_set_affinity_notifier(p->irq, &p->aff_notify);
		irq_set_affinity_hint(p->irq, &p->affinity_mask);
		napi_enable(&p->napi);
	}

	return 0;

unroll:
	while (i--) {
		p--;
		free_irq(p->irq, p);
	}
	return err;
}

static void fun_disable_irqs(struct net_device *dev)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct fun_irq *p;
	unsigned int i;

	for (p = fp->irqs, i = 0; i < fp->num_irqs; i++, p++) {
		if (!p->txq && !p->rxq)
			continue;

		napi_disable(&p->napi);
		irq_set_affinity_notifier(p->irq, NULL);
		irq_set_affinity_hint(p->irq, NULL);
		free_irq(p->irq, p);
	}
}

static void fun_down(struct net_device *dev, struct fun_qset *qset)
{
	struct funeth_priv *fp = netdev_priv(dev);

	/* If we don't have queues the data path is already down.
	 * Note netif_running(dev) may be true.
	 */
	if (!rcu_access_pointer(fp->rxqs))
		return;

	/* It is also down if the queues aren't on the device. */
	if (fp->txqs[0]->init_state >= FUN_QSTATE_INIT_FULL) {
		netif_info(fp, ifdown, dev, "Tearing down device data path\n");
		fun_port_write_cmd(fp, FUN_ADMIN_PORT_KEY_DISABLE, 0);

		netif_carrier_off(dev);
		netif_tx_disable(dev);

		fun_destroy_rss(fp);
		fun_res_destroy(fp->fdev, FUN_ADMIN_OP_VI, 0, dev->dev_port);
		fun_disable_irqs(dev);
	}

	fun_free_rings(dev, qset);
}

static int fun_up(struct net_device *dev, struct fun_qset *qset)
{
	static const int port_keys[] = {
		FUN_ADMIN_PORT_KEY_STATS_DMA_LOW,
		FUN_ADMIN_PORT_KEY_STATS_DMA_HIGH,
		FUN_ADMIN_PORT_KEY_ENABLE
	};

	struct funeth_priv *fp = netdev_priv(dev);
	u64 vals[] = {
		lower_32_bits(fp->stats_dma_addr),
		upper_32_bits(fp->stats_dma_addr),
		FUN_PORT_FLAG_ENABLE_NOTIFY
	};
	int err;

	netif_info(fp, ifup, dev, "Setting up data path on device\n");

	if (qset->rxqs[0]->init_state < FUN_QSTATE_INIT_FULL) {
		err = fun_advance_ring_state(dev, qset);
		if (err)
			return err;
	}

	err = fun_vi_create(fp);
	if (err)
		goto free_queues;

	fp->txqs = qset->txqs;
	rcu_assign_pointer(fp->rxqs, qset->rxqs);
	rcu_assign_pointer(fp->xdpqs, qset->xdpqs);

	err = fun_enable_irqs(dev);
	if (err)
		goto destroy_vi;

	if (fp->rss_cfg) {
		err = fun_config_rss(dev, fp->hash_algo, fp->rss_key,
				     fp->indir_table, FUN_ADMIN_SUBOP_CREATE);
	} else {
		/* The non-RSS case has only 1 queue. */
		err = fun_bind(fp->fdev, FUN_ADMIN_BIND_TYPE_VI, dev->dev_port,
			       FUN_ADMIN_BIND_TYPE_EPCQ,
			       qset->rxqs[0]->hw_cqid);
	}
	if (err)
		goto disable_irqs;

	err = fun_port_write_cmds(fp, 3, port_keys, vals);
	if (err)
		goto free_rss;

	netif_tx_start_all_queues(dev);
	return 0;

free_rss:
	fun_destroy_rss(fp);
disable_irqs:
	fun_disable_irqs(dev);
destroy_vi:
	fun_res_destroy(fp->fdev, FUN_ADMIN_OP_VI, 0, dev->dev_port);
free_queues:
	fun_free_rings(dev, qset);
	return err;
}

static int funeth_open(struct net_device *netdev)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	struct fun_qset qset = {
		.nrxqs = netdev->real_num_rx_queues,
		.ntxqs = netdev->real_num_tx_queues,
		.nxdpqs = fp->num_xdpqs,
		.cq_depth = fp->cq_depth,
		.rq_depth = fp->rq_depth,
		.sq_depth = fp->sq_depth,
		.state = FUN_QSTATE_INIT_FULL,
	};
	int rc;

	rc = fun_alloc_rings(netdev, &qset);
	if (rc)
		return rc;

	rc = fun_up(netdev, &qset);
	if (rc) {
		qset.state = FUN_QSTATE_DESTROYED;
		fun_free_rings(netdev, &qset);
	}

	return rc;
}

static int funeth_close(struct net_device *netdev)
{
	struct fun_qset qset = { .state = FUN_QSTATE_DESTROYED };

	fun_down(netdev, &qset);
	return 0;
}

static void fun_get_stats64(struct net_device *netdev,
			    struct rtnl_link_stats64 *stats)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	struct funeth_txq **xdpqs;
	struct funeth_rxq **rxqs;
	unsigned int i, start;

	stats->tx_packets = fp->tx_packets;
	stats->tx_bytes   = fp->tx_bytes;
	stats->tx_dropped = fp->tx_dropped;

	stats->rx_packets = fp->rx_packets;
	stats->rx_bytes   = fp->rx_bytes;
	stats->rx_dropped = fp->rx_dropped;

	rcu_read_lock();
	rxqs = rcu_dereference(fp->rxqs);
	if (!rxqs)
		goto unlock;

	for (i = 0; i < netdev->real_num_tx_queues; i++) {
		struct funeth_txq_stats txs;

		FUN_QSTAT_READ(fp->txqs[i], start, txs);
		stats->tx_packets += txs.tx_pkts;
		stats->tx_bytes   += txs.tx_bytes;
		stats->tx_dropped += txs.tx_map_err + txs.tx_len_err;
	}

	for (i = 0; i < netdev->real_num_rx_queues; i++) {
		struct funeth_rxq_stats rxs;

		FUN_QSTAT_READ(rxqs[i], start, rxs);
		stats->rx_packets += rxs.rx_pkts;
		stats->rx_bytes   += rxs.rx_bytes;
		stats->rx_dropped += rxs.rx_map_err + rxs.rx_mem_drops;
	}

	xdpqs = rcu_dereference(fp->xdpqs);
	if (!xdpqs)
		goto unlock;

	for (i = 0; i < fp->num_xdpqs; i++) {
		struct funeth_txq_stats txs;

		FUN_QSTAT_READ(xdpqs[i], start, txs);
		stats->tx_packets += txs.tx_pkts;
		stats->tx_bytes   += txs.tx_bytes;
	}
unlock:
	rcu_read_unlock();
}

static int fun_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	int rc;

	rc = fun_port_write_cmd(fp, FUN_ADMIN_PORT_KEY_MTU, new_mtu);
	if (!rc)
		netdev->mtu = new_mtu;
	return rc;
}

static int fun_set_macaddr(struct net_device *netdev, void *addr)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	struct sockaddr *saddr = addr;
	int rc;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(netdev->dev_addr, saddr->sa_data))
		return 0;

	rc = fun_port_write_cmd(fp, FUN_ADMIN_PORT_KEY_MACADDR,
				ether_addr_to_u64(saddr->sa_data));
	if (!rc)
		eth_hw_addr_set(netdev, saddr->sa_data);
	return rc;
}

static int fun_get_port_attributes(struct net_device *netdev)
{
	static const int keys[] = {
		FUN_ADMIN_PORT_KEY_MACADDR, FUN_ADMIN_PORT_KEY_CAPABILITIES,
		FUN_ADMIN_PORT_KEY_ADVERT, FUN_ADMIN_PORT_KEY_MTU
	};
	static const int phys_keys[] = {
		FUN_ADMIN_PORT_KEY_LANE_ATTRS,
	};

	struct funeth_priv *fp = netdev_priv(netdev);
	u64 data[ARRAY_SIZE(keys)];
	u8 mac[ETH_ALEN];
	int i, rc;

	rc = fun_port_read_cmds(fp, ARRAY_SIZE(keys), keys, data);
	if (rc)
		return rc;

	for (i = 0; i < ARRAY_SIZE(keys); i++) {
		switch (keys[i]) {
		case FUN_ADMIN_PORT_KEY_MACADDR:
			u64_to_ether_addr(data[i], mac);
			if (is_zero_ether_addr(mac)) {
				eth_hw_addr_random(netdev);
			} else if (is_valid_ether_addr(mac)) {
				eth_hw_addr_set(netdev, mac);
			} else {
				netdev_err(netdev,
					   "device provided a bad MAC address %pM\n",
					   mac);
				return -EINVAL;
			}
			break;

		case FUN_ADMIN_PORT_KEY_CAPABILITIES:
			fp->port_caps = data[i];
			break;

		case FUN_ADMIN_PORT_KEY_ADVERT:
			fp->advertising = data[i];
			break;

		case FUN_ADMIN_PORT_KEY_MTU:
			netdev->mtu = data[i];
			break;
		}
	}

	if (!(fp->port_caps & FUN_PORT_CAP_VPORT)) {
		rc = fun_port_read_cmds(fp, ARRAY_SIZE(phys_keys), phys_keys,
					data);
		if (rc)
			return rc;

		fp->lane_attrs = data[0];
	}

	if (netdev->addr_assign_type == NET_ADDR_RANDOM)
		return fun_port_write_cmd(fp, FUN_ADMIN_PORT_KEY_MACADDR,
					  ether_addr_to_u64(netdev->dev_addr));
	return 0;
}

static int fun_hwtstamp_get(struct net_device *dev, struct ifreq *ifr)
{
	const struct funeth_priv *fp = netdev_priv(dev);

	return copy_to_user(ifr->ifr_data, &fp->hwtstamp_cfg,
			    sizeof(fp->hwtstamp_cfg)) ? -EFAULT : 0;
}

static int fun_hwtstamp_set(struct net_device *dev, struct ifreq *ifr)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct hwtstamp_config cfg;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	if (cfg.flags)           /* flags is reserved, must be 0 */
		return -EINVAL;

	/* no TX HW timestamps */
	cfg.tx_type = HWTSTAMP_TX_OFF;

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_NTP_ALL:
		cfg.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	fp->hwtstamp_cfg = cfg;
	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static int fun_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCSHWTSTAMP:
		return fun_hwtstamp_set(dev, ifr);
	case SIOCGHWTSTAMP:
		return fun_hwtstamp_get(dev, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

/* Prepare the queues for XDP. */
static int fun_enter_xdp(struct net_device *dev, struct bpf_prog *prog)
{
	struct funeth_priv *fp = netdev_priv(dev);
	unsigned int i, nqs = num_online_cpus();
	struct funeth_txq **xdpqs;
	struct funeth_rxq **rxqs;
	int err;

	xdpqs = alloc_xdpqs(dev, nqs, fp->sq_depth, FUN_QSTATE_INIT_FULL);
	if (IS_ERR(xdpqs))
		return PTR_ERR(xdpqs);

	rxqs = rtnl_dereference(fp->rxqs);
	for (i = 0; i < dev->real_num_rx_queues; i++) {
		err = fun_rxq_set_bpf(rxqs[i], prog);
		if (err)
			goto out;
	}

	fp->num_xdpqs = nqs;
	rcu_assign_pointer(fp->xdpqs, xdpqs);
	return 0;
out:
	while (i--)
		fun_rxq_set_bpf(rxqs[i], NULL);

	free_xdpqs(xdpqs, nqs, FUN_QSTATE_DESTROYED);
	return err;
}

/* Set the queues for non-XDP operation. */
static void fun_end_xdp(struct net_device *dev)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct funeth_txq **xdpqs;
	struct funeth_rxq **rxqs;
	unsigned int i;

	xdpqs = rtnl_dereference(fp->xdpqs);
	rcu_assign_pointer(fp->xdpqs, NULL);
	synchronize_net();
	/* at this point both Rx and Tx XDP processing has ended */

	free_xdpqs(xdpqs, fp->num_xdpqs, FUN_QSTATE_DESTROYED);
	fp->num_xdpqs = 0;

	rxqs = rtnl_dereference(fp->rxqs);
	for (i = 0; i < dev->real_num_rx_queues; i++)
		fun_rxq_set_bpf(rxqs[i], NULL);
}

#define XDP_MAX_MTU \
	(PAGE_SIZE - FUN_XDP_HEADROOM - VLAN_ETH_HLEN - FUN_RX_TAILROOM)

static int fun_xdp_setup(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct bpf_prog *old_prog, *prog = xdp->prog;
	struct funeth_priv *fp = netdev_priv(dev);
	int i, err;

	/* XDP uses at most one buffer */
	if (prog && dev->mtu > XDP_MAX_MTU) {
		netdev_err(dev, "device MTU %u too large for XDP\n", dev->mtu);
		NL_SET_ERR_MSG_MOD(xdp->extack,
				   "Device MTU too large for XDP");
		return -EINVAL;
	}

	if (!netif_running(dev)) {
		fp->num_xdpqs = prog ? num_online_cpus() : 0;
	} else if (prog && !fp->xdp_prog) {
		err = fun_enter_xdp(dev, prog);
		if (err) {
			NL_SET_ERR_MSG_MOD(xdp->extack,
					   "Failed to set queues for XDP.");
			return err;
		}
	} else if (!prog && fp->xdp_prog) {
		fun_end_xdp(dev);
	} else {
		struct funeth_rxq **rxqs = rtnl_dereference(fp->rxqs);

		for (i = 0; i < dev->real_num_rx_queues; i++)
			WRITE_ONCE(rxqs[i]->xdp_prog, prog);
	}

	dev->max_mtu = prog ? XDP_MAX_MTU : FUN_MAX_MTU;
	old_prog = xchg(&fp->xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

	return 0;
}

static int fun_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return fun_xdp_setup(dev, xdp);
	default:
		return -EINVAL;
	}
}

static struct devlink_port *fun_get_devlink_port(struct net_device *netdev)
{
	struct funeth_priv *fp = netdev_priv(netdev);

	return &fp->dl_port;
}

static int fun_init_vports(struct fun_ethdev *ed, unsigned int n)
{
	if (ed->num_vports)
		return -EINVAL;

	ed->vport_info = kvcalloc(n, sizeof(*ed->vport_info), GFP_KERNEL);
	if (!ed->vport_info)
		return -ENOMEM;
	ed->num_vports = n;
	return 0;
}

static void fun_free_vports(struct fun_ethdev *ed)
{
	kvfree(ed->vport_info);
	ed->vport_info = NULL;
	ed->num_vports = 0;
}

static struct fun_vport_info *fun_get_vport(struct fun_dev *fdev,
					    unsigned int vport)
{
	struct fun_ethdev *ed = to_fun_ethdev(fdev);

	if (!ed->vport_info || vport >= ed->num_vports)
		return NULL;

	return ed->vport_info + vport;
}

static int fun_set_vf_mac(struct net_device *dev, int vf, u8 *mac)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct fun_dev *fdev = fp->fdev;
	struct fun_vport_info *vi = fun_get_vport(fdev, vf);
	struct fun_adi_param mac_param = {};
	int rc;

	if (!vi)
		return -EINVAL;
	if (is_multicast_ether_addr(mac))
		return -EINVAL;

	mac_param.u.mac = FUN_ADI_MAC_INIT(ether_addr_to_u64(mac));
	rc = fun_adi_write(fdev, FUN_ADMIN_ADI_ATTR_MACADDR, vf + 1,
			   &mac_param);
	if (!rc)
		ether_addr_copy(vi->mac, mac);
	return rc;
}

static int fun_set_vf_vlan(struct net_device *dev, int vf, u16 vlan, u8 qos,
			   __be16 vlan_proto)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct fun_dev *fdev = fp->fdev;
	struct fun_vport_info *vi = fun_get_vport(fdev, vf);
	struct fun_adi_param vlan_param = {};
	int rc;

	if (!vi)
		return -EINVAL;
	if (vlan > 4095 || qos > 7)
		return -EINVAL;
	if (vlan_proto && vlan_proto != htons(ETH_P_8021Q) &&
	    vlan_proto != htons(ETH_P_8021AD))
		return -EINVAL;

	vlan_param.u.vlan = FUN_ADI_VLAN_INIT(be16_to_cpu(vlan_proto),
					      ((u16)qos << VLAN_PRIO_SHIFT) | vlan);
	rc = fun_adi_write(fdev, FUN_ADMIN_ADI_ATTR_VLAN, vf + 1, &vlan_param);
	if (rc)
		return rc;

	vi->vlan = vlan;
	vi->qos = qos;
	vi->vlan_proto = vlan_proto;
	return 0;
}

static int fun_set_vf_rate(struct net_device *dev, int vf, int min_tx_rate,
			   int max_tx_rate)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct fun_dev *fdev = fp->fdev;
	struct fun_vport_info *vi = fun_get_vport(fdev, vf);
	struct fun_adi_param rate_param = {};
	int rc;

	if (!vi || min_tx_rate)
		return -EINVAL;

	rate_param.u.rate = FUN_ADI_RATE_INIT(max_tx_rate);
	rc = fun_adi_write(fdev, FUN_ADMIN_ADI_ATTR_RATE, vf + 1, &rate_param);
	if (rc)
		return rc;

	vi->max_rate = max_tx_rate;
	return 0;
}

static int fun_get_vf_config(struct net_device *dev, int vf,
			     struct ifla_vf_info *ivi)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct fun_dev *fdev = fp->fdev;
	const struct fun_vport_info *vi = fun_get_vport(fdev, vf);

	if (!vi)
		return -EINVAL;

	memset(ivi, 0, sizeof(*ivi));
	ivi->vf = vf;
	ether_addr_copy(ivi->mac, vi->mac);
	ivi->vlan = vi->vlan;
	ivi->qos = vi->qos;
	ivi->vlan_proto = vi->vlan_proto;
	ivi->max_tx_rate = vi->max_rate;
	ivi->spoofchk = vi->spoofchk;
	return 0;
}

static const struct net_device_ops fun_netdev_ops = {
	.ndo_open		= funeth_open,
	.ndo_stop		= funeth_close,
	.ndo_start_xmit		= fun_start_xmit,
	.ndo_get_stats64	= fun_get_stats64,
	.ndo_change_mtu		= fun_change_mtu,
	.ndo_set_mac_address	= fun_set_macaddr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= fun_ioctl,
	.ndo_uninit		= fun_free_queue_irqs,
	.ndo_bpf		= fun_xdp,
	.ndo_xdp_xmit		= fun_xdp_xmit_frames,
	.ndo_set_vf_mac		= fun_set_vf_mac,
	.ndo_set_vf_vlan	= fun_set_vf_vlan,
	.ndo_set_vf_rate	= fun_set_vf_rate,
	.ndo_get_vf_config	= fun_get_vf_config,
	.ndo_get_devlink_port	= fun_get_devlink_port,
};

#define GSO_ENCAP_FLAGS (NETIF_F_GSO_GRE | NETIF_F_GSO_IPXIP4 | \
			 NETIF_F_GSO_IPXIP6 | NETIF_F_GSO_UDP_TUNNEL | \
			 NETIF_F_GSO_UDP_TUNNEL_CSUM)
#define TSO_FLAGS (NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_TSO_ECN)
#define VLAN_FEAT (NETIF_F_SG | NETIF_F_HW_CSUM | TSO_FLAGS | \
		   GSO_ENCAP_FLAGS | NETIF_F_HIGHDMA)

static void fun_dflt_rss_indir(struct funeth_priv *fp, unsigned int nrx)
{
	unsigned int i;

	for (i = 0; i < fp->indir_table_nentries; i++)
		fp->indir_table[i] = ethtool_rxfh_indir_default(i, nrx);
}

/* Reset the RSS indirection table to equal distribution across the current
 * number of Rx queues. Called at init time and whenever the number of Rx
 * queues changes subsequently. Note that this may also resize the indirection
 * table.
 */
static void fun_reset_rss_indir(struct net_device *dev)
{
	struct funeth_priv *fp = netdev_priv(dev);

	if (!fp->rss_cfg)
		return;

	/* Set the table size to the max possible that allows an equal number
	 * of occurrences of each CQ.
	 */
	fp->indir_table_nentries = rounddown(FUN_ETH_RSS_MAX_INDIR_ENT,
					     dev->real_num_rx_queues);
	fun_dflt_rss_indir(fp, dev->real_num_rx_queues);
}

/* Allocate the DMA area for the RSS configuration commands to the device, and
 * initialize the hash, hash key, indirection table size and its entries to
 * their defaults. The indirection table defaults to equal distribution across
 * the Rx queues.
 */
static int fun_init_rss(struct net_device *dev)
{
	struct funeth_priv *fp = netdev_priv(dev);
	size_t size = sizeof(fp->rss_key) + sizeof(fp->indir_table);

	fp->rss_hw_id = FUN_HCI_ID_INVALID;
	if (!(fp->port_caps & FUN_PORT_CAP_OFFLOADS))
		return 0;

	fp->rss_cfg = dma_alloc_coherent(&fp->pdev->dev, size,
					 &fp->rss_dma_addr, GFP_KERNEL);
	if (!fp->rss_cfg)
		return -ENOMEM;

	fp->hash_algo = FUN_ETH_RSS_ALG_TOEPLITZ;
	netdev_rss_key_fill(fp->rss_key, sizeof(fp->rss_key));
	fun_reset_rss_indir(dev);
	return 0;
}

static void fun_free_rss(struct funeth_priv *fp)
{
	if (fp->rss_cfg) {
		dma_free_coherent(&fp->pdev->dev,
				  sizeof(fp->rss_key) + sizeof(fp->indir_table),
				  fp->rss_cfg, fp->rss_dma_addr);
		fp->rss_cfg = NULL;
	}
}

void fun_set_ring_count(struct net_device *netdev, unsigned int ntx,
			unsigned int nrx)
{
	netif_set_real_num_tx_queues(netdev, ntx);
	if (nrx != netdev->real_num_rx_queues) {
		netif_set_real_num_rx_queues(netdev, nrx);
		fun_reset_rss_indir(netdev);
	}
}

static int fun_init_stats_area(struct funeth_priv *fp)
{
	unsigned int nstats;

	if (!(fp->port_caps & FUN_PORT_CAP_STATS))
		return 0;

	nstats = PORT_MAC_RX_STATS_MAX + PORT_MAC_TX_STATS_MAX +
		 PORT_MAC_FEC_STATS_MAX;

	fp->stats = dma_alloc_coherent(&fp->pdev->dev, nstats * sizeof(u64),
				       &fp->stats_dma_addr, GFP_KERNEL);
	if (!fp->stats)
		return -ENOMEM;
	return 0;
}

static void fun_free_stats_area(struct funeth_priv *fp)
{
	unsigned int nstats;

	if (fp->stats) {
		nstats = PORT_MAC_RX_STATS_MAX + PORT_MAC_TX_STATS_MAX;
		dma_free_coherent(&fp->pdev->dev, nstats * sizeof(u64),
				  fp->stats, fp->stats_dma_addr);
		fp->stats = NULL;
	}
}

static int fun_dl_port_register(struct net_device *netdev)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	struct devlink *dl = priv_to_devlink(fp->fdev);
	struct devlink_port_attrs attrs = {};
	unsigned int idx;

	if (fp->port_caps & FUN_PORT_CAP_VPORT) {
		attrs.flavour = DEVLINK_PORT_FLAVOUR_VIRTUAL;
		idx = fp->lport;
	} else {
		idx = netdev->dev_port;
		attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
		attrs.lanes = fp->lane_attrs & 7;
		if (fp->lane_attrs & FUN_PORT_LANE_SPLIT) {
			attrs.split = 1;
			attrs.phys.port_number = fp->lport & ~3;
			attrs.phys.split_subport_number = fp->lport & 3;
		} else {
			attrs.phys.port_number = fp->lport;
		}
	}

	devlink_port_attrs_set(&fp->dl_port, &attrs);

	return devlink_port_register(dl, &fp->dl_port, idx);
}

/* Determine the max Tx/Rx queues for a port. */
static int fun_max_qs(struct fun_ethdev *ed, unsigned int *ntx,
		      unsigned int *nrx)
{
	int neth;

	if (ed->num_ports > 1 || is_kdump_kernel()) {
		*ntx = 1;
		*nrx = 1;
		return 0;
	}

	neth = fun_get_res_count(&ed->fdev, FUN_ADMIN_OP_ETH);
	if (neth < 0)
		return neth;

	/* We determine the max number of queues based on the CPU
	 * cores, device interrupts and queues, RSS size, and device Tx flows.
	 *
	 * - At least 1 Rx and 1 Tx queues.
	 * - At most 1 Rx/Tx queue per core.
	 * - Each Rx/Tx queue needs 1 SQ.
	 */
	*ntx = min(ed->nsqs_per_port - 1, num_online_cpus());
	*nrx = *ntx;
	if (*ntx > neth)
		*ntx = neth;
	if (*nrx > FUN_ETH_RSS_MAX_INDIR_ENT)
		*nrx = FUN_ETH_RSS_MAX_INDIR_ENT;
	return 0;
}

static void fun_queue_defaults(struct net_device *dev, unsigned int nsqs)
{
	unsigned int ntx, nrx;

	ntx = min(dev->num_tx_queues, FUN_DFLT_QUEUES);
	nrx = min(dev->num_rx_queues, FUN_DFLT_QUEUES);
	if (ntx <= nrx) {
		ntx = min(ntx, nsqs / 2);
		nrx = min(nrx, nsqs - ntx);
	} else {
		nrx = min(nrx, nsqs / 2);
		ntx = min(ntx, nsqs - nrx);
	}

	netif_set_real_num_tx_queues(dev, ntx);
	netif_set_real_num_rx_queues(dev, nrx);
}

/* In case of error, this returns a negative result for errors prior to
 * fun_down(), and inverted positive values for errors after.
 */
int fun_replace_qs(struct net_device *dev, struct fun_qset *newqs)
{
	struct fun_qset oldqs = { .state = FUN_QSTATE_INIT_SW };
	bool differ;
	int err;

	newqs->state = FUN_QSTATE_INIT_SW;
	err = fun_alloc_rings(dev, newqs);
	if (err)
		return err;

	fun_down(dev, &oldqs);

	differ = newqs->ntxqs != oldqs.ntxqs || newqs->nrxqs != oldqs.nrxqs;
	if (differ)
		fun_set_ring_count(dev, newqs->ntxqs, newqs->nrxqs);

	err = fun_up(dev, newqs);
	if (!err)
		goto keep_new;

	/* the new queues couldn't be installed */
	newqs->state = FUN_QSTATE_DESTROYED;
	fun_free_rings(dev, newqs);
	if (differ)
		fun_set_ring_count(dev, oldqs.ntxqs, oldqs.nrxqs);

	/* If we are not increasing queues there isn't any reason to attempt
	 * restoring the old queues, they'll also fail. Queue depth changes
	 * do not matter to the device,
	 */
	if (newqs->nrxqs > oldqs.nrxqs || newqs->ntxqs > oldqs.ntxqs) {
		if (!fun_up(dev, &oldqs))
			return -err;
	}

	oldqs.state = FUN_QSTATE_DESTROYED;
	fun_free_rings(dev, &oldqs);
	return -err;

keep_new:
	oldqs.state = FUN_QSTATE_DESTROYED;
	fun_free_rings(dev, &oldqs);
	return 0;
}

static int fun_create_netdev(struct fun_ethdev *ed, unsigned int portid)
{
	struct fun_dev *fdev = &ed->fdev;
	struct net_device *netdev;
	struct funeth_priv *fp;
	unsigned int ntx, nrx;
	int rc;

	rc = fun_max_qs(ed, &ntx, &nrx);
	if (rc)
		return rc;

	netdev = alloc_etherdev_mqs(sizeof(*fp), ntx, nrx);
	if (!netdev) {
		rc = -ENOMEM;
		goto done;
	}

	netdev->dev_port = portid;
	fun_queue_defaults(netdev, ed->nsqs_per_port);

	fp = netdev_priv(netdev);
	fp->fdev = fdev;
	fp->pdev = to_pci_dev(fdev->dev);
	fp->netdev = netdev;
	fp->ethid_start = portid;
	seqcount_init(&fp->link_seq);

	fp->lport = INVALID_LPORT;
	rc = fun_port_create(netdev);
	if (rc)
		goto free_netdev;

	/* bind port to admin CQ for async events */
	rc = fun_bind(fdev, FUN_ADMIN_BIND_TYPE_PORT, portid,
		      FUN_ADMIN_BIND_TYPE_EPCQ, 0);
	if (rc)
		goto destroy_port;

	rc = fun_get_port_attributes(netdev);
	if (rc)
		goto destroy_port;

	rc = fun_init_rss(netdev);
	if (rc)
		goto destroy_port;

	rc = fun_init_stats_area(fp);
	if (rc)
		goto free_rss;

	SET_NETDEV_DEV(netdev, fdev->dev);
	netdev->netdev_ops = &fun_netdev_ops;

	netdev->hw_features = NETIF_F_SG | NETIF_F_RXHASH | NETIF_F_RXCSUM;
	if (fp->port_caps & FUN_PORT_CAP_OFFLOADS)
		netdev->hw_features |= NETIF_F_HW_CSUM | TSO_FLAGS;
	if (fp->port_caps & FUN_PORT_CAP_ENCAP_OFFLOADS)
		netdev->hw_features |= GSO_ENCAP_FLAGS;

	netdev->features |= netdev->hw_features | NETIF_F_HIGHDMA;
	netdev->vlan_features = netdev->features & VLAN_FEAT;
	netdev->mpls_features = netdev->vlan_features;
	netdev->hw_enc_features = netdev->hw_features;

	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = FUN_MAX_MTU;

	fun_set_ethtool_ops(netdev);

	/* configurable parameters */
	fp->sq_depth = min(SQ_DEPTH, fdev->q_depth);
	fp->cq_depth = min(CQ_DEPTH, fdev->q_depth);
	fp->rq_depth = min_t(unsigned int, RQ_DEPTH, fdev->q_depth);
	fp->rx_coal_usec  = CQ_INTCOAL_USEC;
	fp->rx_coal_count = CQ_INTCOAL_NPKT;
	fp->tx_coal_usec  = SQ_INTCOAL_USEC;
	fp->tx_coal_count = SQ_INTCOAL_NPKT;
	fp->cq_irq_db = FUN_IRQ_CQ_DB(fp->rx_coal_usec, fp->rx_coal_count);

	rc = fun_dl_port_register(netdev);
	if (rc)
		goto free_stats;

	fp->ktls_id = FUN_HCI_ID_INVALID;
	fun_ktls_init(netdev);            /* optional, failure OK */

	netif_carrier_off(netdev);
	ed->netdevs[portid] = netdev;
	rc = register_netdev(netdev);
	if (rc)
		goto unreg_devlink;

	if (fp->dl_port.devlink)
		devlink_port_type_eth_set(&fp->dl_port, netdev);

	return 0;

unreg_devlink:
	ed->netdevs[portid] = NULL;
	fun_ktls_cleanup(fp);
	if (fp->dl_port.devlink)
		devlink_port_unregister(&fp->dl_port);
free_stats:
	fun_free_stats_area(fp);
free_rss:
	fun_free_rss(fp);
destroy_port:
	fun_port_destroy(netdev);
free_netdev:
	free_netdev(netdev);
done:
	dev_err(fdev->dev, "couldn't allocate port %u, error %d", portid, rc);
	return rc;
}

static void fun_destroy_netdev(struct net_device *netdev)
{
	struct funeth_priv *fp;

	fp = netdev_priv(netdev);
	if (fp->dl_port.devlink) {
		devlink_port_type_clear(&fp->dl_port);
		devlink_port_unregister(&fp->dl_port);
	}
	unregister_netdev(netdev);
	fun_ktls_cleanup(fp);
	fun_free_stats_area(fp);
	fun_free_rss(fp);
	fun_port_destroy(netdev);
	free_netdev(netdev);
}

static int fun_create_ports(struct fun_ethdev *ed, unsigned int nports)
{
	struct fun_dev *fd = &ed->fdev;
	int i, rc;

	/* The admin queue takes 1 IRQ and 2 SQs. */
	ed->nsqs_per_port = min(fd->num_irqs - 1,
				fd->kern_end_qid - 2) / nports;
	if (ed->nsqs_per_port < 2) {
		dev_err(fd->dev, "Too few SQs for %u ports", nports);
		return -EINVAL;
	}

	ed->netdevs = kcalloc(nports, sizeof(*ed->netdevs), GFP_KERNEL);
	if (!ed->netdevs)
		return -ENOMEM;

	ed->num_ports = nports;
	for (i = 0; i < nports; i++) {
		rc = fun_create_netdev(ed, i);
		if (rc)
			goto free_netdevs;
	}

	return 0;

free_netdevs:
	while (i)
		fun_destroy_netdev(ed->netdevs[--i]);
	kfree(ed->netdevs);
	ed->netdevs = NULL;
	ed->num_ports = 0;
	return rc;
}

static void fun_destroy_ports(struct fun_ethdev *ed)
{
	unsigned int i;

	for (i = 0; i < ed->num_ports; i++)
		fun_destroy_netdev(ed->netdevs[i]);

	kfree(ed->netdevs);
	ed->netdevs = NULL;
	ed->num_ports = 0;
}

static void fun_update_link_state(const struct fun_ethdev *ed,
				  const struct fun_admin_port_notif *notif)
{
	unsigned int port_idx = be16_to_cpu(notif->id);
	struct net_device *netdev;
	struct funeth_priv *fp;

	if (port_idx >= ed->num_ports)
		return;

	netdev = ed->netdevs[port_idx];
	fp = netdev_priv(netdev);

	write_seqcount_begin(&fp->link_seq);
	fp->link_speed = be32_to_cpu(notif->speed) * 10;  /* 10 Mbps->Mbps */
	fp->active_fc = notif->flow_ctrl;
	fp->active_fec = notif->fec;
	fp->xcvr_type = notif->xcvr_type;
	fp->link_down_reason = notif->link_down_reason;
	fp->lp_advertising = be64_to_cpu(notif->lp_advertising);

	if ((notif->link_state | notif->missed_events) & FUN_PORT_FLAG_MAC_DOWN)
		netif_carrier_off(netdev);
	if (notif->link_state & FUN_PORT_FLAG_MAC_UP)
		netif_carrier_on(netdev);

	write_seqcount_end(&fp->link_seq);
	fun_report_link(netdev);
}

/* handler for async events delivered through the admin CQ */
static void fun_event_cb(struct fun_dev *fdev, void *entry)
{
	u8 op = ((struct fun_admin_rsp_common *)entry)->op;

	if (op == FUN_ADMIN_OP_PORT) {
		const struct fun_admin_port_notif *rsp = entry;

		if (rsp->subop == FUN_ADMIN_SUBOP_NOTIFY) {
			fun_update_link_state(to_fun_ethdev(fdev), rsp);
		} else if (rsp->subop == FUN_ADMIN_SUBOP_RES_COUNT) {
			const struct fun_admin_res_count_rsp *r = entry;

			if (r->count.data)
				set_bit(FUN_SERV_RES_CHANGE, &fdev->service_flags);
			else
				set_bit(FUN_SERV_DEL_PORTS, &fdev->service_flags);
			fun_serv_sched(fdev);
		} else {
			dev_info(fdev->dev, "adminq event unexpected op %u subop %u",
				 op, rsp->subop);
		}
	} else {
		dev_info(fdev->dev, "adminq event unexpected op %u", op);
	}
}

/* handler for pending work managed by the service task */
static void fun_service_cb(struct fun_dev *fdev)
{
	struct fun_ethdev *ed = to_fun_ethdev(fdev);
	int rc;

	if (test_and_clear_bit(FUN_SERV_DEL_PORTS, &fdev->service_flags))
		fun_destroy_ports(ed);

	if (!test_and_clear_bit(FUN_SERV_RES_CHANGE, &fdev->service_flags))
		return;

	rc = fun_get_res_count(fdev, FUN_ADMIN_OP_PORT);
	if (rc < 0 || rc == ed->num_ports)
		return;

	if (ed->num_ports)
		fun_destroy_ports(ed);
	if (rc)
		fun_create_ports(ed, rc);
}

static int funeth_sriov_configure(struct pci_dev *pdev, int nvfs)
{
	struct fun_dev *fdev = pci_get_drvdata(pdev);
	struct fun_ethdev *ed = to_fun_ethdev(fdev);
	int rc;

	if (nvfs == 0) {
		if (pci_vfs_assigned(pdev)) {
			dev_warn(&pdev->dev,
				 "Cannot disable SR-IOV while VFs are assigned\n");
			return -EPERM;
		}

		pci_disable_sriov(pdev);
		fun_free_vports(ed);
		return 0;
	}

	rc = fun_init_vports(ed, nvfs);
	if (rc)
		return rc;

	rc = pci_enable_sriov(pdev, nvfs);
	if (rc) {
		fun_free_vports(ed);
		return rc;
	}

	return nvfs;
}

static int funeth_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct fun_dev_params aqreq = {
		.cqe_size_log2 = ilog2(ADMIN_CQE_SIZE),
		.sqe_size_log2 = ilog2(ADMIN_SQE_SIZE),
		.cq_depth      = ADMIN_CQ_DEPTH,
		.sq_depth      = ADMIN_SQ_DEPTH,
		.rq_depth      = ADMIN_RQ_DEPTH,
		.min_msix      = 2,              /* 1 Rx + 1 Tx */
		.event_cb      = fun_event_cb,
		.serv_cb       = fun_service_cb,
	};
	struct devlink *devlink;
	struct fun_ethdev *ed;
	struct fun_dev *fdev;
	int rc;

	devlink = fun_devlink_alloc(&pdev->dev);
	if (!devlink) {
		dev_err(&pdev->dev, "devlink alloc failed\n");
		return -ENOMEM;
	}

	ed = devlink_priv(devlink);

	fdev = &ed->fdev;
	rc = fun_dev_enable(fdev, pdev, &aqreq, KBUILD_MODNAME);
	if (rc)
		goto free_devlink;

	rc = fun_get_res_count(fdev, FUN_ADMIN_OP_PORT);
	if (rc > 0)
		rc = fun_create_ports(ed, rc);
	if (rc < 0)
		goto disable_dev;

	fun_serv_restart(fdev);
	fun_devlink_register(devlink);
	return 0;

disable_dev:
	fun_dev_disable(fdev);
free_devlink:
	fun_devlink_free(devlink);
	return rc;
}

static void funeth_remove(struct pci_dev *pdev)
{
	struct fun_dev *fdev = pci_get_drvdata(pdev);
	struct devlink *devlink;
	struct fun_ethdev *ed;

	ed = to_fun_ethdev(fdev);
	devlink = priv_to_devlink(ed);
	fun_devlink_unregister(devlink);

#ifdef CONFIG_PCI_IOV
	funeth_sriov_configure(pdev, 0);
#endif

	fun_serv_stop(fdev);
	fun_destroy_ports(ed);
	fun_dev_disable(fdev);

	fun_devlink_free(devlink);
}

static struct pci_driver funeth_driver = {
	.name		 = KBUILD_MODNAME,
	.id_table	 = funeth_id_table,
	.probe		 = funeth_probe,
	.remove		 = funeth_remove,
	.shutdown	 = funeth_remove,
	.sriov_configure = funeth_sriov_configure,
};

module_pci_driver(funeth_driver);

MODULE_AUTHOR("Dimitris Michailidis <dmichail@fungible.com>");
MODULE_DESCRIPTION("Fungible Ethernet Network Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DEVICE_TABLE(pci, funeth_id_table);
