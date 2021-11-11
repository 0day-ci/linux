/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SP_DEFINE_H__
#define __SP_DEFINE_H__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>

//define MAC interrupt status bit
#define MAC_INT_DAISY_MODE_CHG          BIT(31)
#define MAC_INT_IP_CHKSUM_ERR           BIT(23)
#define MAC_INT_WDOG_TIMER1_EXP         BIT(22)
#define MAC_INT_WDOG_TIMER0_EXP         BIT(21)
#define MAC_INT_INTRUDER_ALERT          BIT(20)
#define MAC_INT_PORT_ST_CHG             BIT(19)
#define MAC_INT_BC_STORM                BIT(18)
#define MAC_INT_MUST_DROP_LAN           BIT(17)
#define MAC_INT_GLOBAL_QUE_FULL         BIT(16)
#define MAC_INT_TX_SOC_PAUSE_ON         BIT(15)
#define MAC_INT_RX_SOC_QUE_FULL         BIT(14)
#define MAC_INT_TX_LAN1_QUE_FULL        BIT(9)
#define MAC_INT_TX_LAN0_QUE_FULL        BIT(8)
#define MAC_INT_RX_L_DESCF              BIT(7)
#define MAC_INT_RX_H_DESCF              BIT(6)
#define MAC_INT_RX_DONE_L               BIT(5)
#define MAC_INT_RX_DONE_H               BIT(4)
#define MAC_INT_TX_DONE_L               BIT(3)
#define MAC_INT_TX_DONE_H               BIT(2)
#define MAC_INT_TX_DES_ERR              BIT(1)
#define MAC_INT_RX_DES_ERR              BIT(0)

#define MAC_INT_RX                      (MAC_INT_RX_DONE_H | MAC_INT_RX_DONE_L | \
					MAC_INT_RX_DES_ERR)
#define MAC_INT_TX                      (MAC_INT_TX_DONE_L | MAC_INT_TX_DONE_H | \
					MAC_INT_TX_DES_ERR)
#define MAC_INT_MASK_DEF                (MAC_INT_DAISY_MODE_CHG | MAC_INT_IP_CHKSUM_ERR | \
					MAC_INT_WDOG_TIMER1_EXP | MAC_INT_WDOG_TIMER0_EXP | \
					MAC_INT_INTRUDER_ALERT | MAC_INT_BC_STORM | \
					MAC_INT_MUST_DROP_LAN | MAC_INT_GLOBAL_QUE_FULL | \
					MAC_INT_TX_SOC_PAUSE_ON | MAC_INT_RX_SOC_QUE_FULL | \
					MAC_INT_TX_LAN1_QUE_FULL | MAC_INT_TX_LAN0_QUE_FULL | \
					MAC_INT_RX_L_DESCF | MAC_INT_RX_H_DESCF)

/*define port ability*/
#define PORT_ABILITY_LINK_ST_P1         BIT(25)
#define PORT_ABILITY_LINK_ST_P0         BIT(24)

/*define PHY command bit*/
#define PHY_WT_DATA_MASK                0xffff0000
#define PHY_RD_CMD                      0x00004000
#define PHY_WT_CMD                      0x00002000
#define PHY_REG_MASK                    0x00001f00
#define PHY_ADR_MASK                    0x0000001f

/*define PHY status bit*/
#define PHY_RD_DATA_MASK                0xffff0000
#define PHY_RD_RDY                      BIT(1)
#define PHY_WT_DONE                     BIT(0)

/*define other register bit*/
#define RX_MAX_LEN_MASK                 0x00011000
#define ROUTE_MODE_MASK                 0x00000060
#define POK_INT_THS_MASK                0x000E0000
#define VLAN_TH_MASK                    0x00000007

/*define tx descriptor bit*/
#define OWN_BIT                         BIT(31)
#define FS_BIT                          BIT(25)
#define LS_BIT                          BIT(24)
#define LEN_MASK                        0x000007FF
#define PKTSP_MASK                      0x00007000
#define PKTSP_PORT1                     0x00001000
#define TO_VLAN_MASK                    0x0003F000
#define TO_VLAN_GROUP1                  0x00002000

#define EOR_BIT                         BIT(31)

/*define rx descriptor bit*/
#define ERR_CODE                        (0xf << 26)
#define RX_TCP_UDP_CHKSUM_BIT           BIT(23)
#define RX_IP_CHKSUM_BIT                BIT(18)

#define OWC_BIT                         BIT(31)
#define TXOK_BIT                        BIT(26)
#define LNKF_BIT                        BIT(25)
#define BUR_BIT                         BIT(22)
#define TWDE_BIT                        BIT(20)
#define CC_MASK                         0x000f0000
#define TBE_MASK                        0x00070000

// Address table search
#define MAC_ADDR_LOOKUP_IDLE            BIT(2)
#define MAC_SEARCH_NEXT_ADDR            BIT(1)
#define MAC_BEGIN_SEARCH_ADDR           BIT(0)

// Address table search
#define MAC_HASK_LOOKUP_ADDR_MASK       (0x3ff << 22)
#define MAC_AT_TABLE_END                BIT(1)
#define MAC_AT_DATA_READY               BIT(0)

/*config descriptor*/
#define TX_DESC_NUM                     16
#define MAC_GUARD_DESC_NUM              2
#define RX_QUEUE0_DESC_NUM              16
#define RX_QUEUE1_DESC_NUM              16
#define TX_DESC_QUEUE_NUM               1
#define RX_DESC_QUEUE_NUM               2

#define MAC_TX_BUFF_SIZE                1536
#define MAC_RX_LEN_MAX                  2047

#define DESC_ALIGN_BYTE                 32
#define RX_OFFSET                       0
#define TX_OFFSET                       0

#define ETHERNET_MAC_ADDR_LEN           6

struct mac_desc {
	u32 cmd1;
	u32 cmd2;
	u32 addr1;
	u32 addr2;
};

struct skb_info {
	struct sk_buff *skb;
	u32 mapping;
	u32 len;
};

struct sp_common {
	void __iomem *sp_reg_base;
	void __iomem *moon5_reg_base;

	struct net_device *ndev;
	struct platform_device *pdev;

	void *desc_base;
	dma_addr_t desc_dma;
	s32 desc_size;
	struct clk *clk;
	struct reset_control *rstc;
	int irq;

	struct mac_desc *rx_desc[RX_DESC_QUEUE_NUM];
	struct skb_info *rx_skb_info[RX_DESC_QUEUE_NUM];
	u32 rx_pos[RX_DESC_QUEUE_NUM];
	u32 rx_desc_num[RX_DESC_QUEUE_NUM];
	u32 rx_desc_buff_size;

	struct mac_desc *tx_desc;
	struct skb_info tx_temp_skb_info[TX_DESC_NUM];
	u32 tx_done_pos;
	u32 tx_pos;
	u32 tx_desc_full;

	struct mii_bus *mii_bus;

	struct napi_struct rx_napi;
	struct napi_struct tx_napi;

	spinlock_t rx_lock;      // spinlock for accessing rx buffer
	spinlock_t tx_lock;      // spinlock for accessing tx buffer
	spinlock_t ioctl_lock;   // spinlock for ioctl operations

	u8 enable;
};

struct sp_mac {
	struct net_device *ndev;
	struct net_device *next_ndev;
	struct phy_device *phy_dev;
	struct sp_common *comm;
	struct net_device_stats dev_stats;
	struct device_node *phy_node;
	phy_interface_t phy_mode;
	u32 phy_addr;

	u8 mac_addr[ETHERNET_MAC_ADDR_LEN];

	u8 lan_port;
	u8 to_vlan;
	u8 cpu_port;
	u8 vlan_id;
};

#endif
