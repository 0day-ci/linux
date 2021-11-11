/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SP_HAL_H__
#define __SP_HAL_H__

#include "sp_register.h"
#include "sp_define.h"
#include "sp_desc.h"

void hal_mac_stop(struct sp_mac *mac);
void hal_mac_reset(struct sp_mac *mac);
void hal_mac_start(struct sp_mac *mac);
void hal_mac_addr_set(struct sp_mac *mac);
void hal_mac_addr_del(struct sp_mac *mac);
void hal_mac_addr_table_del_all(struct sp_mac *mac);
void hal_mac_init(struct sp_mac *mac);
void hal_rx_mode_set(struct net_device *ndev);
int  hal_mdio_access(struct sp_mac *mac, u8 op_cd, u8 phy_addr, u8 reg_addr, u32 wdata);
void hal_tx_trigger(struct sp_mac *mac);
void hal_set_rmii_tx_rx_pol(struct sp_mac *mac);
void hal_phy_addr(struct sp_mac *mac);
u32  read_sw_int_mask0(struct sp_mac *mac);
void write_sw_int_mask0(struct sp_mac *mac, u32 value);
void write_sw_int_status0(struct sp_mac *mac, u32 value);
u32  read_sw_int_status0(struct sp_mac *mac);
u32  read_port_ability(struct sp_mac *mac);

#endif
