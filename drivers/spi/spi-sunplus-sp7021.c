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

#define SP7021_MAS_IRQ_NAME "mas_risc_intr"
#define SP7021_SLA_IRQ_NAME "slave_risc_intr"

#define SP7021_SPI_DATA_SIZE (255)

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

enum SPI_MODE {
	SP7021_SLA_READ = 0,
	SP7021_SLA_WRITE = 1,
	SP7021_SPI_IDLE = 2
};

enum {
	SP7021_MASTER_MODE,
	SP7021_SLAVE_MODE,
};

struct sp7021_spi_ctlr {

	struct device *dev;
	int mode;
	struct spi_controller *ctlr;

	void __iomem *mas_base;
	void __iomem *sla_base;

	u32 xfer_conf;

	int mas_irq;
	int sla_irq;

	struct clk *spi_clk;
	struct reset_control *rstc;

	spinlock_t lock;
	struct mutex buf_lock;

	struct completion isr_done;
	struct completion sla_isr;

	unsigned int  rx_cur_len;
	unsigned int  tx_cur_len;

	const u8 *tx_buf;
	u8 *rx_buf;

	unsigned int  data_unit;
};

// spi slave irq handler
static irqreturn_t sp7021_spi_sla_irq(int irq, void *dev)
{
	struct sp7021_spi_ctlr *pspim = dev;
	unsigned int data_status;

	data_status = readl(pspim->sla_base + SP7021_DATA_RDY_REG);
	writel(data_status | SP7021_SLA_CLR_INT,
			pspim->sla_base + SP7021_DATA_RDY_REG);

	complete(&pspim->sla_isr);
	return IRQ_NONE;
}

// slave only. usually called on driver remove
static int sp7021_spi_sla_abort(struct spi_controller *ctlr)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	complete(&pspim->sla_isr);
	complete(&pspim->isr_done);
	return 0;
}

// slave R/W, called from S_transfer_one() only
int sp7021_spi_sla_tx(struct spi_device *spi, struct spi_transfer *xfer)
{
	struct sp7021_spi_ctlr *pspim = spi_controller_get_devdata(spi->controller);
	struct device *devp = &(spi->dev);
	int err = 0;

	mutex_lock(&pspim->buf_lock);

	reinit_completion(&pspim->sla_isr);

	writel_relaxed(SP7021_SLA_DMA_WRITE, pspim->sla_base + SP7021_SLV_DMA_CTRL_REG);
	writel_relaxed(xfer->len, pspim->sla_base + SP7021_SLV_DMA_LENGTH_REG);
	writel_relaxed(xfer->tx_dma, pspim->sla_base + SP7021_SLV_DMA_ADDR_REG);
	writel(readl(pspim->sla_base + SP7021_DATA_RDY_REG) | SP7021_SLA_DATA_RDY,
			pspim->sla_base + SP7021_DATA_RDY_REG);

	if (wait_for_completion_interruptible(&pspim->sla_isr))
		dev_err(devp, "%s() wait_for_completion timeout\n", __func__);

	mutex_unlock(&pspim->buf_lock);
	return err;
}

int sp7021_spi_sla_rx(struct spi_device *spi, struct spi_transfer *xfer)
{
	struct sp7021_spi_ctlr *pspim = spi_controller_get_devdata(spi->controller);
	struct device *devp = &(spi->dev);
	int err = 0;

	mutex_lock(&pspim->buf_lock);

	reinit_completion(&pspim->isr_done);
	writel(SP7021_SLA_DMA_READ, pspim->sla_base + SP7021_SLV_DMA_CTRL_REG);
	writel(xfer->len, pspim->sla_base + SP7021_SLV_DMA_LENGTH_REG);
	writel(xfer->rx_dma, pspim->sla_base + SP7021_SLV_DMA_ADDR_REG);

	// wait for DMA to complete
	if (wait_for_completion_interruptible(&pspim->isr_done)) {
		dev_err(devp, "%s() wait_for_completion timeout\n", __func__);
		err = -ETIMEDOUT;
		goto exit_spi_slave_rw;
	}

	writel(SP7021_SLA_SW_RST, pspim->sla_base + SP7021_SLV_DMA_CTRL_REG);

exit_spi_slave_rw:
	mutex_unlock(&pspim->buf_lock);
	return err;

}

void sp7021_spi_mas_rb(struct sp7021_spi_ctlr *pspim, u8 len)
{
	int i;

	for (i = 0; i < len; i++) {
		pspim->rx_buf[pspim->rx_cur_len] =
			readl(pspim->mas_base + SP7021_FIFO_REG);
		pspim->rx_cur_len++;
	}
}

void sp7021_spi_mas_wb(struct sp7021_spi_ctlr *pspim, u8 len)
{
	int i;

	for (i = 0; i < len; i++) {
		writel(pspim->tx_buf[pspim->tx_cur_len],
			pspim->mas_base + SP7021_FIFO_REG);
		pspim->tx_cur_len++;
	}
}

static irqreturn_t sp7021_spi_mas_irq(int irq, void *dev)
{
	unsigned long flags;
	struct sp7021_spi_ctlr *pspim = dev;
	u32 fd_status = 0;
	unsigned int tx_len, rx_cnt, tx_cnt, total_len;
	bool isrdone = false;

	fd_status = readl(pspim->mas_base + SP7021_SPI_STATUS_REG);
	tx_cnt = SP7021_GET_TX_CNT(fd_status);
	tx_len = SP7021_GET_TX_LEN(fd_status);
	total_len = SP7021_GET_LEN(fd_status);

	if ((fd_status & SP7021_TX_EMP_FLAG) &&
		(fd_status & SP7021_RX_EMP_FLAG) && (total_len == 0))
		return IRQ_NONE;

	if ((tx_len == 0) && (total_len == 0))
		return IRQ_NONE;

	spin_lock_irqsave(&pspim->lock, flags);

	rx_cnt = SP7021_GET_RX_CNT(fd_status);
	// SP7021_RX_FULL_FLAG means RX buffer is full (16 bytes)
	if (fd_status & SP7021_RX_FULL_FLAG)
		rx_cnt = pspim->data_unit;

	tx_cnt = min(tx_len - pspim->tx_cur_len, pspim->data_unit - tx_cnt);

	dev_dbg(pspim->dev, "fd_st=0x%x rx_c:%d tx_c:%d tx_l:%d",
			fd_status, rx_cnt, tx_cnt, tx_len);

	if (rx_cnt > 0)
		sp7021_spi_mas_rb(pspim, rx_cnt);
	if (tx_cnt > 0)
		sp7021_spi_mas_wb(pspim, tx_cnt);

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
				sp7021_spi_mas_rb(pspim, rx_cnt);
		}
		writel(readl(pspim->mas_base + SP7021_INT_BUSY_REG)
			| SP7021_CLR_MAS_INT, pspim->mas_base + SP7021_INT_BUSY_REG);
		writel(SP7021_FINISH_FLAG, pspim->mas_base + SP7021_SPI_STATUS_REG);

		isrdone = true;
	}

	if (isrdone)
		complete(&pspim->isr_done);
	spin_unlock_irqrestore(&pspim->lock, flags);
	return IRQ_HANDLED;
}

// called in *controller_transfer_one*
static void sp7021_prep_transfer(struct spi_controller *ctlr,
			struct spi_device *spi)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	pspim->tx_cur_len = 0;
	pspim->rx_cur_len = 0;
	pspim->data_unit = SP7021_FIFO_DATA_BITS / spi->bits_per_word;
}

// called from *transfer* functions, set clock there
static void sp7021_spi_setup_transfer(struct spi_device *spi,
					struct spi_controller *ctlr, struct spi_transfer *xfer)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);
	u32 rc = 0, rs = 0;
	unsigned int clk_rate;
	unsigned int div;
	unsigned int clk_sel;

	// set clock
	clk_rate = clk_get_rate(pspim->spi_clk);
	div = clk_rate / xfer->speed_hz;

	clk_sel = (div / 2) - 1;
	// set full duplex (bit 6) and fd freq (bits 31:16)
	rc = SP7021_FD_SEL | (0xffff << 16);
	rs = SP7021_FD_SEL | ((clk_sel & 0xffff) << 16);
	writel((pspim->xfer_conf & ~rc) | rs, pspim->mas_base + SP7021_SPI_CONFIG_REG);
}

// preliminary set CS, CPOL, CPHA and LSB
static int sp7021_spi_controller_prepare_message(struct spi_controller *ctlr,
				    struct spi_message *msg)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);
	struct spi_device *s = msg->spi;
	// reg clear bits and set bits
	u32 rs = 0;

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

	rs |= SP7021_WRITE_BYTE(0) | SP7021_READ_BYTE(0);  //set R/W fifo byte

	pspim->xfer_conf = rs;

	if (pspim->xfer_conf & SP7021_CPOL_FD)
		writel(pspim->xfer_conf, pspim->mas_base + SP7021_SPI_CONFIG_REG);

	return 0;
}

static int sp7021_spi_mas_transfer_one(struct spi_controller *ctlr,
		struct spi_device *spi, struct spi_transfer *xfer)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);
	u32 reg_temp = 0;
	unsigned long timeout = msecs_to_jiffies(1000);
	unsigned int i;
	int ret;
	unsigned int xfer_cnt, xfer_len, last_len;

	xfer_cnt = xfer->len / SP7021_SPI_DATA_SIZE;
	last_len = xfer->len % SP7021_SPI_DATA_SIZE;

	for (i = 0; i <= xfer_cnt; i++) {

		mutex_lock(&pspim->buf_lock);

		sp7021_prep_transfer(ctlr, spi);
		sp7021_spi_setup_transfer(spi, ctlr, xfer);

		reinit_completion(&pspim->isr_done);

		if (i == xfer_cnt)
			xfer_len = last_len;
		else
			xfer_len = SP7021_SPI_DATA_SIZE;

		pspim->tx_buf = xfer->tx_buf+i*SP7021_SPI_DATA_SIZE;
		pspim->rx_buf = xfer->rx_buf+i*SP7021_SPI_DATA_SIZE;

		if (pspim->tx_cur_len < xfer_len) {
			reg_temp = min(pspim->data_unit, xfer_len);
			sp7021_spi_mas_wb(pspim, reg_temp);
		}

		// initial SPI master config and change to Full-Duplex mode 91.15
		reg_temp = readl(pspim->mas_base + SP7021_SPI_CONFIG_REG);
		reg_temp &= SP7021_CLEAN_RW_BYTE;
		reg_temp &= SP7021_CLEAN_FLUG_MASK;
		reg_temp |= SP7021_FD_SEL;
		// set SPI master config for full duplex (SPI_FD_CONFIG)  91.15
		reg_temp |= SP7021_FINISH_FLAG_MASK |
				SP7021_TX_EMP_FLAG_MASK | SP7021_RX_FULL_FLAG_MASK;
		reg_temp |= SP7021_WRITE_BYTE(0) | SP7021_READ_BYTE(0);
		writel(reg_temp, pspim->mas_base + SP7021_SPI_CONFIG_REG);

		writel(SP7021_TOTAL_LENGTH(xfer_len) | SP7021_TX_LENGTH(xfer_len)
			| SP7021_SPI_START_FD, pspim->mas_base + SP7021_SPI_STATUS_REG);

		if (!wait_for_completion_interruptible_timeout(&pspim->isr_done,
							       timeout)){
			dev_err(&(spi->dev), "wait_for_completion err\n");
			ret = -ETIMEDOUT;
			goto free_maste_xfer;
		}

		reg_temp = readl(pspim->mas_base + SP7021_SPI_STATUS_REG);
		if (reg_temp & SP7021_FINISH_FLAG) {
			writel(SP7021_FINISH_FLAG, pspim->mas_base + SP7021_SPI_STATUS_REG);
			writel(readl(pspim->mas_base + SP7021_SPI_CONFIG_REG) &
				SP7021_CLEAN_FLUG_MASK, pspim->mas_base + SP7021_SPI_CONFIG_REG);
		}

		ret = 0;

		if (pspim->xfer_conf & SP7021_CPOL_FD)
			writel(pspim->xfer_conf, pspim->mas_base + SP7021_SPI_CONFIG_REG);

		mutex_unlock(&pspim->buf_lock);
	}

free_maste_xfer:
	return ret;
}

// SPI-slave R/W
static int sp7021_spi_sla_transfer_one(struct spi_controller *ctlr,
		struct spi_device *spi, struct spi_transfer *xfer)
{
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);
	struct device *dev = pspim->dev;
	int mode = SP7021_SPI_IDLE, ret = 0;

	if (spi_controller_is_slave(ctlr)) {

		if ((xfer->tx_buf) && (xfer->rx_buf)) {
			dev_dbg(&ctlr->dev, "%s() wrong command\n", __func__);
			ret = -EINVAL;
		} else if (xfer->tx_buf) {
			xfer->tx_dma = dma_map_single(dev, (void *)xfer->tx_buf,
						xfer->len, DMA_TO_DEVICE);

			if (dma_mapping_error(dev, xfer->tx_dma))
				return -ENOMEM;

			mode = SP7021_SLA_WRITE;
		} else if (xfer->rx_buf) {
			xfer->rx_dma = dma_map_single(dev, xfer->rx_buf,
				xfer->len, DMA_FROM_DEVICE);

			if (dma_mapping_error(dev, xfer->rx_dma))
				return -ENOMEM;

			mode = SP7021_SLA_READ;
		}

		switch (mode) {
		case SP7021_SLA_WRITE:
			sp7021_spi_sla_tx(spi, xfer);
			break;
		case SP7021_SLA_READ:
			sp7021_spi_sla_rx(spi, xfer);
			break;
		default:
			break;
		}

		if (xfer->tx_buf)
			dma_unmap_single(dev, xfer->tx_dma,
				xfer->len, DMA_TO_DEVICE);

		if (xfer->rx_buf)
			dma_unmap_single(dev, xfer->rx_dma,
				xfer->len, DMA_FROM_DEVICE);

	}

	spi_finalize_current_transfer(ctlr);

	return ret;

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
		ctlr = devm_spi_alloc_slave(&pdev->dev, sizeof(*pspim));
	else
		ctlr = devm_spi_alloc_master(&pdev->dev, sizeof(*pspim));
	if (!ctlr)
		return -ENOMEM;

	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->bus_num = pdev->id;
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST;
	ctlr->auto_runtime_pm = true;
	ctlr->prepare_message = sp7021_spi_controller_prepare_message;

	if (mode == SP7021_SLAVE_MODE) {
		ctlr->transfer_one = sp7021_spi_sla_transfer_one;
		ctlr->slave_abort = sp7021_spi_sla_abort;
		ctlr->flags = SPI_CONTROLLER_HALF_DUPLEX;
	} else {
		ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
		ctlr->min_speed_hz = 40000;
		ctlr->max_speed_hz = 25000000;
		ctlr->use_gpio_descriptors = true;
		ctlr->flags = SPI_CONTROLLER_MUST_RX | SPI_CONTROLLER_MUST_TX;
		ctlr->transfer_one = sp7021_spi_mas_transfer_one;
	}

	platform_set_drvdata(pdev, ctlr);
	pspim = spi_controller_get_devdata(ctlr);

	pspim->ctlr = ctlr;
	pspim->dev = &pdev->dev;

	spin_lock_init(&pspim->lock);
	mutex_init(&pspim->buf_lock);
	init_completion(&pspim->isr_done);
	init_completion(&pspim->sla_isr);

	pspim->mas_base = devm_platform_ioremap_resource_byname
		(pdev, SP7021_MAS_REG_NAME);
	pspim->sla_base = devm_platform_ioremap_resource_byname
		(pdev, SP7021_SLA_REG_NAME);

	dev_dbg(&pdev->dev, "mas_base 0x%x\n", (unsigned int)pspim->mas_base);

	pspim->mas_irq = platform_get_irq_byname(pdev, SP7021_MAS_IRQ_NAME);
	if (pspim->mas_irq < 0) {
		dev_err(&pdev->dev, "failed to get %s\n", SP7021_MAS_IRQ_NAME);
		return pspim->mas_irq;
	}

	pspim->sla_irq = platform_get_irq_byname(pdev, SP7021_SLA_IRQ_NAME);
	if (pspim->sla_irq < 0) {
		dev_err(&pdev->dev, "failed to get %s\n", SP7021_SLA_IRQ_NAME);
		return pspim->sla_irq;
	}

	ret = devm_request_irq(&pdev->dev, pspim->mas_irq, sp7021_spi_mas_irq
						, IRQF_TRIGGER_RISING, pdev->name, pspim);
	if (ret) {
		dev_err(&pdev->dev, "%s devm_request_irq fail\n", SP7021_MAS_IRQ_NAME);
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, pspim->sla_irq, sp7021_spi_sla_irq
						, IRQF_TRIGGER_RISING, pdev->name, pspim);
	if (ret) {
		dev_err(&pdev->dev, "%s devm_request_irq fail\n", SP7021_SLA_IRQ_NAME);
		return ret;
	}

	pm_runtime_enable(&pdev->dev);

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret != 0) {
		dev_err(&pdev->dev, "spi_register_master fail\n");
		goto disable_runtime_pm;
	}

	// clk
	pspim->spi_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pspim->spi_clk)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(pspim->spi_clk),
				     "devm_clk_get fail\n");
	}

	// reset
	pspim->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	dev_dbg(&pdev->dev, "pspim->rstc : 0x%x\n", (unsigned int)pspim->rstc);
	if (IS_ERR(pspim->rstc)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(pspim->rstc),
				     "devm_rst_get fail\n");
	}

	ret = clk_prepare_enable(pspim->spi_clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
			"failed to enable clk\n");

	ret = reset_control_deassert(pspim->rstc);
	if (ret) {
		dev_err_probe(&pdev->dev, ret,
			"failed to deassert reset\n");
		goto free_reset_assert;

	}

	dev_dbg(&pdev->dev, "pm init done\n");

	return ret;

free_reset_assert:
	reset_control_assert(pspim->rstc);
	clk_disable_unprepare(pspim->spi_clk);
disable_runtime_pm:
	pm_runtime_disable(&pdev->dev);

	dev_dbg(&pdev->dev, "spi_master_probe done\n");
	return ret;
}

static int sp7021_spi_controller_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = dev_get_drvdata(&pdev->dev);
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	spi_unregister_controller(pspim->ctlr);
	clk_disable_unprepare(pspim->spi_clk);
	reset_control_assert(pspim->rstc);

	return 0;
}

static int __maybe_unused sp7021_spi_controller_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	return reset_control_assert(pspim->rstc);
}

static int __maybe_unused sp7021_spi_controller_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	reset_control_deassert(pspim->rstc);

	return clk_prepare_enable(pspim->spi_clk);
}

static int sp7021_spi_runtime_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	dev_dbg(dev, "devid:%d\n", dev->id);
	return reset_control_assert(pspim->rstc);
}

static int sp7021_spi_runtime_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct sp7021_spi_ctlr *pspim = spi_master_get_devdata(ctlr);

	dev_dbg(dev, "devid:%d\n", dev->id);
	return reset_control_deassert(pspim->rstc);
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
