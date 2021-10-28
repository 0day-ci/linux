/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi SoCs FDMA driver
 *
 * Copyright (c) 2021 Microchip
 */
#ifndef _MSCC_OCELOT_FDMA_H_
#define _MSCC_OCELOT_FDMA_H_

#include "ocelot.h"

/**
 * struct ocelot_fdma - FMDA struct
 *
 * @ocelot: Pointer to ocelot struct
 * @base: base address of FDMA registers
 * @dcb_pool: Pool used for DCB allocation
 * @irq: FDMA interrupt
 * @dev: Ocelot device
 * @napi: napi handle
 * @rx_buf_size: Size of RX buffer
 * @tx_ongoing: List of DCB handed out to the FDMA
 * @tx_queued: pending list of DCBs to be given to the hardware
 * @tx_enqueue_lock: Lock used for tx_queued and tx_ongoing
 * @tx_free_dcb: List of DCB available for TX
 * @tx_free_lock: Lock used to access tx_free_dcb list
 * @rx_hw: RX DCBs currently owned by the hardware and not completed
 * @rx_sw: RX DCBs completed
 */
struct ocelot_fdma {
	struct ocelot		*ocelot;
	void __iomem		*base;
	struct dma_pool		*dcb_pool;
	int			irq;
	struct device		*dev;
	struct napi_struct	napi;
	size_t			rx_buf_size;

	struct list_head	tx_ongoing;
	struct list_head	tx_queued;
	/* Lock for tx_queued and tx_ongoing lists */
	spinlock_t		tx_enqueue_lock;

	struct list_head	tx_free_dcb;
	/* Lock for tx_free_dcb list */
	spinlock_t		tx_free_lock;

	struct list_head	rx_hw;
	struct list_head	rx_sw;
};

struct ocelot_fdma *ocelot_fdma_init(struct platform_device *pdev,
				     struct ocelot *ocelot);
int ocelot_fdma_start(struct ocelot_fdma *fdma);
int ocelot_fdma_stop(struct ocelot_fdma *fdma);
int ocelot_fdma_inject_frame(struct ocelot_fdma *fdma, u32 rew_op,
			     struct sk_buff *skb, struct net_device *dev);
int ocelot_fdma_change_mtu(struct net_device *dev, int new_mtu);

#endif
