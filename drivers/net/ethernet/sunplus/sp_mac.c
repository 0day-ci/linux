// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include "sp_mac.h"

void mac_init(struct sp_mac *mac)
{
	u32 i;

	for (i = 0; i < RX_DESC_QUEUE_NUM; i++)
		mac->comm->rx_pos[i] = 0;
	mb();	// make sure settings are effective.

	hal_mac_init(mac);
}

void mac_soft_reset(struct sp_mac *mac)
{
	u32 i;
	struct net_device *ndev2;

	if (netif_carrier_ok(mac->ndev)) {
		netif_carrier_off(mac->ndev);
		netif_stop_queue(mac->ndev);
	}

	ndev2 = mac->next_ndev;
	if (ndev2) {
		if (netif_carrier_ok(ndev2)) {
			netif_carrier_off(ndev2);
			netif_stop_queue(ndev2);
		}
	}

	hal_mac_reset(mac);
	hal_mac_stop(mac);

	rx_descs_flush(mac->comm);
	mac->comm->tx_pos = 0;
	mac->comm->tx_done_pos = 0;
	mac->comm->tx_desc_full = 0;

	for (i = 0; i < RX_DESC_QUEUE_NUM; i++)
		mac->comm->rx_pos[i] = 0;
	mb();	// make sure settings are effective.

	hal_mac_init(mac);
	hal_mac_start(mac);

	if (!netif_carrier_ok(mac->ndev)) {
		netif_carrier_on(mac->ndev);
		netif_start_queue(mac->ndev);
	}

	if (ndev2) {
		if (!netif_carrier_ok(ndev2)) {
			netif_carrier_on(ndev2);
			netif_start_queue(ndev2);
		}
	}
}
