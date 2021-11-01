// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sunplus SPI controller driver
 *
 * Author: Sunplus Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
//#define DEBUG
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
//#include <dt-bindings/pinctrl/sp7021.h>

#define SLAVE_INT_IN

//#define PM_RUNTIME_SPI

/* ---------------------------------------------------------------------------------------------- */

//#define SPI_FUNC_DEBUG
//#define SPI_DBG_INFO
#define SPI_DBG_ERR

#ifdef SPI_FUNC_DEBUG
#define FUNC_DBG(fmt, args ...) pr_info("[SPI] dbg (%d): "  fmt "\n", __LINE__, ## args)
#else
#define FUNC_DBG(fmt, args ...)
#endif

#ifdef SPI_DBG_INFO
#define DBG_INF(fmt, args ...) pr_info("[SPI] inf (%d): "  fmt "\n", __LINE__, ## args)
#else
#define DBG_INF(fmt, args ...)
#endif

#ifdef SPI_DBG_ERR
#define DBG_ERR(fmt, args ...) pr_info("[SPI] err (%d): "  fmt "\n", __LINE__, ## args)
#else
#define DBG_ERR(fmt, args ...)
#endif
/* ---------------------------------------------------------------------------------------------- */

#define SPI_FULL_DUPLEX

#define MAS_REG_NAME "spi_master"
#define SLA_REG_NAME "spi_slave"

#define DMA_IRQ_NAME "dma_w_intr"
#define MAS_IRQ_NAME "mas_risc_intr"

#define SLA_IRQ_NAME "slave_risc_intr"

#define SPI_TRANS_DATA_CNT (4)
#define SPI_TRANS_DATA_SIZE (255)
#define SPI_MSG_DATA_SIZE (SPI_TRANS_DATA_SIZE * SPI_TRANS_DATA_CNT)


#define CLEAR_MASTER_INT (1<<6)
#define MASTER_INT (1<<7)

#define DMA_READ (0xd)
#define SLA_SW_RST (1<<1)


#define DMA_WRITE (0x4d)
#define SPI_START (1<<0)
#define SPI_BUSY (1<<0)
#define CLEAR_DMA_W_INT (1<<7)
#define DMA_W_INT (1<<8)
#define CLEAR_ADDR_BIT (~(0x180))
#define ADDR_BIT(x) (x<<7)
#define DMA_DATA_RDY (1<<0)
#define PENTAGRAM_SPI_SLAVE_SET (0x2c)



#define TOTAL_LENGTH(x) (x<<24)
#define TX_LENGTH(x) (x<<16)
#define GET_LEN(x)     ((x>>24)&0xFF)
#define GET_TX_LEN(x)  ((x>>16)&0xFF)
#define GET_RX_CNT(x)  ((x>>12)&0x0F)
#define GET_TX_CNT(x)  ((x>>8)&0x0F)



#define FINISH_FLAG (1<<6)
#define FINISH_FLAG_MASK (1<<15)
#define RX_FULL_FLAG (1<<5)
#define RX_FULL_FLAG_MASK (1<<14)
#define RX_EMP_FLAG (1<<4)
#define RX_EMP_FLAG_MASK (1<<13)
#define TX_FULL_FLAG (1<<3)
#define TX_FULL_FLAG_MASK (1<<12)
#define TX_EMP_FLAG (1<<2)
#define TX_EMP_FLAG_MASK (1<<11)
#define SPI_START_FD (1<<0)
#define FD_SEL (1<<6)
#define LSB_SEL (1<<4)
#define WRITE_BYTE(x) (x<<9)
#define READ_BYTE(x) (x<<7)
#define CLK_DIVIDER(x) (x<<16)
#define INIT_SPI_MODE (~0x7F)
#define CLEAN_RW_BYTE (~0x780)
#define CLEAN_FLUG_MASK (~0xF800)


#define DELAY_ENABLE (1<<3)
#define CPOL_FD (1<<0)
#define CPHA_R (1<<1)
#define CPHA_W (1<<2)
#define CS_POR (1<<5)

#define SPI_FD_BUSY (1<<7)
#define SPI_FD_INTR (1<<7)

#define FD_SW_RST (1<<1)

#define DEG_CORE_SPI_LATCH0 (0xB<<8)
#define DEG_CORE_SPI_LATCH1 (0xC<<8)

#define LSB_SEL_MST (1<<1)

#define SPI_DEG_SEL(x) (x<<0)
#define DEG_SPI_MST_MASK (0xFF<<2)
#define DEG_SPI_MST(x) (x>>2)

#define FIFO_DATA_BITS (16*8)    // 16 BYTES


#define INT_BYPASS (1<<3)



/* slave*/
#define CLEAR_SLAVE_INT (1<<8)
#define SLAVE_DATA_RDY (1<<0)



enum SPI_MODE {
	SPI_MASTER_READ = 0,
	SPI_MASTER_WRITE = 1,
	SPI_MASTER_RW = 2,
	SPI_SLAVE_READ = 3,
	SPI_SLAVE_WRITE = 4,
	SPI_IDLE = 5
};


struct SPI_MAS {
	// Group 091 : SPI_MASTER
	unsigned int MST_TX_DATA_ADDR                      ; // 00  (ADDR : 0x9C00_2D80)
	unsigned int MST_TX_DATA_3_2_1_0                   ; // 01  (ADDR : 0x9C00_2D84)
	unsigned int MST_TX_DATA_7_6_5_4                   ; // 02  (ADDR : 0x9C00_2D88)
	unsigned int MST_TX_DATA_11_10_9_8                 ; // 03  (ADDR : 0x9C00_2D8C)
	unsigned int MST_TX_DATA_15_14_13_12               ; // 04  (ADDR : 0x9C00_2D90)
	unsigned int G091_RESERVED_0[4]                    ; //     (ADDR : 0x9C00_2D94-2DA0)
	unsigned int MST_RX_DATA_3_2_1_0                   ; // 09  (ADDR : 0x9C00_2DA4)
	unsigned int MST_RX_DATA_7_6_5_4                   ; // 10  (ADDR : 0x9C00_2DA8)
	unsigned int MST_RX_DATA_11_10_9_8                 ; // 11  (ADDR : 0x9C00_2DAC)
	unsigned int MST_RX_DATA_15_14_13_12               ; // 12  (ADDR : 0x9C00_2DB0)
	unsigned int FIFO_DATA                             ; // 13  (ADDR : 0x9C00_2DB4)
	unsigned int SPI_FD_STATUS                         ; // 14  (ADDR : 0x9C00_2DB8)
	unsigned int SPI_FD_CONFIG                         ; // 15  (ADDR : 0x9C00_2DBC)
	unsigned int G091_RESERVED_1                       ; // 16  (ADDR : 0x9C00_2DC0)
	unsigned int SPI_CTRL_CLKSEL                       ; // 17  (ADDR : 0x9C00_2DC4)
	unsigned int BYTE_NO                               ; // 18  (ADDR : 0x9C00_2DC8)
	unsigned int SPI_INT_BUSY                          ; // 19  (ADDR : 0x9C00_2DCC)
	unsigned int DMA_CTRL                              ; // 20  (ADDR : 0x9C00_2DD0)
	unsigned int DMA_LENGTH                            ; // 21  (ADDR : 0x9C00_2DD4)
	unsigned int DMA_ADDR                              ; // 22  (ADDR : 0x9C00_2DD8)
	unsigned int G091_RESERVED_2[1]                    ; // 23  (ADDR : 0x9C00_2DDC)
	unsigned int DMA_ADDR_STAT                         ; // 24  (ADDR : 0x9C00_2DE0)
	unsigned int G091_RESERVED_3[1]                    ; // 25  (ADDR : 0x9C00_2DE4)
	unsigned int UART_DMA_CTRL                         ; // 26  (ADDR : 0x9C00_2DE8)
	unsigned int G091_RESERVED_4[1]                    ; // 27  (ADDR : 0x9C00_2DEC)
	unsigned int SPI_MST_DEBUG_SEL                     ; // 28  (ADDR : 0x9C00_2DF0)
	unsigned int SPI_COM_DEBUG_SEL                     ; // 29  (ADDR : 0x9C00_2DF4)
	unsigned int SPI_EXTRA_CYCLE                       ; // 30  (ADDR : 0x9C00_2DF8)
	unsigned int MST_DMA_DATA_RDY                      ; // 31  (ADDR : 0x9C00_2DFC)
};


struct SPI_SLA {
	// Group 092 : SPI_SLAVE
	unsigned int SLV_TX_DATA_2_1_0                     ; // 00  (ADDR : 0x9C00_2E00)
	unsigned int SLV_TX_DATA_6_5_4_3                   ; // 01  (ADDR : 0x9C00_2E04)
	unsigned int SLV_TX_DATA_10_9_8_7                  ; // 02  (ADDR : 0x9C00_2E08)
	unsigned int SLV_TX_DATA_14_13_12_11               ; // 03  (ADDR : 0x9C00_2E0C)
	unsigned int SLV_TX_DATA_15                        ; // 04  (ADDR : 0x9C00_2E10)
	unsigned int G092_RESERVED_0[4]                    ; //     (ADDR : 0x9C00_2E14-2E20)
	unsigned int SLV_RX_DATA_3_2_1_0                   ; // 09  (ADDR : 0x9C00_2E24)
	unsigned int SLV_RX_DATA_7_6_5_4                   ; // 10  (ADDR : 0x9C00_2E28)
	unsigned int SLV_RX_DATA_11_10_9_8                 ; // 11  (ADDR : 0x9C00_2E2C)
	unsigned int SLV_RX_DATA_15_14_13_12               ; // 12  (ADDR : 0x9C00_2E30)
	unsigned int G092_RESERVED_1[4]                    ; //     (ADDR : 0x9C00_2E34-2E40)
	unsigned int RISC_INT_DATA_RDY                     ; // 17  (ADDR : 0x9C00_2E44)
	unsigned int SLV_DMA_CTRL                          ; // 18  (ADDR : 0x9C00_2E48)
	unsigned int SLV_DMA_LENGTH                        ; // 19  (ADDR : 0x9C00_2E4C)
	unsigned int SLV_DMA_INI_ADDR                      ; // 20  (ADDR : 0x9C00_2E50)
	unsigned int G092_RESERVED_2[2]                    ; //     (ADDR : 0x9C00_2E54-2E58)
	unsigned int ADDR_SPI_BUSY                         ; // 23  (ADDR : 0x9C00_2E5C)
	unsigned int G092_RESERVED_3[8]                    ; //     (ADDR : 0x9C00_2E60-2E7C)
};



enum {
	SPI_MASTER,
	SPI_SLAVE,
};


struct pentagram_spi_master {

	struct device *dev;

	int mode;

	struct spi_master *master;
	struct spi_controller *ctlr;

	void __iomem *mas_base;

	void __iomem *sla_base;

	u32 message_config;

	int dma_irq;
	int mas_irq;
	int sla_irq;

	struct clk *spi_clk;
	struct reset_control *rstc;

	spinlock_t lock;
	struct mutex buf_lock;
	unsigned int spi_max_frequency;
	dma_addr_t tx_dma_phy_base;
	dma_addr_t rx_dma_phy_base;
	void *tx_dma_vir_base;
	void *rx_dma_vir_base;
	struct completion isr_done;	// complete() at *master_(dma|mas)_irq()
	struct completion sla_isr;	// completion() at spi_S_irq() - slave irq jandler
	unsigned int bufsiz;

	unsigned int  rx_cur_len;
	unsigned int  tx_cur_len;

	u8 tx_data_buf[SPI_TRANS_DATA_SIZE];
	u8 rx_data_buf[SPI_TRANS_DATA_SIZE];

	int isr_flag;

	unsigned int  data_unit;
};


static unsigned int bufsiz = 4096;

static void pentagram_set_cs(struct spi_device *_s, bool _on)
{
	if (_s->mode & SPI_NO_CS)
		return;
	if (!(_s->cs_gpiod))
		return;
	dev_dbg(&(_s->dev), "%d gpiod:%d", _on, desc_to_gpio(_s->cs_gpiod));
	if (_s->mode & SPI_CS_HIGH)
		_on = !_on;
	gpiod_set_value_cansleep(_s->cs_gpiod, _on);
}

// spi slave irq handler
static irqreturn_t pentagram_spi_S_irq(int _irq, void *_dev)
{
	unsigned long flags;
	struct pentagram_spi_master *pspim = (struct pentagram_spi_master *)_dev;
	struct SPI_SLA *sr = (struct SPI_SLA *)pspim->sla_base;

	FUNC_DBG();
	spin_lock_irqsave(&pspim->lock, flags);
	writel(readl(&sr->RISC_INT_DATA_RDY) | CLEAR_SLAVE_INT, &sr->RISC_INT_DATA_RDY);
	spin_unlock_irqrestore(&pspim->lock, flags);
	complete(&pspim->sla_isr);
	return IRQ_HANDLED;
}

// slave DMA rw (unused now)
// FIXME: probably frop this?
int pentagram_spi_slave_dma_rw(struct spi_device *spi, u8 *buf, unsigned int len, int RW_phase)
{
	struct pentagram_spi_master *pspim = spi_controller_get_devdata(spi->controller);

	struct SPI_SLA *spis_reg = (struct SPI_SLA *)(pspim->sla_base);
	struct SPI_MAS *spim_reg = (struct SPI_MAS *)(pspim->mas_base);
	struct device dev = spi->dev;
	unsigned long timeout = msecs_to_jiffies(2000);

	FUNC_DBG();
	mutex_lock(&pspim->buf_lock);

	if (RW_phase == SPI_SLAVE_WRITE) {
		memcpy(pspim->tx_dma_vir_base, buf, len);
		writel_relaxed(DMA_WRITE, &spis_reg->SLV_DMA_CTRL);
		writel_relaxed(len, &spis_reg->SLV_DMA_LENGTH);
		writel_relaxed(pspim->tx_dma_phy_base, &spis_reg->SLV_DMA_INI_ADDR);
		writel(readl(&spis_reg->RISC_INT_DATA_RDY) | SLAVE_DATA_RDY,
					&spis_reg->RISC_INT_DATA_RDY);
		//regs1->SLV_DMA_CTRL = 0x4d;
		//regs1->SLV_DMA_LENGTH = 0x50;
		//regs1->SLV_DMA_INI_ADDR = 0x300;
		//regs1->RISC_INT_DATA_RDY |= 0x1;
	} else if (RW_phase == SPI_SLAVE_READ) {
		reinit_completion(&pspim->isr_done);
		writel(DMA_READ, &spis_reg->SLV_DMA_CTRL);
		writel(len, &spis_reg->SLV_DMA_LENGTH);
		writel(pspim->rx_dma_phy_base, &spis_reg->SLV_DMA_INI_ADDR);

		if (!wait_for_completion_timeout(&pspim->isr_done, timeout)) {
			dev_err(&dev, "wait_for_completion_timeout\n");
			goto exit_spi_slave_rw;
		}
		while ((readl(&spim_reg->DMA_CTRL) & DMA_W_INT) == DMA_W_INT) {
			dev_dbg(&dev, "spim_reg->DMA_CTRL 0x%x\n", readl(&spim_reg->DMA_CTRL));
		};
		memcpy(buf, pspim->rx_dma_vir_base, len);
		writel(SLA_SW_RST, &spis_reg->SLV_DMA_CTRL);
		/* read*/
		//regs1->SLV_DMA_CTRL = 0xd;
		//regs1->SLV_DMA_LENGTH = 0x50;
		//regs1->SLV_DMA_INI_ADDR = 0x400;
	}

exit_spi_slave_rw:
	mutex_unlock(&pspim->buf_lock);
	return 0;
}

// slave only. usually called on driver remove
static int pentagram_spi_S_abort(struct spi_controller *_c)
{
	struct pentagram_spi_master *pspim = spi_master_get_devdata(_c);

	complete(&pspim->sla_isr);
	complete(&pspim->isr_done);
	return 0;
}

// slave R/W, called from S_transfer_one() only
int pentagram_spi_S_rw(struct spi_device *_s, struct spi_transfer *_t, int RW_phase)
{
	struct pentagram_spi_master *pspim = spi_controller_get_devdata(_s->controller);
	struct SPI_SLA *spis_reg = (struct SPI_SLA *)(pspim->sla_base);
	struct device *devp = &(_s->dev);

	FUNC_DBG();
	mutex_lock(&pspim->buf_lock);

	if (RW_phase == SPI_SLAVE_WRITE) {
		DBG_INF("S_WRITE len %d", _t->len);
		reinit_completion(&pspim->sla_isr);

		if (_t->tx_dma == pspim->tx_dma_phy_base)
			memcpy(pspim->tx_dma_vir_base, _t->tx_buf, _t->len);

		writel_relaxed(DMA_WRITE, &spis_reg->SLV_DMA_CTRL);
		writel_relaxed(_t->len, &spis_reg->SLV_DMA_LENGTH);
		writel_relaxed(_t->tx_dma, &spis_reg->SLV_DMA_INI_ADDR);
		writel(readl(&spis_reg->RISC_INT_DATA_RDY) | SLAVE_DATA_RDY,
				&spis_reg->RISC_INT_DATA_RDY);

		if (wait_for_completion_interruptible(&pspim->sla_isr))
			dev_err(devp, "%s() wait_for_completion timeout\n", __func__);

	} else if (RW_phase == SPI_SLAVE_READ) {
		DBG_INF("S_READ len %d", _t->len);
		reinit_completion(&pspim->isr_done);
		writel(DMA_READ, &spis_reg->SLV_DMA_CTRL);
		writel(_t->len, &spis_reg->SLV_DMA_LENGTH);
		writel(_t->rx_dma, &spis_reg->SLV_DMA_INI_ADDR);

		// wait for DMA to complete
		if (wait_for_completion_interruptible(&pspim->isr_done)) {
			dev_err(devp, "%s() wait_for_completion timeout\n", __func__);
			goto exit_spi_slave_rw;
		}
		// FIXME: is "len" correct there?
		if (_t->tx_dma == pspim->tx_dma_phy_base)
			memcpy(_t->rx_buf, pspim->rx_dma_vir_base, _t->len);

		writel(SLA_SW_RST, &spis_reg->SLV_DMA_CTRL);
	}

exit_spi_slave_rw:
	mutex_unlock(&pspim->buf_lock);
	return 0;
}

// spi master irq handler
static irqreturn_t pentagram_spi_M_irq_dma(int _irq, void *_dev)
{
	unsigned long flags;
	struct pentagram_spi_master *pspim = (struct pentagram_spi_master *)_dev;
	struct SPI_MAS *sr = (struct SPI_MAS *)pspim->mas_base;

	FUNC_DBG();
	spin_lock_irqsave(&pspim->lock, flags);
	writel(readl(&sr->DMA_CTRL) | CLEAR_DMA_W_INT, &sr->DMA_CTRL);
	spin_unlock_irqrestore(&pspim->lock, flags);
	complete(&pspim->isr_done);
	return IRQ_HANDLED;
}

void sp7021spi_rb(struct pentagram_spi_master *_m, u8 _len)
{
	struct SPI_MAS *sr = (struct SPI_MAS *)_m->mas_base;
	int i;

	for (i = 0; i < _len; i++) {
		_m->rx_data_buf[_m->rx_cur_len] = readl(&sr->FIFO_DATA);
		DBG_INF("RX 0x%x _cur_len = %d", _m->rx_data_buf[_m->rx_cur_len], _m->rx_cur_len);
		_m->rx_cur_len++;
	}
}
void sp7021spi_wb(struct pentagram_spi_master *_m, u8 _len)
{
	struct SPI_MAS *sr = (struct SPI_MAS *)_m->mas_base;
	int i;

	for (i = 0; i < _len; i++) {
		DBG_INF("TX 0x%02x _cur_len %d", _m->tx_data_buf[_m->tx_cur_len], _m->tx_cur_len);
		writel(_m->tx_data_buf[_m->tx_cur_len], &sr->FIFO_DATA);
		_m->tx_cur_len++;
	}
}

static irqreturn_t pentagram_spi_M_irq(int _irq, void *_dev)
{
	unsigned long flags;
	struct pentagram_spi_master *pspim = (struct pentagram_spi_master *)_dev;
	struct SPI_MAS *sr = (struct SPI_MAS *)pspim->mas_base;
	u32 fd_status = 0;
	unsigned int tx_len, rx_cnt, tx_cnt;
	bool isrdone = false;

	FUNC_DBG();

	spin_lock_irqsave(&pspim->lock, flags);

	fd_status = readl(&sr->SPI_FD_STATUS);
	tx_cnt = GET_TX_CNT(fd_status);
	tx_len = GET_TX_LEN(fd_status);

	if ((fd_status & TX_EMP_FLAG) && (fd_status & RX_EMP_FLAG) && (GET_LEN(fd_status) == 0))
		goto fin_irq;

	if (fd_status & FINISH_FLAG)
		DBG_INF("FINISH_FLAG");
	if (fd_status & TX_EMP_FLAG)
		DBG_INF("TX_EMP_FLAG");
	if (fd_status & RX_FULL_FLAG)
		DBG_INF("RX_FULL_FLAG");
	rx_cnt = GET_RX_CNT(fd_status);
	// RX_FULL_FLAG means RX buffer is full (16 bytes)
	if (fd_status & RX_FULL_FLAG)
		rx_cnt = pspim->data_unit;

	tx_cnt = min(tx_len - pspim->tx_cur_len, pspim->data_unit - tx_cnt);

	DBG_INF("fd_st=0x%x rx_c:%d tx_c:%d tx_l:%d", fd_status, rx_cnt, tx_cnt, tx_len);

	if (rx_cnt > 0)
		sp7021spi_rb(pspim, rx_cnt);
	if (tx_cnt > 0)
		sp7021spi_wb(pspim, tx_cnt);

	fd_status = readl(&sr->SPI_FD_STATUS);

	if ((fd_status & FINISH_FLAG) || (GET_TX_LEN(fd_status) == pspim->tx_cur_len)) {

		while (GET_LEN(fd_status) != pspim->rx_cur_len) {
			fd_status = readl(&sr->SPI_FD_STATUS);
			if (fd_status & RX_FULL_FLAG)
				rx_cnt = pspim->data_unit;
			else
				rx_cnt = GET_RX_CNT(fd_status);

			if (rx_cnt > 0)
				sp7021spi_rb(pspim, rx_cnt);
		}
		writel(readl(&sr->SPI_INT_BUSY) | CLEAR_MASTER_INT, &sr->SPI_INT_BUSY);
		isrdone = true;
	}
fin_irq:
	if (isrdone)
		complete(&pspim->isr_done);
	spin_unlock_irqrestore(&pspim->lock, flags);
	DBG_INF("handled irq %d", isrdone);
	return IRQ_HANDLED;
}

// called in *controller_transfer_one*
static void spspi_prep_transfer(struct spi_controller *_c, struct spi_device *_s)
{
	struct pentagram_spi_master *pspim = spi_master_get_devdata(_c);

	pspim->tx_cur_len = 0;
	pspim->rx_cur_len = 0;
	pspim->data_unit = FIFO_DATA_BITS / _s->bits_per_word;
	pspim->isr_flag = SPI_IDLE;
	DBG_INF("pspim->data_unit %d unit", pspim->data_unit);
}

static void spspi_delay_ns(u32 _ns)
{

	if (!_ns)
		return;
	if (_ns <= 1000)
		ndelay(_ns);
	else {
		u32 us = DIV_ROUND_UP(_ns, 1000);

		if (us <= 10)
			udelay(us);
		else
			usleep_range(us, us + DIV_ROUND_UP(us, 10));
	}
}

// called from *transfer* functions, set clock there
static void pentagram_spi_setup_transfer(struct spi_device *_s,
					struct spi_controller *_c, struct spi_transfer *_t)
{
	struct pentagram_spi_master *pspim = spi_master_get_devdata(_c);
	struct SPI_MAS *spim_reg = (struct SPI_MAS *)pspim->mas_base;
	u32 rc = 0, rs = 0;
	unsigned int clk_rate;
	unsigned int div;
	unsigned int clk_sel;

	FUNC_DBG();

	dev_dbg(&(_s->dev), "setup %dHz", _s->max_speed_hz);
	dev_dbg(&(_s->dev), "tx %p, rx %p, len %d", _t->tx_buf, _t->rx_buf, _t->len);
	// set clock
	clk_rate = clk_get_rate(pspim->spi_clk);
	if (_t->speed_hz <= _s->max_speed_hz)
		div = clk_rate / _t->speed_hz;
	else if (_s->max_speed_hz <= _c->max_speed_hz)
		div = clk_rate / _s->max_speed_hz;
	else if (_c->max_speed_hz < pspim->spi_max_frequency)
		div = clk_rate / _c->max_speed_hz;
	else
		div = clk_rate / pspim->spi_max_frequency;

	dev_dbg(&(_s->dev), "clk_rate: %d div %d", clk_rate, div);

	clk_sel = (div / 2) - 1;
	// set full duplex (bit 6) and fd freq (bits 31:16)
	rc = FD_SEL | (0xffff << 16);
	rs = FD_SEL | ((clk_sel & 0xffff) << 16);
	writel((pspim->message_config & ~rc) | rs, &(spim_reg->SPI_FD_CONFIG));
}


static int pentagram_spi_master_combine_write_read(struct spi_controller *_c,
			struct spi_transfer *first, unsigned int transfers_cnt)
{
	struct pentagram_spi_master *pspim = spi_master_get_devdata(_c);
	struct SPI_MAS *sr = (struct SPI_MAS *)pspim->mas_base;
	unsigned int data_len = 0;
	u32 reg_temp = 0;
	unsigned long timeout = msecs_to_jiffies(200);
	unsigned int i, len_temp;
	int ret;
	struct spi_transfer *t = first;
	bool xfer_rx = false;

	FUNC_DBG();

	memset(&pspim->tx_data_buf[0], 0, SPI_TRANS_DATA_SIZE);
	dev_dbg(&(_c->dev), "txrx: tx %p, rx %p, len %d\n", t->tx_buf, t->rx_buf, t->len);

	mutex_lock(&pspim->buf_lock);
	reinit_completion(&pspim->isr_done);

	for (i = 0; i < transfers_cnt; i++) {
		if (t->tx_buf)
			memcpy(&pspim->tx_data_buf[data_len], t->tx_buf, t->len);
		if (t->rx_buf)
			xfer_rx = true;
		data_len += t->len;
		t = list_entry(t->transfer_list.next, struct spi_transfer, transfer_list);
	}
	dev_dbg(&(_c->dev), "data_len %d xfer_rx %d", data_len, xfer_rx);

	// set SPI FIFO data for full duplex (SPI_FD fifo_data)  91.13
	if (pspim->tx_cur_len < data_len) {
		len_temp = min(pspim->data_unit, data_len);
		sp7021spi_wb(pspim, len_temp);
	}
	// initial SPI master config and change to Full-Duplex mode (SPI_FD_CONFIG)  91.15
	reg_temp = readl(&sr->SPI_FD_CONFIG);
	reg_temp &= CLEAN_RW_BYTE;
	reg_temp &= CLEAN_FLUG_MASK;
	reg_temp |= FD_SEL;
	// set SPI master config for full duplex (SPI_FD_CONFIG)  91.15
	reg_temp |= FINISH_FLAG_MASK | TX_EMP_FLAG_MASK | RX_FULL_FLAG_MASK;
	reg_temp |= WRITE_BYTE(0) | READ_BYTE(0);  // set read write byte from fifo
	writel(reg_temp, &sr->SPI_FD_CONFIG);
	DBG_INF("SPI_FD_CONFIG =0x%x", readl(&sr->SPI_FD_CONFIG));
	// set SPI STATUS and start SPI for full duplex (SPI_FD_STATUS)  91.13
	writel(TOTAL_LENGTH(data_len) | TX_LENGTH(data_len) | SPI_START_FD, &sr->SPI_FD_STATUS);
	writel(readl(&sr->SPI_INT_BUSY) | INT_BYPASS, &sr->SPI_INT_BUSY);

	if (!wait_for_completion_timeout(&pspim->isr_done, timeout)) {
		DBG_INF("wait_for_completion timeout");
		ret = 1;
		goto free_master_combite_rw;
	}

	if (xfer_rx == false) {
		ret = 0;
		goto free_master_combite_rw;
	}

	data_len = 0;
	t = first;
	for (i = 0; i < transfers_cnt; i++) {
		if (t->rx_buf)
			memcpy(t->rx_buf, &pspim->rx_data_buf[data_len], t->len);

		data_len += t->len;
		t = list_entry(t->transfer_list.next, struct spi_transfer, transfer_list);
	}
	ret = 0;
free_master_combite_rw:
	// reset SPI
	writel(readl(&sr->SPI_FD_CONFIG) & CLEAN_FLUG_MASK, &sr->SPI_FD_CONFIG);

	if (pspim->message_config & CPOL_FD)
		writel(pspim->message_config, &sr->SPI_FD_CONFIG);

	mutex_unlock(&pspim->buf_lock);
	return ret;
}


static int pentagram_spi_master_transfer(struct spi_controller *_c, struct spi_device *_s,
	struct spi_transfer *xfer)
{
	struct pentagram_spi_master *pspim = spi_master_get_devdata(_c);
	struct SPI_MAS *sr = (struct SPI_MAS *)pspim->mas_base;
	u32 reg_temp = 0;
	unsigned long timeout = msecs_to_jiffies(200);
	unsigned int i;
	int ret;
	long cret;
	unsigned int xfer_cnt, xfer_len, last_len;
	struct spi_transfer *t = xfer;

	FUNC_DBG();

	xfer_cnt = t->len / SPI_TRANS_DATA_SIZE;
	last_len = t->len % SPI_TRANS_DATA_SIZE;

	memset(&pspim->tx_data_buf[0], 0, SPI_TRANS_DATA_SIZE);

	dev_dbg(&(_s->dev), "txrx: tx %p, rx %p, len %d\n", t->tx_buf, t->rx_buf, t->len);

	t->tx_buf+SPI_TRANS_DATA_SIZE, &(t->tx_buf[SPI_TRANS_DATA_SIZE]));

	for (i = 0; i <= xfer_cnt; i++) {

		mutex_lock(&pspim->buf_lock);

		spspi_prep_transfer(_c, _s);
		pentagram_spi_setup_transfer(_s, _c, xfer);

		reinit_completion(&pspim->isr_done);

		if (i == xfer_cnt)
			xfer_len = last_len;
		else
			xfer_len = SPI_TRANS_DATA_SIZE;

		if (t->tx_buf)
			memcpy(pspim->tx_data_buf, t->tx_buf+i*SPI_TRANS_DATA_SIZE, xfer_len);

		// set SPI FIFO data for full duplex (SPI_FD fifo_data)  91.13
		if (pspim->tx_cur_len < xfer_len) {
			reg_temp = min(pspim->data_unit, xfer_len);
			sp7021spi_wb(pspim, reg_temp);
		}

		// initial SPI master config and change to Full-Duplex mode (SPI_FD_CONFIG)  91.15
		reg_temp = readl(&sr->SPI_FD_CONFIG);
		reg_temp &= CLEAN_RW_BYTE;
		reg_temp &= CLEAN_FLUG_MASK;
		reg_temp |= FD_SEL;
		// set SPI master config for full duplex (SPI_FD_CONFIG)  91.15
		reg_temp |= FINISH_FLAG_MASK | TX_EMP_FLAG_MASK | RX_FULL_FLAG_MASK;
		reg_temp |= WRITE_BYTE(0) | READ_BYTE(0);  // set read write byte from fifo
		writel(reg_temp, &sr->SPI_FD_CONFIG);
		dev_dbg(&(_s->dev), "SPI_FD_CONFIG =0x%x", readl(&sr->SPI_FD_CONFIG));
		// set SPI STATUS and start SPI for full duplex (SPI_FD_STATUS)  91.13
		dev_dbg(&(_s->dev), "TOTAL_LENGTH =0x%x  TX_LENGTH =0x%x xfer_len =0x%x ",
					TOTAL_LENGTH(xfer_len), TX_LENGTH(xfer_len), xfer_len);
		writel(TOTAL_LENGTH(xfer_len) | TX_LENGTH(xfer_len) | SPI_START_FD,
					&sr->SPI_FD_STATUS);
		DBG_INF("set SPI_FD_STATUS =0x%x", readl(&sr->SPI_FD_STATUS));

		cret = wait_for_completion_interruptible_timeout(&pspim->isr_done, timeout);
		if (cret <= 0) {
			dev_dbg(&(_s->dev), "wait_for_completion cret=%ld\n", cret);
			writel(readl(&sr->SPI_FD_CONFIG) & CLEAN_FLUG_MASK, &sr->SPI_FD_CONFIG);
			ret = 1;
			goto free_master_combite_rw;
		}

		reg_temp = readl(&sr->SPI_FD_STATUS);
		if (reg_temp & FINISH_FLAG)
			writel(readl(&sr->SPI_FD_CONFIG) & CLEAN_FLUG_MASK, &sr->SPI_FD_CONFIG);

		if (t->rx_buf)
			memcpy(t->rx_buf+i*SPI_TRANS_DATA_SIZE, pspim->rx_data_buf, xfer_len);

		ret = 0;

free_master_combite_rw:

		if (pspim->message_config & CPOL_FD)
			writel(pspim->message_config, &sr->SPI_FD_CONFIG);

		mutex_unlock(&pspim->buf_lock);
	}

	return ret;
}

// called when child device is registering on the bus
static int pentagram_spi_D_setup(struct spi_device *_s)
{

#ifdef CONFIG_PM_RUNTIME_SPI

	struct pentagram_spi_master *pspim = spi_controller_get_devdata(_s->controller);

	if (pm_runtime_enabled(pspim->dev)) {
		ret = pm_runtime_get_sync(pspim->dev)
		if (ret < 0)
			goto pm_out;
	}
#endif

#ifdef CONFIG_PM_RUNTIME_SPI
	pm_runtime_put(pspim->dev);
#endif
	return 0;
#ifdef CONFIG_PM_RUNTIME_SPI
pm_out:
	pm_runtime_mark_last_busy(pspim->dev);
	pm_runtime_put_autosuspend(pspim->dev);
	DBG_INF("pm_out");
	return 0;
#endif
}

// preliminary set CS, CPOL, CPHA and LSB
// called for both - master and slave. See drivers/spi/spi.c
static int pentagram_spi_controller_prepare_message(struct spi_controller *_c,
				    struct spi_message *_m)
{
	struct pentagram_spi_master *pspim = spi_master_get_devdata(_c);
	struct SPI_MAS *sr = (struct SPI_MAS *)pspim->mas_base;
	struct spi_device *s = _m->spi;
	// reg clear bits and set bits
	u32 rs = 0;

	FUNC_DBG();
	dev_dbg(&(s->dev), "setup %d bpw, %scpol, %scpha, %scs-high, %slsb-first %xcs_gpio\n",
		s->bits_per_word,
		s->mode & SPI_CPOL ? "" : "~",
		s->mode & SPI_CPHA ? "" : "~",
		s->mode & SPI_CS_HIGH ? "" : "~",
		s->mode & SPI_LSB_FIRST ? "" : "~",
		s->cs_gpio);

	writel(readl(&sr->SPI_FD_STATUS) | FD_SW_RST, &sr->SPI_FD_STATUS);

	//set up full duplex frequency and enable  full duplex
	rs = FD_SEL | ((0xffff) << 16);

	if (s->mode & SPI_CPOL)
		rs |= CPOL_FD;

	if (s->mode & SPI_LSB_FIRST)
		rs |= LSB_SEL;

	if (s->mode & SPI_CS_HIGH)
		rs |= CS_POR;

	if (s->mode & SPI_CPHA)
		rs |=  CPHA_R;
	else
		rs |=  CPHA_W;

	rs |= WRITE_BYTE(0) | READ_BYTE(0);  // set read write byte from fifo

	pspim->message_config = rs;

	if (pspim->message_config & CPOL_FD)
		writel(pspim->message_config, &sr->SPI_FD_CONFIG);

	return 0;
}
static int pentagram_spi_controller_unprepare_message(struct spi_controller *ctlr,
				    struct spi_message *msg)
{
	FUNC_DBG();
	return 0;
}

static size_t pentagram_spi_max_length(struct spi_device *spi)
{
	return SPI_MSG_DATA_SIZE;
}



// SPI-slave R/W
static int pentagram_spi_S_transfer_one(struct spi_controller *_c, struct spi_device *_s,
					struct spi_transfer *_t)
{
	struct pentagram_spi_master *pspim = spi_master_get_devdata(_c);
	struct device *dev = pspim->dev;
	int mode = SPI_IDLE, ret = 0;

	FUNC_DBG();
#ifdef CONFIG_PM_RUNTIME_SPI
	if (pm_runtime_enabled(pspim->dev)) {
		ret = pm_runtime_get_sync(pspim->dev);
		if (ret < 0)
			goto pm_out;
	}
#endif

	if (spi_controller_is_slave(_c)) {

		pspim->isr_flag = SPI_IDLE;

		if ((_t->tx_buf) && (_t->rx_buf)) {
			dev_dbg(&_c->dev, "%s() wrong command\n", __func__);
		} else if (_t->tx_buf) {
			/* tx_buf is a const void* where we need a void * for
			 * the dma mapping
			 */
			void *nonconst_tx = (void *)_t->tx_buf;

			_t->tx_dma = dma_map_single(dev, nonconst_tx,
						_t->len, DMA_TO_DEVICE);

			if (dma_mapping_error(dev, _t->tx_dma)) {
				if (_t->len <= bufsiz) {
					_t->tx_dma = pspim->tx_dma_phy_base;
					mode = SPI_SLAVE_WRITE;
				} else
					mode = SPI_IDLE;

			} else
				mode = SPI_SLAVE_WRITE;
		} else if (_t->rx_buf) {

			_t->rx_dma = dma_map_single(dev, _t->rx_buf,
				_t->len, DMA_FROM_DEVICE);

			if (dma_mapping_error(dev, _t->rx_dma)) {
				if (_t->len <= bufsiz) {
					_t->rx_dma = pspim->rx_dma_phy_base;
					mode = SPI_SLAVE_READ;
				} else
					mode = SPI_IDLE;
			} else
				mode = SPI_SLAVE_READ;
		}

		switch (mode) {
		case SPI_SLAVE_WRITE:
		case SPI_SLAVE_READ:
			pentagram_spi_S_rw(_s, _t, mode);
			break;
		default:
			DBG_INF("idle?");
			break;
		}


	    //if((xfer->rx_buf) && (xfer->rx_dma == pspim->rx_dma_phy_base)){
		//    memcpy(xfer->rx_buf, pspim->rx_dma_vir_base, xfer->len);
	    //}

		if ((_t->tx_buf) && (_t->tx_dma != pspim->tx_dma_phy_base))
			dma_unmap_single(dev, _t->tx_dma,
				_t->len, DMA_TO_DEVICE);

		if ((_t->rx_buf) && (_t->rx_dma != pspim->rx_dma_phy_base))
			dma_unmap_single(dev, _t->rx_dma,
				_t->len, DMA_FROM_DEVICE);

	}

	spi_finalize_current_transfer(_c);

#ifdef CONFIG_PM_RUNTIME_SPI
	pm_runtime_put(pspim->dev);
	DBG_INF("pm_put");
#endif
	return ret;
#ifdef CONFIG_PM_RUNTIME_SPI
pm_out:
	pm_runtime_mark_last_busy(pspim->dev);
	pm_runtime_put_autosuspend(pspim->dev);
	DBG_INF("pm_out");
	return ret;
#endif
}


static int pentagram_spi_M_transfer_one_message(struct spi_controller *ctlr, struct spi_message *m)
{
	//struct pentagram_spi_master *pspim = spi_master_get_devdata(ctlr);
	struct spi_device *spi = m->spi;
	unsigned int xfer_cnt = 0, total_len = 0;
	bool start_xfer = false;
	struct spi_transfer *xfer, *first_xfer = NULL;
	int ret;
	bool keep_cs = false;

	FUNC_DBG();

#ifdef CONFIG_PM_RUNTIME_SPI
	if (pm_runtime_enabled(pspim->dev)) {
		ret = pm_runtime_get_sync(pspim->dev);
		if (ret < 0)
			goto pm_out;
	}
#endif
	pentagram_set_cs(spi, true);

	list_for_each_entry(xfer, &m->transfers, transfer_list) {
		if (!first_xfer)
			first_xfer = xfer;
		total_len +=  xfer->len;

		/* all combined transfers have to have the same speed */
		if (first_xfer->speed_hz != xfer->speed_hz) {
			dev_err(&(spi->dev), "unable to change speed between transfers\n");
			ret = -EINVAL;
			break;
		}
		/* CS will be deasserted directly after transfer */
//		if ( xfer->delay_usecs) {
//			DBG_ERR( "can't keep CS asserted after transfer");
//			ret = -EINVAL;
//			break;
//		}
		if (xfer->len > SPI_MSG_DATA_SIZE) {
			dev_err(&(spi->dev), "over total trans-length xfer->len = %d", xfer->len);
			ret = -EINVAL;
			break;
		}

		if (list_is_last(&xfer->transfer_list, &m->transfers))
			DBG_INF("xfer = transfer_list");
		if (total_len > SPI_TRANS_DATA_SIZE)
			DBG_INF("(total_len > SPI_TRANS_DATA_SIZE)");
		if (xfer->cs_change)
			DBG_INF("xfer->cs_change");

		if (list_is_last(&xfer->transfer_list, &m->transfers)
			|| (total_len > SPI_TRANS_DATA_SIZE)
			|| xfer->cs_change || (xfer->len > SPI_TRANS_DATA_SIZE))
			start_xfer = true;


		dev_dbg(&(spi->dev), "start_xfer:%d total_len:%d\n", start_xfer, total_len);
		if (start_xfer != true) {
			xfer_cnt++;
			continue;
		}
		if (total_len <= SPI_TRANS_DATA_SIZE)
			xfer_cnt++;

		if (xfer_cnt > 0) {
			spspi_prep_transfer(ctlr, spi);
			pentagram_spi_setup_transfer(spi, ctlr, first_xfer);
			ret = pentagram_spi_master_combine_write_read(ctlr, first_xfer, xfer_cnt);
		}

		if (total_len > SPI_TRANS_DATA_SIZE)
			ret = pentagram_spi_master_transfer(ctlr, spi, xfer);

		if (xfer->delay.value)
			spspi_delay_ns(xfer->delay.value * 1000);
		if (xfer->cs_change) {
			if (list_is_last(&xfer->transfer_list, &m->transfers))
				keep_cs = true;
			else {
				pentagram_set_cs(spi, false);
				spi_delay_to_ns(&xfer->cs_change_delay, xfer);
				pentagram_set_cs(spi, true);
			}
		}

		m->actual_length += total_len;

		first_xfer = NULL;
		xfer_cnt = 0;
		total_len = 0;
		start_xfer = false;
	}

	if (ret != 0 || !keep_cs)
		pentagram_set_cs(spi, false);
	m->status = ret;
	spi_finalize_current_message(ctlr);

#ifdef CONFIG_PM_RUNTIME_SPI
	pm_runtime_put(pspim->dev);
	DBG_INF("pm_put");
#endif
	return ret;
#ifdef CONFIG_PM_RUNTIME_SPI
pm_out:
	pm_runtime_mark_last_busy(pspim->dev);
	pm_runtime_put_autosuspend(pspim->dev);
	DBG_INF("pm_out");
	return 0;
#endif
}

static int pentagram_spi_controller_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;
	int mode;
	unsigned int max_freq;
	struct spi_controller *ctlr;
	struct pentagram_spi_master *pspim;

	FUNC_DBG();

	pdev->id = 0;
	mode = SPI_MASTER;
	if (pdev->dev.of_node) {
		pdev->id = of_alias_get_id(pdev->dev.of_node, "sp_spi");
		if (of_property_read_bool(pdev->dev.of_node, "spi-slave"))
			mode = SPI_SLAVE;
	}
	dev_dbg(&pdev->dev, "pdev->id = %d\n", pdev->id);

	if (mode == SPI_SLAVE)
		ctlr = spi_alloc_slave(&pdev->dev, sizeof(*pspim));
	else
		ctlr = spi_alloc_master(&pdev->dev, sizeof(*pspim));
	if (!ctlr)
		return -ENOMEM;

	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->bus_num = pdev->id;
	// flags, understood by the driver
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST;
	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->min_speed_hz = 40000;
	ctlr->max_speed_hz = 50000000;
	// ctlr->flags = 0
	ctlr->max_transfer_size = pentagram_spi_max_length;
	ctlr->max_message_size = pentagram_spi_max_length;
	ctlr->setup = pentagram_spi_D_setup;
	// FIXME: ctlr->auto_runtime_pm = true;
	ctlr->prepare_message = pentagram_spi_controller_prepare_message;
	ctlr->unprepare_message = pentagram_spi_controller_unprepare_message;

	if (mode == SPI_SLAVE) {
		ctlr->transfer_one = pentagram_spi_S_transfer_one;
		ctlr->slave_abort = pentagram_spi_S_abort;
	} else {
		ctlr->use_gpio_descriptors = true;
		ctlr->transfer_one_message = pentagram_spi_M_transfer_one_message;
	}

	platform_set_drvdata(pdev, ctlr);
	pspim = spi_controller_get_devdata(ctlr);

	pspim->ctlr = ctlr;
	pspim->dev = &pdev->dev;
	if (!of_property_read_u32(pdev->dev.of_node, "spi-max-frequency", &max_freq)) {
		dev_dbg(&pdev->dev, "max_freq %d\n", max_freq);
		pspim->spi_max_frequency = max_freq;
	} else
		pspim->spi_max_frequency = 25000000;

	spin_lock_init(&pspim->lock);
	mutex_init(&pspim->buf_lock);
	init_completion(&pspim->isr_done);
	init_completion(&pspim->sla_isr);

	/* find and map our resources */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, MAS_REG_NAME);
	if (res) {
		pspim->mas_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pspim->mas_base)) {
			dev_err(&pdev->dev, "%s devm_ioremap_resource fail\n", MAS_REG_NAME);
			goto free_alloc;
		}
	}
	dev_dbg(&pdev->dev, "mas_base 0x%x\n", (unsigned int)pspim->mas_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, SLA_REG_NAME);
	if (res) {
		pspim->sla_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
		if (IS_ERR(pspim->sla_base)) {
			dev_err(&pdev->dev, "%s devm_ioremap_resource fail\n", SLA_REG_NAME);
			goto free_alloc;
		}
	}
	dev_dbg(&pdev->dev, "sla_base 0x%x\n", (unsigned int)pspim->sla_base);

	/* irq*/
	pspim->dma_irq = platform_get_irq_byname(pdev, DMA_IRQ_NAME);
	if (pspim->dma_irq < 0) {
		dev_err(&pdev->dev, "failed to get %s\n", DMA_IRQ_NAME);
		goto free_alloc;
	}

	pspim->mas_irq = platform_get_irq_byname(pdev, MAS_IRQ_NAME);
	if (pspim->mas_irq < 0) {
		dev_err(&pdev->dev, "failed to get %s\n", MAS_IRQ_NAME);
		goto free_alloc;
	}


	pspim->sla_irq = platform_get_irq_byname(pdev, SLA_IRQ_NAME);
	if (pspim->sla_irq < 0) {
		dev_err(&pdev->dev, "failed to get %s\n", SLA_IRQ_NAME);
		goto free_alloc;
	}

	/* requset irq*/
	ret = devm_request_irq(&pdev->dev, pspim->dma_irq, pentagram_spi_M_irq_dma
						, IRQF_TRIGGER_RISING, pdev->name, pspim);
	if (ret) {
		dev_err(&pdev->dev, "%s devm_request_irq fail\n", DMA_IRQ_NAME);
		goto free_alloc;
	}

	ret = devm_request_irq(&pdev->dev, pspim->mas_irq, pentagram_spi_M_irq
						, IRQF_TRIGGER_RISING, pdev->name, pspim);
	if (ret) {
		dev_err(&pdev->dev, "%s devm_request_irq fail\n", MAS_IRQ_NAME);
		goto free_alloc;
	}


	ret = devm_request_irq(&pdev->dev, pspim->sla_irq, pentagram_spi_S_irq
						, IRQF_TRIGGER_RISING, pdev->name, pspim);
	if (ret) {
		dev_err(&pdev->dev, "%s devm_request_irq fail\n", SLA_IRQ_NAME);
		goto free_alloc;
	}

	/* clk*/
	pspim->spi_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pspim->spi_clk)) {
		dev_err(&pdev->dev, "devm_clk_get fail\n");
		goto free_alloc;
	}
	ret = clk_prepare_enable(pspim->spi_clk);
	if (ret)
		goto free_alloc;

	/* reset*/
	pspim->rstc = devm_reset_control_get(&pdev->dev, NULL);
	dev_dbg(&pdev->dev, "pspim->rstc : 0x%x\n", (unsigned int)pspim->rstc);
	if (IS_ERR(pspim->rstc)) {
		ret = PTR_ERR(pspim->rstc);
		dev_err(&pdev->dev, "SPI failed to retrieve reset controller: %d\n", ret);
		goto free_clk;
	}
	ret = reset_control_deassert(pspim->rstc);
	if (ret)
		goto free_clk;

	/* dma alloc*/
	pspim->tx_dma_vir_base = dma_alloc_coherent(&pdev->dev, bufsiz,
					&pspim->tx_dma_phy_base, GFP_ATOMIC);
	if (!pspim->tx_dma_vir_base)
		goto free_reset_assert;

	dev_dbg(&pdev->dev, "tx_dma vir 0x%x\n", (unsigned int)pspim->tx_dma_vir_base);
	dev_dbg(&pdev->dev, "tx_dma phy 0x%x\n", (unsigned int)pspim->tx_dma_phy_base);

	pspim->rx_dma_vir_base = dma_alloc_coherent(&pdev->dev, bufsiz,
					&pspim->rx_dma_phy_base, GFP_ATOMIC);
	if (!pspim->rx_dma_vir_base)
		goto free_tx_dma;

	dev_dbg(&pdev->dev, "rx_dma vir 0x%x\n", (unsigned int)pspim->rx_dma_vir_base);
	dev_dbg(&pdev->dev, "rx_dma phy 0x%x\n", (unsigned int)pspim->rx_dma_phy_base);

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret != 0) {
		dev_err(&pdev->dev, "spi_register_master fail\n");
		goto free_rx_dma;
	}

#ifdef CONFIG_PM_RUNTIME_SPI
	pm_runtime_set_autosuspend_delay(&pdev->dev, 5000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	dev_dbg(&pdev->dev, "pm init done\n");
#endif
	return 0;

free_rx_dma:
	dma_free_coherent(&pdev->dev, bufsiz, pspim->rx_dma_vir_base, pspim->rx_dma_phy_base);
free_tx_dma:
	dma_free_coherent(&pdev->dev, bufsiz, pspim->tx_dma_vir_base, pspim->tx_dma_phy_base);
free_reset_assert:
	reset_control_assert(pspim->rstc);
free_clk:
	clk_disable_unprepare(pspim->spi_clk);
free_alloc:
	spi_controller_put(ctlr);

	dev_dbg(&pdev->dev, "spi_master_probe done\n");
	return ret;
}

static int pentagram_spi_controller_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct pentagram_spi_master *pspim = spi_master_get_devdata(master);

	FUNC_DBG();

#ifdef CONFIG_PM_RUNTIME_SPI
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
#endif

	dma_free_coherent(&pdev->dev, bufsiz, pspim->tx_dma_vir_base, pspim->tx_dma_phy_base);
	dma_free_coherent(&pdev->dev, bufsiz, pspim->rx_dma_vir_base, pspim->rx_dma_phy_base);

	spi_unregister_master(pspim->ctlr);
	clk_disable_unprepare(pspim->spi_clk);
	reset_control_assert(pspim->rstc);

	return 0;
}

static int pentagram_spi_controller_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct spi_controller *ctlr = platform_get_drvdata(pdev);
	struct pentagram_spi_master *pspim = spi_master_get_devdata(ctlr);

	FUNC_DBG();

	reset_control_assert(pspim->rstc);

	return 0;
}

static int pentagram_spi_controller_resume(struct platform_device *pdev)
{
	struct spi_controller *ctlr = platform_get_drvdata(pdev);
	struct pentagram_spi_master *pspim = spi_master_get_devdata(ctlr);

	FUNC_DBG();

	reset_control_deassert(pspim->rstc);
	clk_prepare_enable(pspim->spi_clk);

	return 0;
}


#ifdef CONFIG_PM_RUNTIME_SPI
static int sp_spi_runtime_suspend(struct device *dev)
{
	struct spi_controller *ctlr = platform_get_drvdata(pdev);
	struct pentagram_spi_master *pspim = spi_master_get_devdata(ctlr);

	dev_dbg(dev, "devid:%d\n", dev->id);
	reset_control_assert(pspim->rstc);

	return 0;
}

static int sp_spi_runtime_resume(struct device *dev)
{
	struct spi_controller *ctlr = platform_get_drvdata(pdev);
	struct pentagram_spi_master *pspim = spi_master_get_devdata(ctlr);

	dev_dbg(dev, "devid:%d\n", dev->id);
	reset_control_deassert(pspim->rstc);
	clk_prepare_enable(pspim->spi_clk);

	return 0;
}

static const struct dev_pm_ops sp7021_spi_pm_ops = {
	.runtime_suspend = sp_spi_runtime_suspend,
	.runtime_resume  = sp_spi_runtime_resume,
};
#endif

static const struct of_device_id pentagram_spi_controller_ids[] = {
	{ .compatible = "sunplus,sp7021-spi-controller" },
	{}
};
MODULE_DEVICE_TABLE(of, pentagram_spi_controller_ids);

static struct platform_driver pentagram_spi_controller_driver = {
	.probe = pentagram_spi_controller_probe,
	.remove = pentagram_spi_controller_remove,
	.suspend	= pentagram_spi_controller_suspend,
	.resume		= pentagram_spi_controller_resume,
	.driver = {
		.name = "sunplus,sp7021-spi-controller",
		.of_match_table = pentagram_spi_controller_ids,
#ifdef CONFIG_PM_RUNTIME_SPI
		.pm     = &sp7021_spi_pm_ops,
#endif
	},
};
module_platform_driver(pentagram_spi_controller_driver);

MODULE_AUTHOR("lH Kuo <lh.kuo@sunplus.com>");
MODULE_DESCRIPTION("Sunplus SPI controller driver");
MODULE_LICENSE("GPL v2");
