// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Sunplus SoC UART driver
 *
 * Author: Hammer Hsieh <hammer.hsieh@sunplus.com>
 * Tony Huang <tony.huang@sunplus.com>
 * Wells Lu <wells.lu@sunplus.com>
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/of_platform.h>
#include <asm/irq.h>
#if defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#include <linux/sysrq.h>
#endif
#include <linux/serial_core.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <soc/sunplus/sp_uart.h>

#ifdef CONFIG_PM_RUNTIME_UART
#include <linux/pm_runtime.h>
#endif

#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <dt-bindings/clock/sp-sp7021.h>
#include <dt-bindings/pinctrl/sppctl-sp7021.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>

#define DEVICE_NAME				"ttyS"
#define SP_UART_MAJOR			TTY_MAJOR
#define SP_UART_MINOR_START		64

#define NUM_UART		6	/* serial0,  ... */
#define NUM_UARTDMARX	2	/* serial10, ... */
#define NUM_UARTDMATX	2	/* serial20, ... */

#define ID_BASE_DMARX	10
#define ID_BASE_DMATX	20

#define IS_UARTDMARX_ID(X) \
	(((X) >= (ID_BASE_DMARX)) && ((X) < (ID_BASE_DMARX + NUM_UARTDMARX)))
#define IS_UARTDMATX_ID(X) \
	(((X) >= (ID_BASE_DMATX)) && ((X) < (ID_BASE_DMATX + NUM_UARTDMATX)))

//#define TTYS_KDBG_INFO
#define TTYS_KDBG_ERR
//#define TTYS_GPIO

#ifdef TTYS_KDBG_INFO
#define DBG_INFO(fmt, args ...)	pr_info("K_TTYS: " fmt, ## args)
#else
#define DBG_INFO(fmt, args ...)
#endif
#ifdef TTYS_KDBG_ERR
#define DBG_ERR(fmt, args ...)	pr_err("K_TTYS: " fmt, ## args)
#else
#define DBG_ERR(fmt, args ...)
#endif

#define SP_UART_CREAD_DISABLED		(1 << 16)

#define UARXDMA_BUF_SZ			PAGE_SIZE
#define MAX_SZ_RXDMA_ISR		(1 << 9)
#define UATXDMA_BUF_SZ			PAGE_SIZE

#define CLK_HIGH_UART			202500000
#define UART_RATIO				232

#if defined(CONFIG_SP_MON)
extern unsigned int uart0_mask_tx;	/* Used for masking uart0 tx output */
#endif

struct sunplus_uart_port {
	char name[16];
	struct uart_port uport;
	struct sunplus_uartdma_info *uartdma_rx;
	struct sunplus_uartdma_info *uartdma_tx;
	struct clk *clk;
	struct reset_control *rstc;
	struct gpio_desc *rts_gpio;
	struct hrtimer CheckTXE;
	struct hrtimer DelayRtsBeforeSend;
	struct hrtimer DelayRtsAfterSend;
};
struct sunplus_uart_port sunplus_uart_ports[NUM_UART];

struct sunplus_uartdma_info {
	void __iomem *membase;	/* virtual address */
	unsigned long addr_phy;
	void __iomem *gdma_membase;	/* virtual address */
	int irq;
	int which_uart;
	struct sunplus_uart_port *binding_port;
	void *buf_va;
	dma_addr_t dma_handle;
};
static struct sunplus_uartdma_info
	sunplus_uartdma[NUM_UARTDMARX + NUM_UARTDMATX];

static inline void sp_uart_set_int_en(unsigned char __iomem *base,
	unsigned int int_state)
{
	writel(int_state, &((struct regs_uart *)base)->uart_isc);
}

static inline unsigned int sp_uart_get_int_en(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_isc);
}

static inline int sp_uart_get_char(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_data);
}

static inline void sp_uart_put_char(struct uart_port *port, unsigned int ch)
{
	struct sunplus_uart_port *sp_port =
		(struct sunplus_uart_port *)(port->private_data);
	struct sunplus_uartdma_info *uartdma_tx =
		sp_port->uartdma_tx;
	struct regs_uatxdma *txdma_reg;
	unsigned char __iomem *base = port->membase;
	u32 addr_sw, addr_start;
	u32 offset_sw;
	u8 *byte_ptr;

#if defined(CONFIG_SP_MON)
	if ((uart0_mask_tx == 1) && ((u32)base == LOGI_ADDR_UART0_REG))
		return;
#endif

	if (!uartdma_tx) {
		writel(ch, &((struct regs_uart *)base)->uart_data);
	} else {
		txdma_reg = (struct regs_uatxdma *)(uartdma_tx->membase);
		addr_sw = readl(&(txdma_reg->txdma_wr_adr));
		addr_start = readl(&(txdma_reg->txdma_start_addr));
		offset_sw = addr_sw - addr_start;
		byte_ptr = (u8 *)(uartdma_tx->buf_va + offset_sw);
		*byte_ptr = (u8)(ch);
		if (offset_sw == (UATXDMA_BUF_SZ - 1))
			writel((u32)(uartdma_tx->dma_handle), &(txdma_reg->txdma_wr_adr));
		else
			writel((addr_sw + 1), &(txdma_reg->txdma_wr_adr));
	}
}

static inline unsigned int sp_uart_get_line_status(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_lsr);
}

static inline u32 sp_uart_line_status_tx_buf_not_full(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port =
		(struct sunplus_uart_port *)(port->private_data);
	struct sunplus_uartdma_info *uartdma_tx =
		sp_port->uartdma_tx;
	struct regs_uatxdma *txdma_reg;
	unsigned char __iomem *base = port->membase;
	u32 addr_sw, addr_hw;

	if (uartdma_tx) {
		txdma_reg = (struct regs_uatxdma *)(uartdma_tx->membase);
		if (readl(&(txdma_reg->txdma_status)) & 0x01)
			return 0;

		addr_sw = readl(&(txdma_reg->txdma_wr_adr));
		addr_hw = readl(&(txdma_reg->txdma_rd_adr));
		if (addr_sw >= addr_hw)
			return (UATXDMA_BUF_SZ - (addr_sw - addr_hw));
		else
			return (addr_hw - addr_sw);

	} else {
		if (readl(&((struct regs_uart *)base)->uart_lsr) & SP_UART_LSR_TX) {
			/*
			 * In PIO mode, just return 1 byte
			 * becauase exactly number is unknown
			 */
			return 1;
		} else {
			return 0;
		}
	}
}

static inline void sp_uart_set_line_ctrl(unsigned char __iomem *base,
	unsigned int ctrl)
{
	writel(ctrl, &((struct regs_uart *)base)->uart_lcr);
}

static inline unsigned int sp_uart_get_line_ctrl(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_lcr);
}

static inline void sp_uart_set_divider_low_register(unsigned char __iomem *base,
	unsigned int val)
{
	writel(val, &((struct regs_uart *)base)->uart_div_l);
}

static inline unsigned int sp_uart_get_divider_low_register(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_div_l);
}

static inline void sp_uart_set_divider_high_register(unsigned char __iomem *base,
	unsigned int val)
{
	writel(val, &((struct regs_uart *)base)->uart_div_h);
}

static inline unsigned int sp_uart_get_divider_high_register(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_div_h);
}

static inline void sp_uart_set_rx_residue(unsigned char __iomem *base,
	unsigned int val)
{
	writel(val, &((struct regs_uart *)base)->uart_rx_residue);
}

static inline void sp_uart_set_modem_ctrl(unsigned char __iomem *base,
	unsigned int val)
{
	writel(val, &((struct regs_uart *)base)->uart_mcr);
}

static inline unsigned int sp_uart_get_modem_ctrl(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_mcr);
}

static inline void sp_uart_set_clk_src(unsigned char __iomem *base,
	unsigned int val)
{
	writel(val, &((struct regs_uart *)base)->uart_clk_src);
}

/*
 * Note:
 * When (uart0_as_console == 0), please make sure:
 *     There is no "console=ttyS0,115200", "earlyprintk", ... in kernel command line.
 *     In /etc/inittab, there is no something like "ttyS0::respawn:/bin/sh"
 */
unsigned int uart0_as_console = ~0;
unsigned int uart_enable_status = ~0;	/* bit 0: UART0, bit 1: UART1, ... */

#if defined(CONFIG_SP_MON)
extern int sysrqCheckState(char, struct uart_port *);
#endif

static struct sunplus_uartdma_info *sunplus_uartdma_rx_binding(int id)
{
	int i;

	for (i = 0; i < NUM_UARTDMARX; i++) {
		if ((sunplus_uartdma[i].which_uart == id) && (sunplus_uartdma[i].membase)) {
			sunplus_uartdma[i].binding_port = &sunplus_uart_ports[id];
			return &sunplus_uartdma[i];
		}
	}
	return NULL;
}

static struct sunplus_uartdma_info *sunplus_uartdma_tx_binding(int id)
{
	int i;

	for (i = NUM_UARTDMARX; i < (NUM_UARTDMARX + NUM_UARTDMATX); i++) {
		if ((sunplus_uartdma[i].which_uart == id) && (sunplus_uartdma[i].membase)) {
			sunplus_uartdma[i].binding_port = &sunplus_uart_ports[id];
			return &sunplus_uartdma[i];
		}
	}
	return NULL;
}

static inline void wait_for_xmitr(struct uart_port *port)
{
	while (1) {
		if (sp_uart_line_status_tx_buf_not_full(port))
			break;
	}
}

static unsigned int sunplus_uart_ops_tx_empty(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = (struct sunplus_uart_port *)(port->private_data);
	struct sunplus_uartdma_info *uartdma_tx = sp_port->uartdma_tx;
	struct regs_uatxdma *txdma_reg;

	if (uartdma_tx) {
		txdma_reg = (struct regs_uatxdma *)(uartdma_tx->membase);
		if (readl(&(txdma_reg->txdma_status)) & 0x02)
			return TIOCSER_TEMT;
		else
			return 0;
	} else {
		return ((sp_uart_get_line_status(port->membase) & SP_UART_LSR_TXE)
			? TIOCSER_TEMT : 0);
	}
}

static void sunplus_uart_ops_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct sunplus_uart_port *sp_port =
		(struct sunplus_uart_port *)(port->private_data);
	unsigned char mcr = sp_uart_get_modem_ctrl(port->membase);
	ktime_t ktime;

	if (mctrl & TIOCM_DTR)
		mcr |= SP_UART_MCR_DTS;
	else
		mcr &= ~SP_UART_MCR_DTS;

	if (mctrl & TIOCM_RTS) {
		if ((sp_port->uport.rs485.flags & SER_RS485_ENABLED)
			&& (sp_port->uport.rs485.flags & SER_RS485_RTS_ON_SEND))
			mcr &= ~SP_UART_MCR_RTS;  /* RTS invert */
		else
			mcr |= SP_UART_MCR_RTS;
	} else {
		/* data transfer ended */
		if (sp_port->uport.rs485.flags & SER_RS485_ENABLED)
			return;

		mcr &= ~SP_UART_MCR_RTS;
	}

	if (mctrl & TIOCM_CAR)
		mcr |= SP_UART_MCR_DCD;
	else
		mcr &= ~SP_UART_MCR_DCD;

	if (mctrl & TIOCM_RI)
		mcr |= SP_UART_MCR_RI;
	else
		mcr &= ~SP_UART_MCR_RI;

	if (mctrl & TIOCM_LOOP)
		mcr |= SP_UART_MCR_LB;
	else
		mcr &= ~SP_UART_MCR_LB;

	sp_uart_set_modem_ctrl(port->membase, mcr);

	if (sp_port->uport.rs485.flags & SER_RS485_ENABLED) {
		mcr = sp_uart_get_modem_ctrl(port->membase);
		if (mctrl & TIOCM_RTS) {
			if (((mcr & SP_UART_MCR_RTS)
				&& (sp_port->uport.rs485.flags & SER_RS485_RTS_AFTER_SEND))
				|| (sp_port->uport.rs485.flags & SER_RS485_RTS_ON_SEND)) {
				if (!IS_ERR(sp_port->rts_gpio))
					gpiod_set_value(sp_port->rts_gpio, 1);
				dev_dbg(port->dev, "%s set rts_gpio=1\n", port->name);
				if (sp_port->uport.rs485.delay_rts_before_send == 0) {
					ktime = ktime_set(0, 500000); /* 500us */
					hrtimer_start(&sp_port->CheckTXE, ktime, HRTIMER_MODE_REL);
				}
			}
		}
	}
}

static unsigned int sunplus_uart_ops_get_mctrl(struct uart_port *port)
{
	unsigned char status;
	unsigned int ret = 0;

	status = sp_uart_get_modem_ctrl(port->membase);

	if (status & SP_UART_MCR_DTS)
		ret |= TIOCM_DTR;

	if (status & SP_UART_MCR_RTS)
		ret |= TIOCM_RTS;

	if (status & SP_UART_MCR_DCD)
		ret |= TIOCM_CAR;

	if (status & SP_UART_MCR_RI)
		ret |= TIOCM_RI;

	if (status & SP_UART_MCR_LB)
		ret |= TIOCM_LOOP;

	if (status & SP_UART_MCR_AC)
		ret |= TIOCM_CTS;

	return ret;
}

static void sunplus_uart_ops_stop_tx(struct uart_port *port)
{
	unsigned int isc = sp_uart_get_int_en(port->membase);

	/*
	 * Even if (uartdma_tx != NULL), "BUF_NOT_FULL" interrupt
	 * is used for getting into ISR
	 */
	isc &= ~SP_UART_ISC_TXM;
	sp_uart_set_int_en(port->membase, isc);
}

static inline void __start_tx(struct uart_port *port)
{
	unsigned int isc;

	isc = sp_uart_get_int_en(port->membase) | SP_UART_ISC_TXM;
	sp_uart_set_int_en(port->membase, isc);
}

static void sunplus_uart_ops_start_tx(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port =
		(struct sunplus_uart_port *)(port->private_data);
	ktime_t ktime;
	int val;

	if (sp_port->uport.rs485.flags & SER_RS485_ENABLED) {
		val = (sp_port->uport.rs485.flags & SER_RS485_RTS_ON_SEND ? 1 : 0);
		if (!IS_ERR(sp_port->rts_gpio))
			gpiod_set_value(sp_port->rts_gpio, val);

		dev_dbg(port->dev, "%s set rts_gpio=%d\n", sp_port->uport.name, val);
		if (sp_port->uport.rs485.delay_rts_before_send > 0) {
			long nsec = sp_port->uport.rs485.delay_rts_before_send * 1000000;

			ktime = ktime_set(0, nsec);
			hrtimer_start(&sp_port->DelayRtsBeforeSend, ktime, HRTIMER_MODE_REL);
			return;
		}
	}
	__start_tx(port);
}

static void sunplus_uart_ops_stop_rx(struct uart_port *port)
{
	unsigned int isc;

	isc = sp_uart_get_int_en(port->membase);
	isc &= ~SP_UART_ISC_RXM;
	sp_uart_set_int_en(port->membase, isc);
}

static void sunplus_uart_ops_enable_ms(struct uart_port *port)
{
	/* Do nothing */
}

static void sunplus_uart_ops_break_ctl(struct uart_port *port, int ctl)
{
	unsigned long flags;
	unsigned int h_lcr;

	spin_lock_irqsave(&port->lock, flags);

	h_lcr = sp_uart_get_line_ctrl(port->membase);
	if (ctl != 0)
		h_lcr |= SP_UART_LCR_BC;	/* start break */
	else
		h_lcr &= ~SP_UART_LCR_BC;	/* stop break */

	sp_uart_set_line_ctrl(port->membase, h_lcr);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void transmit_chars(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port =
		(struct sunplus_uart_port *)(port->private_data);
	struct sunplus_uartdma_info *uartdma_tx = sp_port->uartdma_tx;
	u32 tx_buf_available;
	struct regs_uatxdma *txdma_reg;
	u32 addr_sw, addr_start;
	u32 offset_sw;
	u8 *byte_ptr;
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		sp_uart_put_char(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		sunplus_uart_ops_stop_tx(port);
		return;
	}

	if (uartdma_tx) {
		txdma_reg = (struct regs_uatxdma *)(uartdma_tx->membase);
		addr_sw = readl(&(txdma_reg->txdma_wr_adr));
		addr_start = readl(&(txdma_reg->txdma_start_addr));
		offset_sw = addr_sw - addr_start;
		byte_ptr = (u8 *)(uartdma_tx->buf_va + offset_sw);
		tx_buf_available = sp_uart_line_status_tx_buf_not_full(port);
		while (tx_buf_available) {
			*byte_ptr = xmit->buf[xmit->tail];
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
			port->icount.tx++;

			byte_ptr++;
			addr_sw++;
			offset_sw++;
			tx_buf_available--;
			if (offset_sw == UATXDMA_BUF_SZ) {
				offset_sw = 0;
				addr_sw = (u32)(uartdma_tx->dma_handle);
				byte_ptr = (u8 *)(uartdma_tx->buf_va);
			}

			if (uart_circ_empty(xmit))
				break;
		}
		writel(addr_sw, &(txdma_reg->txdma_wr_adr));
	} else {
		do {
			sp_uart_put_char(port, xmit->buf[xmit->tail]);
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
			port->icount.tx++;

			if (uart_circ_empty(xmit))
				break;
		} while (sp_uart_line_status_tx_buf_not_full(port));
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		sunplus_uart_ops_stop_tx(port);
}

/* called by ISR */
static void receive_chars(struct uart_port *port)
{
	struct tty_struct *tty = port->state->port.tty;
	unsigned char lsr = sp_uart_get_line_status(port->membase);
	unsigned int ch, flag;

	do {
		ch = sp_uart_get_char(port->membase);

#if defined(CONFIG_SP_MON)
		if (sysrqCheckState(ch, port) != 0)
			goto ignore_char;
#endif

		flag = TTY_NORMAL;
		port->icount.rx++;

		if (unlikely(lsr & SP_UART_LSR_BRK_ERROR_BITS)) {
			if (port->cons == NULL)
				DBG_ERR("UART%d, SP_UART_LSR_BRK_ERROR_BITS, lsr = 0x%08X\n",
					port->line, lsr);

			if (lsr & SP_UART_LSR_BC) {
				lsr &= ~(SP_UART_LSR_FE | SP_UART_LSR_PE);
				port->icount.brk++;
				if (uart_handle_break(port))
					goto ignore_char;
			} else if (lsr & SP_UART_LSR_PE) {
				if (port->cons == NULL)
					DBG_ERR("UART%d, SP_UART_LSR_PE\n", port->line);
				port->icount.parity++;
			} else if (lsr & SP_UART_LSR_FE) {
				if (port->cons == NULL)
					DBG_ERR("UART%d, SP_UART_LSR_FE\n", port->line);
				port->icount.frame++;
			}
			if (lsr & SP_UART_LSR_OE) {
				if (port->cons == NULL)
					DBG_ERR("UART%d, SP_UART_LSR_OE\n", port->line);
				port->icount.overrun++;
			}

			/*
			 * Mask off conditions which should be ignored.
			 */

			/* lsr &= port->read_status_mask; */

			if (lsr & SP_UART_LSR_BC)
				flag = TTY_BREAK;
			else if (lsr & SP_UART_LSR_PE)
				flag = TTY_PARITY;
			else if (lsr & SP_UART_LSR_FE)
				flag = TTY_FRAME;
		}

		if (port->ignore_status_mask & SP_UART_CREAD_DISABLED)
			goto ignore_char;

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		uart_insert_char(port, lsr, SP_UART_LSR_OE, ch, flag);

ignore_char:
		lsr = sp_uart_get_line_status(port->membase);
	} while (lsr & SP_UART_LSR_RX);

	if (tty) {
		spin_unlock(&port->lock);
		tty_flip_buffer_push(tty->port);
		spin_lock(&port->lock);
	}
}

static irqreturn_t sunplus_uart_irq(int irq, void *args)
{
	struct uart_port *port = (struct uart_port *)args;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	if (sp_uart_get_int_en(port->membase) & SP_UART_ISC_RX)
		receive_chars(port);

	if (sp_uart_get_int_en(port->membase) & SP_UART_ISC_TX)
		transmit_chars(port);

	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

/* called by ISR */
static void receive_chars_rxdma(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port =
		(struct sunplus_uart_port *)(port->private_data);
	struct sunplus_uartdma_info *uartdma_rx;
	struct regs_uarxdma *rxdma_reg;
	struct tty_struct *tty = port->state->port.tty;
	u32 offset_sw, offset_hw, rx_size, dma_start;
	u8 *sw_ptr, *buf_end_ptr, *u8_ptr;
	u32 icount_rx;
	u32 tmp_u32;
	u8 tmp_buf[32];
	int i;

	uartdma_rx = sp_port->uartdma_rx;
	rxdma_reg = (struct regs_uarxdma *)(uartdma_rx->membase);

	dma_start = readl(&(rxdma_reg->rxdma_start_addr));
	offset_sw = readl(&(rxdma_reg->rxdma_rd_adr)) - dma_start;
	offset_hw = readl(&(rxdma_reg->rxdma_wr_adr)) - dma_start;

	if (offset_hw >= offset_sw)
		rx_size = offset_hw - offset_sw;
	else
		rx_size = offset_hw + UARXDMA_BUF_SZ - offset_sw;

	sw_ptr = (u8 *)(uartdma_rx->buf_va + offset_sw);
	buf_end_ptr = (u8 *)(uartdma_rx->buf_va + UARXDMA_BUF_SZ);

	/*
	 * Retrieve all data in ISR.
	 * The max. received size is (buffer_size - threshold_size)
	 * = (rxdma_length_thr[31:16] - rxdma_length_thr[15:0]) = MAX_SZ_RXDMA_ISR
	 * In order to limit the execution time in this ISR:
	 * => Increase rxdma_length_thr[15:0] to shorten each ISR execution time.
	 * => Don't need to set a small threshold_size,
	 *    and split a long ISR into several shorter ISRs.
	 */
	icount_rx = 0;
	while (rx_size > icount_rx) {
		/* 32-byte aligned */
		if (!(((unsigned long)(sw_ptr)) & (32 - 1))
		    && ((rx_size - icount_rx) >= 32)) {
			/*
			 * Copy 32 bytes data from non cache area to cache area.
			 * => It should use less DRAM controller's read command.
			 */
			memcpy(tmp_buf, sw_ptr, 32);
			u8_ptr = (u8 *)(tmp_buf);
			for (i = 0; i < 32; i++) {
				port->icount.rx++;
				uart_insert_char(port, 0, SP_UART_LSR_OE,
					(unsigned int)(*u8_ptr), TTY_NORMAL);
				u8_ptr++;
			}
			sw_ptr += 32;
			icount_rx += 32;
		} else {
			port->icount.rx++;
			uart_insert_char(port, 0, SP_UART_LSR_OE,
				(unsigned int)(*sw_ptr), TTY_NORMAL);
			sw_ptr++;
			icount_rx++;
		}
		if (sw_ptr >= buf_end_ptr)
			sw_ptr = (u8 *)(uartdma_rx->buf_va);
	}
	tmp_u32 = readl(&(rxdma_reg->rxdma_rd_adr)) + rx_size;
	if (tmp_u32 <= readl(&(rxdma_reg->rxdma_end_addr)))
		writel(tmp_u32, &(rxdma_reg->rxdma_rd_adr));
	else
		writel((tmp_u32 - UARXDMA_BUF_SZ), &(rxdma_reg->rxdma_rd_adr));

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tty->port);
	spin_lock(&port->lock);

	writel(readl(&(rxdma_reg->rxdma_enable_sel)) | DMA_INT,
		&(rxdma_reg->rxdma_enable_sel));
	writel(readl(&(rxdma_reg->rxdma_enable_sel)) | DMA_GO,
		&(rxdma_reg->rxdma_enable_sel));
}

static irqreturn_t sunplus_uart_rxdma_irq(int irq, void *args)
{
	struct uart_port *port = (struct uart_port *)args;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
#ifdef TTYS_GPIO
	gpio_set_value(0, 1);
#endif
	receive_chars_rxdma(port);

	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

static int sunplus_uart_ops_startup(struct uart_port *port)
{
	int ret;
	u32 timeout, interrupt_en;
	struct sunplus_uart_port *sp_port =
		(struct sunplus_uart_port *)(port->private_data);
	struct sunplus_uartdma_info *uartdma_rx, *uartdma_tx;
	struct regs_uarxdma *rxdma_reg;
	struct regs_uatxdma *txdma_reg;
	struct regs_uatxgdma *gdma_reg;
	unsigned int ch;

	if (sp_port->uport.rs485.flags & SER_RS485_ENABLED)
		hrtimer_cancel(&sp_port->DelayRtsAfterSend);

#ifdef CONFIG_PM_RUNTIME_UART
	if (port->line > 0) {
		ret = pm_runtime_get_sync(port->dev);
		if (ret < 0)
			goto out;
	}
#endif

	ret = request_irq(port->irq, sunplus_uart_irq, 0, sp_port->name, port);
	if (ret)
		return ret;

	uartdma_rx = sp_port->uartdma_rx;
	if (uartdma_rx) {
		rxdma_reg = (struct regs_uarxdma *)(uartdma_rx->membase);

		if (uartdma_rx->buf_va == NULL) {
			/* Drop data in Rx FIFO (PIO mode) */
			while (sp_uart_get_line_status(port->membase) & SP_UART_LSR_RX)
				ch = sp_uart_get_char(port->membase);

			uartdma_rx->buf_va =
				dma_alloc_coherent(port->dev, UARXDMA_BUF_SZ,
				&(uartdma_rx->dma_handle), GFP_KERNEL);
			if (uartdma_rx->buf_va == NULL) {
				DBG_ERR("%s, %d, Can't allocation buffer for %s\n",
					__func__, __LINE__, sp_port->name);
				ret = -ENOMEM;
				goto error_01;
			}
			DBG_INFO("DMA buffer (Rx) for %s: VA: 0x%p, PA: 0x%x\n",
				sp_port->name, uartdma_rx->buf_va, (u32)(uartdma_rx->dma_handle));

			writel((u32)(uartdma_rx->dma_handle), &(rxdma_reg->rxdma_start_addr));
			writel((u32)(uartdma_rx->dma_handle), &(rxdma_reg->rxdma_rd_adr));

			/* Force to use CLK_HIGH_UART in this mode */
			/* Switch clock source when setting baud rate */
			timeout = (CLK_HIGH_UART / 2) / 1000000 * 1000;	/* 1 msec */

			/* DBG_INFO("timeout: 0x%x\n", timeout); */
			writel(timeout, &(rxdma_reg->rxdma_timeout_set));

			/*
			 * When there are only rxdma_length_thr[15:0] bytes
			 * of free buffer => Trigger interrupt
			 */
			writel(((UARXDMA_BUF_SZ << 16) | (UARXDMA_BUF_SZ - MAX_SZ_RXDMA_ISR)),
			    &(rxdma_reg->rxdma_length_thr));
			writel((readl(&(rxdma_reg->rxdma_enable_sel)) & (~DMA_SEL_UARTX_MASK)),
			    &(rxdma_reg->rxdma_enable_sel));
			writel((readl(&(rxdma_reg->rxdma_enable_sel))
				| (DMA_INIT | (uartdma_rx->which_uart << DMA_SEL_UARTX_SHIFT))),
				&(rxdma_reg->rxdma_enable_sel));
			writel((readl(&(rxdma_reg->rxdma_enable_sel)) & (~DMA_INIT)),
			    &(rxdma_reg->rxdma_enable_sel));
			writel((readl(&(rxdma_reg->rxdma_enable_sel))
				| (DMA_SW_RST_B | DMA_AUTO_ENABLE
				| DMA_TIMEOUT_INT_EN | DMA_ENABLE)),
				&(rxdma_reg->rxdma_enable_sel));
			writel((readl(&(rxdma_reg->rxdma_enable_sel)) | (DMA_GO)),
			    &(rxdma_reg->rxdma_enable_sel));
		}
		DBG_INFO("Enable RXDMA for %s (irq=%d)\n", sp_port->name, uartdma_rx->irq);
		ret = request_irq(uartdma_rx->irq, sunplus_uart_rxdma_irq, 0, "UARTDMA_RX", port);
		if (ret) {
			dma_free_coherent(port->dev, UARXDMA_BUF_SZ,
				uartdma_rx->buf_va, uartdma_rx->dma_handle);
			goto error_00;
		}
	}

	uartdma_tx = sp_port->uartdma_tx;
	if (uartdma_tx) {
		txdma_reg = (struct regs_uatxdma *)(uartdma_tx->membase);
		gdma_reg = (struct regs_uatxgdma *)(uartdma_tx->gdma_membase);
		DBG_INFO("Enable TXDMA for %s\n", sp_port->name);

		if (uartdma_tx->buf_va == NULL) {
			uartdma_tx->buf_va =
				dma_alloc_coherent(port->dev, UATXDMA_BUF_SZ,
				&(uartdma_tx->dma_handle), GFP_KERNEL);
			if (uartdma_tx->buf_va == NULL) {
				DBG_ERR("%s, %d, Can't allocation buffer for %s\n",
					__func__, __LINE__, sp_port->name);
				ret = -ENOMEM;
				goto error_01;
			}
			DBG_INFO("DMA buffer (Tx) for %s: VA: 0x%p, PA: 0x%x\n",
				sp_port->name, uartdma_tx->buf_va, (u32)(uartdma_tx->dma_handle));

			/*
			 * set 1ms , set wr_adr , set start_addr/end_addr ,
			 * set bind to uart# , set int enable ,
			 * set txdma enable (Use ring buffer for UART's Tx)
			 */
			writel((CLK_HIGH_UART / 1000), &(txdma_reg->txdma_tmr_unit));
			writel((u32)(uartdma_tx->dma_handle), &(txdma_reg->txdma_wr_adr));
			writel((u32)(uartdma_tx->dma_handle), &(txdma_reg->txdma_start_addr));
			writel(((u32)(uartdma_tx->dma_handle) + UATXDMA_BUF_SZ - 1),
				&(txdma_reg->txdma_end_addr));
			writel(uartdma_tx->which_uart, &(txdma_reg->txdma_sel));
			writel(0x41, &(gdma_reg->gdma_int_en));
			writel(0x00000005, &(txdma_reg->txdma_enable));
		}
	}

	/*
	 * don't need to use spin_lock_irqsave() because interrupts
	 * are globally disabled
	 */
	spin_lock_irq(&port->lock);

	/* SP_UART_ISC_TXM is enabled in .start_tx() */
	interrupt_en = 0;
	if (uartdma_rx == NULL)
		interrupt_en |= SP_UART_ISC_RXM;

	sp_uart_set_int_en(port->membase, interrupt_en);

	spin_unlock_irq(&port->lock);

#ifdef CONFIG_PM_RUNTIME_UART
	if (port->line > 0)
		pm_runtime_put(port->dev);

	return 0;

out:
	if (port->line > 0) {
		pr_info("pm_out\n");
		pm_runtime_mark_last_busy(port->dev);
		pm_runtime_put_autosuspend(port->dev);
	}
#endif
	return 0;

error_01:
	if (uartdma_rx) {
		dma_free_coherent(port->dev, UARXDMA_BUF_SZ,
			uartdma_rx->buf_va, uartdma_rx->dma_handle);
		free_irq(uartdma_rx->irq, port);
	}
error_00:
	free_irq(port->irq, port);
	return ret;
}

static void sunplus_uart_ops_shutdown(struct uart_port *port)
{
	unsigned long flags;
	struct sunplus_uart_port *sp_port =
		(struct sunplus_uart_port *)(port->private_data);
	struct sunplus_uartdma_info *uartdma_rx;
	struct regs_uarxdma *rxdma_reg;

	spin_lock_irqsave(&port->lock, flags);
	sp_uart_set_int_en(port->membase, 0);
	spin_unlock_irqrestore(&port->lock, flags);

	free_irq(port->irq, port);

	uartdma_rx = sp_port->uartdma_rx;
	if (uartdma_rx) {
		rxdma_reg = (struct regs_uarxdma *)(uartdma_rx->membase);

		/* Drop whatever is still in buffer */
		writel(readl(&(rxdma_reg->rxdma_wr_adr)), &(rxdma_reg->rxdma_rd_adr));

		free_irq(uartdma_rx->irq, port);
		DBG_INFO("free_irq(%d)\n", uartdma_rx->irq);
	}

	/* Disable flow control of Tx, so that queued data can be sent out
	 * There is no way for s/w to let h/w abort in the middle of
	 * transaction.
	 * Don't reset module except it's in idle state. Otherwise, it might
	 * cause bus to hang.
	 */
	sp_uart_set_modem_ctrl(port->membase,
		sp_uart_get_modem_ctrl(port->membase) & (~(SP_UART_MCR_AC)));

}

static void sunplus_uart_ops_set_termios(struct uart_port *port,
	struct ktermios *termios, struct ktermios *oldtermios)
{
	u32 clk, ext, div, div_l, div_h, baud;
	u32 lcr;
	unsigned long flags;

	baud = uart_get_baud_rate(port, termios, oldtermios, 0, (CLK_HIGH_UART >> 4));

	clk = port->uartclk;

	/*
	 * Send all data in Tx FIFO before changing clock source,
	 * it should be UART0 only
	 */
	while (!(sp_uart_get_line_status(port->membase) & SP_UART_LSR_TXE))
		;

	/* Switch clock source: 0 for sysclk, 1 for XTAL */
	sp_uart_set_clk_src(port->membase, clk == XTAL);

	clk += baud >> 1;
	div = clk / baud;
	ext = div & 0x0F;
	div = (div >> 4) - 1;
	div_l = (div & 0xFF) | (ext << 12);
	div_h = div >> 8;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr = SP_UART_LCR_WL5;
		break;
	case CS6:
		lcr = SP_UART_LCR_WL6;
		break;
	case CS7:
		lcr = SP_UART_LCR_WL7;
		break;
	default:	/* CS8 */
		lcr = SP_UART_LCR_WL8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		lcr |= SP_UART_LCR_ST;

	if (termios->c_cflag & PARENB) {
		lcr |= SP_UART_LCR_PE;

		if (!(termios->c_cflag & PARODD))
			lcr |= SP_UART_LCR_PR;
	}

	spin_lock_irqsave(&port->lock, flags);

	uart_update_timeout(port, termios->c_cflag, baud);

	port->read_status_mask = 0;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= SP_UART_LSR_PE | SP_UART_LSR_FE;

	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= SP_UART_LSR_BC;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= SP_UART_LSR_FE | SP_UART_LSR_PE;

	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= SP_UART_LSR_BC;

		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= SP_UART_LSR_OE;
	}

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((termios->c_cflag & CREAD) == 0) {
		port->ignore_status_mask |= SP_UART_CREAD_DISABLED;
		/* flush rx data FIFO */
		sp_uart_set_rx_residue(port->membase, 0);
	}

	if (termios->c_cflag & CRTSCTS) {
		sp_uart_set_modem_ctrl(port->membase,
			sp_uart_get_modem_ctrl(port->membase)
			| (SP_UART_MCR_AC | SP_UART_MCR_AR));
	} else {
		sp_uart_set_modem_ctrl(port->membase,
			sp_uart_get_modem_ctrl(port->membase)
			& (~(SP_UART_MCR_AC | SP_UART_MCR_AR)));
	}

	/* do not set these in emulation */
	sp_uart_set_divider_high_register(port->membase, div_h);
	sp_uart_set_divider_low_register(port->membase, div_l);
	sp_uart_set_line_ctrl(port->membase, lcr);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void sunplus_uart_ops_set_ldisc(struct uart_port *port,
				       struct ktermios *termios)
{
	int new = termios->c_line;

	if (new == N_PPS) {
		port->flags |= UPF_HARDPPS_CD;
		sunplus_uart_ops_enable_ms(port);
	} else {
		port->flags &= ~UPF_HARDPPS_CD;
	}
}

static const char *sunplus_uart_ops_type(struct uart_port *port)
{
	struct sunplus_uart_port *sunplus_uart_port =
		(struct sunplus_uart_port *)port->private_data;
	return sunplus_uart_port->name;
}

static void sunplus_uart_ops_release_port(struct uart_port *port)
{
}

static int sunplus_uart_ops_request_port(struct uart_port *port)
{
	return 0;
}

static void sunplus_uart_ops_config_port(struct uart_port *port, int type)
{
	if (type & UART_CONFIG_TYPE) {
		port->type = PORT_SUNPLUS;
		sunplus_uart_ops_request_port(port);
	}
}

static int sunplus_uart_ops_verify_port(struct uart_port *port, struct serial_struct *serial)
{
	return -EINVAL;
}

#ifdef CONFIG_CONSOLE_POLL
static int sunplus_uart_ops_poll_init(struct uart_port *port)
{
	return 0;
}

static void sunplus_uart_ops_poll_put_char(struct uart_port *port,
	unsigned char data)
{
	wait_for_xmitr(port);
	sp_uart_put_char(port, data);
}

static int sunplus_uart_ops_poll_get_char(struct uart_port *port)
{
	unsigned int status;
	unsigned char data;

	do {
		status = sp_uart_get_line_status(port->membase);
	} while (!(status & SP_UART_LSR_RX));

	data = sp_uart_get_char(port->membase);
	return (int)data;
}
#endif /* CONFIG_CONSOLE_POLL */

static enum hrtimer_restart Check_TXE(struct hrtimer *t)
{
	unsigned char lsr;
	struct sunplus_uart_port *rs485;
	unsigned char mcr, val;
	ktime_t ktime;
	unsigned long nsec;

	rs485 = container_of(t, struct sunplus_uart_port, CheckTXE);
	mcr = sp_uart_get_modem_ctrl(rs485->uport.membase);
	lsr = sp_uart_get_line_status(rs485->uport.membase);
	// TX is NOT empty
	if (!(lsr & SP_UART_LSR_TXE))
		return HRTIMER_RESTART;
	// TX is empty
	nsec = rs485->uport.rs485.delay_rts_after_send * 1000000;
	// schedule RTS set
	if (nsec > 0) {
		ktime = ktime_set(0, nsec);
		hrtimer_start(&rs485->DelayRtsAfterSend, ktime, HRTIMER_MODE_REL);
		return HRTIMER_NORESTART;
	}
	// immediately set RTS
	val = (rs485->uport.rs485.flags & SER_RS485_RTS_AFTER_SEND ? 1 : 0);
	if (rs485->uport.rs485.flags & SER_RS485_RTS_ON_SEND)
		mcr |= SP_UART_MCR_RTS;
	else
		mcr &= ~SP_UART_MCR_RTS;
	if (rs485->uport.rs485.flags & SER_RS485_ENABLED && !IS_ERR(rs485->rts_gpio)) {
		gpiod_set_value(rs485->rts_gpio, val);
		dev_dbg(rs485->uport.dev, "%s set rts_gpio=%d\n",
			rs485->uport.name, val);
	} else {
		sp_uart_set_modem_ctrl(rs485->uport.membase, mcr);
	}
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart Delay_Rts_Before_Send(struct hrtimer *t)
{
	struct sunplus_uart_port *rs485;
	ktime_t ktime;

	rs485 = container_of(t, struct sunplus_uart_port, DelayRtsBeforeSend);
	ktime = ktime_set(0, 500000); //500us
	hrtimer_start(&rs485->CheckTXE, ktime, HRTIMER_MODE_REL);
	__start_tx(&rs485->uport);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart Delay_Rts_After_Send(struct hrtimer *t)
{
	struct sunplus_uart_port *rs485;
	unsigned char mcr, val;

	rs485 = container_of(t, struct sunplus_uart_port, DelayRtsAfterSend);
	mcr = sp_uart_get_modem_ctrl(rs485->uport.membase);

	val = (rs485->uport.rs485.flags & SER_RS485_RTS_AFTER_SEND ? 1 : 0);
	if (rs485->uport.rs485.flags & SER_RS485_RTS_ON_SEND)
		mcr |= SP_UART_MCR_RTS;
	else
		mcr &= ~SP_UART_MCR_RTS;
	if (rs485->uport.rs485.flags & SER_RS485_ENABLED && !IS_ERR(rs485->rts_gpio)) {
		gpiod_set_value(rs485->rts_gpio, val);
		dev_dbg(rs485->uport.dev, "%s set rts_gpio=%d\n",
			rs485->uport.name, val);
	} else {
		sp_uart_set_modem_ctrl(rs485->uport.membase, mcr);
	}
	return HRTIMER_NORESTART;
}

static int sunplus_uart_rs485_onn(struct uart_port *_up,
	struct sunplus_uart_port *_sup)
{
	if (IS_ERR(_sup->rts_gpio)) {
		DBG_ERR("%s %s No valid rts_gpio, disabling 485\n",
			_up->name, __func__);
		_up->rs485.flags &= ~SER_RS485_ENABLED;
		return (-EINVAL);
	}
	DBG_INFO("%s %s rts_gpio is at G_MX[%d].\n",
		_up->name, __func__, desc_to_gpio(_sup->rts_gpio));
	if (!_sup->CheckTXE.function) {
		hrtimer_init(&(_sup->CheckTXE), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		_sup->CheckTXE.function = Check_TXE;
	}
	if (!_sup->DelayRtsBeforeSend.function) {
		hrtimer_init(&(_sup->DelayRtsBeforeSend), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		_sup->DelayRtsBeforeSend.function = Delay_Rts_Before_Send;
	}
	if (!_sup->DelayRtsAfterSend.function) {
		hrtimer_init(&(_sup->DelayRtsAfterSend), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		_sup->DelayRtsAfterSend.function = Delay_Rts_After_Send;
	}
	return (0);
}

static int sunplus_uart_rs485_off(struct uart_port *_up,
	struct sunplus_uart_port *_sup)
{
	_up->rs485.flags &= ~SER_RS485_ENABLED;
	if (_sup->CheckTXE.function) {
		hrtimer_cancel(&_sup->CheckTXE);
		_sup->CheckTXE.function = NULL;
	}
	if (_sup->DelayRtsBeforeSend.function) {
		hrtimer_cancel(&_sup->DelayRtsBeforeSend);
		_sup->DelayRtsBeforeSend.function = NULL;
	}
	if (_sup->DelayRtsAfterSend.function) {
		hrtimer_cancel(&_sup->DelayRtsAfterSend);
		_sup->DelayRtsAfterSend.function = NULL;
	}
	return (0);
}

static int sunplus_uart_config_rs485(struct uart_port *_up,
	struct serial_rs485 *_rs485)
{
	struct sunplus_uart_port *_sup =
		(struct sunplus_uart_port *)_up->private_data;
	int val;

	if (IS_ERR(_sup->rts_gpio)) {
		DBG_ERR("%s %s No valid rts_gpio - skipping rs485\n",
			_up->name, __func__);
		_up->rs485.flags &= ~SER_RS485_ENABLED;
		return (-EINVAL);
	}

#ifdef CONFIG_PM_RUNTIME_UART
	pm_runtime_get_sync(_sup->dev);
#endif

	_rs485->delay_rts_before_send =
		min(_rs485->delay_rts_before_send, 10000U);
	_rs485->delay_rts_after_send  =
		min(_rs485->delay_rts_after_send, 10000U);

	if (_rs485->flags & SER_RS485_ENABLED) {
		dev_dbg(_up->dev, "%s enabling rs485...\n", _up->name);
		dev_dbg(_up->dev, "%s on_    send:%d (delay:%d)\n",
			_up->name, _rs485->flags & SER_RS485_RTS_ON_SEND,
			_rs485->delay_rts_before_send);
		dev_dbg(_up->dev, "%s after_ send:%d (delay:%d)\n",
			_up->name, _rs485->flags & SER_RS485_RTS_AFTER_SEND,
			_rs485->delay_rts_after_send);
		sunplus_uart_rs485_onn(_up, _sup);
		val = (_rs485->flags & SER_RS485_RTS_AFTER_SEND ? 1 : 0);
		gpiod_set_value(_sup->rts_gpio, val);
		dev_dbg(_up->dev, "%s set rts_gpio=%d\n", _up->name, val);
	} else {
		dev_dbg(_up->dev, "%s disabling rs485...\n", _up->name);
		sunplus_uart_rs485_off(_up, _sup);
	}
	_up->rs485 = *_rs485;

#ifdef CONFIG_PM_RUNTIME_UART
	pm_runtime_mark_last_busy(_sup->dev);
	pm_runtime_put_autosuspend(_sup->dev);
#endif
	return (0);
}

static const struct uart_ops sunplus_uart_ops = {
	.tx_empty		= sunplus_uart_ops_tx_empty,
	.set_mctrl		= sunplus_uart_ops_set_mctrl,
	.get_mctrl		= sunplus_uart_ops_get_mctrl,
	.stop_tx		= sunplus_uart_ops_stop_tx,
	.start_tx		= sunplus_uart_ops_start_tx,
	.stop_rx		= sunplus_uart_ops_stop_rx,
	.enable_ms		= sunplus_uart_ops_enable_ms,
	.break_ctl		= sunplus_uart_ops_break_ctl,
	.startup		= sunplus_uart_ops_startup,
	.shutdown		= sunplus_uart_ops_shutdown,
	.set_termios		= sunplus_uart_ops_set_termios,
	.set_ldisc		= sunplus_uart_ops_set_ldisc,
	.type			= sunplus_uart_ops_type,
	.release_port		= sunplus_uart_ops_release_port,
	.request_port		= sunplus_uart_ops_request_port,
	.config_port		= sunplus_uart_ops_config_port,
	.verify_port		= sunplus_uart_ops_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init		= sunplus_uart_ops_poll_init,
	.poll_put_char		= sunplus_uart_ops_poll_put_char,
	.poll_get_char		= sunplus_uart_ops_poll_get_char,
#endif /* CONFIG_CONSOLE_POLL */
};

static struct uart_driver sunplus_uart_driver;

#ifdef CONFIG_SERIAL_SUNPLUS_CONSOLE
static void sunplus_uart_console_putchar(struct uart_port *port, int ch)
{
	wait_for_xmitr(port);
	sp_uart_put_char(port, ch);
}

static void sunplus_console_write(struct console *co,
	const char *s, unsigned int count)
{
	unsigned long flags;
	int locked = 1;
	struct sunplus_uart_port *sp_port;
	struct sunplus_uartdma_info *uartdma_tx;
	struct regs_uatxdma *txdma_reg;
	struct regs_uatxgdma *gdma_reg;

	local_irq_save(flags);

#if defined(SUPPORT_SYSRQ)
	if (sunplus_uart_ports[co->index].uport.sysrq)
#else
	if (0)
#endif
	{
		locked = 0;
	} else if (oops_in_progress) {
		locked = spin_trylock(&sunplus_uart_ports[co->index].uport.lock);
	} else {
		spin_lock(&sunplus_uart_ports[co->index].uport.lock);
	}

	sp_port = (struct sunplus_uart_port *)(sunplus_uart_ports[co->index].uport.private_data);
	uartdma_tx = sp_port->uartdma_tx;
	if (uartdma_tx) {
		txdma_reg = (struct regs_uatxdma *)(uartdma_tx->membase);
		gdma_reg = (struct regs_uatxgdma *)(uartdma_tx->gdma_membase);
		if (readl(&(txdma_reg->txdma_enable)) == 0x00000005) {
			/* ring buffer for UART's Tx has been enabled */
			uart_console_write(&sunplus_uart_ports[co->index].uport,
				s, count, sunplus_uart_console_putchar);
		} else {
			/* Refer to .startup() */
			if (uartdma_tx->buf_va == NULL) {
				uartdma_tx->buf_va =
					dma_alloc_coherent(sunplus_uart_ports[co->index].uport.dev,
					UATXDMA_BUF_SZ, &(uartdma_tx->dma_handle), GFP_KERNEL);
				/*
				 * This message can't be sent to console
				 * because it's not ready yet
				 */
				if (uartdma_tx->buf_va == NULL)
					panic("Die here.");

				/*
				 * set 1ms , set wr_adr , set start_addr/end_addr ,
				 * set bind to uart# , set int enable ,
				 * set txdma enable (Use ring buffer for UART's Tx)
				 */
				writel((CLK_HIGH_UART / 1000),
					&(txdma_reg->txdma_tmr_unit));
				writel((u32)(uartdma_tx->dma_handle),
					&(txdma_reg->txdma_wr_adr));
				writel((u32)(uartdma_tx->dma_handle),
					&(txdma_reg->txdma_start_addr));
				writel(((u32)(uartdma_tx->dma_handle) + UATXDMA_BUF_SZ - 1),
					&(txdma_reg->txdma_end_addr));
				writel(uartdma_tx->which_uart,
					&(txdma_reg->txdma_sel));
				writel(0x41, &(gdma_reg->gdma_int_en));
				writel(0x00000005, &(txdma_reg->txdma_enable));
			}
		}
	} else {
		uart_console_write(&sunplus_uart_ports[co->index].uport,
			s, count, sunplus_uart_console_putchar);
	}

	if (locked)
		spin_unlock(&sunplus_uart_ports[co->index].uport.lock);

	local_irq_restore(flags);
}

static int __init sunplus_console_setup(struct console *co, char *options)
{
	int ret = 0;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if ((co->index >= NUM_UART) || (co->index < 0))
		return -EINVAL;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	ret = uart_set_options(&sunplus_uart_ports[co->index].uport,
		co, baud, parity, bits, flow);

	return ret;
}

static struct console sunplus_console = {
	.name		= DEVICE_NAME,
	.write		= sunplus_console_write,
	.device		= uart_console_device,	/* default */
	.setup		= sunplus_console_setup,
	/*
	 * CON_ENABLED,
	 * CON_CONSDEV: preferred console,
	 * CON_BOOT: primary boot console,
	 * CON_PRINTBUFFER: used for printk buffer
	 */
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &sunplus_uart_driver
};
#endif

static struct uart_driver sunplus_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "Sunplus_UART",
	.dev_name	= DEVICE_NAME,
	.major		= SP_UART_MAJOR,
	.minor		= SP_UART_MINOR_START,
	.nr		= NUM_UART,
#ifdef CONFIG_SERIAL_SUNPLUS_CONSOLE
	.cons		= &sunplus_console
#endif
};

struct platform_device *sunpluse_uart_platform_device;

static int sunplus_uart_platform_driver_probe_of(struct platform_device *pdev)
{
	struct resource *res_mem;
	struct uart_port *port;
	struct clk *clk;
	int ret, irq;
	int idx_offset, idx;
	int idx_which_uart;
	char peri_name[16];
#ifdef TTYS_GPIO
	int uart_gpio;
#endif

	if (pdev->dev.of_node) {
		pdev->id = of_alias_get_id(pdev->dev.of_node, "serial");
		if (pdev->id < 0)
			pdev->id = of_alias_get_id(pdev->dev.of_node, "uart");
	}

	DBG_INFO("sunplus_uart-ID = %d\n", pdev->id);

	idx_offset = -1;
	if (IS_UARTDMARX_ID(pdev->id)) {
		idx_offset = 0;
		DBG_INFO("Setup DMA Rx %d\n", (pdev->id - ID_BASE_DMARX));
	} else if (IS_UARTDMATX_ID(pdev->id)) {
		idx_offset = NUM_UARTDMARX;
		DBG_INFO("Setup DMA Tx %d\n", (pdev->id - ID_BASE_DMATX));
	}
	if (idx_offset >= 0) {
		DBG_INFO("Enable DMA clock(s)\n");
		clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(clk)) {
			DBG_ERR("Can't find clock source\n");
			return PTR_ERR(clk);
		}

		ret = clk_prepare_enable(clk);
		if (ret) {
			DBG_ERR("Clock can't be enabled correctly\n");
			return ret;
		}

		if (idx_offset == 0)
			idx = idx_offset + pdev->id - ID_BASE_DMARX;
		else
			idx = idx_offset + pdev->id - ID_BASE_DMATX;

		res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res_mem)
			return -ENODEV;

		sprintf(peri_name, "PERI%d", (idx & 0x01));
		DBG_INFO("Enable clock %s\n", peri_name);
		clk = devm_clk_get(&pdev->dev, peri_name);
		if (IS_ERR(clk)) {
			DBG_ERR("Can't find clock source: %s\n", peri_name);
			return PTR_ERR(clk);
		}

		ret = clk_prepare_enable(clk);
		if (ret) {
			DBG_ERR("%s can't be enabled correctly\n", peri_name);
			return ret;
		}

		sunplus_uartdma[idx].addr_phy =
			(unsigned long)(res_mem->start);
		sunplus_uartdma[idx].membase =
			devm_ioremap_resource(&pdev->dev, res_mem);

		if (IS_ERR(sunplus_uartdma[idx].membase))
			return PTR_ERR(sunplus_uartdma[idx].membase);

		if (IS_UARTDMARX_ID(pdev->id)) {
			irq = platform_get_irq(pdev, 0);
			if (irq < 0)
				return -ENODEV;

			sunplus_uartdma[idx].irq = irq;
		} else {
			res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
			if (!res_mem)
				return -ENODEV;

			sunplus_uartdma[idx].gdma_membase =
				devm_ioremap_resource(&pdev->dev, res_mem);

			if (IS_ERR(sunplus_uartdma[idx].gdma_membase))
				return -EINVAL;

			DBG_INFO("gdma_phy: 0x%x gdma_membase: 0x%p\n",
				res_mem->start,
				sunplus_uartdma[idx].gdma_membase);
		}

		if (of_property_read_u32(pdev->dev.of_node, "which-uart", &idx_which_uart) != 0) {
			DBG_ERR("\"which-uart\" is not assigned.");
			return -EINVAL;
		}
		if (idx_which_uart >= NUM_UART) {
			DBG_ERR("\"which-uart\" is not valid.");
			return -EINVAL;
		}
		sunplus_uartdma[idx].which_uart = idx_which_uart;

		DBG_INFO("adr: 0x%lx, base: 0x%p, irq: %d, uart: %d\n",
			sunplus_uartdma[idx].addr_phy,
			sunplus_uartdma[idx].membase,
			sunplus_uartdma[idx].irq,
			sunplus_uartdma[idx].which_uart);

		return 0;
	} else if (pdev->id < 0 || pdev->id >= NUM_UART) {
		return -EINVAL;
	}

	port = &sunplus_uart_ports[pdev->id].uport;
	if (port->membase)
		return -EBUSY;

	memset(port, 0, sizeof(*port));

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem)
		return -ENODEV;

	port->dev = &pdev->dev;
	port->mapbase = res_mem->start;
	port->membase = devm_ioremap_resource(&pdev->dev, res_mem);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	uart_get_rs485_mode(port);
	sunplus_uart_ports[pdev->id].rts_gpio =
		devm_gpiod_get(&pdev->dev, "rts", GPIOD_OUT_LOW);
	port->rs485_config = sunplus_uart_config_rs485;
	sunplus_uart_ports[pdev->id].CheckTXE.function = NULL;
	sunplus_uart_ports[pdev->id].DelayRtsBeforeSend.function = NULL;
	sunplus_uart_ports[pdev->id].DelayRtsAfterSend.function = NULL;
	if (port->rs485.flags & SER_RS485_ENABLED)
		sunplus_uart_rs485_onn(port, &(sunplus_uart_ports[pdev->id]));

	DBG_INFO("Enable UART clock(s)\n");
	sunplus_uart_ports[pdev->id].clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sunplus_uart_ports[pdev->id].clk)) {
		DBG_ERR("Can't find clock source\n");
		return PTR_ERR(sunplus_uart_ports[pdev->id].clk);
	}

	ret = clk_prepare_enable(sunplus_uart_ports[pdev->id].clk);
	if (ret) {
		DBG_ERR("Clock can't be enabled correctly\n");
		return ret;
	}

	sunplus_uart_ports[pdev->id].rstc =
		devm_reset_control_get(&pdev->dev, NULL);

	if (IS_ERR(sunplus_uart_ports[pdev->id].rstc)) {
		DBG_ERR("failed to retrieve reset controller: %d\n", ret);
		return PTR_ERR(sunplus_uart_ports[pdev->id].rstc);
	}

	ret = reset_control_deassert(sunplus_uart_ports[pdev->id].rstc);

	if (ret) {
		DBG_ERR("failed to deassert reset line: %d\n", ret);
		return ret;
	}

	clk = sunplus_uart_ports[pdev->id].clk;
	if (IS_ERR(clk))
		port->uartclk = XTAL;
	else
		port->uartclk = clk_get_rate(clk);

	port->iotype = UPIO_MEM;
	port->irq = irq;
	port->ops = &sunplus_uart_ops;
	port->flags = UPF_BOOT_AUTOCONF;
	port->fifosize = 128;
	port->line = pdev->id;

#ifdef CONFIG_SERIAL_SUNPLUS_CONSOLE
	if (pdev->id == 0)
		port->cons = &sunplus_console;
#endif
#ifdef TTYS_GPIO
	if (pdev->id == 1) {
		uart_gpio = of_get_named_gpio(pdev->dev.of_node,
			"uart-gpio", 0);
		if (!gpio_is_valid(uart_gpio))
			pr_info("gpio get error\n");
		else
			pr_info("gpio no. %d\n", uart_gpio);
	}
#endif
	port->private_data = container_of(&sunplus_uart_ports[pdev->id].uport,
		struct sunplus_uart_port, uport);
	sprintf(sunplus_uart_ports[pdev->id].name, "sp_uart%d", pdev->id);

	sunplus_uart_ports[pdev->id].uartdma_rx =
		sunplus_uartdma_rx_binding(pdev->id);

	if (sunplus_uart_ports[pdev->id].uartdma_rx)
		DBG_ERR("%s's Rx is in DMA mode.\n",
			sunplus_uart_ports[pdev->id].name);
	else
		DBG_ERR("%s's Rx is in PIO mode.\n",
			sunplus_uart_ports[pdev->id].name);

	sunplus_uart_ports[pdev->id].uartdma_tx =
		sunplus_uartdma_tx_binding(pdev->id);

	if (sunplus_uart_ports[pdev->id].uartdma_tx)
		DBG_ERR("%s's Tx is in DMA mode.\n",
			sunplus_uart_ports[pdev->id].name);
	else
		DBG_ERR("%s's Tx is in PIO mode.\n",
			sunplus_uart_ports[pdev->id].name);

	ret = uart_add_one_port(&sunplus_uart_driver, port);
	if (ret) {
		port->membase = NULL;
		return ret;
	}
	platform_set_drvdata(pdev, port);

#ifdef CONFIG_PM_RUNTIME_UART
	if (pdev->id != 0) {
		pm_runtime_set_autosuspend_delay(&pdev->dev, 5000);
		pm_runtime_use_autosuspend(&pdev->dev);
		pm_runtime_set_active(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
	}
#endif
	return 0;
}

static int sunplus_uart_platform_driver_remove(struct platform_device *pdev)
{
#ifdef CONFIG_PM_RUNTIME_UART
	if (pdev->id != 0) {
		pm_runtime_disable(&pdev->dev);
		pm_runtime_set_suspended(&pdev->dev);
	}
#endif
	uart_remove_one_port(&sunplus_uart_driver,
		&sunplus_uart_ports[pdev->id].uport);

	if (pdev->id < NUM_UART) {
		clk_disable_unprepare(sunplus_uart_ports[pdev->id].clk);
		reset_control_assert(sunplus_uart_ports[pdev->id].rstc);
	}

	return 0;
}

static int sunplus_uart_platform_driver_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	/* Don't suspend uart0 for cmd line usage */
	if ((pdev->id < NUM_UART) && (pdev->id > 0))
		reset_control_assert(sunplus_uart_ports[pdev->id].rstc);

	return 0;
}

static int sunplus_uart_platform_driver_resume(struct platform_device *pdev)
{
	if (pdev->id < NUM_UART) {
		clk_prepare_enable(sunplus_uart_ports[pdev->id].clk);
		reset_control_deassert(sunplus_uart_ports[pdev->id].rstc);
	}
	return 0;
}

static const struct of_device_id sp_uart_of_match[] = {
	{ .compatible = "sunplus,sp7021-uart" },
	{}
};
MODULE_DEVICE_TABLE(of, sp_uart_of_match);

#ifdef CONFIG_PM_RUNTIME_UART
static int sunplus_uart_runtime_suspend(struct device *dev)
{
	struct platform_device *uartpdev = to_platform_device(dev);

	/* Don't suspend uart0 for cmd line usage */
	if ((uartpdev->id < NUM_UART) && (uartpdev->id > 0))
		reset_control_assert(sunplus_uart_ports[uartpdev->id].rstc);

	return 0;
}

static int sunplus_uart_runtime_resume(struct device *dev)
{
	struct platform_device *uartpdev = to_platform_device(dev);

	if (uartpdev->id < NUM_UART) {
		clk_prepare_enable(sunplus_uart_ports[uartpdev->id].clk);
		reset_control_deassert(sunplus_uart_ports[uartpdev->id].rstc);
	}

	return 0;
}
static const struct dev_pm_ops sp7021_uart_pm_ops = {
	.runtime_suspend = sunplus_uart_runtime_suspend,
	.runtime_resume  = sunplus_uart_runtime_resume,
};
#define sunplus_uart_pm_ops  (&sp7021_uart_pm_ops)
#endif

static struct platform_driver sunplus_uart_platform_driver = {
	.probe		= sunplus_uart_platform_driver_probe_of,
	.remove		= sunplus_uart_platform_driver_remove,
	.suspend	= sunplus_uart_platform_driver_suspend,
	.resume		= sunplus_uart_platform_driver_resume,
	.driver = {
		.name	= DEVICE_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sp_uart_of_match),
#ifdef CONFIG_PM_RUNTIME_UART
		.pm     = sunplus_uart_pm_ops,
#endif
	}
};

static int __init sunplus_uart_init(void)
{
	int ret;

	memset(sunplus_uart_ports, 0, sizeof(sunplus_uart_ports));
	memset(sunplus_uartdma, 0, sizeof(sunplus_uartdma));

	if (!uart0_as_console || !(uart_enable_status & 0x01))
		sunplus_uart_driver.cons = NULL;

	/* /proc/tty/driver/(sunplus_uart_driver->driver_name) */
	ret = uart_register_driver(&sunplus_uart_driver);
	if (ret < 0)
		return ret;

	ret = platform_driver_register(&sunplus_uart_platform_driver);
	if (ret != 0) {
		uart_unregister_driver(&sunplus_uart_driver);
		return ret;
	}

	return 0;
}
module_init(sunplus_uart_init);

module_param(uart0_as_console, uint, 0444);

#ifdef CONFIG_SERIAL_EARLYCON
static void sunplus_uart_putc(struct uart_port *port, int c)
{
	unsigned int status;

	for (;;) {
		status = readl(port->membase + SP_UART_LSR);
		if ((status & SP_UART_LSR_TXE) == SP_UART_LSR_TXE)
			break;
		cpu_relax();
	}
	writel(c, port->membase + SP_UART_DATA);
}

static void sunplus_uart_early_write(struct console *con,
			      const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, sunplus_uart_putc);
}

int __init sunplus_uart_early_setup(struct earlycon_device *device,
					 const char *opt)
{
	if (!(device->port.membase || device->port.iobase))
		return -ENODEV;

	device->con->write = sunplus_uart_early_write;
	return 0;
}
OF_EARLYCON_DECLARE(sunplus_uart, "sunplus,sp7021-uart", sunplus_uart_early_setup);
#endif

MODULE_DESCRIPTION("Sunplus UART driver");
MODULE_AUTHOR("Hammer Hsieh <hammer.hsieh@sunplus.com>");
MODULE_LICENSE("GPL v2");
