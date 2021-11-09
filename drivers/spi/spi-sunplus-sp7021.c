// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 Sunplus Inc.
// Author: LH Kuo <lh.kuo@sunplus.com>

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

#define SLAVE_INT_IN


#define SP7021_MAS_REG_NAME "spi_master"
#define SP7021_SLA_REG_NAME "spi_slave"

#define SP7021_DMA_IRQ_NAME "dma_w_intr"
#define SP7021_MAS_IRQ_NAME "mas_risc_intr"

#define SP7021_SLA_IRQ_NAME "slave_risc_intr"

#define SP7021_SPI_DATA_CNT (4)
#define SP7021_SPI_DATA_SIZE (255)
#define SP7021_SPI_MSG_SIZE (SP7021_SPI_DATA_SIZE * SP7021_SPI_DATA_CNT)

#define SP7021_CLR_MAS_INT (1<<6)

#define SP7021_SLA_DMA_READ (0xd)
#define SP7021_SLA_SW_RST (1<<1)
#define SP7021_SLA_DMA_WRITE (0x4d)
#define SP7021_SLA_DMA_W_INT (1<<8)
#define SP7021_SLA_CLR_INT (1<<8)
#define SP7021_SLA_DATA_RDY (1<<0)

#define SP7021_CLR_MAS_W_INT (1<<7)

#define SP7021_TOTAL_LENGTH(x) (x<<24)
#define SP7021_TX_LENGTH(x) (x<<16)
#define SP7021_GET_LEN(x)     ((x>>24)&0xFF)
#define SP7021_GET_TX_LEN(x)  ((x>>16)&0xFF)
#define SP7021_GET_RX_CNT(x)  ((x>>12)&0x0F)
#define SP7021_GET_TX_CNT(x)  ((x>>8)&0x0F)

#define SP7021_FINISH_FLAG (1<<6)
#define SP7021_FINISH_FLAG_MASK (1<<15)
#define SP7021_RX_FULL_FLAG (1<<5)
#define SP7021_RX_FULL_FLAG_MASK (1<<14)
#define SP7021_RX_EMP_FLAG (1<<4)
#define SP7021_TX_EMP_FLAG (1<<2)
#define SP7021_TX_EMP_FLAG_MASK (1<<11)
#define SP7021_SPI_START_FD (1<<0)
#define SP7021_FD_SEL (1<<6)
#define SP7021_LSB_SEL (1<<4)
#define SP7021_WRITE_BYTE(x) (x<<9)
#define SP7021_READ_BYTE(x) (x<<7)
#define SP7021_CLEAN_RW_BYTE (~0x780)
#define SP7021_CLEAN_FLUG_MASK (~0xF800)

#define SP7021_CPOL_FD (1<<0)
#define SP7021_CPHA_R (1<<1)
#define SP7021_CPHA_W (1<<2)
#define SP7021_CS_POR (1<<5)

#define SP7021_FD_SW_RST (1<<1)
#define SP7021_FIFO_DATA_BITS (16*8)    // 16 BYTES
#define SP7021_INT_BYPASS (1<<3)

#define SP7021_FIFO_REG 0x0034
#define SP7021_SPI_STATUS_REG 0x0038
#define SP7021_SPI_CONFIG_REG 0x003c
#define SP7021_INT_BUSY_REG 0x004c
#define SP7021_DMA_CTRL_REG 0x0050

#define SP7021_DATA_RDY_REG 0x0044
#define SP7021_SLV_DMA_CTRL_REG 0x0048
#define SP7021_SLV_DMA_LENGTH_REG 0x004c
#define SP7021_SLV_DMA_ADDR_REG 0x004c

#define SP7021_BUFSIZE 4096

enum SPI_MODE {
	SP7021_MAS_READ = 0,
	SP7021_MAS_WRITE = 1,
	SP7021_MAS_RW = 2,
	SP7021_SLA_READ = 3,
	SP7021_SLA_WRITE = 4,
	SP7021_SPI_IDLE = 5
};

enum {
	SP7021_MASTER_MODE,
	SP7021_SLAVE_MODE,
};


struct sp7021_spi_ctlr {

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
	unsigned int data_status;

	unsigned int  rx_cur_len;
	unsigned int  tx_cur_len;

	u8 tx_data_buf[SP7021_SPI_DATA_SIZE];
	u8 rx_data_buf[SP7021_SPI_DATA_SIZE];

	int isr_flag;

	unsigned int  data_unit;
};

static void sp7021_spi_set_cs(struct spi_device *_s, bool _on)
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
static irqreturn_t sp7021_spi_sla_irq(int _irq, void *_dev)
{
	struct sp7021_spi_ctlr *pspim = (struct sp7021_spi_ctlr *)_dev;

	spin_lock_irq(&pspim->lock);
	pspim->data_status = readl(pspim->sla_base + SP7021_DATA_RDY_REG);
	writel(pspim->data_status | SP7021_SLA_CLR_INT,
			pspim->sla_base + SP7021_DATA_RDY_REG);
	spin_unlock_irq(&pspim->lock);
	complete(&pspim->sla_isr);
	return IRQ_HANDLED;
}

// slave only. usually called on driver remove
static int sp7021_spi_sla_abort(struct spi_controller *_c)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(_c);

	complete(&pspim->sla_isr);
	complete(&pspim->isr_done);
	return 0;
}

// slave R/W, called from S_transfer_one() only
int sp7021_spi_sla_rw(struct spi_device *_s, struct spi_transfer *_t, int RW_phase)
{
	struct sp7021_spi_ctlr *pspim = spi_controller_get_devdata(_s->controller);
	struct device *devp = &(_s->dev);
	int err = 0;


	mutex_lock(&pspim->buf_lock);

	if (RW_phase == SP7021_SLA_WRITE) {
		reinit_completion(&pspim->sla_isr);

		if (_t->tx_dma == pspim->tx_dma_phy_base)
			memcpy(pspim->tx_dma_vir_base, _t->tx_buf, _t->len);

		writel_relaxed(SP7021_SLA_DMA_WRITE, pspim->sla_base + SP7021_SLV_DMA_CTRL_REG);
		writel_relaxed(_t->len, pspim->sla_base + SP7021_SLV_DMA_LENGTH_REG);
		writel_relaxed(_t->tx_dma, pspim->sla_base + SP7021_SLV_DMA_ADDR_REG);
		writel(readl(pspim->sla_base + SP7021_DATA_RDY_REG) | SP7021_SLA_DATA_RDY,
				pspim->sla_base + SP7021_DATA_RDY_REG);

		if (wait_for_completion_interruptible(&pspim->sla_isr))
			dev_err(devp, "%s() wait_for_completion timeout\n", __func__);

	} else if (RW_phase == SP7021_SLA_READ) {
		reinit_completion(&pspim->isr_done);
		writel(SP7021_SLA_DMA_READ, pspim->sla_base + SP7021_SLV_DMA_CTRL_REG);
		writel(_t->len, pspim->sla_base + SP7021_SLV_DMA_LENGTH_REG);
		writel(_t->rx_dma, pspim->sla_base + SP7021_SLV_DMA_ADDR_REG);

		// wait for DMA to complete
		if (wait_for_completion_interruptible(&pspim->isr_done)) {
			dev_err(devp, "%s() wait_for_completion timeout\n", __func__);
			err = -ETIMEDOUT;
			goto exit_spi_slave_rw;
		}
		// FIXME: is "len" correct there?
		if (_t->tx_dma == pspim->tx_dma_phy_base)
			memcpy(_t->rx_buf, pspim->rx_dma_vir_base, _t->len);

		writel(SP7021_SLA_SW_RST, pspim->sla_base + SP7021_SLV_DMA_CTRL_REG);
	}

exit_spi_slave_rw:
	mutex_unlock(&pspim->buf_lock);
	return err;
}

// spi master irq handler
static irqreturn_t sp7021_spi_mas_irq_dma(int _irq, void *_dev)
{
	struct sp7021_spi_ctlr *pspim = (struct sp7021_spi_ctlr *)_dev;

	spin_lock_irq(&pspim->lock);
	writel(readl(pspim->mas_base + SP7021_DMA_CTRL_REG) | SP7021_CLR_MAS_W_INT,
		pspim->mas_base + SP7021_DMA_CTRL_REG);
	spin_unlock_irq(&pspim->lock);
	complete(&pspim->isr_done);
	return IRQ_HANDLED;
}

void sp7021spi_rb(struct sp7021_spi_ctlr *_m, u8 _len)
{
	int i;

	for (i = 0; i < _len; i++) {
		_m->rx_data_buf[_m->rx_cur_len] = readl(_m->mas_base + SP7021_FIFO_REG);
		_m->rx_cur_len++;
	}
}
void sp7021spi_wb(struct sp7021_spi_ctlr *_m, u8 _len)
{
	int i;

	for (i = 0; i < _len; i++) {
		writel(_m->tx_data_buf[_m->tx_cur_len], _m->mas_base + SP7021_FIFO_REG);
		_m->tx_cur_len++;
	}
}

static irqreturn_t sp7021_spi_mas_irq(int _irq, void *_dev)
{
	unsigned long flags;
	struct sp7021_spi_ctlr *pspim = (struct sp7021_spi_ctlr *)_dev;
	u32 fd_status = 0;
	unsigned int tx_len, rx_cnt, tx_cnt;
	bool isrdone = false;

	spin_lock_irqsave(&pspim->lock, flags);

	fd_status = readl(pspim->mas_base + SP7021_SPI_STATUS_REG);
	tx_cnt = SP7021_GET_TX_CNT(fd_status);
	tx_len = SP7021_GET_TX_LEN(fd_status);

	if ((fd_status & SP7021_TX_EMP_FLAG) && (fd_status & SP7021_RX_EMP_FLAG)
				&& (SP7021_GET_LEN(fd_status) == 0))
		goto fin_irq;

	rx_cnt = SP7021_GET_RX_CNT(fd_status);
	// SP7021_RX_FULL_FLAG means RX buffer is full (16 bytes)
	if (fd_status & SP7021_RX_FULL_FLAG)
		rx_cnt = pspim->data_unit;

	tx_cnt = min(tx_len - pspim->tx_cur_len, pspim->data_unit - tx_cnt);

	dev_dbg(pspim->dev, "fd_st=0x%x rx_c:%d tx_c:%d tx_l:%d",
			fd_status, rx_cnt, tx_cnt, tx_len);

	if (rx_cnt > 0)
		sp7021spi_rb(pspim, rx_cnt);
	if (tx_cnt > 0)
		sp7021spi_wb(pspim, tx_cnt);

	fd_status = readl(pspim->mas_base + SP7021_SPI_STATUS_REG);

	if ((fd_status & SP7021_FINISH_FLAG) ||
		(SP7021_GET_TX_LEN(fd_status) == pspim->tx_cur_len)) {

		while (SP7021_GET_LEN(fd_status) != pspim->rx_cur_len) {
			fd_status = readl(pspim->mas_base + SP7021_SPI_STATUS_REG);
			if (fd_status & SP7021_RX_FULL_FLAG)
				rx_cnt = pspim->data_unit;
			else
				rx_cnt = SP7021_GET_RX_CNT(fd_status);

			if (rx_cnt > 0)
				sp7021spi_rb(pspim, rx_cnt);
		}
		writel(readl(pspim->mas_base + SP7021_INT_BUSY_REG) | SP7021_CLR_MAS_INT,
				pspim->mas_base + SP7021_INT_BUSY_REG);
		isrdone = true;
	}
fin_irq:
	if (isrdone)
		complete(&pspim->isr_done);
	spin_unlock_irqrestore(&pspim->lock, flags);
	return IRQ_HANDLED;
}

// called in *controller_transfer_one*
static void sp7021_prep_transfer(struct spi_controller *_c, struct spi_device *_s)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(_c);

	pspim->tx_cur_len = 0;
	pspim->rx_cur_len = 0;
	pspim->data_unit = SP7021_FIFO_DATA_BITS / _s->bits_per_word;
	pspim->isr_flag = SP7021_SPI_IDLE;
}

// called from *transfer* functions, set clock there
static void sp7021_spi_setup_transfer(struct spi_device *_s,
					struct spi_controller *_c, struct spi_transfer *_t)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(_c);
	u32 rc = 0, rs = 0;
	unsigned int clk_rate;
	unsigned int div;
	unsigned int clk_sel;

	dev_dbg(&(_s->dev), "setup %dHz", _s->max_speed_hz);
	dev_dbg(&(_s->dev), "tx %p, rx %p, len %d", _t->tx_buf, _t->rx_buf, _t->len);
	// set clock
	clk_rate = clk_get_rate(pspim->spi_clk);
	div = clk_rate / _t->speed_hz;

	dev_dbg(&(_s->dev), "clk_rate: %d div %d", clk_rate, div);

	clk_sel = (div / 2) - 1;
	// set full duplex (bit 6) and fd freq (bits 31:16)
	rc = SP7021_FD_SEL | (0xffff << 16);
	rs = SP7021_FD_SEL | ((clk_sel & 0xffff) << 16);
	writel((pspim->message_config & ~rc) | rs, pspim->mas_base + SP7021_SPI_CONFIG_REG);
}

static int sp7021_spi_master_combine_write_read(struct spi_controller *_c,
			struct spi_transfer *first, unsigned int transfers_cnt)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(_c);
	unsigned int data_len = 0;
	u32 reg_temp = 0;
	unsigned long timeout = msecs_to_jiffies(200);
	unsigned int i, len_temp;
	int ret;
	struct spi_transfer *t = first;
	bool xfer_rx = false;

	memset(&pspim->tx_data_buf[0], 0, SP7021_SPI_DATA_SIZE);
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
	reg_temp = readl(pspim->mas_base + SP7021_SPI_CONFIG_REG);
	reg_temp &= SP7021_CLEAN_RW_BYTE;
	reg_temp &= SP7021_CLEAN_FLUG_MASK;
	reg_temp |= SP7021_FD_SEL;
	// set SPI master config for full duplex (SPI_FD_CONFIG)  91.15
	reg_temp |= SP7021_FINISH_FLAG_MASK | SP7021_TX_EMP_FLAG_MASK | SP7021_RX_FULL_FLAG_MASK;
	reg_temp |= SP7021_WRITE_BYTE(0) | SP7021_READ_BYTE(0);  // set read write byte from fifo
	writel(reg_temp, pspim->mas_base + SP7021_SPI_CONFIG_REG);
	// set SPI STATUS and start SPI for full duplex (SPI_FD_STATUS)  91.13
	writel(SP7021_TOTAL_LENGTH(data_len) | SP7021_TX_LENGTH(data_len) | SP7021_SPI_START_FD,
				pspim->mas_base + SP7021_SPI_STATUS_REG);
	writel(readl(pspim->mas_base + SP7021_INT_BUSY_REG) | SP7021_INT_BYPASS,
				pspim->mas_base + SP7021_INT_BUSY_REG);

	if (!wait_for_completion_timeout(&pspim->isr_done, timeout)) {
		dev_dbg(&(_c->dev), "wait_for_completion timeout");
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
	writel(readl(pspim->mas_base + SP7021_SPI_CONFIG_REG) & SP7021_CLEAN_FLUG_MASK,
					pspim->mas_base + SP7021_SPI_CONFIG_REG);

	if (pspim->message_config & SP7021_CPOL_FD)
		writel(pspim->message_config, pspim->mas_base + SP7021_SPI_CONFIG_REG);

	mutex_unlock(&pspim->buf_lock);
	return ret;
}

static int sp7021_spi_master_transfer(struct spi_controller *_c, struct spi_device *_s,
		struct spi_transfer *xfer)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(_c);
	u32 reg_temp = 0;
	unsigned long timeout = msecs_to_jiffies(200);
	unsigned int i;
	int ret;
	long cret;
	unsigned int xfer_cnt, xfer_len, last_len;
	struct spi_transfer *t = xfer;

	xfer_cnt = t->len / SP7021_SPI_DATA_SIZE;
	last_len = t->len % SP7021_SPI_DATA_SIZE;

	memset(&pspim->tx_data_buf[0], 0, SP7021_SPI_DATA_SIZE);

	dev_dbg(&(_s->dev), "txrx: tx %p, rx %p, len %d\n", t->tx_buf, t->rx_buf, t->len);

	for (i = 0; i <= xfer_cnt; i++) {

		mutex_lock(&pspim->buf_lock);

		sp7021_prep_transfer(_c, _s);
		sp7021_spi_setup_transfer(_s, _c, xfer);

		reinit_completion(&pspim->isr_done);

		if (i == xfer_cnt)
			xfer_len = last_len;
		else
			xfer_len = SP7021_SPI_DATA_SIZE;

		if (t->tx_buf)
			memcpy(pspim->tx_data_buf, t->tx_buf+i*SP7021_SPI_DATA_SIZE, xfer_len);

		// set SPI FIFO data for full duplex (SPI_FD fifo_data)  91.13
		if (pspim->tx_cur_len < xfer_len) {
			reg_temp = min(pspim->data_unit, xfer_len);
			sp7021spi_wb(pspim, reg_temp);
		}

		// initial SPI master config and change to Full-Duplex mode (SPI_FD_CONFIG)  91.15
		reg_temp = readl(pspim->mas_base + SP7021_SPI_CONFIG_REG);
		reg_temp &= SP7021_CLEAN_RW_BYTE;
		reg_temp &= SP7021_CLEAN_FLUG_MASK;
		reg_temp |= SP7021_FD_SEL;
		// set SPI master config for full duplex (SPI_FD_CONFIG)  91.15
		reg_temp |= SP7021_FINISH_FLAG_MASK |
				SP7021_TX_EMP_FLAG_MASK | SP7021_RX_FULL_FLAG_MASK;
		reg_temp |= SP7021_WRITE_BYTE(0) | SP7021_READ_BYTE(0);
		writel(reg_temp, pspim->mas_base + SP7021_SPI_CONFIG_REG);
		dev_dbg(&(_s->dev), "SP7021_SPI_CONFIG_REG =0x%x",
			readl(pspim->mas_base + SP7021_SPI_CONFIG_REG));
		// set SPI STATUS and start SPI for full duplex (SPI_FD_STATUS)  91.13
		dev_dbg(&(_s->dev), "SP7021_TOTAL_LENGTH =0x%x  SP7021_TX_LENGTH =0x%x ",
				SP7021_TOTAL_LENGTH(xfer_len), SP7021_TX_LENGTH(xfer_len));
		writel(SP7021_TOTAL_LENGTH(xfer_len) | SP7021_TX_LENGTH(xfer_len)
			| SP7021_SPI_START_FD, pspim->mas_base + SP7021_SPI_STATUS_REG);

		cret = wait_for_completion_interruptible_timeout(&pspim->isr_done, timeout);
		if (cret <= 0) {
			dev_dbg(&(_s->dev), "wait_for_completion cret=%ld\n", cret);
			writel(readl(pspim->mas_base + SP7021_SPI_CONFIG_REG) &
				SP7021_CLEAN_FLUG_MASK, pspim->mas_base + SP7021_SPI_CONFIG_REG);
			ret = 1;
			goto free_master_combite_rw;
		}

		reg_temp = readl(pspim->mas_base + SP7021_SPI_STATUS_REG);
		if (reg_temp & SP7021_FINISH_FLAG)
			writel(readl(pspim->mas_base + SP7021_SPI_CONFIG_REG) &
				SP7021_CLEAN_FLUG_MASK, pspim->mas_base + SP7021_SPI_CONFIG_REG);

		if (t->rx_buf)
			memcpy(t->rx_buf+i*SP7021_SPI_DATA_SIZE, pspim->rx_data_buf, xfer_len);

		ret = 0;

free_master_combite_rw:

		if (pspim->message_config & SP7021_CPOL_FD)
			writel(pspim->message_config, pspim->mas_base + SP7021_SPI_CONFIG_REG);

		mutex_unlock(&pspim->buf_lock);
	}

	return ret;
}

// called when child device is registering on the bus
static int sp7021_spi_dev_setup(struct spi_device *_s)
{
	struct sp7021_spi_ctlr *pspim = spi_controller_get_devdata(_s->controller);
	int ret;

	ret = pm_runtime_get_sync(pspim->dev);
		if (ret < 0)
			return 0;

	pm_runtime_put(pspim->dev);

	return 0;

}

// preliminary set CS, CPOL, CPHA and LSB
// called for both - master and slave. See drivers/spi/spi.c
static int sp7021_spi_controller_prepare_message(struct spi_controller *_c,
				    struct spi_message *_m)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(_c);
	struct spi_device *s = _m->spi;
	// reg clear bits and set bits
	u32 rs = 0;

	dev_dbg(&(s->dev), "setup %d bpw, %scpol, %scpha, %scs-high, %slsb-first %xcs_gpio\n",
		s->bits_per_word,
		s->mode & SPI_CPOL ? "" : "~",
		s->mode & SPI_CPHA ? "" : "~",
		s->mode & SPI_CS_HIGH ? "" : "~",
		s->mode & SPI_LSB_FIRST ? "" : "~",
		s->cs_gpio);

	writel(readl(pspim->mas_base + SP7021_SPI_STATUS_REG) | SP7021_FD_SW_RST,
					pspim->mas_base + SP7021_SPI_STATUS_REG);

	//set up full duplex frequency and enable  full duplex
	rs = SP7021_FD_SEL | ((0xffff) << 16);

	if (s->mode & SPI_CPOL)
		rs |= SP7021_CPOL_FD;

	if (s->mode & SPI_LSB_FIRST)
		rs |= SP7021_LSB_SEL;

	if (s->mode & SPI_CS_HIGH)
		rs |= SP7021_CS_POR;

	if (s->mode & SPI_CPHA)
		rs |=  SP7021_CPHA_R;
	else
		rs |=  SP7021_CPHA_W;

	rs |= SP7021_WRITE_BYTE(0) | SP7021_READ_BYTE(0);  // set read write byte from fifo

	pspim->message_config = rs;

	if (pspim->message_config & SP7021_CPOL_FD)
		writel(pspim->message_config, pspim->mas_base + SP7021_SPI_CONFIG_REG);

	return 0;
}

static int sp7021_spi_controller_unprepare_message(struct spi_controller *_c,
				    struct spi_message *msg)
{
	return 0;
}

static size_t sp7021_spi_max_length(struct spi_device *spi)
{
	return SP7021_SPI_MSG_SIZE;
}



// SPI-slave R/W
static int sp7021_spi_sla_transfer_one(struct spi_controller *_c, struct spi_device *_s,
					struct spi_transfer *_t)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(_c);
	struct device *dev = pspim->dev;
	int mode = SP7021_SPI_IDLE, ret = 0;

	pm_runtime_get_sync(pspim->dev);

	if (spi_controller_is_slave(_c)) {

		pspim->isr_flag = SP7021_SPI_IDLE;

		if ((_t->tx_buf) && (_t->rx_buf)) {
			dev_dbg(&_c->dev, "%s() wrong command\n", __func__);
			ret = -EINVAL;
		} else if (_t->tx_buf) {
			_t->tx_dma = dma_map_single(dev, (void *)_t->tx_buf,
						_t->len, DMA_TO_DEVICE);

			if (dma_mapping_error(dev, _t->tx_dma)) {
				if (_t->len <= SP7021_BUFSIZE) {
					_t->tx_dma = pspim->tx_dma_phy_base;
					mode = SP7021_SLA_WRITE;
				} else
					mode = SP7021_SPI_IDLE;

			} else
				mode = SP7021_SLA_WRITE;
		} else if (_t->rx_buf) {

			_t->rx_dma = dma_map_single(dev, _t->rx_buf,
				_t->len, DMA_FROM_DEVICE);

			if (dma_mapping_error(dev, _t->rx_dma)) {
				if (_t->len <= SP7021_BUFSIZE) {
					_t->rx_dma = pspim->rx_dma_phy_base;
					mode = SP7021_SLA_READ;
				} else
					mode = SP7021_SPI_IDLE;
			} else
				mode = SP7021_SLA_READ;
		}

		switch (mode) {
		case SP7021_SLA_WRITE:
		case SP7021_SLA_READ:
			sp7021_spi_sla_rw(_s, _t, mode);
			break;
		default:
			break;
		}

		if ((_t->tx_buf) && (_t->tx_dma != pspim->tx_dma_phy_base))
			dma_unmap_single(dev, _t->tx_dma,
				_t->len, DMA_TO_DEVICE);

		if ((_t->rx_buf) && (_t->rx_dma != pspim->rx_dma_phy_base))
			dma_unmap_single(dev, _t->rx_dma,
				_t->len, DMA_FROM_DEVICE);

	}

	spi_finalize_current_transfer(_c);

	pm_runtime_put(pspim->dev);
	return ret;

}

static int sp7021_spi_mas_transfer_one_message(struct spi_controller *_c, struct spi_message *m)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(_c);
	struct spi_device *spi = m->spi;
	unsigned int xfer_cnt = 0, total_len = 0;
	bool start_xfer = false;
	struct spi_transfer *xfer, *first_xfer = NULL;
	int ret;
	bool keep_cs = false;

	pm_runtime_get_sync(pspim->dev);

	sp7021_spi_set_cs(spi, true);

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

		if (xfer->len > SP7021_SPI_MSG_SIZE) {
			dev_err(&(spi->dev), "over total trans-length xfer->len = %d", xfer->len);
			ret = -EINVAL;
			break;
		}

		if (list_is_last(&xfer->transfer_list, &m->transfers))
			dev_dbg(&(spi->dev), "xfer = transfer_list");
		if (total_len > SP7021_SPI_DATA_SIZE)
			dev_dbg(&(spi->dev), "(total_len > SP7021_SPI_DATA_SIZE)");
		if (xfer->cs_change)
			dev_dbg(&(spi->dev), "xfer->cs_change");

		if (list_is_last(&xfer->transfer_list, &m->transfers)
			|| (total_len > SP7021_SPI_DATA_SIZE)
			|| xfer->cs_change || (xfer->len > SP7021_SPI_DATA_SIZE))
			start_xfer = true;


		dev_dbg(&(spi->dev), "start_xfer:%d total_len:%d\n", start_xfer, total_len);
		if (start_xfer != true) {
			xfer_cnt++;
			continue;
		}
		if (total_len <= SP7021_SPI_DATA_SIZE)
			xfer_cnt++;

		if (xfer_cnt > 0) {
			sp7021_prep_transfer(_c, spi);
			sp7021_spi_setup_transfer(spi, _c, first_xfer);
			ret = sp7021_spi_master_combine_write_read(_c, first_xfer, xfer_cnt);
		}

		if (total_len > SP7021_SPI_DATA_SIZE)
			ret = sp7021_spi_master_transfer(_c, spi, xfer);

		if (xfer->delay.value)
			spi_delay_to_ns(&xfer->delay, xfer);

		if (xfer->cs_change) {
			if (list_is_last(&xfer->transfer_list, &m->transfers))
				keep_cs = true;
			else {
				sp7021_spi_set_cs(spi, false);
				spi_delay_to_ns(&xfer->cs_change_delay, xfer);
				sp7021_spi_set_cs(spi, true);
			}
		}

		m->actual_length += total_len;

		first_xfer = NULL;
		xfer_cnt = 0;
		total_len = 0;
		start_xfer = false;
	}

	if (ret != 0 || !keep_cs)
		sp7021_spi_set_cs(spi, false);
	m->status = ret;
	spi_finalize_current_message(_c);


	pm_runtime_put(pspim->dev);

	return ret;

	pm_runtime_mark_last_busy(pspim->dev);
	pm_runtime_put_autosuspend(pspim->dev);
	return 0;

}

static int sp7021_spi_controller_probe(struct platform_device *pdev)
{
	int ret;
	int mode;
	struct spi_controller *ctlr;
	struct sp7021_spi_ctlr *pspim;

	pdev->id = 0;
	mode = SP7021_MASTER_MODE;
	if (pdev->dev.of_node) {
		pdev->id = of_alias_get_id(pdev->dev.of_node, "sp_spi");
		if (of_property_read_bool(pdev->dev.of_node, "spi-slave"))
			mode = SP7021_SLAVE_MODE;
	}
	dev_dbg(&pdev->dev, "pdev->id = %d\n", pdev->id);

	if (mode == SP7021_SLAVE_MODE)
		ctlr = spi_alloc_slave(&pdev->dev, sizeof(*pspim));
	else
		ctlr = spi_alloc_master(&pdev->dev, sizeof(*pspim));
	if (!ctlr)
		return -ENOMEM;

	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->bus_num = pdev->id;
	// flags, understood by the driver
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;
	ctlr->max_transfer_size = sp7021_spi_max_length;
	ctlr->max_message_size = sp7021_spi_max_length;
	ctlr->setup = sp7021_spi_dev_setup;
	// FIXME: ctlr->auto_runtime_pm = true;
	ctlr->prepare_message = sp7021_spi_controller_prepare_message;
	ctlr->unprepare_message = sp7021_spi_controller_unprepare_message;

	if (mode == SP7021_SLAVE_MODE) {
		ctlr->transfer_one = sp7021_spi_sla_transfer_one;
		ctlr->slave_abort = sp7021_spi_sla_abort;
		ctlr->flags = SPI_CONTROLLER_HALF_DUPLEX;
	} else {
		ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
		ctlr->min_speed_hz = 40000;
		ctlr->max_speed_hz = 25000000;
		ctlr->use_gpio_descriptors = true;
		ctlr->transfer_one_message = sp7021_spi_mas_transfer_one_message;
	}

	dev_set_drvdata(&pdev->dev, ctlr);
	pspim = spi_controller_get_devdata(ctlr);

	pspim->ctlr = ctlr;
	pspim->dev = &pdev->dev;

	spin_lock_init(&pspim->lock);
	mutex_init(&pspim->buf_lock);
	init_completion(&pspim->isr_done);
	init_completion(&pspim->sla_isr);

	pspim->mas_base = devm_platform_ioremap_resource_byname(pdev, SP7021_MAS_REG_NAME);
	pspim->sla_base = devm_platform_ioremap_resource_byname(pdev, SP7021_SLA_REG_NAME);

	dev_dbg(&pdev->dev, "mas_base 0x%x\n", (unsigned int)pspim->mas_base);

	/* irq*/
	pspim->dma_irq = platform_get_irq_byname(pdev, SP7021_DMA_IRQ_NAME);
	if (pspim->dma_irq < 0) {
		dev_err(&pdev->dev, "failed to get %s\n", SP7021_DMA_IRQ_NAME);
		goto free_alloc;
	}

	pspim->mas_irq = platform_get_irq_byname(pdev, SP7021_MAS_IRQ_NAME);
	if (pspim->mas_irq < 0) {
		dev_err(&pdev->dev, "failed to get %s\n", SP7021_MAS_IRQ_NAME);
		goto free_alloc;
	}

	pspim->sla_irq = platform_get_irq_byname(pdev, SP7021_SLA_IRQ_NAME);
	if (pspim->sla_irq < 0) {
		dev_err(&pdev->dev, "failed to get %s\n", SP7021_SLA_IRQ_NAME);
		goto free_alloc;
	}

	ret = devm_request_irq(&pdev->dev, pspim->dma_irq, sp7021_spi_mas_irq_dma
						, IRQF_TRIGGER_RISING, pdev->name, pspim);
	if (ret) {
		dev_err(&pdev->dev, "%s devm_request_irq fail\n", SP7021_DMA_IRQ_NAME);
		goto free_alloc;
	}

	/* requset irq*/
	ret = devm_request_irq(&pdev->dev, pspim->mas_irq, sp7021_spi_mas_irq
						, IRQF_TRIGGER_RISING, pdev->name, pspim);
	if (ret) {
		dev_err(&pdev->dev, "%s devm_request_irq fail\n", SP7021_MAS_IRQ_NAME);
		goto free_alloc;
	}

	ret = devm_request_irq(&pdev->dev, pspim->sla_irq, sp7021_spi_sla_irq
						, IRQF_TRIGGER_RISING, pdev->name, pspim);
	if (ret) {
		dev_err(&pdev->dev, "%s devm_request_irq fail\n", SP7021_SLA_IRQ_NAME);
		goto free_alloc;
	}

	/* dma alloc*/
	pspim->tx_dma_vir_base = dmam_alloc_coherent(&pdev->dev, SP7021_BUFSIZE,
					&pspim->tx_dma_phy_base, GFP_ATOMIC);
	if (!pspim->tx_dma_vir_base)
		goto free_alloc;

	dev_dbg(&pdev->dev, "tx_dma vir 0x%x\n", (unsigned int)pspim->tx_dma_vir_base);
	dev_dbg(&pdev->dev, "tx_dma phy 0x%x\n", (unsigned int)pspim->tx_dma_phy_base);

	pspim->rx_dma_vir_base = dmam_alloc_coherent(&pdev->dev, SP7021_BUFSIZE,
					&pspim->rx_dma_phy_base, GFP_ATOMIC);
	if (!pspim->rx_dma_vir_base)
		goto free_tx_dma;

	dev_dbg(&pdev->dev, "rx_dma vir 0x%x\n", (unsigned int)pspim->rx_dma_vir_base);
	dev_dbg(&pdev->dev, "rx_dma phy 0x%x\n", (unsigned int)pspim->rx_dma_phy_base);

	/* clk*/
	pspim->spi_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pspim->spi_clk)) {
		dev_err(&pdev->dev, "devm_clk_get fail\n");
		goto free_rx_dma;
	}

	/* reset*/
	pspim->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	dev_dbg(&pdev->dev, "pspim->rstc : 0x%x\n", (unsigned int)pspim->rstc);
	if (IS_ERR(pspim->rstc)) {
		ret = PTR_ERR(pspim->rstc);
		dev_err(&pdev->dev, "SPI failed to retrieve reset controller: %d\n", ret);
		goto free_reset_assert;
	}

	ret = clk_prepare_enable(pspim->spi_clk);
	if (ret)
		goto free_reset_assert;

	ret = reset_control_deassert(pspim->rstc);
	if (ret)
		goto free_reset_assert;

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret != 0) {
		dev_err(&pdev->dev, "spi_register_master fail\n");
		goto free_reset_assert;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, 5000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	dev_dbg(&pdev->dev, "pm init done\n");

	return 0;

free_reset_assert:
	reset_control_assert(pspim->rstc);
	clk_disable_unprepare(pspim->spi_clk);
free_rx_dma:
	dma_free_coherent(&pdev->dev, SP7021_BUFSIZE,
		pspim->rx_dma_vir_base, pspim->rx_dma_phy_base);
free_tx_dma:
	dma_free_coherent(&pdev->dev, SP7021_BUFSIZE,
		pspim->tx_dma_vir_base, pspim->tx_dma_phy_base);
free_alloc:
	spi_controller_put(ctlr);

	dev_dbg(&pdev->dev, "spi_master_probe done\n");
	return ret;
}

static int sp7021_spi_controller_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = dev_get_drvdata(&pdev->dev);
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	dma_free_coherent(&pdev->dev, SP7021_BUFSIZE,
		pspim->tx_dma_vir_base, pspim->tx_dma_phy_base);
	dma_free_coherent(&pdev->dev, SP7021_BUFSIZE,
		pspim->rx_dma_vir_base, pspim->rx_dma_phy_base);

	spi_unregister_master(pspim->ctlr);
	clk_disable_unprepare(pspim->spi_clk);
	reset_control_assert(pspim->rstc);

	return 0;
}

static int __maybe_unused sp7021_spi_controller_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	reset_control_assert(pspim->rstc);

	return 0;
}

static int __maybe_unused sp7021_spi_controller_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	reset_control_deassert(pspim->rstc);
	clk_prepare_enable(pspim->spi_clk);

	return 0;
}

static int sp7021_spi_runtime_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	dev_dbg(dev, "devid:%d\n", dev->id);
	reset_control_assert(pspim->rstc);

	return 0;
}

static int sp7021_spi_runtime_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	dev_dbg(dev, "devid:%d\n", dev->id);
	reset_control_deassert(pspim->rstc);
	clk_prepare_enable(pspim->spi_clk);

	return 0;
}

static const struct dev_pm_ops sp7021_spi_pm_ops = {
	SET_RUNTIME_PM_OPS(sp7021_spi_runtime_suspend,
				sp7021_spi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(sp7021_spi_controller_suspend,
				sp7021_spi_controller_resume)
};

static const struct of_device_id sp7021_spi_controller_ids[] = {
	{ .compatible = "sunplus,sp7021-spi-controller" },
	{}
};
MODULE_DEVICE_TABLE(of, sp7021_spi_controller_ids);

static struct platform_driver sp7021_spi_controller_driver = {
	.probe = sp7021_spi_controller_probe,
	.remove = sp7021_spi_controller_remove,
	.driver = {
		.name = "sunplus,sp7021-spi-controller",
		.of_match_table = sp7021_spi_controller_ids,
		.pm     = &sp7021_spi_pm_ops,
	},
};
module_platform_driver(sp7021_spi_controller_driver);

MODULE_AUTHOR("lH Kuo <lh.kuo@sunplus.com>");
MODULE_DESCRIPTION("Sunplus SPI controller driver");
MODULE_LICENSE("GPL v2");
