/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SP_MDIO_H__
#define __SP_MDIO_H__

#include "sp_define.h"
#include "sp_hal.h"

#define MDIO_READ_CMD           0x02
#define MDIO_WRITE_CMD          0x01

u32  mdio_read(struct sp_mac *mac, u32 phy_id, u16 regnum);
u32  mdio_write(struct sp_mac *mac, u32 phy_id, u32 regnum, u16 val);
u32  mdio_init(struct platform_device *pdev, struct net_device *ndev);
void mdio_remove(struct net_device *ndev);

#endif
