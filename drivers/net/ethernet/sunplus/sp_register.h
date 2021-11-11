/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SP_REGISTER_H__
#define __SP_REGISTER_H__

#include "sp_define.h"

/* TYPE: RegisterFile_L2SW */
#define SP_SW_INT_STATUS_0		0x0
#define SP_SW_INT_MASK_0		0x4
#define SP_FL_CNTL_TH			0x8
#define SP_CPU_FL_CNTL_TH		0xc
#define SP_PRI_FL_CNTL			0x10
#define SP_VLAN_PRI_TH			0x14
#define SP_EN_TOS_BUS			0x18
#define SP_TOS_MAP0			0x1c
#define SP_TOS_MAP1			0x20
#define SP_TOS_MAP2			0x24
#define SP_TOS_MAP3			0x28
#define SP_TOS_MAP4			0x2c
#define SP_TOS_MAP5			0x30
#define SP_TOS_MAP6			0x34
#define SP_TOS_MAP7			0x38
#define SP_GLOBAL_QUE_STATUS		0x3c
#define SP_ADDR_TBL_SRCH		0x40
#define SP_ADDR_TBL_ST			0x44
#define SP_MAC_AD_SER0			0x48
#define SP_MAC_AD_SER1			0x4c
#define SP_WT_MAC_AD0			0x50
#define SP_W_MAC_15_0			0x54
#define SP_W_MAC_47_16			0x58
#define SP_PVID_CONFIG0			0x5c
#define SP_PVID_CONFIG1			0x60
#define SP_VLAN_MEMSET_CONFIG0		0x64
#define SP_VLAN_MEMSET_CONFIG1		0x68
#define SP_PORT_ABILITY			0x6c
#define SP_PORT_ST			0x70
#define SP_CPU_CNTL			0x74
#define SP_PORT_CNTL0			0x78
#define SP_PORT_CNTL1			0x7c
#define SP_PORT_CNTL2			0x80
#define SP_SW_GLB_CNTL			0x84
#define SP_SP_SW_RESET			0x88
#define SP_LED_PORT0			0x8c
#define SP_LED_PORT1			0x90
#define SP_LED_PORT2			0x94
#define SP_LED_PORT3			0x98
#define SP_LED_PORT4			0x9c
#define SP_WATCH_DOG_TRIG_RST		0xa0
#define SP_WATCH_DOG_STOP_CPU		0xa4
#define SP_PHY_CNTL_REG0		0xa8
#define SP_PHY_CNTL_REG1		0xac
#define SP_MAC_FORCE_MODE		0xb0
#define SP_VLAN_GROUP_CONFIG0		0xb4
#define SP_VLAN_GROUP_CONFIG1		0xb8
#define SP_FLOW_CTRL_TH3		0xbc
#define SP_QUEUE_STATUS_0		0xc0
#define SP_DEBUG_CNTL			0xc4
#define SP_RESERVED_1			0xc8
#define SP_MEM_TEST_INFO		0xcc
#define SP_SW_INT_STATUS_1		0xd0
#define SP_SW_INT_MASK_1		0xd4
#define SP_SW_GLOBAL_SIGNAL		0xd8

#define SP_CPU_TX_TRIG			0x208
#define SP_TX_HBASE_ADDR_0		0x20c
#define SP_TX_LBASE_ADDR_0		0x210
#define SP_RX_HBASE_ADDR_0		0x214
#define SP_RX_LBASE_ADDR_0		0x218
#define SP_TX_HW_ADDR_0			0x21c
#define SP_TX_LW_ADDR_0			0x220
#define SP_RX_HW_ADDR_0			0x224
#define SP_RX_LW_ADDR_0			0x228
#define SP_CPU_PORT_CNTL_REG_0		0x22c
#define SP_TX_HBASE_ADDR_1		0x230
#define SP_TX_LBASE_ADDR_1		0x234
#define SP_RX_HBASE_ADDR_1		0x238
#define SP_RX_LBASE_ADDR_1		0x23c
#define SP_TX_HW_ADDR_1			0x240
#define SP_TX_LW_ADDR_1			0x244
#define SP_RX_HW_ADDR_1			0x248
#define SP_RX_LW_ADDR_1			0x24c
#define SP_CPU_PORT_CNTL_REG_1		0x250

/* TYPE: RegisterFile_MOON5 */
#define MOON5_MO5_THERMAL_CTL_0		0x0
#define MOON5_MO5_THERMAL_CTL_1		0x4
#define MOON5_MO4_THERMAL_CTL_2		0xc
#define MOON5_MO4_THERMAL_CTL_3		0x8
#define MOON5_MO4_TMDS_L2SW_CTL		0x10
#define MOON5_MO4_L2SW_CLKSW_CTL	0x14

#endif
