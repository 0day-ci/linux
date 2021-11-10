/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Sunplus SoC UART driver header file
 */

#ifndef __SP_UART_H__
#define __SP_UART_H__

#ifdef CONFIG_DEBUG_SP_UART
#include <mach/io_map.h>

#define LL_UART_PADDR		PA_IOB_ADDR(18 * 32 * 4)
#define LL_UART_VADDR		VA_IOB_ADDR(18 * 32 * 4)
#define LOGI_ADDR_UART0_REG	VA_IOB_ADDR(18 * 32 * 4)
#endif

/*
 * UART REG
 *
 */
#define SP_UART_DATA			0x00
#define SP_UART_LSR				0x04
#define SP_UART_MSR				0x08
#define SP_UART_LCR				0x0C
#define SP_UART_MCR				0x10
#define SP_UART_DIV_L			0x14
#define SP_UART_DIV_H			0x18
#define SP_UART_ISC				0x1C
#define SP_UART_TX_RESIDUE		0x20
#define SP_UART_RX_RESIDUE		0x24

/* lsr
 * 1: trasmit fifo is empty
 */
#define SP_UART_LSR_TXE		(1 << 6)

/* interrupt
 * SP_UART_LSR_BC : break condition
 * SP_UART_LSR_FE : frame error
 * SP_UART_LSR_OE : overrun error
 * SP_UART_LSR_PE : parity error
 * SP_UART_LSR_RX : 1: receive fifo not empty
 * SP_UART_LSR_TX : 1: transmit fifo is not full
 */
#define SP_UART_CREAD_DISABLED		(1 << 16)
#define SP_UART_LSR_BC		(1 << 5)
#define SP_UART_LSR_FE		(1 << 4)
#define SP_UART_LSR_OE		(1 << 3)
#define SP_UART_LSR_PE		(1 << 2)
#define SP_UART_LSR_RX		(1 << 1)
#define SP_UART_LSR_TX		(1 << 0)

#define SP_UART_LSR_BRK_ERROR_BITS	\
	(SP_UART_LSR_PE | SP_UART_LSR_OE | SP_UART_LSR_FE | SP_UART_LSR_BC)

/* lcr */
#define SP_UART_LCR_WL5		(0 << 0)
#define SP_UART_LCR_WL6		(1 << 0)
#define SP_UART_LCR_WL7		(2 << 0)
#define SP_UART_LCR_WL8		(3 << 0)
#define SP_UART_LCR_ST		(1 << 2)
#define SP_UART_LCR_PE		(1 << 3)
#define SP_UART_LCR_PR		(1 << 4)
#define SP_UART_LCR_BC		(1 << 5)

/* isc
 * SP_UART_ISC_MSM : Modem status ctrl
 * SP_UART_ISC_LSM : Line status interrupt
 * SP_UART_ISC_RXM : RX interrupt, when got some input data
 * SP_UART_ISC_TXM : TX interrupt, when trans start
 */
#define SP_UART_ISC_MSM		(1 << 7)
#define SP_UART_ISC_LSM		(1 << 6)
#define SP_UART_ISC_RXM		(1 << 5)
#define SP_UART_ISC_TXM		(1 << 4)
#define HAS_UART_ISC_FLAGMASK	0x0F
#define SP_UART_ISC_MS		(1 << 3)
#define SP_UART_ISC_LS		(1 << 2)
#define SP_UART_ISC_RX		(1 << 1)
#define SP_UART_ISC_TX		(1 << 0)

/* modem control register */
#define SP_UART_MCR_AT		(1 << 7)
#define SP_UART_MCR_AC		(1 << 6)
#define SP_UART_MCR_AR		(1 << 5)
#define SP_UART_MCR_LB		(1 << 4)
#define SP_UART_MCR_RI		(1 << 3)
#define SP_UART_MCR_DCD		(1 << 2)
#define SP_UART_MCR_RTS		(1 << 1)
#define SP_UART_MCR_DTS		(1 << 0)

/*
 * RX DMA REG
 *
 */
#define SP_UART_RXDMA_ENABLE_SEL		0x00
#define DMA_INT				(1 << 31)
#define DMA_MSI_ID_SHIFT	12
#define DMA_MSI_ID_MASK		(0x7F << DMA_MSI_ID_SHIFT)
#define DMA_SEL_UARTX_SHIFT	8
#define DMA_SEL_UARTX_MASK	(0x07 << DMA_SEL_UARTX_SHIFT)
#define DMA_SW_RST_B		(1 << 7)
#define DMA_INIT			(1 << 6)
#define DMA_GO				(1 << 5)
#define DMA_AUTO_ENABLE		(1 << 4)
#define DMA_TIMEOUT_INT_EN	(1 << 3)
#define DMA_P_SAFE_DISABLE	(1 << 2)
#define DMA_PBUS_DATA_SWAP	(1 << 1)
#define DMA_ENABLE			(1 << 0)
#define SP_UART_RXDMA_START_ADDR		0x04
#define SP_UART_RXDMA_TIMEOUT_SET		0x08
#define SP_UART_RXDMA_WR_ADR			0x10
#define SP_UART_RXDMA_RD_ADR			0x14
#define SP_UART_RXDMA_LENGTH_THR		0x18
#define SP_UART_RXDMA_END_ADDR			0x1C
#define SP_UART_RXDMA_DATABYTES			0x20
#define SP_UART_RXDMA_DEBUG_INFO		0x24
/*
 * TX DMA REG
 *
 */
#define SP_UART_TXDMA_ENABLE			0x00
#define SP_UART_TXDMA_SEL				0x04
#define SP_UART_TXDMA_START_ADDR		0x08
#define SP_UART_TXDMA_END_ADDR			0x0C
#define SP_UART_TXDMA_WR_ADR			0x10
#define SP_UART_TXDMA_RD_ADR			0x14
#define SP_UART_TXDMA_STATUS			0x18
#define TXDMA_BUF_FULL	0x01
#define TXDMA_BUF_EMPTY	0x02
#define SP_UART_TXDMA_TMR_UNIT			0x1C
#define SP_UART_TXDMA_TMR_CNT			0x20
#define SP_UART_TXDMA_RST_DONE			0x24
/*
 * TX GDMA REG
 *
 */
#define SP_UART_TXGDMA_HW_VER			0x00
#define SP_UART_TXGDMA_CONFIG			0x04
#define SP_UART_TXGDMA_LENGTH			0x08
#define SP_UART_TXGDMA_ADDR				0x0C
#define SP_UART_TXGDMA_PORT_MUX			0x10
#define SP_UART_TXGDMA_INT_FLAG			0x14
#define SP_UART_TXGDMA_INT_EN			0x18
#define SP_UART_TXGDMA_SOFT_RESET_STATUS	0x1C

#endif /* __SP_UART_H__ */
