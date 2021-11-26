// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi SoCs FDMA driver
 *
 * Copyright (c) 2021 Microchip
 */

#include <linux/bitops.h>
#include <linux/dmapool.h>
#include <linux/dsa/ocelot.h>
#include <linux/netdevice.h>
#include <linux/of_platform.h>
#include <linux/skbuff.h>

#include "ocelot_fdma.h"
#include "ocelot_qs.h"

#define MSCC_FDMA_DCB_LLP(x)			((x) * 4 + 0x0)
#define MSCC_FDMA_DCB_LLP_PREV(x)		((x) * 4 + 0xA0)

#define MSCC_FDMA_DCB_STAT_BLOCKO(x)		(((x) << 20) & GENMASK(31, 20))
#define MSCC_FDMA_DCB_STAT_BLOCKO_M		GENMASK(31, 20)
#define MSCC_FDMA_DCB_STAT_BLOCKO_X(x)		(((x) & GENMASK(31, 20)) >> 20)
#define MSCC_FDMA_DCB_STAT_PD			BIT(19)
#define MSCC_FDMA_DCB_STAT_ABORT		BIT(18)
#define MSCC_FDMA_DCB_STAT_EOF			BIT(17)
#define MSCC_FDMA_DCB_STAT_SOF			BIT(16)
#define MSCC_FDMA_DCB_STAT_BLOCKL_M		GENMASK(15, 0)
#define MSCC_FDMA_DCB_STAT_BLOCKL(x)		((x) & GENMASK(15, 0))

#define MSCC_FDMA_CH_SAFE			0xcc

#define MSCC_FDMA_CH_ACTIVATE			0xd0

#define MSCC_FDMA_CH_DISABLE			0xd4

#define MSCC_FDMA_EVT_ERR			0x164

#define MSCC_FDMA_EVT_ERR_CODE			0x168

#define MSCC_FDMA_INTR_LLP			0x16c

#define MSCC_FDMA_INTR_LLP_ENA			0x170

#define MSCC_FDMA_INTR_FRM			0x174

#define MSCC_FDMA_INTR_FRM_ENA			0x178

#define MSCC_FDMA_INTR_ENA			0x184

#define MSCC_FDMA_INTR_IDENT			0x188

#define MSCC_FDMA_INJ_CHAN			2
#define MSCC_FDMA_XTR_CHAN			0

#define OCELOT_FDMA_RX_MTU			ETH_DATA_LEN
#define OCELOT_FDMA_WEIGHT			32
#define OCELOT_FDMA_RX_REFILL_COUNT		(OCELOT_FDMA_MAX_DCB / 2)

#define OCELOT_FDMA_CH_SAFE_TIMEOUT_MS		100

#define OCELOT_FDMA_RX_EXTRA_SIZE \
				(OCELOT_TAG_LEN + ETH_FCS_LEN + ETH_HLEN)

static int ocelot_fdma_rx_buf_size(int mtu)
{
	return ALIGN(mtu + OCELOT_FDMA_RX_EXTRA_SIZE, 4);
}

static void ocelot_fdma_writel(struct ocelot_fdma *fdma, u32 reg, u32 data)
{
	writel(data, fdma->base + reg);
}

static u32 ocelot_fdma_readl(struct ocelot_fdma *fdma, u32 reg)
{
	return readl(fdma->base + reg);
}

static unsigned int ocelot_fdma_idx_incr(unsigned int idx)
{
	idx++;
	if (idx == OCELOT_FDMA_MAX_DCB)
		idx = 0;

	return idx;
}

static unsigned int ocelot_fdma_idx_decr(unsigned int idx)
{
	if (idx == 0)
		idx = OCELOT_FDMA_MAX_DCB - 1;
	else
		idx--;

	return idx;
}

static int ocelot_fdma_tx_free_count(struct ocelot_fdma *fdma)
{
	struct ocelot_fdma_ring *ring = &fdma->inj;

	if (ring->tail >= ring->head)
		return OCELOT_FDMA_MAX_DCB - (ring->tail - ring->head) - 1;
	else
		return ring->head - ring->tail - 1;
}

static bool ocelot_fdma_ring_empty(struct ocelot_fdma_ring *ring)
{
	return ring->head == ring->tail;
}

static void ocelot_fdma_activate_chan(struct ocelot_fdma *fdma,
				      struct ocelot_fdma_dcb *dcb, int chan)
{
	ocelot_fdma_writel(fdma, MSCC_FDMA_DCB_LLP(chan), dcb->hw_dma);
	ocelot_fdma_writel(fdma, MSCC_FDMA_CH_ACTIVATE, BIT(chan));
}

static int ocelot_fdma_wait_chan_safe(struct ocelot_fdma *fdma, int chan)
{
	unsigned long timeout;
	u32 safe;

	timeout = jiffies + msecs_to_jiffies(OCELOT_FDMA_CH_SAFE_TIMEOUT_MS);
	do {
		safe = ocelot_fdma_readl(fdma, MSCC_FDMA_CH_SAFE);
		if (safe & BIT(chan))
			return 0;
	} while (time_after(jiffies, timeout));

	return -ETIMEDOUT;
}

static int ocelot_fdma_stop_channel(struct ocelot_fdma *fdma, int chan)
{
	ocelot_fdma_writel(fdma, MSCC_FDMA_CH_DISABLE, BIT(chan));

	return ocelot_fdma_wait_chan_safe(fdma, chan);
}

static bool ocelot_fdma_dcb_set_data(struct ocelot_fdma *fdma,
				     struct ocelot_fdma_dcb *dcb,
				     struct sk_buff *skb,
				     size_t size, enum dma_data_direction dir)
{
	struct ocelot_fdma_dcb_hw_v2 *hw = dcb->hw;
	u32 offset;

	dcb->skb = skb;
	dcb->mapped_size = size;
	dcb->mapping = dma_map_single(fdma->dev, skb->data, size, dir);
	if (unlikely(dma_mapping_error(fdma->dev, dcb->mapping)))
		return false;

	offset = dcb->mapping & 0x3;

	hw->llp = 0;
	hw->datap = ALIGN_DOWN(dcb->mapping, 4);
	hw->datal = ALIGN_DOWN(size, 4);
	hw->stat = MSCC_FDMA_DCB_STAT_BLOCKO(offset);

	return true;
}

static bool ocelot_fdma_rx_set_skb(struct ocelot_fdma *fdma,
				   struct ocelot_fdma_dcb *dcb,
				   struct sk_buff *skb, size_t size)
{
	return ocelot_fdma_dcb_set_data(fdma, dcb, skb, size,
					DMA_FROM_DEVICE);
}

static bool ocelot_fdma_tx_dcb_set_skb(struct ocelot_fdma *fdma,
				       struct ocelot_fdma_dcb *dcb,
				       struct sk_buff *skb)
{
	if (!ocelot_fdma_dcb_set_data(fdma, dcb, skb, skb->len,
				      DMA_TO_DEVICE))
		return false;

	dcb->hw->stat |= MSCC_FDMA_DCB_STAT_BLOCKL(skb->len);
	dcb->hw->stat |= MSCC_FDMA_DCB_STAT_SOF | MSCC_FDMA_DCB_STAT_EOF;

	return true;
}

static void ocelot_fdma_rx_restart(struct ocelot_fdma *fdma)
{
	struct ocelot_fdma_ring *ring = &fdma->xtr;
	struct ocelot_fdma_dcb *dcb, *last_dcb;
	unsigned int idx;
	int ret;
	u32 llp;

	/* Check if the FDMA hits the DCB with LLP == NULL */
	llp = ocelot_fdma_readl(fdma, MSCC_FDMA_DCB_LLP(MSCC_FDMA_XTR_CHAN));
	if (llp)
		return;

	ret = ocelot_fdma_stop_channel(fdma, MSCC_FDMA_XTR_CHAN);
	if (ret) {
		dev_warn(fdma->dev, "Unable to stop RX channel\n");
		return;
	}

	/* Chain the tail with the next DCB */
	dcb = &ring->dcbs[ring->tail];
	idx = ocelot_fdma_idx_incr(ring->tail);
	dcb->hw->llp = ring->dcbs[idx].hw_dma;
	dcb = &ring->dcbs[idx];

	/* Place a NULL terminator in last DCB added (head - 1) */
	idx = ocelot_fdma_idx_decr(ring->head);
	last_dcb = &ring->dcbs[idx];
	last_dcb->hw->llp = 0;
	ring->tail = idx;

	/* Finally reactivate the channel */
	ocelot_fdma_activate_chan(fdma, dcb, MSCC_FDMA_XTR_CHAN);
}

static bool ocelot_fdma_rx_get(struct ocelot_fdma *fdma, int budget)
{
	struct ocelot_fdma_ring *ring = &fdma->xtr;
	struct ocelot_fdma_dcb *dcb, *next_dcb;
	struct ocelot *ocelot = fdma->ocelot;
	struct net_device *ndev;
	struct sk_buff *skb;
	bool valid = true;
	u64 timestamp;
	u64 src_port;
	void *xfh;
	u32 stat;

	/* We should not go past the tail */
	if (ring->head == ring->tail)
		return false;

	dcb = &ring->dcbs[ring->head];
	stat = dcb->hw->stat;
	if (MSCC_FDMA_DCB_STAT_BLOCKL(stat) == 0)
		return false;

	ring->head = ocelot_fdma_idx_incr(ring->head);

	if (stat & MSCC_FDMA_DCB_STAT_ABORT || stat & MSCC_FDMA_DCB_STAT_PD)
		valid = false;

	if (!(stat & MSCC_FDMA_DCB_STAT_SOF) ||
	    !(stat & MSCC_FDMA_DCB_STAT_EOF))
		valid = false;

	dma_unmap_single(fdma->dev, dcb->mapping, dcb->mapped_size,
			 DMA_FROM_DEVICE);

	skb = dcb->skb;

	if (unlikely(!valid)) {
		dev_warn(fdma->dev, "Invalid packet\n");
		goto refill;
	}

	xfh = skb->data;
	ocelot_xfh_get_src_port(xfh, &src_port);

	if (WARN_ON(src_port >= ocelot->num_phys_ports))
		goto refill;

	ndev = ocelot_port_to_netdev(ocelot, src_port);
	if (unlikely(!ndev))
		goto refill;

	skb_put(skb, MSCC_FDMA_DCB_STAT_BLOCKL(stat) - ETH_FCS_LEN);
	skb_pull(skb, OCELOT_TAG_LEN);

	skb->dev = ndev;
	skb->protocol = eth_type_trans(skb, skb->dev);
	skb->dev->stats.rx_bytes += skb->len;
	skb->dev->stats.rx_packets++;

	ocelot_ptp_rx_timestamp(ocelot, skb, timestamp);

	if (!skb_defer_rx_timestamp(skb))
		netif_receive_skb(skb);

	skb = napi_alloc_skb(&fdma->napi, fdma->rx_buf_size);
	if (!skb)
		return false;

refill:
	if (!ocelot_fdma_rx_set_skb(fdma, dcb, skb, fdma->rx_buf_size))
		return false;

	/* Chain the next DCB */
	next_dcb = &ring->dcbs[ring->head];
	dcb->hw->llp = next_dcb->hw_dma;

	return true;
}

static void ocelot_fdma_tx_cleanup(struct ocelot_fdma *fdma, int budget)
{
	struct ocelot_fdma_ring *ring = &fdma->inj;
	unsigned int tmp_head, new_null_llp_idx;
	struct ocelot_fdma_dcb *dcb;
	bool end_of_list = false;
	int ret;

	spin_lock_bh(&fdma->xmit_lock);

	/* Purge the TX packets that have been sent up to the NULL llp or the
	 * end of done list.
	 */
	while (!ocelot_fdma_ring_empty(&fdma->inj)) {
		dcb = &ring->dcbs[ring->head];
		if (!(dcb->hw->stat & MSCC_FDMA_DCB_STAT_PD))
			break;

		tmp_head = ring->head;
		ring->head = ocelot_fdma_idx_incr(ring->head);

		dma_unmap_single(fdma->dev, dcb->mapping, dcb->mapped_size,
				 DMA_TO_DEVICE);
		napi_consume_skb(dcb->skb, budget);

		/* If we hit the NULL LLP, stop, we might need to reload FDMA */
		if (dcb->hw->llp == 0) {
			end_of_list = true;
			break;
		}
	}

	/* If there is still some DCBs to be processed by the FDMA or if the
	 * pending list is empty, there is no need to restart the FDMA.
	 */
	if (!end_of_list || ocelot_fdma_ring_empty(&fdma->inj))
		goto out_unlock;

	ret = ocelot_fdma_wait_chan_safe(fdma, MSCC_FDMA_INJ_CHAN);
	if (ret) {
		dev_warn(fdma->dev, "Failed to wait for TX channel to stop\n");
		goto out_unlock;
	}

	/* Set NULL LLP */
	new_null_llp_idx = ocelot_fdma_idx_decr(ring->tail);
	dcb = &ring->dcbs[new_null_llp_idx];
	dcb->hw->llp = 0;

	dcb = &ring->dcbs[ring->head];
	ocelot_fdma_activate_chan(fdma, dcb, MSCC_FDMA_INJ_CHAN);

out_unlock:
	spin_unlock_bh(&fdma->xmit_lock);
}

static int ocelot_fdma_napi_poll(struct napi_struct *napi, int budget)
{
	struct ocelot_fdma *fdma = container_of(napi, struct ocelot_fdma, napi);
	int work_done = 0;

	ocelot_fdma_tx_cleanup(fdma, budget);

	while (work_done < budget) {
		if (!ocelot_fdma_rx_get(fdma, budget))
			break;

		work_done++;
	}

	ocelot_fdma_rx_restart(fdma);

	if (work_done < budget) {
		napi_complete_done(&fdma->napi, work_done);
		ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_ENA,
				   BIT(MSCC_FDMA_INJ_CHAN) |
				   BIT(MSCC_FDMA_XTR_CHAN));
	}

	return work_done;
}

static irqreturn_t ocelot_fdma_interrupt(int irq, void *dev_id)
{
	u32 ident, llp, frm, err, err_code;
	struct ocelot_fdma *fdma = dev_id;

	ident = ocelot_fdma_readl(fdma, MSCC_FDMA_INTR_IDENT);
	frm = ocelot_fdma_readl(fdma, MSCC_FDMA_INTR_FRM);
	llp = ocelot_fdma_readl(fdma, MSCC_FDMA_INTR_LLP);

	ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_LLP, llp & ident);
	ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_FRM, frm & ident);
	if (frm || llp) {
		ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_ENA, 0);
		napi_schedule(&fdma->napi);
	}

	err = ocelot_fdma_readl(fdma, MSCC_FDMA_EVT_ERR);
	if (unlikely(err)) {
		err_code = ocelot_fdma_readl(fdma, MSCC_FDMA_EVT_ERR_CODE);
		dev_err_ratelimited(fdma->dev,
				    "Error ! chans mask: %#x, code: %#x\n",
				    err, err_code);

		ocelot_fdma_writel(fdma, MSCC_FDMA_EVT_ERR, err);
		ocelot_fdma_writel(fdma, MSCC_FDMA_EVT_ERR_CODE, err_code);
	}

	return IRQ_HANDLED;
}

static void ocelot_fdma_send_skb(struct ocelot_fdma *fdma, struct sk_buff *skb)
{
	struct ocelot_fdma_ring *ring = &fdma->inj;
	struct ocelot_fdma_dcb *dcb, *next;

	dcb = &ring->dcbs[ring->tail];
	if (!ocelot_fdma_tx_dcb_set_skb(fdma, dcb, skb)) {
		dev_kfree_skb_any(skb);
		return;
	}

	if (ocelot_fdma_ring_empty(&fdma->inj)) {
		ocelot_fdma_activate_chan(fdma, dcb, MSCC_FDMA_INJ_CHAN);
	} else {
		next = &ring->dcbs[ocelot_fdma_idx_incr(ring->tail)];
		dcb->hw->llp = next->hw_dma;
	}

	ring->tail = ocelot_fdma_idx_incr(ring->tail);

	skb_tx_timestamp(skb);
}

static int ocelot_fdma_prepare_skb(struct ocelot_fdma *fdma, int port,
				   u32 rew_op, struct sk_buff *skb,
				   struct net_device *dev)
{
	int needed_headroom = max_t(int, OCELOT_TAG_LEN - skb_headroom(skb), 0);
	int needed_tailroom = max_t(int, ETH_FCS_LEN - skb_tailroom(skb), 0);
	struct ocelot_port *ocelot_port = fdma->ocelot->ports[port];
	void *ifh;
	int err;

	if (unlikely(needed_headroom || needed_tailroom ||
		     skb_header_cloned(skb))) {
		err = pskb_expand_head(skb, needed_headroom, needed_tailroom,
				       GFP_ATOMIC);
		if (unlikely(err)) {
			dev_kfree_skb_any(skb);
			return 1;
		}
	}

	err = skb_linearize(skb);
	if (err) {
		net_err_ratelimited("%s: skb_linearize error (%d)!\n",
				    dev->name, err);
		dev_kfree_skb_any(skb);
		return 1;
	}

	ifh = skb_push(skb, OCELOT_TAG_LEN);
	skb_put(skb, ETH_FCS_LEN);
	ocelot_ifh_port_set(ifh, ocelot_port, rew_op, skb_vlan_tag_get(skb));

	return 0;
}

int ocelot_fdma_inject_frame(struct ocelot_fdma *fdma, int port, u32 rew_op,
			     struct sk_buff *skb, struct net_device *dev)
{
	int ret = NETDEV_TX_OK;

	spin_lock(&fdma->xmit_lock);

	if (ocelot_fdma_tx_free_count(fdma) == 0) {
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	if (ocelot_fdma_prepare_skb(fdma, port, rew_op, skb, dev))
		goto out;

	ocelot_fdma_send_skb(fdma, skb);

out:
	spin_unlock(&fdma->xmit_lock);

	return ret;
}

static void ocelot_fdma_ring_free(struct ocelot_fdma *fdma,
				  struct ocelot_fdma_ring *ring)
{
	dmam_free_coherent(fdma->dev, OCELOT_DCBS_HW_ALLOC_SIZE, ring->hw_dcbs,
			   ring->hw_dcbs_dma);
}

static int ocelot_fdma_ring_alloc(struct ocelot_fdma *fdma,
				  struct ocelot_fdma_ring *ring)
{
	struct ocelot_fdma_dcb_hw_v2 *hw_dcbs;
	struct ocelot_fdma_dcb *dcb;
	dma_addr_t hw_dcbs_dma;
	unsigned int adjust;
	int i;

	/* Create a pool of consistent memory blocks for hardware descriptors */
	ring->hw_dcbs = dmam_alloc_coherent(fdma->dev,
					    OCELOT_DCBS_HW_ALLOC_SIZE,
					    &ring->hw_dcbs_dma, GFP_KERNEL);
	if (!ring->hw_dcbs)
		return -ENOMEM;

	/* DCBs must be aligned on a 32bit boundary */
	hw_dcbs = ring->hw_dcbs;
	hw_dcbs_dma = ring->hw_dcbs_dma;
	if (!IS_ALIGNED(hw_dcbs_dma, 4)) {
		adjust = hw_dcbs_dma & 0x3;
		hw_dcbs_dma = ALIGN(hw_dcbs_dma, 4);
		hw_dcbs = (void *)hw_dcbs + adjust;
	}

	for (i = 0; i < OCELOT_FDMA_MAX_DCB; i++) {
		dcb = &ring->dcbs[i];
		dcb->hw = &hw_dcbs[i];
		dcb->hw_dma = hw_dcbs_dma +
			     i * sizeof(struct ocelot_fdma_dcb_hw_v2);
	}

	return 0;
}

static int ocelot_fdma_rx_skb_alloc(struct ocelot_fdma *fdma)
{
	struct ocelot_fdma_dcb *dcb, *prev_dcb = NULL;
	struct ocelot_fdma_ring *ring = &fdma->xtr;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < OCELOT_FDMA_MAX_DCB; i++) {
		dcb = &ring->dcbs[i];
		skb = napi_alloc_skb(&fdma->napi, fdma->rx_buf_size);
		if (!skb)
			goto skb_alloc_failed;

		ocelot_fdma_rx_set_skb(fdma, dcb, skb, fdma->rx_buf_size);

		if (prev_dcb)
			prev_dcb->hw->llp = dcb->hw_dma;

		prev_dcb = dcb;
	}

	ring->head = 0;
	ring->tail = OCELOT_FDMA_MAX_DCB - 1;

	return 0;

skb_alloc_failed:
	for (i = 0; i < OCELOT_FDMA_MAX_DCB; i++) {
		dcb = &ring->dcbs[i];
		if (!dcb->skb)
			break;

		dev_kfree_skb_any(dcb->skb);
	}

	return -ENOMEM;
}

static int ocelot_fdma_rx_init(struct ocelot_fdma *fdma)
{
	int ret;

	fdma->rx_buf_size = ocelot_fdma_rx_buf_size(OCELOT_FDMA_RX_MTU);

	ret = ocelot_fdma_rx_skb_alloc(fdma);
	if (ret) {
		netif_napi_del(&fdma->napi);
		return ret;
	}

	napi_enable(&fdma->napi);

	ocelot_fdma_activate_chan(fdma, &fdma->xtr.dcbs[0],
				  MSCC_FDMA_XTR_CHAN);

	return 0;
}

void ocelot_fdma_netdev_init(struct ocelot_fdma *fdma, struct net_device *dev)
{
	dev->needed_headroom = OCELOT_TAG_LEN;
	dev->needed_tailroom = ETH_FCS_LEN;

	if (fdma->ndev)
		return;

	fdma->ndev = dev;
	netif_napi_add(dev, &fdma->napi, ocelot_fdma_napi_poll,
		       OCELOT_FDMA_WEIGHT);
}

void ocelot_fdma_netdev_deinit(struct ocelot_fdma *fdma, struct net_device *dev)
{
	if (dev == fdma->ndev)
		netif_napi_del(&fdma->napi);
}

struct ocelot_fdma *ocelot_fdma_init(struct platform_device *pdev,
				     struct ocelot *ocelot)
{
	struct ocelot_fdma *fdma;
	void __iomem *base;
	int ret;

	base = devm_platform_ioremap_resource_byname(pdev, "fdma");
	if (IS_ERR_OR_NULL(base))
		return NULL;

	fdma = devm_kzalloc(&pdev->dev, sizeof(*fdma), GFP_KERNEL);
	if (!fdma)
		goto err_release_resource;

	fdma->ocelot = ocelot;
	fdma->base = base;
	fdma->dev = &pdev->dev;
	fdma->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_ENA, 0);

	fdma->irq = platform_get_irq_byname(pdev, "fdma");
	ret = devm_request_irq(&pdev->dev, fdma->irq, ocelot_fdma_interrupt, 0,
			       dev_name(&pdev->dev), fdma);
	if (ret)
		goto err_free_fdma;

	ret = ocelot_fdma_ring_alloc(fdma, &fdma->inj);
	if (ret)
		goto err_free_irq;

	ret = ocelot_fdma_ring_alloc(fdma, &fdma->xtr);
	if (ret)
		goto free_inj_ring;

	return fdma;

free_inj_ring:
	ocelot_fdma_ring_free(fdma, &fdma->inj);
err_free_irq:
	devm_free_irq(&pdev->dev, fdma->irq, fdma);
err_free_fdma:
	devm_kfree(&pdev->dev, fdma);
err_release_resource:
	devm_iounmap(&pdev->dev, base);

	return NULL;
}

int ocelot_fdma_start(struct ocelot_fdma *fdma)
{
	struct ocelot *ocelot = fdma->ocelot;
	int ret;

	ret = ocelot_fdma_rx_init(fdma);
	if (ret)
		return -EINVAL;

	/* Reconfigure for extraction and injection using DMA */
	ocelot_write_rix(ocelot, QS_INJ_GRP_CFG_MODE(2), QS_INJ_GRP_CFG, 0);
	ocelot_write_rix(ocelot, QS_INJ_CTRL_GAP_SIZE(0), QS_INJ_CTRL, 0);

	ocelot_write_rix(ocelot, QS_XTR_GRP_CFG_MODE(2), QS_XTR_GRP_CFG, 0);

	ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_LLP, 0xffffffff);
	ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_FRM, 0xffffffff);

	ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_LLP_ENA,
			   BIT(MSCC_FDMA_INJ_CHAN) | BIT(MSCC_FDMA_XTR_CHAN));
	ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_FRM_ENA, BIT(MSCC_FDMA_XTR_CHAN));
	ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_ENA,
			   BIT(MSCC_FDMA_INJ_CHAN) | BIT(MSCC_FDMA_XTR_CHAN));

	return 0;
}

int ocelot_fdma_stop(struct ocelot_fdma *fdma)
{
	struct ocelot_fdma_ring *ring = &fdma->xtr;
	struct ocelot_fdma_dcb *dcb;
	int i;

	ocelot_fdma_writel(fdma, MSCC_FDMA_INTR_ENA, 0);

	ocelot_fdma_stop_channel(fdma, MSCC_FDMA_XTR_CHAN);
	ocelot_fdma_stop_channel(fdma, MSCC_FDMA_INJ_CHAN);

	/* Free the SKB hold in the extraction ring */
	for (i = 0; i < OCELOT_FDMA_MAX_DCB; i++) {
		dcb = &ring->dcbs[i];
		dev_kfree_skb_any(dcb->skb);
	}

	napi_synchronize(&fdma->napi);
	napi_disable(&fdma->napi);

	return 0;
}
