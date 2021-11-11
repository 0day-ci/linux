/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#ifndef __SP_INT_H__
#define __SP_INT_H__

int rx_poll(struct napi_struct *napi, int budget);
int tx_poll(struct napi_struct *napi, int budget);
irqreturn_t ethernet_interrupt(int irq, void *dev_id);

#endif
