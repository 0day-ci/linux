// SPDX-License-Identifier: GPL-2.0
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
#include <linux/sysrq.h>
#include <linux/serial_core.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <soc/sunplus/sp_uart.h>
#include <dt-bindings/clock/sp-sp7021.h>

/*
 * UART_NR : Nunmer of UART channel, serial0, ...
 * UART_DMARX_NR : Nunmer of UART RX DMA, serial10, ...
 * UART_DMATX_NR : Nunmer of UART TX DMA, serial20, ...
 *
 */
#define UART_NR			5
#define UART_DMARX_NR	2
#define UART_DMATX_NR	2

/*
 * IS_UARTDMARX_ID(X) : if true , do rxdma init in probe
 * IS_UARTDMATX_ID(X) : if true , do txdma init in probe
 *
 */
#define ID_BASE_DMARX	10
#define ID_BASE_DMATX	20

#define IS_UARTDMARX_ID(X) \
	(((X) >= (ID_BASE_DMARX)) && ((X) < (ID_BASE_DMARX + UART_DMARX_NR)))
#define IS_UARTDMATX_ID(X) \
	(((X) >= (ID_BASE_DMATX)) && ((X) < (ID_BASE_DMATX + UART_DMATX_NR)))

struct sunplus_uart_port {
	char name[16];
	struct uart_port uport;
	struct sunplus_dma_info *uartdma_rx;
	struct sunplus_dma_info *uartdma_tx;
	struct clk *clk;
	struct reset_control *rstc;
	unsigned int pllsys_rate;
	struct gpio_desc *rts_gpio;
	struct hrtimer rts_check_tx_empty;
	struct hrtimer rts_delay_before_send;
	struct hrtimer rts_delay_after_send;
};
struct sunplus_uart_port sunplus_uart_ports[UART_NR];
static struct uart_driver sunplus_uart_driver;

struct sunplus_dma_info {
	void __iomem *membase;
	void __iomem *gdma_membase;
	unsigned long addr_phy;
	int irq;
	int which_uart;
	struct sunplus_uart_port *binding_port;
	void *buf_va;
	dma_addr_t dma_handle;
};
static struct sunplus_dma_info
	sunplus_uartdma[UART_DMARX_NR + UART_DMATX_NR];

static inline struct sunplus_uart_port *to_sunplus_uart(struct uart_port *uport)
{
	return container_of(uport, struct sunplus_uart_port, uport);
}

static inline void sp_uart_put_char(struct uart_port *port, unsigned int ch)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);
	struct sunplus_dma_info *tx_dma = sp_port->uartdma_tx;
	u32 addr_sw, addr_start;
	u32 offset_sw;
	u8 *byte_ptr;

	if (!tx_dma) {
		writel(ch, port->membase + SP_UART_DATA);
	} else {
		addr_sw = readl(tx_dma->membase + SP_UART_TXDMA_WR_ADR);
		addr_start = readl(tx_dma->membase + SP_UART_TXDMA_START_ADDR);
		offset_sw = addr_sw - addr_start;
		byte_ptr = (u8 *)(tx_dma->buf_va + offset_sw);
		*byte_ptr = (u8)(ch);
		if (offset_sw == (PAGE_SIZE - 1))
			writel((u32)(tx_dma->dma_handle),
				tx_dma->membase + SP_UART_TXDMA_WR_ADR);
		else
			writel((addr_sw + 1),
				tx_dma->membase + SP_UART_TXDMA_WR_ADR);
	}
}

static inline u32 sp_uart_line_status_tx_buf_not_full(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);
	struct sunplus_dma_info *tx_dma = sp_port->uartdma_tx;
	u32 addr_sw, addr_hw;

	if (tx_dma) {
		if (readl(tx_dma->membase + SP_UART_TXDMA_STATUS) & TXDMA_BUF_FULL)
			return 0;

		addr_sw = readl(tx_dma->membase + SP_UART_TXDMA_WR_ADR);
		addr_hw = readl(tx_dma->membase + SP_UART_TXDMA_RD_ADR);
		if (addr_sw >= addr_hw)
			return (PAGE_SIZE - (addr_sw - addr_hw));
		else
			return (addr_hw - addr_sw);

	} else {
		if (readl(port->membase + SP_UART_LSR) & SP_UART_LSR_TX)
			return 1;
		else
			return 0;
	}
}

/*
 * Note:
 * When (uart0_as_console == 0), please make sure:
 *     There is no "console=ttyS0,115200", "earlyprintk", ... in kernel command line.
 *     In /etc/inittab, there is no something like "ttyS0::respawn:/bin/sh"
 */
unsigned int uart0_as_console = ~0;
unsigned int uart_enable_status = ~0;

static struct sunplus_dma_info *sunplus_uartdma_rx_binding(int id)
{
	int i;

	for (i = 0; i < UART_DMARX_NR; i++) {
		if ((sunplus_uartdma[i].which_uart == id) && (sunplus_uartdma[i].membase)) {
			sunplus_uartdma[i].binding_port = &sunplus_uart_ports[id];
			return &sunplus_uartdma[i];
		}
	}
	return NULL;
}

static struct sunplus_dma_info *sunplus_uartdma_tx_binding(int id)
{
	int i;

	for (i = UART_DMARX_NR; i < (UART_DMARX_NR + UART_DMATX_NR); i++) {
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

static unsigned int sunplus_tx_empty(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);
	struct sunplus_dma_info *tx_dma = sp_port->uartdma_tx;

	if (tx_dma) {
		if (readl(tx_dma->membase + SP_UART_TXDMA_STATUS) & TXDMA_BUF_EMPTY)
			return TIOCSER_TEMT;
		else
			return 0;
	} else {
		return ((readl(port->membase + SP_UART_LSR) & SP_UART_LSR_TXE)
			? TIOCSER_TEMT : 0);
	}
}

static void sunplus_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);
	unsigned int mcr = readl(port->membase + SP_UART_MCR);
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

	writel(mcr, port->membase + SP_UART_MCR);

	if (sp_port->uport.rs485.flags & SER_RS485_ENABLED) {
		mcr = readl(port->membase + SP_UART_MCR);
		if (mctrl & TIOCM_RTS) {
			if (((mcr & SP_UART_MCR_RTS)
				&& (sp_port->uport.rs485.flags & SER_RS485_RTS_AFTER_SEND))
				|| (sp_port->uport.rs485.flags & SER_RS485_RTS_ON_SEND)) {
				if (!IS_ERR(sp_port->rts_gpio))
					gpiod_set_value(sp_port->rts_gpio, 1);
				dev_dbg(port->dev, "%s set rts_gpio=1\n", port->name);
				if (sp_port->uport.rs485.delay_rts_before_send == 0) {
					ktime = ktime_set(0, 500000); /* 500us */
					hrtimer_start(&sp_port->rts_check_tx_empty,
						ktime, HRTIMER_MODE_REL);
				}
			}
		}
	}
}

static unsigned int sunplus_get_mctrl(struct uart_port *port)
{
	unsigned int ret, mcr;

	mcr = readl(port->membase + SP_UART_MCR);

	if (mcr & SP_UART_MCR_DTS)
		ret |= TIOCM_DTR;

	if (mcr & SP_UART_MCR_RTS)
		ret |= TIOCM_RTS;

	if (mcr & SP_UART_MCR_DCD)
		ret |= TIOCM_CAR;

	if (mcr & SP_UART_MCR_RI)
		ret |= TIOCM_RI;

	if (mcr & SP_UART_MCR_LB)
		ret |= TIOCM_LOOP;

	if (mcr & SP_UART_MCR_AC)
		ret |= TIOCM_CTS;

	return ret;
}

static void sunplus_stop_tx(struct uart_port *port)
{
	unsigned int isc;

	isc = readl(port->membase + SP_UART_ISC);
	isc &= ~SP_UART_ISC_TXM;
	writel(isc, port->membase + SP_UART_ISC);
}

static inline void __start_tx(struct uart_port *port)
{
	unsigned int isc;

	isc = readl(port->membase + SP_UART_ISC);
	isc |= SP_UART_ISC_TXM;
	writel(isc, port->membase + SP_UART_ISC);
}

static void sunplus_start_tx(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);
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
			hrtimer_start(&sp_port->rts_delay_before_send, ktime, HRTIMER_MODE_REL);
			return;
		}
	}
	__start_tx(port);
}

static void sunplus_stop_rx(struct uart_port *port)
{
	unsigned int isc;

	isc = readl(port->membase + SP_UART_ISC);
	isc &= ~SP_UART_ISC_RXM;
	writel(isc, port->membase + SP_UART_ISC);
}

static void sunplus_enable_ms(struct uart_port *port)
{
	/* Do nothing */
}

static void sunplus_break_ctl(struct uart_port *port, int ctl)
{
	unsigned long flags;
	unsigned int lcr;

	spin_lock_irqsave(&port->lock, flags);

	lcr = readl(port->membase + SP_UART_LCR);

	if (ctl != 0)
		lcr |= SP_UART_LCR_BC;	/* start break */
	else
		lcr &= ~SP_UART_LCR_BC;	/* stop break */

	writel(lcr, port->membase + SP_UART_LCR);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void transmit_chars(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);
	struct sunplus_dma_info *tx_dma = sp_port->uartdma_tx;
	u32 tx_buf_available;
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
		sunplus_stop_tx(port);
		return;
	}

	if (tx_dma) {
		addr_sw = readl(tx_dma->membase + SP_UART_TXDMA_WR_ADR);
		addr_start = readl(tx_dma->membase + SP_UART_TXDMA_START_ADDR);
		offset_sw = addr_sw - addr_start;
		byte_ptr = (u8 *)(tx_dma->buf_va + offset_sw);
		tx_buf_available = sp_uart_line_status_tx_buf_not_full(port);
		while (tx_buf_available) {
			*byte_ptr = xmit->buf[xmit->tail];
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
			port->icount.tx++;

			byte_ptr++;
			addr_sw++;
			offset_sw++;
			tx_buf_available--;
			if (offset_sw == PAGE_SIZE) {
				offset_sw = 0;
				addr_sw = (u32)(tx_dma->dma_handle);
				byte_ptr = (u8 *)(tx_dma->buf_va);
			}

			if (uart_circ_empty(xmit))
				break;
		}
		writel(addr_sw, tx_dma->membase + SP_UART_TXDMA_WR_ADR);
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
		sunplus_stop_tx(port);
}

/* called by ISR */
static void receive_chars(struct uart_port *port)
{
	struct tty_struct *tty = port->state->port.tty;
	unsigned int lsr = readl(port->membase + SP_UART_LSR);
	unsigned int ch, flag;

	do {
		ch = readl(port->membase + SP_UART_DATA);

		flag = TTY_NORMAL;
		port->icount.rx++;

		if (unlikely(lsr & SP_UART_LSR_BRK_ERROR_BITS)) {
			if (port->cons == NULL)
				dev_err(port->dev, "UART%d, SP_UART_LSR_BRK_ERROR_BITS, lsr = 0x%08X\n",
					port->line, lsr);

			if (lsr & SP_UART_LSR_BC) {
				lsr &= ~(SP_UART_LSR_FE | SP_UART_LSR_PE);
				port->icount.brk++;
				if (uart_handle_break(port))
					goto ignore_char;
			} else if (lsr & SP_UART_LSR_PE) {
				if (port->cons == NULL)
					dev_err(port->dev, "UART%d, SP_UART_LSR_PE\n", port->line);
				port->icount.parity++;
			} else if (lsr & SP_UART_LSR_FE) {
				if (port->cons == NULL)
					dev_err(port->dev, "UART%d, SP_UART_LSR_FE\n", port->line);
				port->icount.frame++;
			}
			if (lsr & SP_UART_LSR_OE) {
				if (port->cons == NULL)
					dev_err(port->dev, "UART%d, SP_UART_LSR_OE\n", port->line);
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
		lsr = readl(port->membase + SP_UART_LSR);
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

	if (readl(port->membase + SP_UART_ISC) & SP_UART_ISC_RX)
		receive_chars(port);

	if (readl(port->membase + SP_UART_ISC) & SP_UART_ISC_TX)
		transmit_chars(port);

	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

/* called by ISR */
static void receive_chars_rxdma(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);
	struct sunplus_dma_info *rx_dma = sp_port->uartdma_rx;
	struct tty_struct *tty = port->state->port.tty;
	u32 offset_sw, offset_hw, rx_size, dma_start;
	u8 *sw_ptr, *buf_end_ptr, *u8_ptr;
	u32 icount_rx;
	u32 tmp_u32, value;
	u8 tmp_buf[32];
	int i;

	dma_start = readl(rx_dma->membase + SP_UART_RXDMA_START_ADDR);
	offset_sw = readl(rx_dma->membase + SP_UART_RXDMA_RD_ADR) - dma_start;
	offset_hw = readl(rx_dma->membase + SP_UART_RXDMA_WR_ADR) - dma_start;

	if (offset_hw >= offset_sw)
		rx_size = offset_hw - offset_sw;
	else
		rx_size = offset_hw + PAGE_SIZE - offset_sw;

	sw_ptr = (u8 *)(rx_dma->buf_va + offset_sw);
	buf_end_ptr = (u8 *)(rx_dma->buf_va + PAGE_SIZE);

	/*
	 * Retrieve all data in ISR.
	 * The max. received size is (buffer_size - threshold_size)
	 * = (rxdma_length_thr[31:16] - rxdma_length_thr[15:0])
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
			sw_ptr = (u8 *)(rx_dma->buf_va);
	}

	tmp_u32 = readl(rx_dma->membase + SP_UART_RXDMA_RD_ADR) + rx_size;

	if (tmp_u32 <= readl(rx_dma->membase + SP_UART_RXDMA_END_ADDR))
		writel(tmp_u32, rx_dma->membase + SP_UART_RXDMA_RD_ADR);
	else
		writel((tmp_u32 - PAGE_SIZE), rx_dma->membase + SP_UART_RXDMA_RD_ADR);

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tty->port);
	spin_lock(&port->lock);

	value = readl(rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);
	value |= DMA_INT;
	writel(value, rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);

	value = readl(rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);
	value |= DMA_GO;
	writel(value, rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);

}

static irqreturn_t sunplus_uart_rxdma_irq(int irq, void *args)
{
	struct uart_port *port = (struct uart_port *)args;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	receive_chars_rxdma(port);

	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

static int sunplus_startup(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);
	struct sunplus_dma_info *rx_dma, *tx_dma;
	u32 timeout, interrupt_en;
	unsigned int ch, value;
	int ret;

	if (sp_port->uport.rs485.flags & SER_RS485_ENABLED)
		hrtimer_cancel(&sp_port->rts_delay_after_send);

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

	rx_dma = sp_port->uartdma_rx;

	if (rx_dma) {
		if (rx_dma->buf_va == NULL) {
			/* Drop data in Rx FIFO (PIO mode) */
			while (readl(port->membase + SP_UART_LSR) & SP_UART_LSR_RX)
				ch = readl(port->membase + SP_UART_DATA);

			rx_dma->buf_va =
				dma_alloc_coherent(port->dev, PAGE_SIZE,
				&(rx_dma->dma_handle), GFP_KERNEL);
			if (rx_dma->buf_va == NULL) {
				dev_err(port->dev, "Can't allocation buffer for %s\n",
					sp_port->name);
				ret = -ENOMEM;
				goto error_01;
			}

			writel((u32)(rx_dma->dma_handle),
				rx_dma->membase + SP_UART_RXDMA_START_ADDR);
			writel((u32)(rx_dma->dma_handle),
				rx_dma->membase + SP_UART_RXDMA_RD_ADR);

			/* Switch clock source when setting baud rate */
			timeout = (sp_port->pllsys_rate / 2) / 1000;	/* 1 msec */
			writel(timeout, rx_dma->membase + SP_UART_RXDMA_TIMEOUT_SET);

			/*
			 * When there are only rxdma_length_thr[15:0] bytes
			 * of free buffer => Trigger interrupt
			 */
			writel((PAGE_SIZE << 16) | (PAGE_SIZE - (PAGE_SIZE >> 3)),
			    rx_dma->membase + SP_UART_RXDMA_LENGTH_THR);

			value = readl(rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);
			value &= ~DMA_SEL_UARTX_MASK;
			value |= (DMA_INIT | (rx_dma->which_uart << DMA_SEL_UARTX_SHIFT));
			writel(value, rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);

			value = readl(rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);
			value &= ~DMA_INIT;
			writel(value, rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);

			value = readl(rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);
			value |= (DMA_SW_RST_B | DMA_AUTO_ENABLE | DMA_TIMEOUT_INT_EN | DMA_ENABLE);
			writel(value, rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);

			value = readl(rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);
			value |= DMA_GO;
			writel(value, rx_dma->membase + SP_UART_RXDMA_ENABLE_SEL);
		}

		ret = request_irq(rx_dma->irq, sunplus_uart_rxdma_irq, 0, "UARTDMA_RX", port);
		if (ret) {
			dma_free_coherent(port->dev, PAGE_SIZE,
				rx_dma->buf_va, rx_dma->dma_handle);
			goto error_00;
		}
	}

	tx_dma = sp_port->uartdma_tx;
	if (tx_dma) {
		if (tx_dma->buf_va == NULL) {
			tx_dma->buf_va =
				dma_alloc_coherent(port->dev, PAGE_SIZE,
				&(tx_dma->dma_handle), GFP_KERNEL);
			if (tx_dma->buf_va == NULL) {
				dev_err(port->dev, "Can't allocation buffer for %s\n",
					sp_port->name);
				ret = -ENOMEM;
				goto error_01;
			}

			/*
			 * set 1ms , set wr_adr , set start_addr/end_addr ,
			 * set bind to uart# , set int enable ,
			 * set txdma enable (Use ring buffer for UART's Tx)
			 */
			writel((sp_port->pllsys_rate / 1000),
				tx_dma->membase + SP_UART_TXDMA_TMR_UNIT);
			writel((u32)(tx_dma->dma_handle), tx_dma->membase + SP_UART_TXDMA_WR_ADR);
			writel((u32)(tx_dma->dma_handle),
				tx_dma->membase + SP_UART_TXDMA_START_ADDR);
			writel(((u32)(tx_dma->dma_handle) + PAGE_SIZE - 1),
				tx_dma->membase + SP_UART_TXDMA_END_ADDR);
			writel(tx_dma->which_uart, tx_dma->membase + SP_UART_TXDMA_SEL);
			writel(0x41, tx_dma->gdma_membase + SP_UART_TXGDMA_INT_EN);
			writel(0x00000005, tx_dma->membase + SP_UART_TXDMA_ENABLE);
		}
	}

	/*
	 * don't need to use spin_lock_irqsave() because interrupts
	 * are globally disabled
	 */
	spin_lock_irq(&port->lock);

	/* SP_UART_ISC_TXM is enabled in .start_tx() */
	interrupt_en = 0;
	if (rx_dma == NULL)
		interrupt_en |= SP_UART_ISC_RXM;

	writel(interrupt_en, port->membase + SP_UART_ISC);

	spin_unlock_irq(&port->lock);

#ifdef CONFIG_PM_RUNTIME_UART
	if (port->line > 0)
		pm_runtime_put(port->dev);

	return 0;

out:
	if (port->line > 0) {
		pm_runtime_mark_last_busy(port->dev);
		pm_runtime_put_autosuspend(port->dev);
	}
#endif
	return 0;

error_01:
	if (rx_dma) {
		dma_free_coherent(port->dev, PAGE_SIZE,
			rx_dma->buf_va, rx_dma->dma_handle);
		free_irq(rx_dma->irq, port);
	}
error_00:
	free_irq(port->irq, port);
	return ret;
}

static void sunplus_shutdown(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);
	struct sunplus_dma_info *rx_dma;
	unsigned int mcr, value;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	writel(0, port->membase + SP_UART_ISC);
	spin_unlock_irqrestore(&port->lock, flags);

	free_irq(port->irq, port);

	rx_dma = sp_port->uartdma_rx;
	if (rx_dma) {
		/* Drop whatever is still in buffer */
		value = readl(rx_dma->membase + SP_UART_RXDMA_WR_ADR);
		writel(value, rx_dma->membase + SP_UART_RXDMA_RD_ADR);

		free_irq(rx_dma->irq, port);
	}

	/* Disable flow control of Tx, so that queued data can be sent out
	 * There is no way for s/w to let h/w abort in the middle of
	 * transaction.
	 * Don't reset module except it's in idle state. Otherwise, it might
	 * cause bus to hang.
	 */
	mcr = readl(port->membase + SP_UART_MCR);
	mcr &= ~SP_UART_MCR_AC;
	writel(mcr, port->membase + SP_UART_MCR);

}

static void sunplus_set_termios(struct uart_port *port,
	struct ktermios *termios, struct ktermios *oldtermios)
{
	u32 clk, ext, div, div_l, div_h, baud;
	u32 lcr, mcr;
	unsigned long flags;

	clk = port->uartclk;

	baud = uart_get_baud_rate(port, termios, oldtermios, 0, port->uartclk / 16);

	/*
	 * Send all data in Tx FIFO before changing clock source,
	 * it should be UART0 only
	 */
	while (!(readl(port->membase + SP_UART_LSR) & SP_UART_LSR_TXE))
		;

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
		writel(0, port->membase + SP_UART_RX_RESIDUE);
	}

	mcr = readl(port->membase + SP_UART_MCR);

	if (termios->c_cflag & CRTSCTS)
		mcr |= (SP_UART_MCR_AC | SP_UART_MCR_AR);
	else
		mcr &= ~(SP_UART_MCR_AC | SP_UART_MCR_AR);

	writel(mcr, port->membase + SP_UART_MCR);

	/* do not set these in emulation */
	writel(div_h, port->membase + SP_UART_DIV_H);
	writel(div_l, port->membase + SP_UART_DIV_L);
	writel(lcr, port->membase + SP_UART_LCR);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void sunplus_set_ldisc(struct uart_port *port,
				       struct ktermios *termios)
{
	int new = termios->c_line;

	if (new == N_PPS) {
		port->flags |= UPF_HARDPPS_CD;
		sunplus_enable_ms(port);
	} else {
		port->flags &= ~UPF_HARDPPS_CD;
	}
}

static const char *sunplus_type(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);

	return sp_port->name;
}

static void sunplus_release_port(struct uart_port *port)
{
}

static int sunplus_request_port(struct uart_port *port)
{
	return 0;
}

static void sunplus_config_port(struct uart_port *port, int type)
{
	if (type & UART_CONFIG_TYPE) {
		port->type = PORT_SUNPLUS;
		sunplus_request_port(port);
	}
}

static int sunplus_verify_port(struct uart_port *port, struct serial_struct *serial)
{
	return -EINVAL;
}

#ifdef CONFIG_CONSOLE_POLL
static int sunplus_poll_init(struct uart_port *port)
{
	return 0;
}

static void sunplus_poll_put_char(struct uart_port *port, unsigned char data)
{
	wait_for_xmitr(port);
	sp_uart_put_char(port, data);
}

static int sunplus_poll_get_char(struct uart_port *port)
{
	unsigned int lsr;

	do {
		lsr = readl(port->membase + SP_UART_LSR);
	} while (!(lsr & SP_UART_LSR_RX));

	return readl(port->membase + SP_UART_DATA);
}
#endif /* CONFIG_CONSOLE_POLL */

static enum hrtimer_restart sp_rts_check_tx_empty(struct hrtimer *t)
{
	struct sunplus_uart_port *sp_port;
	unsigned int mcr, lsr;
	unsigned char val;
	ktime_t ktime;
	unsigned long nsec;

	sp_port = container_of(t, struct sunplus_uart_port, rts_check_tx_empty);
	mcr = readl(sp_port->uport.membase + SP_UART_MCR);
	lsr = readl(sp_port->uport.membase + SP_UART_LSR);
	// TX is NOT empty
	if (!(lsr & SP_UART_LSR_TXE))
		return HRTIMER_RESTART;
	// TX is empty
	nsec = sp_port->uport.rs485.delay_rts_after_send * 1000000;
	// schedule RTS set
	if (nsec > 0) {
		ktime = ktime_set(0, nsec);
		hrtimer_start(&sp_port->rts_delay_after_send, ktime, HRTIMER_MODE_REL);
		return HRTIMER_NORESTART;
	}
	// immediately set RTS
	val = (sp_port->uport.rs485.flags & SER_RS485_RTS_AFTER_SEND ? 1 : 0);
	if (sp_port->uport.rs485.flags & SER_RS485_RTS_ON_SEND)
		mcr |= SP_UART_MCR_RTS;
	else
		mcr &= ~SP_UART_MCR_RTS;
	if (sp_port->uport.rs485.flags & SER_RS485_ENABLED && !IS_ERR(sp_port->rts_gpio)) {
		gpiod_set_value(sp_port->rts_gpio, val);
		dev_dbg(sp_port->uport.dev, "%s set rts_gpio=%d\n",
			sp_port->uport.name, val);
	} else
		writel(mcr, sp_port->uport.membase + SP_UART_MCR);

	return HRTIMER_NORESTART;
}

static enum hrtimer_restart sp_rts_delay_before_send(struct hrtimer *t)
{
	struct sunplus_uart_port *sp_port;
	ktime_t ktime;

	sp_port = container_of(t, struct sunplus_uart_port, rts_delay_before_send);
	ktime = ktime_set(0, 500000); //500us
	hrtimer_start(&sp_port->rts_check_tx_empty, ktime, HRTIMER_MODE_REL);
	__start_tx(&sp_port->uport);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart sp_rts_delay_after_send(struct hrtimer *t)
{
	struct sunplus_uart_port *sp_port;
	unsigned char val;
	unsigned int mcr;

	sp_port = container_of(t, struct sunplus_uart_port, rts_delay_after_send);
	mcr = readl(sp_port->uport.membase + SP_UART_MCR);

	val = (sp_port->uport.rs485.flags & SER_RS485_RTS_AFTER_SEND ? 1 : 0);
	if (sp_port->uport.rs485.flags & SER_RS485_RTS_ON_SEND)
		mcr |= SP_UART_MCR_RTS;
	else
		mcr &= ~SP_UART_MCR_RTS;
	if (sp_port->uport.rs485.flags & SER_RS485_ENABLED && !IS_ERR(sp_port->rts_gpio)) {
		gpiod_set_value(sp_port->rts_gpio, val);
		dev_dbg(sp_port->uport.dev, "%s set rts_gpio=%d\n",
			sp_port->uport.name, val);
	} else
		writel(mcr, sp_port->uport.membase + SP_UART_MCR);

	return HRTIMER_NORESTART;
}

static int sunplus_uart_rs485_on(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);

	if (IS_ERR(sp_port->rts_gpio)) {
		dev_err(port->dev, "%s No valid rts_gpio, disabling 485\n",
			port->name);
		port->rs485.flags &= ~SER_RS485_ENABLED;
		return (-EINVAL);
	}

	if (!sp_port->rts_check_tx_empty.function) {
		hrtimer_init(&(sp_port->rts_check_tx_empty), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		sp_port->rts_check_tx_empty.function = sp_rts_check_tx_empty;
	}
	if (!sp_port->rts_delay_before_send.function) {
		hrtimer_init(&(sp_port->rts_delay_before_send), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		sp_port->rts_delay_before_send.function = sp_rts_delay_before_send;
	}
	if (!sp_port->rts_delay_after_send.function) {
		hrtimer_init(&(sp_port->rts_delay_after_send), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		sp_port->rts_delay_after_send.function = sp_rts_delay_after_send;
	}
	return (0);
}

static int sunplus_uart_rs485_off(struct uart_port *port)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);

	port->rs485.flags &= ~SER_RS485_ENABLED;
	if (sp_port->rts_check_tx_empty.function) {
		hrtimer_cancel(&sp_port->rts_check_tx_empty);
		sp_port->rts_check_tx_empty.function = NULL;
	}
	if (sp_port->rts_delay_before_send.function) {
		hrtimer_cancel(&sp_port->rts_delay_before_send);
		sp_port->rts_delay_before_send.function = NULL;
	}
	if (sp_port->rts_delay_after_send.function) {
		hrtimer_cancel(&sp_port->rts_delay_after_send);
		sp_port->rts_delay_after_send.function = NULL;
	}
	return (0);
}

static int sunplus_uart_config_rs485(struct uart_port *port,
	struct serial_rs485 *rs485)
{
	struct sunplus_uart_port *sp_port = to_sunplus_uart(port);
	int val;

	if (IS_ERR(sp_port->rts_gpio)) {
		dev_err(port->dev, "%s No valid rts_gpio - skipping rs485\n",
			port->name);
		port->rs485.flags &= ~SER_RS485_ENABLED;
		return (-EINVAL);
	}

#ifdef CONFIG_PM_RUNTIME_UART
	pm_runtime_get_sync(sp_port->dev);
#endif

	rs485->delay_rts_before_send =
		min(rs485->delay_rts_before_send, 10000U);
	rs485->delay_rts_after_send  =
		min(rs485->delay_rts_after_send, 10000U);

	if (rs485->flags & SER_RS485_ENABLED) {
		sunplus_uart_rs485_on(port);
		val = (rs485->flags & SER_RS485_RTS_AFTER_SEND ? 1 : 0);
		gpiod_set_value(sp_port->rts_gpio, val);
	} else
		sunplus_uart_rs485_off(port);

	port->rs485 = *rs485;

#ifdef CONFIG_PM_RUNTIME_UART
	pm_runtime_mark_last_busy(sp_port->dev);
	pm_runtime_put_autosuspend(sp_port->dev);
#endif
	return (0);
}

static const struct uart_ops sunplus_uart_ops = {
	.tx_empty		= sunplus_tx_empty,
	.set_mctrl		= sunplus_set_mctrl,
	.get_mctrl		= sunplus_get_mctrl,
	.stop_tx		= sunplus_stop_tx,
	.start_tx		= sunplus_start_tx,
	.stop_rx		= sunplus_stop_rx,
	.enable_ms		= sunplus_enable_ms,
	.break_ctl		= sunplus_break_ctl,
	.startup		= sunplus_startup,
	.shutdown		= sunplus_shutdown,
	.set_termios	= sunplus_set_termios,
	.set_ldisc		= sunplus_set_ldisc,
	.type			= sunplus_type,
	.release_port	= sunplus_release_port,
	.request_port	= sunplus_request_port,
	.config_port	= sunplus_config_port,
	.verify_port	= sunplus_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init		= sunplus_poll_init,
	.poll_put_char	= sunplus_poll_put_char,
	.poll_get_char	= sunplus_poll_get_char,
#endif /* CONFIG_CONSOLE_POLL */
};

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

	local_irq_save(flags);

	if (sunplus_uart_ports[co->index].uport.sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&sunplus_uart_ports[co->index].uport.lock);
	else
		spin_lock(&sunplus_uart_ports[co->index].uport.lock);

	uart_console_write(&sunplus_uart_ports[co->index].uport,
		s, count, sunplus_uart_console_putchar);

	if (locked)
		spin_unlock(&sunplus_uart_ports[co->index].uport.lock);

	local_irq_restore(flags);
}

static int __init sunplus_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if ((co->index >= UART_NR) || (co->index < 0))
		return -EINVAL;

	port = &sunplus_uart_ports[co->index].uport;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct console sunplus_console = {
	.name		= "ttyS",
	.write		= sunplus_console_write,
	.device		= uart_console_device,
	.setup		= sunplus_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &sunplus_uart_driver
};
#endif

static struct uart_driver sunplus_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "Sunplus_UART",
	.dev_name	= "ttyS",
	.major		= TTY_MAJOR,
	.minor		= 64,
	.nr			= UART_NR,
#ifdef CONFIG_SERIAL_SUNPLUS_CONSOLE
	.cons		= &sunplus_console
#endif
};

static int sunplus_uart_probe(struct platform_device *pdev)
{
	struct resource *res_mem;
	struct uart_port *port;
	struct clk *clk, *pllsys;
	unsigned int pllsys_rate;
	int ret, irq;
	int idx_offset, idx;
	int idx_which_uart;
	char peri_name[16];

	if (pdev->dev.of_node) {
		pdev->id = of_alias_get_id(pdev->dev.of_node, "serial");
		if (pdev->id < 0)
			pdev->id = of_alias_get_id(pdev->dev.of_node, "uart");
	}

	idx_offset = -1;

	if (IS_UARTDMARX_ID(pdev->id))
		idx_offset = 0;
	else if (IS_UARTDMATX_ID(pdev->id))
		idx_offset = UART_DMARX_NR;

	/* init txdma or rxdma */
	if (idx_offset >= 0) {
		clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		ret = clk_prepare_enable(clk);
		if (ret)
			return ret;

		if (idx_offset == 0)
			idx = idx_offset + pdev->id - ID_BASE_DMARX;
		else
			idx = idx_offset + pdev->id - ID_BASE_DMATX;

		res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res_mem)
			return -ENODEV;

		sprintf(peri_name, "PERI%d", (idx & 0x01));

		clk = devm_clk_get(&pdev->dev, peri_name);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		ret = clk_prepare_enable(clk);
		if (ret)
			return ret;

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
		}

		if (of_property_read_u32(pdev->dev.of_node, "which-uart", &idx_which_uart) != 0) {
			dev_err(&pdev->dev, "\"which-uart\" is not assigned.");
			return -EINVAL;
		}

		if (idx_which_uart >= UART_NR) {
			dev_err(&pdev->dev, "\"which-uart\" is not valid.");
			return -EINVAL;
		}

		sunplus_uartdma[idx].which_uart = idx_which_uart;

		return 0;
	} else if (pdev->id < 0 || pdev->id >= UART_NR)
		return -EINVAL;

	/* init uart */
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
	sunplus_uart_ports[pdev->id].rts_check_tx_empty.function = NULL;
	sunplus_uart_ports[pdev->id].rts_delay_before_send.function = NULL;
	sunplus_uart_ports[pdev->id].rts_delay_after_send.function = NULL;
	if (port->rs485.flags & SER_RS485_ENABLED)
		sunplus_uart_rs485_on(port);

	sunplus_uart_ports[pdev->id].clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sunplus_uart_ports[pdev->id].clk))
		return PTR_ERR(sunplus_uart_ports[pdev->id].clk);

	ret = clk_prepare_enable(sunplus_uart_ports[pdev->id].clk);
	if (ret)
		return ret;

	sunplus_uart_ports[pdev->id].rstc =
		devm_reset_control_get(&pdev->dev, NULL);

	if (IS_ERR(sunplus_uart_ports[pdev->id].rstc))
		return PTR_ERR(sunplus_uart_ports[pdev->id].rstc);

	ret = reset_control_deassert(sunplus_uart_ports[pdev->id].rstc);
	if (ret)
		return ret;

	clk = sunplus_uart_ports[pdev->id].clk;
	if (IS_ERR(clk))
		port->uartclk = XTAL;
	else
		port->uartclk = clk_get_rate(clk);

	pllsys = clk_get_sys("pllsys", NULL);
	pllsys_rate = clk_get_rate(pllsys);

	sunplus_uart_ports[pdev->id].pllsys_rate = pllsys_rate;

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

	port->private_data = container_of(&sunplus_uart_ports[pdev->id].uport,
		struct sunplus_uart_port, uport);

	sprintf(sunplus_uart_ports[pdev->id].name, "sp_uart%d", pdev->id);

	sunplus_uart_ports[pdev->id].uartdma_rx =
		sunplus_uartdma_rx_binding(pdev->id);

	if (sunplus_uart_ports[pdev->id].uartdma_rx)
		dev_info(port->dev, "%s's Rx is in DMA mode.\n",
			sunplus_uart_ports[pdev->id].name);
	else
		dev_info(port->dev, "%s's Rx is in PIO mode.\n",
			sunplus_uart_ports[pdev->id].name);

	sunplus_uart_ports[pdev->id].uartdma_tx =
		sunplus_uartdma_tx_binding(pdev->id);

	if (sunplus_uart_ports[pdev->id].uartdma_tx)
		dev_info(port->dev, "%s's Tx is in DMA mode.\n",
			sunplus_uart_ports[pdev->id].name);
	else
		dev_info(port->dev, "%s's Tx is in PIO mode.\n",
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

static int sunplus_uart_remove(struct platform_device *pdev)
{
#ifdef CONFIG_PM_RUNTIME_UART
	if (pdev->id != 0) {
		pm_runtime_disable(&pdev->dev);
		pm_runtime_set_suspended(&pdev->dev);
	}
#endif
	uart_remove_one_port(&sunplus_uart_driver,
		&sunplus_uart_ports[pdev->id].uport);

	if (pdev->id < UART_NR) {
		clk_disable_unprepare(sunplus_uart_ports[pdev->id].clk);
		reset_control_assert(sunplus_uart_ports[pdev->id].rstc);
	}

	return 0;
}

static int sunplus_uart_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	/* Don't suspend uart0 for cmd line usage */
	if ((pdev->id < UART_NR) && (pdev->id > 0))
		reset_control_assert(sunplus_uart_ports[pdev->id].rstc);

	return 0;
}

static int sunplus_uart_resume(struct platform_device *pdev)
{
	if (pdev->id < UART_NR) {
		clk_prepare_enable(sunplus_uart_ports[pdev->id].clk);
		reset_control_deassert(sunplus_uart_ports[pdev->id].rstc);
	}
	return 0;
}

static const struct of_device_id sp_uart_of_match[] = {
	{ .compatible = "sunplus,sp7021-uart" },
	{ .compatible = "sunplus,sp7021-uart-rxdma" },
	{ .compatible = "sunplus,sp7021-uart-txdma" },
	{}
};
MODULE_DEVICE_TABLE(of, sp_uart_of_match);

#ifdef CONFIG_PM_RUNTIME_UART
static int sunplus_uart_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	/* Don't suspend uart0 for cmd line usage */
	if ((pdev->id < UART_NR) && (pdev->id > 0))
		reset_control_assert(sunplus_uart_ports[pdev->id].rstc);

	return 0;
}

static int sunplus_uart_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	if (pdev->id < UART_NR) {
		clk_prepare_enable(sunplus_uart_ports[pdev->id].clk);
		reset_control_deassert(sunplus_uart_ports[pdev->id].rstc);
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
	.probe		= sunplus_uart_probe,
	.remove		= sunplus_uart_remove,
	.suspend	= sunplus_uart_suspend,
	.resume		= sunplus_uart_resume,
	.driver = {
		.name	= "sunplus-uart",
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
