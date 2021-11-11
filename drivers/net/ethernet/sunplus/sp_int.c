// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include "sp_define.h"
#include "sp_int.h"
#include "sp_driver.h"
#include "sp_hal.h"

static void port_status_change(struct sp_mac *mac)
{
	u32 reg;
	struct net_device *ndev = mac->ndev;

	reg = read_port_ability(mac);
	if (!netif_carrier_ok(ndev) && (reg & PORT_ABILITY_LINK_ST_P0)) {
		netif_carrier_on(ndev);
		netif_start_queue(ndev);
	} else if (netif_carrier_ok(ndev) && !(reg & PORT_ABILITY_LINK_ST_P0)) {
		netif_carrier_off(ndev);
		netif_stop_queue(ndev);
	}

	if (mac->next_ndev) {
		struct net_device *ndev2 = mac->next_ndev;

		if (!netif_carrier_ok(ndev2) && (reg & PORT_ABILITY_LINK_ST_P1)) {
			netif_carrier_on(ndev2);
			netif_start_queue(ndev2);
		} else if (netif_carrier_ok(ndev2) && !(reg & PORT_ABILITY_LINK_ST_P1)) {
			netif_carrier_off(ndev2);
			netif_stop_queue(ndev2);
		}
	}
}

static void rx_skb(struct sp_mac *mac, struct sk_buff *skb)
{
	mac->dev_stats.rx_packets++;
	mac->dev_stats.rx_bytes += skb->len;
	netif_receive_skb(skb);
}

int rx_poll(struct napi_struct *napi, int budget)
{
	struct sp_common *comm = container_of(napi, struct sp_common, rx_napi);
	struct sp_mac *mac = netdev_priv(comm->ndev);
	struct sk_buff *skb, *new_skb;
	struct skb_info *sinfo;
	struct mac_desc *desc;
	struct mac_desc *h_desc;
	u32 rx_pos, pkg_len;
	u32 cmd;
	u32 num, rx_count;
	s32 queue;
	int ndev2_pkt;
	struct net_device_stats *dev_stats;

	spin_lock(&comm->rx_lock);

	// Process high-priority queue and then low-priority queue.
	for (queue = 0; queue < RX_DESC_QUEUE_NUM; queue++) {
		rx_pos = comm->rx_pos[queue];
		rx_count = comm->rx_desc_num[queue];

		for (num = 0; num < rx_count; num++) {
			sinfo = comm->rx_skb_info[queue] + rx_pos;
			desc = comm->rx_desc[queue] + rx_pos;
			cmd = desc->cmd1;

			if (cmd & OWN_BIT)
				break;

			if ((cmd & PKTSP_MASK) == PKTSP_PORT1) {
				struct sp_mac *mac2;

				ndev2_pkt = 1;
				mac2 = (mac->next_ndev) ? netdev_priv(mac->next_ndev) : NULL;
				dev_stats = (mac2) ? &mac2->dev_stats : &mac->dev_stats;
			} else {
				ndev2_pkt = 0;
				dev_stats = &mac->dev_stats;
			}

			pkg_len = cmd & LEN_MASK;
			if (unlikely((cmd & ERR_CODE) || (pkg_len < 64))) {
				dev_stats->rx_length_errors++;
				dev_stats->rx_dropped++;
				goto NEXT;
			}

			if (unlikely(cmd & RX_IP_CHKSUM_BIT)) {
				dev_stats->rx_crc_errors++;
				dev_stats->rx_dropped++;
				goto NEXT;
			}

			// Allocate an skbuff for receiving.
			new_skb = __dev_alloc_skb(comm->rx_desc_buff_size + RX_OFFSET,
						  GFP_ATOMIC | GFP_DMA);
			if (unlikely(!new_skb)) {
				dev_stats->rx_dropped++;
				goto NEXT;
			}
			new_skb->dev = mac->ndev;

			dma_unmap_single(&comm->pdev->dev, sinfo->mapping,
					 comm->rx_desc_buff_size, DMA_FROM_DEVICE);

			skb = sinfo->skb;
			skb->ip_summed = CHECKSUM_NONE;

			/*skb_put will judge if tail exceeds end, but __skb_put won't */
			__skb_put(skb, (pkg_len - 4 > comm->rx_desc_buff_size) ?
				       comm->rx_desc_buff_size : pkg_len - 4);

			sinfo->mapping = dma_map_single(&comm->pdev->dev, new_skb->data,
							comm->rx_desc_buff_size,
							DMA_FROM_DEVICE);
			sinfo->skb = new_skb;

			if (ndev2_pkt) {
				struct net_device *netdev2 = mac->next_ndev;

				if (netdev2) {
					skb->protocol = eth_type_trans(skb, netdev2);
					rx_skb(netdev_priv(netdev2), skb);
				}
			} else {
				skb->protocol = eth_type_trans(skb, mac->ndev);
				rx_skb(mac, skb);
			}

			desc->addr1 = sinfo->mapping;

NEXT:
			desc->cmd2 = (rx_pos == comm->rx_desc_num[queue] - 1) ?
				     EOR_BIT | MAC_RX_LEN_MAX : MAC_RX_LEN_MAX;
			wmb();	// Set OWN_BIT after other fields of descriptor are effective.
			desc->cmd1 = OWN_BIT | (comm->rx_desc_buff_size & LEN_MASK);

			NEXT_RX(queue, rx_pos);

			// If there are packets in high-priority queue,
			// stop processing low-priority queue.
			if ((queue == 1) && ((h_desc->cmd1 & OWN_BIT) == 0))
				break;
		}

		comm->rx_pos[queue] = rx_pos;

		// Save pointer to last rx descriptor of high-priority queue.
		if (queue == 0)
			h_desc = comm->rx_desc[queue] + rx_pos;
	}

	spin_unlock(&comm->rx_lock);

	wmb();			// make sure settings are effective.
	write_sw_int_mask0(mac, read_sw_int_mask0(mac) & ~MAC_INT_RX);

	napi_complete(napi);
	return 0;
}

int tx_poll(struct napi_struct *napi, int budget)
{
	struct sp_common *comm = container_of(napi, struct sp_common, tx_napi);
	struct sp_mac *mac = netdev_priv(comm->ndev);
	u32 tx_done_pos;
	u32 cmd;
	struct skb_info *skbinfo;
	struct sp_mac *smac;

	spin_lock(&comm->tx_lock);

	tx_done_pos = comm->tx_done_pos;
	while ((tx_done_pos != comm->tx_pos) || (comm->tx_desc_full == 1)) {
		cmd = comm->tx_desc[tx_done_pos].cmd1;
		if (cmd & OWN_BIT)
			break;

		skbinfo = &comm->tx_temp_skb_info[tx_done_pos];
		if (unlikely(!skbinfo->skb))
			netdev_err(mac->ndev, "skb is null!\n");

		smac = mac;
		if (mac->next_ndev && ((cmd & TO_VLAN_MASK) == TO_VLAN_GROUP1))
			smac = netdev_priv(mac->next_ndev);

		if (unlikely(cmd & (ERR_CODE))) {
			smac->dev_stats.tx_errors++;
		} else {
			smac->dev_stats.tx_packets++;
			smac->dev_stats.tx_bytes += skbinfo->len;
		}

		dma_unmap_single(&comm->pdev->dev, skbinfo->mapping, skbinfo->len,
				 DMA_TO_DEVICE);
		skbinfo->mapping = 0;
		dev_kfree_skb_irq(skbinfo->skb);
		skbinfo->skb = NULL;

		NEXT_TX(tx_done_pos);
		if (comm->tx_desc_full == 1)
			comm->tx_desc_full = 0;
	}

	comm->tx_done_pos = tx_done_pos;
	if (!comm->tx_desc_full) {
		if (netif_queue_stopped(mac->ndev))
			netif_wake_queue(mac->ndev);

		if (mac->next_ndev) {
			if (netif_queue_stopped(mac->next_ndev))
				netif_wake_queue(mac->next_ndev);
		}
	}

	spin_unlock(&comm->tx_lock);

	wmb();			// make sure settings are effective.
	write_sw_int_mask0(mac, read_sw_int_mask0(mac) & ~MAC_INT_TX);

	napi_complete(napi);
	return 0;
}

irqreturn_t ethernet_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev;
	struct sp_mac *mac;
	struct sp_common *comm;
	u32 status;

	ndev = (struct net_device *)dev_id;
	if (unlikely(!ndev)) {
		netdev_err(ndev, "ndev is null!\n");
		goto OUT;
	}

	mac = netdev_priv(ndev);
	comm = mac->comm;

	status = read_sw_int_status0(mac);
	if (unlikely(status == 0)) {
		netdev_err(ndev, "Interrput status is null!\n");
		goto OUT;
	}
	write_sw_int_status0(mac, status);

	if (status & MAC_INT_RX) {
		// Disable RX interrupts.
		write_sw_int_mask0(mac, read_sw_int_mask0(mac) | MAC_INT_RX);

		if (unlikely(status & MAC_INT_RX_DES_ERR)) {
			netdev_err(ndev, "Illegal RX Descriptor!\n");
			mac->dev_stats.rx_fifo_errors++;
		}
		if (napi_schedule_prep(&comm->rx_napi))
			__napi_schedule(&comm->rx_napi);
	}

	if (status & MAC_INT_TX) {
		// Disable TX interrupts.
		write_sw_int_mask0(mac, read_sw_int_mask0(mac) | MAC_INT_TX);

		if (unlikely(status & MAC_INT_TX_DES_ERR)) {
			netdev_err(ndev, "Illegal TX Descriptor Error\n");
			mac->dev_stats.tx_fifo_errors++;
			mac_soft_reset(mac);
			wmb();			// make sure settings are effective.
			write_sw_int_mask0(mac, read_sw_int_mask0(mac) & ~MAC_INT_TX);
		} else {
			if (napi_schedule_prep(&comm->tx_napi))
				__napi_schedule(&comm->tx_napi);
		}
	}

	if (status & MAC_INT_PORT_ST_CHG)	/* link status changed */
		port_status_change(mac);

OUT:
	return IRQ_HANDLED;
}
