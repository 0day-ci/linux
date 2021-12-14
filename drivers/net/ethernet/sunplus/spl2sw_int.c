// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>
#include <linux/of_mdio.h>

#include "spl2sw_register.h"
#include "spl2sw_define.h"
#include "spl2sw_int.h"

int spl2sw_rx_poll(struct napi_struct *napi, int budget)
{
	struct spl2sw_common *comm = container_of(napi, struct spl2sw_common, rx_napi);
	struct spl2sw_mac_desc *desc, *h_desc;
	struct net_device_stats *stats;
	struct sk_buff *skb, *new_skb;
	struct spl2sw_skb_info *sinfo;
	u32 rx_pos, pkg_len;
	u32 num, rx_count;
	s32 queue;
	u32 mask;
	int port;
	u32 cmd;

	spin_lock(&comm->rx_lock);

	/* Process high-priority queue and then low-priority queue. */
	for (queue = 0; queue < RX_DESC_QUEUE_NUM; queue++) {
		rx_pos = comm->rx_pos[queue];
		rx_count = comm->rx_desc_num[queue];

		for (num = 0; num < rx_count; num++) {
			sinfo = comm->rx_skb_info[queue] + rx_pos;
			desc = comm->rx_desc[queue] + rx_pos;
			cmd = desc->cmd1;

			if (cmd & RXD_OWN)
				break;

			port = FIELD_GET(RXD_PKT_SP, cmd);
			if (port < MAX_NETDEV_NUM && comm->ndev[port])
				stats = &comm->ndev[port]->stats;
			else
				goto spl2sw_rx_poll_rec_err;

			pkg_len = FIELD_GET(RXD_PKT_LEN, cmd);
			if (unlikely((cmd & RXD_ERR_CODE) || pkg_len < ETH_ZLEN + 4)) {
				stats->rx_length_errors++;
				stats->rx_dropped++;
				goto spl2sw_rx_poll_rec_err;
			}

			if (unlikely(cmd & RXD_IP_CHKSUM)) {
				stats->rx_crc_errors++;
				stats->rx_dropped++;
				goto spl2sw_rx_poll_rec_err;
			}

			dma_unmap_single(&comm->pdev->dev, sinfo->mapping,
					 comm->rx_desc_buff_size, DMA_FROM_DEVICE);

			skb = sinfo->skb;
			skb_put(skb, pkg_len - 4); /* Minus FCS */
			skb->ip_summed = CHECKSUM_NONE;
			skb->protocol = eth_type_trans(skb, comm->ndev[port]);
			netif_receive_skb(skb);

			stats->rx_packets++;
			stats->rx_bytes += skb->len;

			/* Allocate a new skb for receiving. */
			new_skb = netdev_alloc_skb(NULL, comm->rx_desc_buff_size);
			if (unlikely(!new_skb)) {
				desc->cmd2 = (rx_pos == comm->rx_desc_num[queue] - 1) ?
					     RXD_EOR : 0;
				sinfo->skb = NULL;
				sinfo->mapping = 0;
				goto spl2sw_rx_poll_alloc_err;
			}

			sinfo->mapping = dma_map_single(&comm->pdev->dev, new_skb->data,
							comm->rx_desc_buff_size,
							DMA_FROM_DEVICE);
			if (dma_mapping_error(&comm->pdev->dev, sinfo->mapping)) {
				dev_kfree_skb_irq(new_skb);
				desc->cmd2 = (rx_pos == comm->rx_desc_num[queue] - 1) ?
					     RXD_EOR : 0;
				sinfo->skb = NULL;
				goto spl2sw_rx_poll_alloc_err;
			}

			sinfo->skb = new_skb;
			desc->addr1 = sinfo->mapping;

spl2sw_rx_poll_rec_err:
			desc->cmd2 = (rx_pos == comm->rx_desc_num[queue] - 1) ?
				     RXD_EOR | comm->rx_desc_buff_size :
				     comm->rx_desc_buff_size;

spl2sw_rx_poll_alloc_err:
			wmb();	/* Set RXD_OWN after other fields are effective. */
			desc->cmd1 = RXD_OWN;

			/* Move rx_pos to next position */
			rx_pos = ((rx_pos + 1) == comm->rx_desc_num[queue]) ? 0 : rx_pos + 1;

			/* If there are packets in high-priority queue,
			 * stop processing low-priority queue.
			 */
			if (queue == 1 && !(h_desc->cmd1 & RXD_OWN))
				break;
		}

		comm->rx_pos[queue] = rx_pos;

		/* Save pointer to last rx descriptor of high-priority queue. */
		if (queue == 0)
			h_desc = comm->rx_desc[queue] + rx_pos;
	}

	spin_unlock(&comm->rx_lock);

	wmb();	/* make sure settings are effective. */
	mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
	mask &= ~MAC_INT_RX;
	writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);

	napi_complete(napi);
	return 0;
}

int spl2sw_tx_poll(struct napi_struct *napi, int budget)
{
	struct spl2sw_common *comm = container_of(napi, struct spl2sw_common, tx_napi);
	struct spl2sw_skb_info *skbinfo;
	struct net_device_stats *stats;
	u32 tx_done_pos;
	u32 mask;
	u32 cmd;
	int i;

	spin_lock(&comm->tx_lock);

	tx_done_pos = comm->tx_done_pos;
	while ((tx_done_pos != comm->tx_pos) || (comm->tx_desc_full == 1)) {
		cmd = comm->tx_desc[tx_done_pos].cmd1;
		if (cmd & TXD_OWN)
			break;

		skbinfo = &comm->tx_temp_skb_info[tx_done_pos];
		if (unlikely(!skbinfo->skb))
			goto spl2sw_tx_poll_next;

		i = ffs(FIELD_GET(TXD_VLAN, cmd)) - 1;
		if (i < MAX_NETDEV_NUM && comm->ndev[i])
			stats = &comm->ndev[i]->stats;
		else
			goto spl2sw_tx_poll_unmap;

		if (unlikely(cmd & (TXD_ERR_CODE))) {
			stats->tx_errors++;
		} else {
			stats->tx_packets++;
			stats->tx_bytes += skbinfo->len;
		}

spl2sw_tx_poll_unmap:
		dma_unmap_single(&comm->pdev->dev, skbinfo->mapping, skbinfo->len,
				 DMA_TO_DEVICE);
		skbinfo->mapping = 0;
		dev_kfree_skb_irq(skbinfo->skb);
		skbinfo->skb = NULL;

spl2sw_tx_poll_next:
		/* Move tx_done_pos to next position */
		tx_done_pos = ((tx_done_pos + 1) == TX_DESC_NUM) ? 0 : tx_done_pos + 1;

		if (comm->tx_desc_full == 1)
			comm->tx_desc_full = 0;
	}

	comm->tx_done_pos = tx_done_pos;
	if (!comm->tx_desc_full)
		for (i = 0; i < MAX_NETDEV_NUM; i++)
			if (comm->ndev[i])
				if (netif_queue_stopped(comm->ndev[i]))
					netif_wake_queue(comm->ndev[i]);

	spin_unlock(&comm->tx_lock);

	wmb();			/* make sure settings are effective. */
	mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
	mask &= ~MAC_INT_TX;
	writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);

	napi_complete(napi);
	return 0;
}

irqreturn_t spl2sw_ethernet_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct spl2sw_mac *mac = netdev_priv(ndev);
	struct spl2sw_common *comm = mac->comm;
	u32 status;
	u32 mask;

	status = readl(comm->l2sw_reg_base + L2SW_SW_INT_STATUS_0);
	if (unlikely(!status)) {
		netdev_dbg(ndev, "Interrput status is null!\n");
		goto spl2sw_ethernet_int_out;
	}
	writel(status, comm->l2sw_reg_base + L2SW_SW_INT_STATUS_0);

	if (status & MAC_INT_RX) {
		/* Disable RX interrupts. */
		mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
		mask |= MAC_INT_RX;
		writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);

		if (unlikely(status & MAC_INT_RX_DES_ERR)) {
			netdev_dbg(ndev, "Illegal RX Descriptor!\n");
			ndev->stats.rx_fifo_errors++;
		}

		napi_schedule(&comm->rx_napi);
	}

	if (status & MAC_INT_TX) {
		/* Disable TX interrupts. */
		mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
		mask |= MAC_INT_TX;
		writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);

		if (unlikely(status & MAC_INT_TX_DES_ERR)) {
			netdev_dbg(ndev, "Illegal TX Descriptor Error\n");
			ndev->stats.tx_fifo_errors++;
			mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
			mask &= ~MAC_INT_TX;
			writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
		} else {
			napi_schedule(&comm->tx_napi);
		}
	}

spl2sw_ethernet_int_out:
	return IRQ_HANDLED;
}
