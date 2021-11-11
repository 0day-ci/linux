/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SP_DRIVER_H__
#define __SP_DRIVER_H__

#include "sp_define.h"
#include "sp_register.h"
#include "sp_hal.h"
#include "sp_int.h"
#include "sp_mdio.h"
#include "sp_mac.h"
#include "sp_desc.h"

#define NEXT_TX(N)              ((N) = (((N) + 1) == TX_DESC_NUM) ? 0 : (N) + 1)
#define NEXT_RX(QUEUE, N)       ((N) = (((N) + 1) == mac->comm->rx_desc_num[QUEUE]) ? 0 : (N) + 1)

#define RX_NAPI_WEIGHT          16
#define TX_NAPI_WEIGHT          16

#endif
