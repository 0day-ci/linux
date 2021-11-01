/* SPDX-License-Identifier: GPL-2.0-or-later */
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

/* uart register map */
#define SP_UART_DATA	0x00
#define SP_UART_LSR		0x04
#define SP_UART_MSR		0x08
#define SP_UART_LCR		0x0C
#define SP_UART_MCR		0x10
#define SP_UART_DIV_L	0x14
#define SP_UART_DIV_H	0x18
#define SP_UART_ISC		0x1C

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

/* DMA-RX, dma_enable_sel */
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

#if !defined(__ASSEMBLY__)
#define UART_SZ			0x80
struct regs_uart {
	u32 uart_data;
	u32 uart_lsr;
	u32 uart_msr;
	u32 uart_lcr;
	u32 uart_mcr;
	u32 uart_div_l;
	u32 uart_div_h;
	u32 uart_isc;
	u32 uart_tx_residue;
	u32 uart_rx_residue;
	u32 uart_rx_threshold;
	u32 uart_clk_src;
};

struct regs_uarxdma {
	u32 rxdma_enable_sel;
	u32 rxdma_start_addr;
	u32 rxdma_timeout_set;
	u32 rxdma_reserved;
	u32 rxdma_wr_adr;
	u32 rxdma_rd_adr;
	u32 rxdma_length_thr;
	u32 rxdma_end_addr;
	u32 rxdma_databytes;
	u32 rxdma_debug_info;
};

struct regs_uatxdma {
	u32 txdma_enable;
	u32 txdma_sel;
	u32 txdma_start_addr;
	u32 txdma_end_addr;
	u32 txdma_wr_adr;
	u32 txdma_rd_adr;
	u32 txdma_status;
	u32 txdma_tmr_unit;
	u32 txdma_tmr_cnt;
	u32 txdma_rst_done;
};

struct regs_uatxgdma {
	u32 gdma_hw_ver;
	u32 gdma_config;
	u32 gdma_length;
	u32 gdma_addr;
	u32 gdma_port_mux;
	u32 gdma_int_flag;
	u32 gdma_int_en;
	u32 gdma_software_reset_status;
	u32 txdma_tmr_cnt;
};

#endif /* __ASSEMBLY__ */

#endif /* __SP_UART_H__ */
