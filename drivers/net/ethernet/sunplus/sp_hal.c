// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/iopoll.h>
#include "sp_hal.h"

void hal_mac_stop(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;
	u32 reg, disable;

	if (comm->enable == 0) {
		// Mask and clear all interrupts, except PORT_ST_CHG.
		write_sw_int_mask0(mac, 0xffffffff);
		writel(0xffffffff & (~MAC_INT_PORT_ST_CHG),
		       comm->sp_reg_base + SP_SW_INT_STATUS_0);

		// Disable cpu 0 and cpu 1.
		reg = readl(comm->sp_reg_base + SP_CPU_CNTL);
		writel((0x3 << 6) | reg, comm->sp_reg_base + SP_CPU_CNTL);
	}

	// Disable lan 0 and lan 1.
	disable = ((~comm->enable) & 0x3) << 24;
	reg = readl(comm->sp_reg_base + SP_PORT_CNTL0);
	writel(disable | reg, comm->sp_reg_base + SP_PORT_CNTL0);
}

void hal_mac_reset(struct sp_mac *mac)
{
}

void hal_mac_start(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;
	u32 reg;

	// Enable cpu port 0 (6) & port 0 crc padding (8)
	reg = readl(comm->sp_reg_base + SP_CPU_CNTL);
	writel((reg & (~(0x1 << 6))) | (0x1 << 8), comm->sp_reg_base + SP_CPU_CNTL);

	// Enable lan 0 & lan 1
	reg = readl(comm->sp_reg_base + SP_PORT_CNTL0);
	writel(reg & (~(comm->enable << 24)), comm->sp_reg_base + SP_PORT_CNTL0);
}

void hal_mac_addr_set(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;
	u32 reg;

	// Write MAC address.
	writel(mac->mac_addr[0] + (mac->mac_addr[1] << 8),
	       comm->sp_reg_base + SP_W_MAC_15_0);
	writel(mac->mac_addr[2] + (mac->mac_addr[3] << 8) + (mac->mac_addr[4] << 16) +
	      (mac->mac_addr[5] << 24),	comm->sp_reg_base + SP_W_MAC_47_16);

	// Set aging=1
	writel((mac->cpu_port << 10) + (mac->vlan_id << 7) + (1 << 4) + 0x1,
	       comm->sp_reg_base + SP_WT_MAC_AD0);

	// Wait for completing.
	do {
		reg = readl(comm->sp_reg_base + SP_WT_MAC_AD0);
		ndelay(10);
		netdev_dbg(mac->ndev, "wt_mac_ad0 = %08x\n", reg);
	} while ((reg & (0x1 << 1)) == 0x0);

	netdev_dbg(mac->ndev, "mac_ad0 = %08x, mac_ad = %08x%04x\n",
		   readl(comm->sp_reg_base + SP_WT_MAC_AD0),
		   readl(comm->sp_reg_base + SP_W_MAC_47_16),
		   readl(comm->sp_reg_base + SP_W_MAC_15_0) & 0xffff);
}

void hal_mac_addr_del(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;
	u32 reg;

	// Write MAC address.
	writel(mac->mac_addr[0] + (mac->mac_addr[1] << 8),
	       comm->sp_reg_base + SP_W_MAC_15_0);
	writel(mac->mac_addr[2] + (mac->mac_addr[3] << 8) + (mac->mac_addr[4] << 16) +
	       (mac->mac_addr[5] << 24), comm->sp_reg_base + SP_W_MAC_47_16);

	// Wait for completing.
	writel((0x1 << 12) + (mac->vlan_id << 7) + 0x1,
	       comm->sp_reg_base + SP_WT_MAC_AD0);
	do {
		reg = readl(comm->sp_reg_base + SP_WT_MAC_AD0);
		ndelay(10);
		netdev_dbg(mac->ndev, "wt_mac_ad0 = %08x\n", reg);
	} while ((reg & (0x1 << 1)) == 0x0);

	netdev_dbg(mac->ndev, "mac_ad0 = %08x, mac_ad = %08x%04x\n",
		   readl(comm->sp_reg_base + SP_WT_MAC_AD0),
		   readl(comm->sp_reg_base + SP_W_MAC_47_16),
		   readl(comm->sp_reg_base + SP_W_MAC_15_0) & 0xffff);
}

void hal_mac_addr_table_del_all(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;
	u32 reg;

	// Wait for address table being idle.
	do {
		reg = readl(comm->sp_reg_base + SP_ADDR_TBL_SRCH);
		ndelay(10);
	} while (!(reg & MAC_ADDR_LOOKUP_IDLE));

	// Search address table from start.
	writel(readl(comm->sp_reg_base + SP_ADDR_TBL_SRCH) | MAC_BEGIN_SEARCH_ADDR,
	       comm->sp_reg_base + SP_ADDR_TBL_SRCH);
	while (1) {
		do {
			reg = readl(comm->sp_reg_base + SP_ADDR_TBL_ST);
			ndelay(10);
			netdev_dbg(mac->ndev, "addr_tbl_st = %08x\n", reg);
		} while (!(reg & (MAC_AT_TABLE_END | MAC_AT_DATA_READY)));

		if (reg & MAC_AT_TABLE_END)
			break;

		netdev_dbg(mac->ndev, "addr_tbl_st = %08x\n", reg);
		netdev_dbg(mac->ndev, "@AT #%u: port=%01x, cpu=%01x, vid=%u, aging=%u, proxy=%u, mc_ingress=%u\n",
			   (reg >> 22) & 0x3ff, (reg >> 12) & 0x3, (reg >> 10) & 0x3,
			   (reg >> 7) & 0x7, (reg >> 4) & 0x7, (reg >> 3) & 0x1,
			   (reg >> 2) & 0x1);

		// Delete all entries which are learnt from lan ports.
		if ((reg >> 12) & 0x3) {
			writel(readl(comm->sp_reg_base + SP_MAC_AD_SER0),
			       comm->sp_reg_base + SP_W_MAC_15_0);
			writel(readl(comm->sp_reg_base + SP_MAC_AD_SER1),
			       comm->sp_reg_base + SP_W_MAC_47_16);

			writel((0x1 << 12) + (reg & (0x7 << 7)) + 0x1,
			       comm->sp_reg_base + SP_WT_MAC_AD0);
			do {
				reg = readl(comm->sp_reg_base + SP_WT_MAC_AD0);
				ndelay(10);
				netdev_dbg(mac->ndev, "wt_mac_ad0 = %08x\n", reg);
			} while ((reg & (0x1 << 1)) == 0x0);
			netdev_dbg(mac->ndev, "mac_ad0 = %08x, mac_ad = %08x%04x\n",
				   readl(comm->sp_reg_base + SP_WT_MAC_AD0),
				   readl(comm->sp_reg_base + SP_W_MAC_47_16),
				   readl(comm->sp_reg_base + SP_W_MAC_15_0) & 0xffff);
		}

		// Search next.
		writel(readl(comm->sp_reg_base + SP_ADDR_TBL_SRCH) | MAC_SEARCH_NEXT_ADDR,
		       comm->sp_reg_base + SP_ADDR_TBL_SRCH);
	}
}

void hal_mac_init(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;
	u32 reg;

	// Disable cpu0 and cpu 1.
	reg = readl(comm->sp_reg_base + SP_CPU_CNTL);
	writel((0x3 << 6) | reg, comm->sp_reg_base + SP_CPU_CNTL);

	// Descriptor base address
	writel(mac->comm->desc_dma, comm->sp_reg_base + SP_TX_LBASE_ADDR_0);
	writel(mac->comm->desc_dma + sizeof(struct mac_desc) * TX_DESC_NUM,
	       comm->sp_reg_base + SP_TX_HBASE_ADDR_0);
	writel(mac->comm->desc_dma + sizeof(struct mac_desc) * (TX_DESC_NUM +
	       MAC_GUARD_DESC_NUM), comm->sp_reg_base + SP_RX_HBASE_ADDR_0);
	writel(mac->comm->desc_dma + sizeof(struct mac_desc) * (TX_DESC_NUM +
	       MAC_GUARD_DESC_NUM + RX_QUEUE0_DESC_NUM),
	       comm->sp_reg_base + SP_RX_LBASE_ADDR_0);

	// Fc_rls_th=0x4a, Fc_set_th=0x3a, Drop_rls_th=0x2d, Drop_set_th=0x1d
	writel(0x4a3a2d1d, comm->sp_reg_base + SP_FL_CNTL_TH);

	// Cpu_rls_th=0x4a, Cpu_set_th=0x3a, Cpu_th=0x12, Port_th=0x12
	writel(0x4a3a1212, comm->sp_reg_base + SP_CPU_FL_CNTL_TH);

	// mtcc_lmt=0xf, Pri_th_l=6, Pri_th_h=6, weigh_8x_en=1
	writel(0xf6680000, comm->sp_reg_base + SP_PRI_FL_CNTL);

	// High-active LED
	reg = readl(comm->sp_reg_base + SP_LED_PORT0);
	writel(reg | (1 << 28), comm->sp_reg_base + SP_LED_PORT0);

	// Disable cpu port0 aging (12)
	// Disable cpu port0 learning (14)
	// Enable UC and MC packets
	reg = readl(comm->sp_reg_base + SP_CPU_CNTL);
	writel((reg & (~((0x1 << 14) | (0x3c << 0)))) | (0x1 << 12),
	       comm->sp_reg_base + SP_CPU_CNTL);

	// Disable lan port SA learning.
	reg = readl(comm->sp_reg_base + SP_PORT_CNTL1);
	writel(reg | (0x3 << 8), comm->sp_reg_base + SP_PORT_CNTL1);

	// Port 0: VLAN group 0
	// Port 1: VLAN group 1
	writel((1 << 4) + 0, comm->sp_reg_base + SP_PVID_CONFIG0);

	// VLAN group 0: cpu0+port0
	// VLAN group 1: cpu0+port1
	writel((0xa << 8) + 0x9, comm->sp_reg_base + SP_VLAN_MEMSET_CONFIG0);

	// RMC forward: to cpu
	// LED: 60mS
	// BC storm prev: 31 BC
	reg = readl(comm->sp_reg_base + SP_SW_GLB_CNTL);
	writel((reg & (~((0x3 << 25) | (0x3 << 23) | (0x3 << 4)))) |
	       (0x1 << 25) | (0x1 << 23) | (0x1 << 4),
	       comm->sp_reg_base + SP_SW_GLB_CNTL);

	write_sw_int_mask0(mac, MAC_INT_MASK_DEF);
}

void hal_rx_mode_set(struct net_device *ndev)
{
	struct sp_mac *mac = netdev_priv(ndev);
	struct sp_common *comm = mac->comm;
	u32 mask, reg, rx_mode;

	netdev_dbg(ndev, "ndev->flags = %08x\n", ndev->flags);

	mask = (mac->lan_port << 2) | (mac->lan_port << 0);
	reg = readl(comm->sp_reg_base + SP_CPU_CNTL);

	if (ndev->flags & IFF_PROMISC) {	/* Set promiscuous mode */
		// Allow MC and unknown UC packets
		rx_mode = (mac->lan_port << 2) | (mac->lan_port << 0);
	} else if ((!netdev_mc_empty(ndev) && (ndev->flags & IFF_MULTICAST)) ||
		   (ndev->flags & IFF_ALLMULTI)) {
		// Allow MC packets
		rx_mode = (mac->lan_port << 2);
	} else {
		// Disable MC and unknown UC packets
		rx_mode = 0;
	}

	writel((reg & (~mask)) | ((~rx_mode) & mask), comm->sp_reg_base + SP_CPU_CNTL);
	netdev_dbg(ndev, "cpu_cntl = %08x\n", readl(comm->sp_reg_base + SP_CPU_CNTL));
}

int hal_mdio_access(struct sp_mac *mac, u8 op_cd, u8 phy_addr, u8 reg_addr, u32 wdata)
{
	struct sp_common *comm = mac->comm;
	u32 val, ret;

	writel((wdata << 16) | (op_cd << 13) | (reg_addr << 8) | phy_addr,
	       comm->sp_reg_base + SP_PHY_CNTL_REG0);

	ret = read_poll_timeout(readl, val, val & op_cd, 10, 1000, 1,
				comm->sp_reg_base + SP_PHY_CNTL_REG1);
	if (ret == 0)
		return val >> 16;
	else
		return ret;
}

void hal_tx_trigger(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;

	writel((0x1 << 1), comm->sp_reg_base + SP_CPU_TX_TRIG);
}

void hal_set_rmii_tx_rx_pol(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;
	u32 reg;

	// Set polarity of RX and TX of RMII signal.
	reg = readl(comm->moon5_reg_base + MOON5_MO4_L2SW_CLKSW_CTL);
	writel(reg | (0xf << 16) | 0xf, comm->moon5_reg_base + MOON5_MO4_L2SW_CLKSW_CTL);
}

void hal_phy_addr(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;
	u32 reg;

	// Set address of phy.
	reg = readl(comm->sp_reg_base + SP_MAC_FORCE_MODE);
	reg = (reg & (~(0x1f << 16))) | ((mac->phy_addr & 0x1f) << 16);
	if (mac->next_ndev) {
		struct net_device *ndev2 = mac->next_ndev;
		struct sp_mac *mac2 = netdev_priv(ndev2);

		reg = (reg & (~(0x1f << 24))) | ((mac2->phy_addr & 0x1f) << 24);
	}
	writel(reg, comm->sp_reg_base + SP_MAC_FORCE_MODE);
}

u32 read_sw_int_mask0(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;

	return readl(comm->sp_reg_base + SP_SW_INT_MASK_0);
}

void write_sw_int_mask0(struct sp_mac *mac, u32 value)
{
	struct sp_common *comm = mac->comm;

	writel(value, comm->sp_reg_base + SP_SW_INT_MASK_0);
}

void write_sw_int_status0(struct sp_mac *mac, u32 value)
{
	struct sp_common *comm = mac->comm;

	writel(value, comm->sp_reg_base + SP_SW_INT_STATUS_0);
}

u32 read_sw_int_status0(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;

	return readl(comm->sp_reg_base + SP_SW_INT_STATUS_0);
}

u32 read_port_ability(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;

	return readl(comm->sp_reg_base + SP_PORT_ABILITY);
}
