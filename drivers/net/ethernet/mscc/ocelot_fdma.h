/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi SoCs FDMA driver
 *
 * Copyright (c) 2021 Microchip
 */
#ifndef _MSCC_OCELOT_FDMA_H_
#define _MSCC_OCELOT_FDMA_H_

#include "ocelot.h"

#define OCELOT_FDMA_MAX_DCB		128
/* +4 allows for word alignment after allocation */
#define OCELOT_DCBS_HW_ALLOC_SIZE	(OCELOT_FDMA_MAX_DCB * \
					 sizeof(struct ocelot_fdma_dcb_hw_v2) + \
					 4)

struct ocelot_fdma_dcb_hw_v2 {
	u32 llp;
	u32 datap;
	u32 datal;
	u32 stat;
};

/**
 * struct ocelot_fdma_dcb - Software DCBs description
 *
 * @hw: hardware DCB used by hardware(coherent memory)
 * @hw_dma: DMA address of the DCB
 * @skb: skb associated with the DCB
 * @mapping: Address of the skb data mapping
 * @mapped_size: Mapped size
 */
struct ocelot_fdma_dcb {
	struct ocelot_fdma_dcb_hw_v2	*hw;
	dma_addr_t			hw_dma;
	struct sk_buff			*skb;
	dma_addr_t			mapping;
	size_t				mapped_size;
};

/**
 * struct ocelot_fdma_ring - "Ring" description of DCBs
 *
 * @hw_dcbs: Hardware DCBs allocated for the ring
 * @hw_dcbs_dma: DMA address of the DCBs
 * @dcbs: List of software DCBs
 * @head: pointer to first available DCB
 * @tail: pointer to last available DCB
 */
struct ocelot_fdma_ring {
	struct ocelot_fdma_dcb_hw_v2	*hw_dcbs;
	dma_addr_t			hw_dcbs_dma;
	struct ocelot_fdma_dcb		dcbs[OCELOT_FDMA_MAX_DCB];
	unsigned int			head;
	unsigned int			tail;
};

/**
 * struct ocelot_fdma - FMDA struct
 *
 * @ocelot: Pointer to ocelot struct
 * @base: base address of FDMA registers
 * @irq: FDMA interrupt
 * @dev: Ocelot device
 * @napi: napi handle
 * @rx_buf_size: Size of RX buffer
 * @inj: Injection ring
 * @xtr: Extraction ring
 * @xmit_lock: Xmit lock
 *
 */
struct ocelot_fdma {
	struct ocelot			*ocelot;
	void __iomem			*base;
	int				irq;
	struct device			*dev;
	struct napi_struct		napi;
	struct net_device		*ndev;
	size_t				rx_buf_size;
	struct ocelot_fdma_ring		inj;
	struct ocelot_fdma_ring		xtr;
	spinlock_t			xmit_lock;
};

struct ocelot_fdma *ocelot_fdma_init(struct platform_device *pdev,
				     struct ocelot *ocelot);
int ocelot_fdma_start(struct ocelot_fdma *fdma);
int ocelot_fdma_stop(struct ocelot_fdma *fdma);
int ocelot_fdma_inject_frame(struct ocelot_fdma *fdma, int port, u32 rew_op,
			     struct sk_buff *skb, struct net_device *dev);
void ocelot_fdma_netdev_init(struct ocelot_fdma *fdma, struct net_device *dev);
void ocelot_fdma_netdev_deinit(struct ocelot_fdma *fdma,
			       struct net_device *dev);

#endif
