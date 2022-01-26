/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __DWMAC1000_DMA_H__
#define __DWMAC1000_DMA_H__
#include "dwmac1000.h"

void dwmac1000_dma_axi(void __iomem *ioaddr, struct stmmac_axi *axi);
void dwmac1000_dma_init(void __iomem *ioaddr,
			struct stmmac_dma_cfg *dma_cfg, int atds);
void dwmac1000_dma_init_rx(void __iomem *ioaddr,
			   struct stmmac_dma_cfg *dma_cfg,
			   dma_addr_t dma_rx_phy, u32 chan);
void dwmac1000_dma_init_tx(void __iomem *ioaddr,
			   struct stmmac_dma_cfg *dma_cfg,
			   dma_addr_t dma_tx_phy, u32 chan);
void dwmac1000_dma_operation_mode_rx(void __iomem *ioaddr, int mode,
				     u32 channel, int fifosz, u8 qmode);
void dwmac1000_dma_operation_mode_tx(void __iomem *ioaddr, int mode,
				     u32 channel, int fifosz, u8 qmode);
void dwmac1000_dump_dma_regs(void __iomem *ioaddr, u32 *reg_space);

int  dwmac1000_get_hw_feature(void __iomem *ioaddr,
			      struct dma_features *dma_cap);

void dwmac1000_rx_watchdog(void __iomem *ioaddr, u32 riwt, u32 number_chan);
#endif /* __DWMAC1000_DMA_H__ */
