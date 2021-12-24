// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Sunplus Inc.
 * Author: Li-hao Kuo <lhjeff911@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#define SPSDC_MIN_CLK			400000
#define SPSDC_MAX_CLK			52000000
#define SPSDC_50M_CLK			50000000
#define SPSDC_MAX_BLK_COUNT		65536

#define SPSD2_MEDIA_TYPE_REG		0x0000
#define SPSDC_MEDIA_MASK		GENMASK(2, 0)
#define SPSDC_MEDIA_NONE		0
#define SPSDC_MEDIA_SD			6
#define SPSDC_MEDIA_MS			7

#define SPSD2_SDRAM_SECTOR_SIZE_REG	0x0010
#define SPSDC_MAX_DMA_MEMORY_SECTORS	8

#define SPSD2_SDRAM_SECTOR_ADDR_REG	0x001C
#define SPSD2_SD_INT_REG		0x00B0
#define SPSDC_SDINT_SDCMPEN		BIT(0)
#define SPSDC_SDINT_SDCMP		BIT(1)
#define SPSDC_SDINT_SDCMP_CLR		BIT(2)
#define SPSDC_SDINT_SDIOEN		BIT(4)
#define SPSDC_SDINT_SDIO		BIT(5)
#define SPSDC_SDINT_SDIO_CLR		BIT(6)

#define SPSD2_SD_PAGE_NUM_REG		0x00B4
#define SPSD2_SD_CONF0_REG		0x00B8
#define SPSDC_CONF0_SDPIO_MODE		BIT(0)
#define SPSDC_CONF0_SDLEN_MODE		BIT(2)
#define SPSDC_CONF0_TRANS_MODE		GENMASK(5, 4)
#define SPSDC_TRANS_CMD_MODE		0
#define SPSDC_TRANS_WR_MODE		1
#define SPSDC_TRANS_RD_MODE		2
#define SPSDC_CONF0_AUTORSP		BIT(6)
#define SPSDC_CONF0_CMDDUMMY		BIT(7)
#define SPSDC_CONF0_RSPCHK		BIT(8)

#define SPSD2_SDIO_CTRL_REG		0x00BC
#define SPSDC_SDIO_CTRL_MULTI_TRIG	BIT(6)

#define SPSD2_SD_RST_REG		0x00C0
#define SPSDC_RST_ALL			0x07

#define SPSD2_SD_CONF_REG		0x00C4
#define SPSDC_CONF_CLK_DIV		GENMASK(11, 0)
#define SPSDC_CONF_4BIT_MODE		BIT(12)
#define SPSDC_CONF_SDRSP_TYPE		BIT(13)
#define SPSDC_CONF_SD_MODE		BIT(16)
#define SPSDC_CONF_MMC8BIT		BIT(18)
#define SPSDC_CONF_SDIO_MODE		BIT(20)
#define SPSDC_MODE_SDIO			2
#define SPSDC_MODE_EMMC			1
#define SPSDC_MODE_SD			0

#define SPSD2_SD_CTRL_REG		0x00C8
#define SPSDC_CTRL_CMD_TRIG		BIT(0)
#define SPSDC_CTRL_TXDUMMY_TRIG		BIT(1)

#define SPSD2_SD_STATUS_REG		0x00CC
#define SPSDC_STS_DUMMY_RDY		BIT(0)
#define SPSDC_STS_RSP_BUF_FULL		BIT(1)
#define SPSDC_STS_TX_BUF_EMP		BIT(2)
#define SPSDC_STS_RX_BUF_FULL		BIT(3)
#define SPSDC_STS_CMD_PIN_STS		BIT(4)
#define SPSDC_STS_DAT0_PIN_STS		BIT(5)
#define SPSDC_STS_RSP_TIMEOUT		BIT(6)
#define SPSDC_STS_CARD_CRC_TIMEOUT	BIT(7)
#define SPSDC_STS_STB_TIMEOUT		BIT(8)
#define SPSDC_STS_RSP_CRC7_ERR		BIT(9)
#define SPSDC_STS_CRC_TOKEN_ERR		BIT(10)
#define SPSDC_STS_RDATA_CRC16_ERR	BIT(11)
#define SPSDC_STS_SUSPEND_STATE_RDY	BIT(12)
#define SPSDC_STS_BUSY_CYCLE		BIT(13)

#define SPSD2_SD_STATE_REG		0x00D0
#define SPSDC_STATE_IDLE		(0x0)
#define SPSDC_STATE_TXDUMMY		(0x1)
#define SPSDC_STATE_TXCMD		(0x2)
#define SPSDC_STATE_RXRSP		(0x3)
#define SPSDC_STATE_TXDATA		(0x4)
#define SPSDC_STATE_RXCRC		(0x5)
#define SPSDC_STATE_RXDATA		(0x6)
#define SPSDC_STATE_MASK		(0x7)
#define SPSDC_STATE_ERROR		BIT(13)
#define SPSDC_STATE_FINISH		BIT(14)

#define SPSD2_BLOCKSIZE_REG		0x00D4
#define SPSD2_SD_TIMING_CONF0_REG	0x00DC
#define SPSDC_TIMING_CONF0_HS_EN	BIT(11)
#define SPSDC_TIMING_CONF0_WRTD		GENMASK(14, 12)

#define SPSD2_SD_TIMING_CONF1_REG	0x00E0
#define SPSDC_TIMING_CONF1_RDTD		GENMASK(15, 13)

#define SPSD2_SD_PIO_TX_REG		0x00E4
#define SPSD2_SD_PIO_RX_REG		0x00E8
#define SPSD2_SD_CMD_BUF0_REG		0x00EC
#define SPSD2_SD_CMD_BUF1_REG		0x00F0
#define SPSD2_SD_CMD_BUF2_REG		0x00F4
#define SPSD2_SD_CMD_BUF3_REG		0x00F8
#define SPSD2_SD_CMD_BUF4_REG		0x00FC
#define SPSD2_SD_RSP_BUF0_3_REG		0x0100
#define SPSD2_SD_RSP_BUF4_5_REG		0x0104
#define SPSD2_DMA_SRCDST_REG		0x0204
#define SPSD2_DMA_SIZE_REG		0x0208
#define SPSD2_DMA_STOP_RST_REG		0x020c
#define SPSD2_DMA_CTRL_REG		0x0210
#define SPSD2_DMA_BASE_ADDR0_REG	0x0214
#define SPSD2_DMA_BASE_ADDR16_REG	0x0218

struct spsdc_tuning_info {
	int need_tuning;
#define SPSDC_MAX_RETRIES (8 * 8)
	int retried; /* how many times has been retried */
	u32 wr_dly:3;
	u32 rd_dly:3;
	u32 clk_dly:3;
};

enum {
	SPSDC_DMA_MODE = 0,
	SPSDC_PIO_MODE = 1,
};

struct spsdc_host {
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rstc;
	int mode; /* SD/SDIO/eMMC */
	spinlock_t lock; /* controller lock */
	struct mutex mrq_lock;
	/* tasklet used to handle error then finish the request */
	struct tasklet_struct tsklet_finish_req;
	struct mmc_host *mmc;
	struct mmc_request *mrq; /* current mrq */
	int irq;
	int use_int; /* should raise irq when done */
	int power_state; /* current power state: off/up/on */
	int restore_4bit_sdio_bus;
	int dmapio_mode;
	/* for purpose of reducing context switch, only when transfer data that
	 *  length is greater than `dma_int_threshold' should use interrupt
	 */
	int dma_int_threshold;
	int dma_use_int; /* should raise irq when dma done */
	struct sg_mapping_iter sg_miter; /* for pio mode to access sglist */
	struct spsdc_tuning_info tuning_info;
};

/**
 * wait for transaction done, return -1 if error.
 */
static inline int spsdc_wait_finish(struct spsdc_host *host)
{
	/* Wait for transaction finish */
	unsigned long timeout = jiffies + msecs_to_jiffies(5000);

	while (!time_after(jiffies, timeout)) {
		if (readl(host->base + SPSD2_SD_STATE_REG) & SPSDC_STATE_FINISH)
			return 0;
		if (readl(host->base + SPSD2_SD_STATE_REG) & SPSDC_STATE_ERROR)
			return -1;
	}
	return -1;
}

static inline int spsdc_wait_sdstatus(struct spsdc_host *host, unsigned int status_bit)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(5000);

	while (!time_after(jiffies, timeout)) {
		if (readl(host->base + SPSD2_SD_STATUS_REG) & status_bit)
			return 0;
		if (readl(host->base + SPSD2_SD_STATE_REG) & SPSDC_STATE_ERROR)
			return -1;
	}
	return -1;
}

#define spsdc_wait_rspbuf_full(host) spsdc_wait_sdstatus(host, SPSDC_STS_RSP_BUF_FULL)
#define spsdc_wait_rxbuf_full(host) spsdc_wait_sdstatus(host, SPSDC_STS_RX_BUF_FULL)
#define spsdc_wait_txbuf_empty(host) spsdc_wait_sdstatus(host, SPSDC_STS_TX_BUF_EMP)

static void spsdc_get_rsp(struct spsdc_host *host, struct mmc_command *cmd)
{
	u32 value0_3, value4_5;

	if (unlikely(!(cmd->flags & MMC_RSP_PRESENT)))
		return;
	if (unlikely(cmd->flags & MMC_RSP_136)) {
		if (spsdc_wait_rspbuf_full(host))
			return;
		value0_3 = readl(host->base + SPSD2_SD_RSP_BUF0_3_REG);
		value4_5 = readl(host->base + SPSD2_SD_RSP_BUF4_5_REG) & 0xffff;
		cmd->resp[0] = (value0_3 << 8) | (value4_5 >> 8);
		cmd->resp[1] = value4_5 << 24;
		if (spsdc_wait_rspbuf_full(host))
			return;
		value0_3 = readl(host->base + SPSD2_SD_RSP_BUF0_3_REG);
		value4_5 = readl(host->base + SPSD2_SD_RSP_BUF4_5_REG) & 0xffff;
		cmd->resp[1] |= value0_3 >> 8;
		cmd->resp[2] = value0_3 << 24;
		cmd->resp[2] |= value4_5 << 8;
		if (spsdc_wait_rspbuf_full(host))
			return;
		value0_3 = readl(host->base + SPSD2_SD_RSP_BUF0_3_REG);
		value4_5 = readl(host->base + SPSD2_SD_RSP_BUF4_5_REG) & 0xffff;
		cmd->resp[2] |= value0_3 >> 24;
		cmd->resp[3] = value0_3 << 8;
		cmd->resp[3] |= value4_5 >> 8;
	} else {
		if (spsdc_wait_rspbuf_full(host))
			return;
		value0_3 = readl(host->base + SPSD2_SD_RSP_BUF0_3_REG);
		value4_5 = readl(host->base + SPSD2_SD_RSP_BUF4_5_REG) & 0xffff;
		cmd->resp[0] = (value0_3 << 8) | (value4_5 >> 8);
		cmd->resp[1] = value4_5 << 24;
	}
}

static void spsdc_set_bus_clk(struct spsdc_host *host, int clk)
{
	unsigned int clkdiv;
	int f_min = host->mmc->f_min;
	int f_max = host->mmc->f_max;
	u32 value = readl(host->base + SPSD2_SD_CONF_REG);

	if (clk < f_min)
		clk = f_min;
	if (clk > f_max)
		clk = f_max;

	// SD 2.0 only max set to 50Mhz CLK
	if (clk >= SPSDC_50M_CLK)
		clk = f_max;

	clkdiv = (clk_get_rate(host->clk) + clk) / clk - 1;
	if (clkdiv > 0xfff)
		clkdiv = 0xfff;

	value &= ~SPSDC_CONF_CLK_DIV;
	value |= FIELD_PREP(SPSDC_CONF_CLK_DIV, clkdiv);
	writel(value, host->base + SPSD2_SD_CONF_REG);
	/* In order to reduce the frequency of context switch,
	 * if it is high speed or upper, we do not use interrupt
	 * when send a command that without data.
	 */
	if (clk > 25000000)
		host->use_int = 0;
	else
		host->use_int = 1;
}

static void spsdc_set_bus_timing(struct spsdc_host *host, unsigned int timing)
{
	u32 value = readl(host->base + SPSD2_SD_TIMING_CONF0_REG);
	int clkdiv = FIELD_GET(SPSDC_CONF_CLK_DIV, readl(host->base + SPSD2_SD_CONF_REG));
	int delay = (clkdiv / 2 < 7) ? clkdiv / 2 : 7;
	char *timing_name;

	switch (timing) {
	case MMC_TIMING_LEGACY:
		value &= ~SPSDC_TIMING_CONF0_HS_EN;
		timing_name = "legacy";
		break;
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_MMC_HS:
		value |= SPSDC_TIMING_CONF0_HS_EN |
			FIELD_PREP(SPSDC_TIMING_CONF0_WRTD, delay);
		timing_name = "hs";
		break;
	}
	writel(value, host->base + SPSD2_SD_TIMING_CONF0_REG);
}

static void spsdc_set_bus_width(struct spsdc_host *host, int width)
{
	u32 value = readl(host->base + SPSD2_SD_CONF_REG);
	int bus_width;

	switch (width) {
	case MMC_BUS_WIDTH_8:
		value &= ~SPSDC_CONF_4BIT_MODE;
		value |= SPSDC_CONF_MMC8BIT;
		bus_width = 8;
		break;
	case MMC_BUS_WIDTH_4:
		value |= SPSDC_CONF_4BIT_MODE;
		value &= ~SPSDC_CONF_MMC8BIT;
		bus_width = 4;
		break;
	default:
		value &= ~SPSDC_CONF_4BIT_MODE;
		value &= ~SPSDC_CONF_MMC8BIT;
		bus_width = 1;
		break;
	};
	writel(value, host->base + SPSD2_SD_CONF_REG);
}

/**
 * select the working mode of controller: sd/sdio/emmc
 */
static void spsdc_select_mode(struct spsdc_host *host, int mode)
{
	u32 value = readl(host->base + SPSD2_SD_CONF_REG);

	host->mode = mode;
	/* set `sdmmcmode', as it will sample data at fall edge
	 * of SD bus clock if `sdmmcmode' is not set when
	 * `sd_high_speed_en' is not set, which is not compliant
	 * with SD specification
	 */
	value |= SPSDC_CONF_SD_MODE;
	switch (mode) {
	case SPSDC_MODE_EMMC:
		value &= ~SPSDC_CONF_SDIO_MODE;
		writel(value, host->base + SPSD2_SD_CONF_REG);
		break;
	case SPSDC_MODE_SDIO:
		value |= SPSDC_CONF_SDIO_MODE;
		writel(value, host->base + SPSD2_SD_CONF_REG);
		value = readl(host->base + SPSD2_SDIO_CTRL_REG);
		value |= SPSDC_SDIO_CTRL_MULTI_TRIG;
		writel(value, host->base + SPSD2_SDIO_CTRL_REG);
		break;
	case SPSDC_MODE_SD:
	default:
		value &= ~SPSDC_CONF_SDIO_MODE;
		host->mode = SPSDC_MODE_SD;
		writel(value, host->base + SPSD2_SD_CONF_REG);
		break;
	}
}

static void spsdc_sw_reset(struct spsdc_host *host)
{
	writel(SPSDC_RST_ALL, host->base + SPSD2_SD_RST_REG);
	writel(0x6, host->base + SPSD2_DMA_STOP_RST_REG);
	while (readl(host->base + SPSD2_DMA_STOP_RST_REG) & BIT(2))
		;
	/* reset dma operation */
	writel(0x0, host->base + SPSD2_DMA_CTRL_REG);
	writel(0x1, host->base + SPSD2_DMA_CTRL_REG);
	writel(0x0, host->base + SPSD2_DMA_CTRL_REG);
}

static void spsdc_prepare_cmd(struct spsdc_host *host, struct mmc_command *cmd)
{
	u32 value;

	writeb((u8)(cmd->opcode | 0x40), host->base + SPSD2_SD_CMD_BUF0_REG);
	writeb((u8)((cmd->arg >> 24) & 0x000000ff), host->base + SPSD2_SD_CMD_BUF1_REG);
	writeb((u8)((cmd->arg >> 16) & 0x000000ff), host->base + SPSD2_SD_CMD_BUF2_REG);
	writeb((u8)((cmd->arg >> 8) & 0x000000ff), host->base + SPSD2_SD_CMD_BUF3_REG);
	writeb((u8)((cmd->arg >> 0) & 0x000000ff), host->base + SPSD2_SD_CMD_BUF4_REG);

	/* disable interrupt if needed */
	value = readl(host->base + SPSD2_SD_INT_REG);
	value |= SPSDC_SDINT_SDCMP_CLR;
	if (likely(!host->use_int || cmd->flags & MMC_RSP_136))
		value &= ~SPSDC_SDINT_SDCMPEN;
	else
		value |= SPSDC_SDINT_SDCMPEN;
	writel(value, host->base + SPSD2_SD_INT_REG);

	value = readl(host->base + SPSD2_SD_CONF0_REG);
	value &= ~SPSDC_CONF0_TRANS_MODE; /* sd_trans_mode = 0 cmd mode */
	value |= SPSDC_CONF0_CMDDUMMY;
	if (likely(cmd->flags & MMC_RSP_PRESENT)) {
		value |= SPSDC_CONF0_AUTORSP;
	} else {
		value &= ~SPSDC_CONF0_AUTORSP;
		writel(value, host->base + SPSD2_SD_CONF0_REG);
		return;
	}
	/*
	 * Currently, host is not capable of checking R2's CRC7,
	 * thus, enable crc7 check only for 48 bit response commands
	 */
	if (likely(cmd->flags & MMC_RSP_CRC && !(cmd->flags & MMC_RSP_136)))
		value |= SPSDC_CONF0_RSPCHK;
	else
		value &= ~SPSDC_CONF0_RSPCHK;

	writel(value, host->base + SPSD2_SD_CONF0_REG);
	value = readl(host->base + SPSD2_SD_CONF_REG);
	if (unlikely(cmd->flags & MMC_RSP_136))
		value |= SPSDC_CONF_SDRSP_TYPE;
	else
		value &= ~SPSDC_CONF_SDRSP_TYPE;
	writel(value, host->base + SPSD2_SD_CONF_REG);
}

static void spsdc_prepare_data(struct spsdc_host *host, struct mmc_data *data)
{
	u32 value;

	writel(data->blocks - 1, host->base + SPSD2_SD_PAGE_NUM_REG);
	writel(data->blksz - 1, host->base + SPSD2_BLOCKSIZE_REG);
	value = readl(host->base + SPSD2_SD_CONF0_REG);
	value &= ~SPSDC_CONF0_TRANS_MODE;
	if (data->flags & MMC_DATA_READ) {
		value &= ~(SPSDC_CONF0_AUTORSP | SPSDC_CONF0_CMDDUMMY);
		value |= FIELD_PREP(SPSDC_CONF0_TRANS_MODE, SPSDC_TRANS_RD_MODE);
		writel(0x12, host->base + SPSD2_DMA_SRCDST_REG);
	} else {
		value |= FIELD_PREP(SPSDC_CONF0_TRANS_MODE, SPSDC_TRANS_WR_MODE);
		writel(0x21, host->base + SPSD2_DMA_SRCDST_REG);
	}
	/* to prevent of the responses of CMD18/25 being by CMD12's,
	 * send CMD12 by ourself instead of by controller automatically
	 *
	 *	#if 0
	 *	if ((cmd->opcode == MMC_READ_MULTIPLE_BLOCK)
	 *	 || (cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK))
	 *		value = bitfield_replace(value, 2, 1, 0); //sd_len_mode
	 *	else
	 *		value = bitfield_replace(value, 2, 1, 1);
	 *	#endif
	 */
	value |= SPSDC_CONF0_SDLEN_MODE;
	if (likely(host->dmapio_mode == SPSDC_DMA_MODE)) {
		struct scatterlist *sg;
		dma_addr_t dma_addr;
		unsigned int dma_size;
		void __iomem *reg_addr;
		int dma_direction = data->flags & MMC_DATA_READ ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
		int i, count = dma_map_sg(host->mmc->parent, data->sg, data->sg_len, dma_direction);

		if (unlikely(!count || count > SPSDC_MAX_DMA_MEMORY_SECTORS)) {
			data->error = -EINVAL;
			return;
		}
		for_each_sg(data->sg, sg, count, i) {
			dma_addr = sg_dma_address(sg);
			dma_size = sg_dma_len(sg) / data->blksz - 1;
			//dma_size = sg_dma_len(sg) / 512 - 1;
			if (i == 0) {
				writel(dma_addr, host->base + SPSD2_DMA_BASE_ADDR0_REG);
				writel(dma_addr >> 16, host->base + SPSD2_DMA_BASE_ADDR16_REG);
				writel(dma_size, host->base + SPSD2_SDRAM_SECTOR_SIZE_REG);
			} else {
				reg_addr = host->base + (SPSD2_SDRAM_SECTOR_ADDR_REG + (i - 1) * 8);
				writel(dma_addr, reg_addr);
				writel(dma_size, reg_addr + 4);
			}
		}
		value &= ~SPSDC_CONF0_SDPIO_MODE;
		writel(value, host->base + SPSD2_SD_CONF0_REG);
		writel(data->blksz - 1, host->base + SPSD2_DMA_SIZE_REG);
		/* enable interrupt if needed */
		if (!host->use_int && data->blksz * data->blocks > host->dma_int_threshold) {
			host->dma_use_int = 1;
			value = readl(host->base + SPSD2_SD_INT_REG);
			value |= SPSDC_SDINT_SDCMPEN;
			writel(value, host->base + SPSD2_SD_INT_REG);
		}
	} else {
		value |= SPSDC_CONF0_SDPIO_MODE;
		writel(value, host->base + SPSD2_SD_CONF0_REG);
	}
}

static inline void spsdc_trigger_transaction(struct spsdc_host *host)
{
	u32 value = readl(host->base + SPSD2_SD_CTRL_REG);

	value |= SPSDC_CTRL_CMD_TRIG;
	writel(value, host->base + SPSD2_SD_CTRL_REG);
}

static int __send_stop_cmd(struct spsdc_host *host, struct mmc_command *stop)
{
	u32 value;

	spsdc_prepare_cmd(host, stop);
	value = readl(host->base + SPSD2_SD_INT_REG);
	value &= ~SPSDC_SDINT_SDCMPEN;
	writel(value, host->base + SPSD2_SD_INT_REG);
	spsdc_trigger_transaction(host);
	if (spsdc_wait_finish(host)) {
		value = readl(host->base + SPSD2_SD_STATUS_REG);
		if (value & SPSDC_STS_RSP_CRC7_ERR)
			stop->error = -EILSEQ;
		else
			stop->error = -ETIMEDOUT;
		return -1;
	}
	spsdc_get_rsp(host, stop);
	return 0;
}

static int __switch_sdio_bus_width(struct spsdc_host *host, int width)
{
	struct mmc_command cmd = {0};
	int ret = 0;
	u32 value;
	u8 ctrl;

	cmd.opcode = SD_IO_RW_DIRECT;
	cmd.arg |= SDIO_CCCR_IF << 9;
	cmd.flags = MMC_RSP_R5;
	spsdc_prepare_cmd(host, &cmd);
	value = readl(host->base + SPSD2_SD_INT_REG);
	value &= ~SPSDC_SDINT_SDCMPEN;
	writel(value, host->base + SPSD2_SD_INT_REG);
	spsdc_trigger_transaction(host);
	ret = spsdc_wait_finish(host);
	if (ret) {
		spsdc_sw_reset(host);
		return ret;
	}
	spsdc_get_rsp(host, &cmd);
	ctrl = cmd.resp[0] & 0xff;

	ctrl &= ~SDIO_BUS_WIDTH_MASK;
	if (width == MMC_BUS_WIDTH_4) {
		/* set to 4-bit bus width */
		ctrl |= SDIO_BUS_WIDTH_4BIT;
	}

	cmd.arg |= 0x80000000;
	cmd.arg |= ctrl;
	spsdc_prepare_cmd(host, &cmd);
	value = readl(host->base + SPSD2_SD_INT_REG);
	value |= ~SPSDC_SDINT_SDCMPEN;
	writel(value, host->base + SPSD2_SD_INT_REG);
	spsdc_trigger_transaction(host);
	ret = spsdc_wait_finish(host);
	if (ret) {
		spsdc_sw_reset(host);
		return ret;
	}
	spsdc_get_rsp(host, &cmd);
	spsdc_set_bus_width(host, width);

	return ret;
}

/**
 * check if error during transaction.
 * @host -  host
 * @mrq - the mrq
 * @return 0 if no error otherwise the error number.
 */
static int spsdc_check_error(struct spsdc_host *host, struct mmc_request *mrq)
{
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;
	int ret = 0;
	u32 value = readl(host->base + SPSD2_SD_STATE_REG);

	if (unlikely(value & SPSDC_STATE_ERROR)) {
		u32 timing_cfg0, timing_cfg1;

		value = readl(host->base + SPSD2_SD_STATUS_REG);
		timing_cfg0 = readl(host->base + SPSD2_SD_TIMING_CONF0_REG);
		host->tuning_info.wr_dly = FIELD_GET(SPSDC_TIMING_CONF0_WRTD, timing_cfg0);
		timing_cfg1 = readl(host->base + SPSD2_SD_TIMING_CONF1_REG);
		host->tuning_info.rd_dly = FIELD_GET(SPSDC_TIMING_CONF1_RDTD, timing_cfg1);
		if (value & SPSDC_STS_RSP_TIMEOUT) {
			ret = -ETIMEDOUT;
			host->tuning_info.wr_dly++;
		} else if (value & SPSDC_STS_RSP_CRC7_ERR) {
			ret = -EILSEQ;
			host->tuning_info.rd_dly++;
		}
		if (data) {
			if ((value & SPSDC_STS_STB_TIMEOUT) ||
			    (value & SPSDC_STS_CARD_CRC_TIMEOUT)) {
				ret = -ETIMEDOUT;
				host->tuning_info.rd_dly++;
			} else if (value & SPSDC_STS_CRC_TOKEN_ERR) {
				ret = -EILSEQ;
				host->tuning_info.wr_dly++;
			} else if (value & SPSDC_STS_RDATA_CRC16_ERR) {
				ret = -EILSEQ;
				host->tuning_info.rd_dly++;
			}
			data->error = ret;
			data->bytes_xfered = 0;
		}
		cmd->error = ret;
		if (!host->tuning_info.need_tuning)
			cmd->retries = SPSDC_MAX_RETRIES; /* retry it */
		spsdc_sw_reset(host);
		timing_cfg0 |= FIELD_PREP(SPSDC_TIMING_CONF0_WRTD, host->tuning_info.wr_dly);
		writel(timing_cfg0, host->base + SPSD2_SD_TIMING_CONF0_REG);
		timing_cfg1 |= FIELD_PREP(SPSDC_TIMING_CONF1_RDTD, host->tuning_info.rd_dly);
		writel(timing_cfg1, host->base + SPSD2_SD_TIMING_CONF1_REG);

	} else if (data) {
		data->bytes_xfered = data->blocks * data->blksz;
	}
	host->tuning_info.need_tuning = ret;
	return ret;
}

static inline __maybe_unused void spsdc_txdummy(struct spsdc_host *host)
{
	u32 value = readl(host->base + SPSD2_SD_CTRL_REG);

	value |= SPSDC_CTRL_TXDUMMY_TRIG;
	writel(value, host->base + SPSD2_SD_CTRL_REG);
}

static void spsdc_xfer_data_pio(struct spsdc_host *host, struct mmc_data *data)
{
	int data_left = data->blocks * data->blksz;
	int consumed, remain;
	struct sg_mapping_iter *sg_miter = &host->sg_miter;
	unsigned int flags = 0;
	u16 *buf; /* tx/rx 2 bytes one time in pio mode */

	if (data->flags & MMC_DATA_WRITE)
		flags |= SG_MITER_FROM_SG;
	else
		flags |= SG_MITER_TO_SG;
	sg_miter_start(&host->sg_miter, data->sg, data->sg_len, flags);
	while (data_left > 0) {
		consumed = 0;
		if (!sg_miter_next(sg_miter))
			break;
		buf = sg_miter->addr;
		remain = sg_miter->length;
		do {
			if (data->flags & MMC_DATA_WRITE) {
				if (spsdc_wait_txbuf_empty(host))
					goto done;
				writel(*buf, host->base + SPSD2_SD_PIO_TX_REG);
			} else {
				if (spsdc_wait_rxbuf_full(host))
					goto done;
				*buf = readl(host->base + SPSD2_SD_PIO_RX_REG);
			}
			buf++;
			consumed += 2;
			remain -= 2;
		} while (remain);
		sg_miter->consumed = consumed;
		data_left -= consumed;
	}
done:
	sg_miter_stop(sg_miter);
}

static void spsdc_controller_init(struct spsdc_host *host)
{
	u32 value;
	int ret = reset_control_assert(host->rstc);

	if (!ret) {
		usleep_range(1000, 1250);
		ret = reset_control_deassert(host->rstc);
	}
	value = readl(host->base + SPSD2_MEDIA_TYPE_REG);
	value &= ~SPSDC_MEDIA_MASK;
	value |= FIELD_PREP(SPSDC_MEDIA_MASK, SPSDC_MEDIA_SD);
	writel(value, host->base + SPSD2_MEDIA_TYPE_REG);
}

static void spsdc_set_power_mode(struct spsdc_host *host, struct mmc_ios *ios)
{
	if (host->power_state == ios->power_mode)
		return;

	switch (ios->power_mode) {
		/* power off->up->on */
	case MMC_POWER_ON:
		spsdc_controller_init(host);
		pm_runtime_get_sync(host->mmc->parent);
		break;
	case MMC_POWER_UP:
		break;
	case MMC_POWER_OFF:
		pm_runtime_put(host->mmc->parent);
		break;
	}
	host->power_state = ios->power_mode;
}

/**
 * 1. unmap scatterlist if needed;
 * 2. get response & check error conditions;
 * 3. unlock host->mrq_lock
 * 4. notify mmc layer the request is done
 */
static void spsdc_finish_request(struct spsdc_host *host, struct mmc_request *mrq)
{
	struct mmc_command *cmd;
	struct mmc_data *data;

	if (!mrq)
		return;

	cmd = mrq->cmd;
	data = mrq->data;
	if (data && SPSDC_DMA_MODE == host->dmapio_mode) {
		int dma_direction = data->flags & MMC_DATA_READ ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

		dma_unmap_sg(host->mmc->parent, data->sg, data->sg_len, dma_direction);
		host->dma_use_int = 0;
	}
	spsdc_get_rsp(host, cmd);
	spsdc_check_error(host, mrq);
	if (mrq->stop) {
		if (__send_stop_cmd(host, mrq->stop))
			spsdc_sw_reset(host);
	}
	host->mrq = NULL;

	if (host->restore_4bit_sdio_bus) {
		__switch_sdio_bus_width(host, MMC_BUS_WIDTH_4);
		host->restore_4bit_sdio_bus = 0;
	}
	mutex_unlock(&host->mrq_lock);
	mmc_request_done(host->mmc, mrq);
}

/* Interrupt Service Routine */
irqreturn_t spsdc_irq(int irq, void *dev_id)
{
	struct spsdc_host *host = dev_id;
	u32 value = readl(host->base + SPSD2_SD_INT_REG);

	spin_lock(&host->lock);
	if ((value & SPSDC_SDINT_SDCMP) && (value & SPSDC_SDINT_SDCMPEN)) {
		value &= ~SPSDC_SDINT_SDCMP;
		value |= SPSDC_SDINT_SDCMP_CLR;
		writel(value, host->base + SPSD2_SD_INT_REG);
		/* we may need send stop command to stop data transaction,
		 * which is time consuming, so make use of tasklet to handle this.
		 */
		if (host->mrq && host->mrq->stop)
			tasklet_schedule(&host->tsklet_finish_req);
		else
			spsdc_finish_request(host, host->mrq);
	}

	if ((value & SPSDC_SDINT_SDIO) && (value & SPSDC_SDINT_SDIOEN))
		mmc_signal_sdio_irq(host->mmc);
	spin_unlock(&host->lock);
	return IRQ_HANDLED;
}

static void spsdc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct spsdc_host *host = mmc_priv(mmc);
	struct mmc_command *cmd;
	struct mmc_data *data;
	int bus_width = mmc->ios.bus_width;

	mutex_lock_interruptible(&host->mrq_lock);
	host->mrq = mrq;
	data = mrq->data;
	cmd = mrq->cmd;

	if (cmd->opcode == SD_IO_RW_EXTENDED && bus_width ==
			MMC_BUS_WIDTH_4 && data->blocks * data->blksz <= 4) {
		if (__switch_sdio_bus_width(host, MMC_BUS_WIDTH_1)) {
			cmd->error = -1;
			host->mrq = NULL;
			mutex_unlock(&host->mrq_lock);
			mmc_request_done(host->mmc, mrq);
			return;
		}
		host->restore_4bit_sdio_bus = 1;
	}

	spsdc_prepare_cmd(host, cmd);
	/* we need manually read response R2. */
	if (unlikely(cmd->flags & MMC_RSP_136)) {
		spsdc_trigger_transaction(host);
		spsdc_get_rsp(host, cmd);
		spsdc_wait_finish(host);
		spsdc_check_error(host, mrq);
		host->mrq = NULL;
		mutex_unlock(&host->mrq_lock);
		mmc_request_done(host->mmc, mrq);
	} else {
		if (data)
			spsdc_prepare_data(host, data);

		if ((host->dmapio_mode && data) ==  SPSDC_PIO_MODE) {
			u32 value;
			/* pio data transfer do not use interrupt */
			value = readl(host->base + SPSD2_SD_INT_REG);
			value &= ~SPSDC_SDINT_SDCMPEN;
			writel(value, host->base + SPSD2_SD_INT_REG);
			spsdc_trigger_transaction(host);
			spsdc_xfer_data_pio(host, data);
			spsdc_wait_finish(host);
			spsdc_finish_request(host, mrq);
		} else {
			if (!(host->use_int || host->dma_use_int)) {
				spsdc_trigger_transaction(host);
				spsdc_wait_finish(host);
				spsdc_finish_request(host, mrq);
			} else {
				spsdc_trigger_transaction(host);
			}
		}
	}
}

static void spsdc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct spsdc_host *host = (struct spsdc_host *)mmc_priv(mmc);

	mutex_lock(&host->mrq_lock);
	spsdc_set_power_mode(host, ios);
	spsdc_set_bus_clk(host, ios->clock);
	spsdc_set_bus_timing(host, ios->timing);
	spsdc_set_bus_width(host, ios->bus_width);
	/* ensure mode is correct, because we might have hw reset the controller */
	spsdc_select_mode(host, host->mode);
	mutex_unlock(&host->mrq_lock);
}

/**
 * Return values for the get_cd callback should be:
 *   0 for a absent card
 *   1 for a present card
 *   -ENOSYS when not supported (equal to NULL callback)
 *   or a negative errno value when something bad happened
 */
int spsdc_get_cd(struct mmc_host *mmc)
{
	int ret = 0;

	if (mmc_can_gpio_cd(mmc))
		ret = mmc_gpio_get_cd(mmc);
	if (ret < 0)
		ret = 0;

	return ret;
}

static void spsdc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct spsdc_host *host = mmc_priv(mmc);
	u32 value = readl(host->base + SPSD2_SD_INT_REG);

	value |= SPSDC_SDINT_SDIO_CLR;
	if (enable)
		value |= SPSDC_SDINT_SDIOEN;
	else
		value &= ~SPSDC_SDINT_SDIOEN;
	writel(value, host->base + SPSD2_SD_INT_REG);
}

static const struct mmc_host_ops spsdc_ops = {
	.request = spsdc_request,
	.set_ios = spsdc_set_ios,
	.get_cd = spsdc_get_cd,
	.enable_sdio_irq = spsdc_enable_sdio_irq,
};

static void tsklet_func_finish_req(unsigned long data)
{
	struct spsdc_host *host = (struct spsdc_host *)data;

	spin_lock(&host->lock);
	spsdc_finish_request(host, host->mrq);
	spin_unlock(&host->lock);
}

static void spsdc_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static void spsdc_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

static int spsdc_drv_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct resource *res;
	struct spsdc_host *host;
	unsigned int mode;
	int ret = 0;

	mmc = mmc_alloc_host(sizeof(*host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto probe_free_host;
	}
	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->power_state = MMC_POWER_OFF;
	host->dma_int_threshold = 1024;
	host->dmapio_mode = SPSDC_DMA_MODE;
	host->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(host->base))
		return PTR_ERR(host->base);

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq <= 0)
		return host->irq;

	ret = devm_request_irq(&pdev->dev, host->irq, spsdc_irq,
			       IRQF_SHARED, dev_name(&pdev->dev), host);
	if (ret)
		return ret;

	host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(host->clk), "clk get fail\n");

	host->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(host->rstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(host->rstc), "rst get fail\n");

	ret = clk_prepare_enable(host->clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to enable clk\n");

	ret = devm_add_action_or_reset(&pdev->dev, spsdc_disable_unprepare, host->clk);
	if (ret)
		return ret;

	ret = reset_control_deassert(host->rstc);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to deassert reset\n");

	ret = devm_add_action_or_reset(&pdev->dev, spsdc_reset_control_assert, host->rstc);
	if (ret)
		return ret;

	ret = mmc_of_parse(mmc);
	if (ret)
		goto probe_free_host;

	spin_lock_init(&host->lock);
	mutex_init(&host->mrq_lock);
	tasklet_init(&host->tsklet_finish_req, tsklet_func_finish_req, (unsigned long)host);
	mmc->ops = &spsdc_ops;
	mmc->f_min = SPSDC_MIN_CLK;
	if (mmc->f_max > SPSDC_MAX_CLK)
		mmc->f_max = SPSDC_MAX_CLK;

	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->max_seg_size = SPSDC_MAX_BLK_COUNT * 512;
	/* Host controller supports up to "SPSDC_MAX_DMA_MEMORY_SECTORS",
	 * a.k.a. max scattered memory segments per request
	 */
	/* Limited by the max value of dma_size & data_length, set it to 512 bytes for now */
	mmc->max_segs = SPSDC_MAX_DMA_MEMORY_SECTORS;
	mmc->max_req_size = SPSDC_MAX_BLK_COUNT * 512;
	mmc->max_blk_size = 512;
	mmc->max_blk_count = SPSDC_MAX_BLK_COUNT; /* Limited by sd_page_num */

	dev_set_drvdata(&pdev->dev, host);
	spsdc_controller_init(host);
	mode = (int)of_device_get_match_data(&pdev->dev);
	spsdc_select_mode(host, mode);
	mmc_add_host(mmc);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	return ret;

probe_free_host:
	if (mmc)
		mmc_free_host(mmc);

	return ret;
}

static int spsdc_drv_remove(struct platform_device *dev)
{
	struct spsdc_host *host = platform_get_drvdata(dev);

	mmc_remove_host(host->mmc);
	pm_runtime_disable(&dev->dev);
	platform_set_drvdata(dev, NULL);
	mmc_free_host(host->mmc);

	return 0;
}

static int spsdc_drv_suspend(struct platform_device *dev, pm_message_t state)
{
	struct spsdc_host *host;

	host = platform_get_drvdata(dev);
	mutex_lock(&host->mrq_lock); /* Make sure that no one is holding the controller */
	mutex_unlock(&host->mrq_lock);
	clk_disable(host->clk);
	return 0;
}

static int spsdc_drv_resume(struct platform_device *dev)
{
	struct spsdc_host *host;

	host = platform_get_drvdata(dev);
	return clk_enable(host->clk);
}

#ifdef CONFIG_PM
#ifdef CONFIG_PM_SLEEP
static int spsdc_pm_suspend(struct device *dev)
{
	pm_runtime_force_suspend(dev);
	return 0;
}

static int spsdc_pm_resume(struct device *dev)
{
	pm_runtime_force_resume(dev);
	return 0;
}
#endif /* ifdef CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_RUNTIME_SD
static int spsdc_pm_runtime_suspend(struct device *dev)
{
	struct spsdc_host *host;

	host = dev_get_drvdata(dev);
	if (__clk_is_enabled(host->clk))
		clk_disable(host->clk);
	return 0;
}

static int spsdc_pm_runtime_resume(struct device *dev)
{
	struct spsdc_host *host;
	int ret = 0;

	host = dev_get_drvdata(dev);
	if (!host->mmc)
		return -EINVAL;
	if (mmc_can_gpio_cd(host->mmc)) {
		ret = mmc_gpio_get_cd(host->mmc);
		if (!ret)
			return 0;
	}
	return clk_enable(host->clk);
}
#endif /* ifdef CONFIG_PM_RUNTIME_SD */

static const struct dev_pm_ops spsdc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(spsdc_pm_suspend, spsdc_pm_resume)
#ifdef CONFIG_PM_RUNTIME_SD
	SET_RUNTIME_PM_OPS(spsdc_pm_runtime_suspend, spsdc_pm_runtime_resume, NULL)
#endif
};
#endif /* ifdef CONFIG_PM */

static const struct of_device_id spsdc_of_table[] = {
	{
		.compatible = "sunplus,sp7021-card",
		.data = (void *)SPSDC_MODE_SD,
	},
	{
		.compatible = "sunplus,sp7021-sdio",
		.data = (void *)SPSDC_MODE_SDIO,
	},
	{/* sentinel */}

};
MODULE_DEVICE_TABLE(of, spsdc_of_table);

static struct platform_driver spsdc_driver = {
	.probe = spsdc_drv_probe,
	.remove = spsdc_drv_remove,
	.suspend = spsdc_drv_suspend,
	.resume = spsdc_drv_resume,
	.driver = {
		.name = "spsdc",
#ifdef CONFIG_PM
		.pm = &spsdc_pm_ops,
#endif
		.of_match_table = spsdc_of_table,
	},
};
module_platform_driver(spsdc_driver);

MODULE_AUTHOR("Li-hao Kuo <lhjeff911@gmail.com>");
MODULE_DESCRIPTION("Thermal driver for SP7021 SoC");
MODULE_LICENSE("GPL v2");
