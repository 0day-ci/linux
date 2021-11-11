/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SP_PHY_H__
#define __SP_PHY_H__

#include "sp_define.h"

int  sp_phy_probe(struct net_device *netdev);
void sp_phy_start(struct net_device *netdev);
void sp_phy_stop(struct net_device *netdev);
void sp_phy_remove(struct net_device *netdev);

#endif
