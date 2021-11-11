/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SP_MAC_H__
#define __SP_MAC_H__

#include "sp_define.h"
#include "sp_hal.h"

void mac_init(struct sp_mac *mac);
void mac_soft_reset(struct sp_mac *mac);

// Calculate the empty tx descriptor number
#define TX_DESC_AVAIL(mac) \
	(((mac)->tx_pos != (mac)->tx_done_pos) ? \
	(((mac)->tx_done_pos < (mac)->tx_pos) ? \
	(TX_DESC_NUM - ((mac)->tx_pos - (mac)->tx_done_pos)) : \
	((mac)->tx_done_pos - (mac)->tx_pos)) : \
	((mac)->tx_desc_full ? 0 : TX_DESC_NUM))

#endif
