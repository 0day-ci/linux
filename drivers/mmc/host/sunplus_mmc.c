// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Sunplus Inc.
 * Author: Tony Huang <tonyhuang.sunplus@gmail.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#define SPMMC_MIN_CLK			400000
#define SPMMC_MAX_CLK			52000000
#define SPMMC_MAX_BLK_COUNT		65536
#define SPMMC_MAX_TUNABLE_DLY	7

#define SPMMC_CARD_MEDIATYPE_SRCDST_REG 0x0000
#define SPMMC_MEDIA_NONE	0
#define SPMMC_MEDIA_SD		6
#define SPMMC_MEDIA_MS		7

#define SPMMC_SDRAM_SECTOR_0_SIZE_REG	0x0008
#define SPMMC_DMA_BASE_ADDR_REG			0x000C
#define SPMMC_HW_DMA_CTRL_REG			0x0010
#define SPMMC_MAX_DMA_MEMORY_SECTORS	8

#define SPMMC_SDRAM_SECTOR_1_ADDR_REG 0x0018

#define SPMMC_SD_INT_REG	0x0088
#define SPMMC_SDINT_SDCMPEN	BIT(0)
#define SPMMC_SDINT_SDCMP	BIT(1)
#define SPMMC_SDINT_SDIOEN	BIT(3)
#define SPMMC_SDINT_SDIO	BIT(4)

#define SPMMC_SD_PAGE_NUM_REG	0x008C
#define SPMMC_SD_CONFIG0_REG	0x0090
#define SPMMC_MODE_SD			0
#define SPMMC_MODE_EMMC			1
#define SPMMC_MODE_SDIO			2

#define SPMMC_SDIO_CTRL_REG		0x0094
#define SPMMC_SD_RST_REG		0x0098
#define SPMMC_SD_CTRL_REG		0x009C

#define SPMMC_SD_STATUS_REG						0x00A0
#define SPMMC_SDSTATUS_DUMMY_READY				BIT(0)
#define SPMMC_SDSTATUS_RSP_BUF_FULL				BIT(1)
#define SPMMC_SDSTATUS_TX_DATA_BUF_EMPTY		BIT(2)
#define SPMMC_SDSTATUS_RX_DATA_BUF_FULL			BIT(3)
#define SPMMC_SDSTATUS_CMD_PIN_STATUS			BIT(4)
#define SPMMC_SDSTATUS_DAT0_PIN_STATUS			BIT(5)
#define SPMMC_SDSTATUS_RSP_TIMEOUT				BIT(6)
#define SPMMC_SDSTATUS_CARD_CRC_CHECK_TIMEOUT	BIT(7)
#define SPMMC_SDSTATUS_STB_TIMEOUT				BIT(8)
#define SPMMC_SDSTATUS_RSP_CRC7_ERROR			BIT(9)
#define SPMMC_SDSTATUS_CRC_TOKEN_CHECK_ERROR	BIT(10)
#define SPMMC_SDSTATUS_RDATA_CRC16_ERROR		BIT(11)
#define SPMMC_SDSTATUS_SUSPEND_STATE_READY		BIT(12)
#define SPMMC_SDSTATUS_BUSY_CYCLE				BIT(13)
#define SPMMC_SDSTATUS_DAT1_PIN_STATUS			BIT(14)
#define SPMMC_SDSTATUS_SD_SENSE_STATUS			BIT(15)
#define SPMMC_SDSTATUS_BOOT_ACK_TIMEOUT			BIT(16)
#define SPMMC_SDSTATUS_BOOT_DATA_TIMEOUT		BIT(17)
#define SPMMC_SDSTATUS_BOOT_ACK_ERROR			BIT(18)

#define SPMMC_SD_STATE_REG		0x00A4
#define SPMMC_SDSTATE_ERROR		BIT(13)
#define SPMMC_SDSTATE_FINISH	BIT(14)

#define SPMMC_SD_HW_STATE_REG		0x00A8
#define SPMMC_SD_BLOCKSIZE_REG		0x00AC
#define SPMMC_SD_CONFIG1_REG		0x00B0
#define SPMMC_SD_TIMING_CONFIG0_REG 0x00B4
#define SPMMC_SD_PIODATATX_REG		0x00BC
#define SPMMC_SD_PIODATARX_REG		0x00C0
#define SPMMC_SD_CMDBUF0_3_REG		0x00C4
#define SPMMC_SD_CMDBUF4_REG		0x00C8
#define SPMMC_SD_RSPBUF0_3_REG		0x00CC
#define SPMMC_SD_RSPBUF4_5_REG		0x00D0

struct spmmc_tuning_info {
	int enable_tuning;
	int need_tuning;
#define SPMMC_MAX_RETRIES (8 * 8)
	int retried; /* how many times has been retried */
	u32 rd_crc_dly:3;
	u32 rd_dat_dly:3;
	u32 rd_rsp_dly:3;
	u32 wr_cmd_dly:3;
	u32 wr_dat_dly:3;
	u32 clk_dly:3;
};

struct spmmc_host {
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
	int use_int; /* should raise irq when done? */
	int power_state; /* current power state: off/up/on */
	int ddr_enabled;
	int signal_voltage;
#define SPMMC_DMA_MODE 0
#define SPMMC_PIO_MODE 1
	int dmapio_mode;
	/* for purpose of reducing context switch, only when transfer data that*/
	/* length is greater than `dma_int_threshold' should use interrupt */
	int dma_int_threshold;
	struct sg_mapping_iter sg_miter; /* for pio mode to access sglist */
	int dma_use_int; /* should raise irq when dma done */
	struct spmmc_tuning_info tuning_info;
};

static const u8 tuning_blk_pattern_4bit[] = {
	0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc,
	0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
	0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb,
	0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
	0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c,
	0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
	0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff,
	0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde,
};

static const u8 tuning_blk_pattern_8bit[] = {
	0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
	0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc,
	0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff,
	0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff,
	0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd,
	0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb,
	0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff,
	0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff,
	0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00,
	0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc,
	0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff,
	0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee,
	0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd,
	0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff,
	0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff,
	0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee,
};

/* Produces a mask of set bits covering a range of a 32-bit value */
static inline u32 bitfield_mask(u32 shift, u32 width)
{
	return ((1 << width) - 1) << shift;
}

/* Extract the value of a bitfield found within a given register value */
static inline u32 bitfield_extract(u32 reg_val, u32 shift, u32 width)
{
	return (reg_val & bitfield_mask(shift, width)) >> shift;
}

/* Replace the value of a bitfield found within a given register value */
static inline u32 bitfield_replace(u32 reg_val, u32 shift, u32 width, u32 val)
{
	u32 mask = bitfield_mask(shift, width);

	return (reg_val & ~mask) | (val << shift);
}

/**
 * wait for transaction done, return -1 if error.
 */
static inline int spmmc_wait_finish(struct spmmc_host *host)
{
	/* Wait for transaction finish */
	unsigned long timeout = jiffies + msecs_to_jiffies(5000);

	while (!time_after(jiffies, timeout)) {
		if (readl(host->base + SPMMC_SD_STATE_REG) & SPMMC_SDSTATE_FINISH)
			return 0;
		if (readl(host->base + SPMMC_SD_STATE_REG) & SPMMC_SDSTATE_ERROR)
			return -1;
	}
	return -1;
}

static inline int spmmc_wait_sdstatus(struct spmmc_host *host, unsigned int status_bit)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(5000);

	while (!time_after(jiffies, timeout)) {
		if (readl(host->base + SPMMC_SD_STATUS_REG) & status_bit)
			return 0;
		if (readl(host->base + SPMMC_SD_STATE_REG) & SPMMC_SDSTATE_ERROR)
			return -1;
	}
	return -1;
}

#define spmmc_wait_rspbuf_full(host) spmmc_wait_sdstatus(host, SPMMC_SDSTATUS_RSP_BUF_FULL)
#define spmmc_wait_rxbuf_full(host) spmmc_wait_sdstatus(host, SPMMC_SDSTATUS_RX_DATA_BUF_FULL)
#define spmmc_wait_txbuf_empty(host) spmmc_wait_sdstatus(host, SPMMC_SDSTATUS_TX_DATA_BUF_EMPTY)

static void spmmc_get_rsp(struct spmmc_host *host, struct mmc_command *cmd)
{
	u32 value0_3, value4_5;

	if (unlikely(!(cmd->flags & MMC_RSP_PRESENT)))
		return;
	if (unlikely(cmd->flags & MMC_RSP_136)) {
		if (spmmc_wait_rspbuf_full(host))
			return;
		value0_3 = readl(host->base + SPMMC_SD_RSPBUF0_3_REG);
		value4_5 = readl(host->base + SPMMC_SD_RSPBUF4_5_REG) & 0xffff;
		cmd->resp[0] = (value0_3 << 8) | (value4_5 >> 8);
		cmd->resp[1] = value4_5 << 24;
		value0_3 = readl(host->base + SPMMC_SD_RSPBUF0_3_REG);
		value4_5 = readl(host->base + SPMMC_SD_RSPBUF4_5_REG) & 0xffff;
		cmd->resp[1] |= value0_3 >> 8;
		cmd->resp[2] = value0_3 << 24;
		cmd->resp[2] |= value4_5 << 8;
		value0_3 = readl(host->base + SPMMC_SD_RSPBUF0_3_REG);
		value4_5 = readl(host->base + SPMMC_SD_RSPBUF4_5_REG) & 0xffff;
		cmd->resp[2] |= value0_3 >> 24;
		cmd->resp[3] = value0_3 << 8;
		cmd->resp[3] |= value4_5 >> 8;
	} else {
		if (spmmc_wait_rspbuf_full(host))
			return;
		value0_3 = readl(host->base + SPMMC_SD_RSPBUF0_3_REG);
		value4_5 = readl(host->base + SPMMC_SD_RSPBUF4_5_REG) & 0xffff;
		cmd->resp[0] = (value0_3 << 8) | (value4_5 >> 8);
		cmd->resp[1] = value4_5 << 24;
	}
	//return;
}

static void spmmc_set_bus_clk(struct spmmc_host *host, int clk)
{
	unsigned int clkdiv;
	int f_min = host->mmc->f_min;
	int f_max = host->mmc->f_max;
	u32 value = readl(host->base + SPMMC_SD_CONFIG0_REG);

	if (clk < f_min)
		clk = f_min;
	if (clk > f_max)
		clk = f_max;

	clkdiv = (clk_get_rate(host->clk) + clk) / clk - 1;
	if (clkdiv > 0xfff)
		clkdiv = 0xfff;

	value = bitfield_replace(value, 20, 12, clkdiv);
	writel(value, host->base + SPMMC_SD_CONFIG0_REG);

	/* In order to reduce the frequency of context switch,
	 * if it is high speed or upper, we do not use interrupt
	 * when send a command that without data transferring.
	 */
	if (clk > 25000000)
		host->use_int = 0;
	else
		host->use_int = 1;
}

static void spmmc_set_bus_timing(struct spmmc_host *host, unsigned int timing)
{
	u32 value = readl(host->base + SPMMC_SD_CONFIG1_REG);
	int clkdiv = readl(host->base + SPMMC_SD_CONFIG0_REG) >> 20;
	int delay = clkdiv / 2 < 7 ? clkdiv / 2 : 7;
	int hs_en = 1;
	char *timing_name;

	host->ddr_enabled = 0;
	switch (timing) {
	case MMC_TIMING_LEGACY:
		hs_en = 0;
		timing_name = "legacy";
		break;
	case MMC_TIMING_MMC_HS:
		timing_name = "mmc high-speed";
		break;
	case MMC_TIMING_SD_HS:
		timing_name = "sd high-speed";
		break;
	case MMC_TIMING_UHS_SDR50:
		timing_name = "sd uhs SDR50";
		break;
	case MMC_TIMING_UHS_SDR104:
		timing_name = "sd uhs SDR104";
		break;
	case MMC_TIMING_UHS_DDR50:
		host->ddr_enabled = 1;
		timing_name = "sd uhs DDR50";
		break;
	case MMC_TIMING_MMC_DDR52:
		host->ddr_enabled = 1;
		timing_name = "mmc DDR52";
		break;
	case MMC_TIMING_MMC_HS200:
		timing_name = "mmc HS200";
		break;
	default:
		timing_name = "invalid";
		hs_en = 0;
		break;
	}

	if (hs_en) {
		value = bitfield_replace(value, 31, 1, 1); /* sd_high_speed_en */
		writel(value, host->base + SPMMC_SD_CONFIG1_REG);
		value = readl(host->base + SPMMC_SD_TIMING_CONFIG0_REG);
		value = bitfield_replace(value, 4, 3, delay); /* sd_wr_dat_dly_sel */
		value = bitfield_replace(value, 8, 3, delay); /* sd_wr_cmd_dly_sel */
		writel(value, host->base + SPMMC_SD_TIMING_CONFIG0_REG);
	} else {
		value = bitfield_replace(value, 31, 1, 0);
		writel(value, host->base + SPMMC_SD_CONFIG1_REG);
	}
	if (host->ddr_enabled) {
		value = readl(host->base + SPMMC_SD_CONFIG0_REG);
		value = bitfield_replace(value, 1, 1, 1); /* sdddrmode */
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
	} else {
		value = readl(host->base + SPMMC_SD_CONFIG0_REG);
		value = bitfield_replace(value, 1, 1, 0);
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
	}
}

static void spmmc_set_bus_width(struct spmmc_host *host, int width)
{
	u32 value = readl(host->base + SPMMC_SD_CONFIG0_REG);
	int bus_width;

	switch (width) {
	case MMC_BUS_WIDTH_8:
		value = bitfield_replace(value, 11, 1, 0);
		value = bitfield_replace(value, 18, 1, 1);
		bus_width = 8;
		break;
	case MMC_BUS_WIDTH_4:
		value = bitfield_replace(value, 11, 1, 1);
		value = bitfield_replace(value, 18, 1, 0);
		bus_width = 4;
		break;
	default:
		value = bitfield_replace(value, 11, 1, 0);
		value = bitfield_replace(value, 18, 1, 0);
		bus_width = 1;
		break;
	};
	writel(value, host->base + SPMMC_SD_CONFIG0_REG);
}

/**
 * select the working mode of controller: sd/sdio/emmc
 */
static void spmmc_select_mode(struct spmmc_host *host, int mode)
{
	u32 value = readl(host->base + SPMMC_SD_CONFIG0_REG);

	host->mode = mode;
	/* set `sdmmcmode', as it will sample data at fall edge */
	/* of SD bus clock if `sdmmcmode' is not set when */
	/* `sd_high_speed_en' is not set, which is not compliant */
	/* with SD specification */
	value = bitfield_replace(value, 10, 1, 1);
	switch (mode) {
	case SPMMC_MODE_EMMC:
		value = bitfield_replace(value, 9, 1, 0);
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
		break;
	case SPMMC_MODE_SDIO:
		value = bitfield_replace(value, 9, 1, 1);
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
		value = readl(host->base + SPMMC_SDIO_CTRL_REG);
		value = bitfield_replace(value, 6, 1, 1); /* int_multi_trig */
		writel(value, host->base + SPMMC_SDIO_CTRL_REG);
		break;
	case SPMMC_MODE_SD:
	default:
		value = bitfield_replace(value, 9, 1, 0);
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
		break;
	}
}

static void spmmc_sw_reset(struct spmmc_host *host)
{
	u32 value;

	/* Must reset dma operation first, or it will*/
	/* be stuck on sd_state == 0x1c00 because of*/
	/* a controller software reset bug */
	value = readl(host->base + SPMMC_HW_DMA_CTRL_REG);
	value = bitfield_replace(value, 10, 1, 1);
	writel(value, host->base + SPMMC_HW_DMA_CTRL_REG);
	value = bitfield_replace(value, 10, 1, 0);
	writel(value, host->base + SPMMC_HW_DMA_CTRL_REG);
	value = readl(host->base + SPMMC_HW_DMA_CTRL_REG);
	value = bitfield_replace(value, 9, 1, 1);
	writel(value, host->base + SPMMC_HW_DMA_CTRL_REG);
	writel(0x7, host->base + SPMMC_SD_RST_REG);
	while (readl(host->base + SPMMC_SD_HW_STATE_REG) & BIT(6))
		;
}

static void spmmc_prepare_cmd(struct spmmc_host *host, struct mmc_command *cmd)
{
	u32 value;

	/* add start bit, according to spec, command format */
	value = ((cmd->opcode | 0x40) << 24) | (cmd->arg >> 8);
	writel(value, host->base + SPMMC_SD_CMDBUF0_3_REG);
	writeb(cmd->arg & 0xff, host->base + SPMMC_SD_CMDBUF4_REG);

	/* disable interrupt if needed */
	value = readl(host->base + SPMMC_SD_INT_REG);
	value = bitfield_replace(value, 2, 1, 1); /* sd_cmp_clr */
	if (likely(!host->use_int || cmd->flags & MMC_RSP_136))
		value = bitfield_replace(value, 0, 1, 0); /* sdcmpen */
	else
		value = bitfield_replace(value, 0, 1, 1);
	writel(value, host->base + SPMMC_SD_INT_REG);

	value = readl(host->base + SPMMC_SD_CONFIG0_REG);
	value = bitfield_replace(value, 4, 2, 0); /* sd_trans_mode */
	value = bitfield_replace(value, 7, 1, 1); /* sdcmddummy */
	if (likely(cmd->flags & MMC_RSP_PRESENT)) {
		value = bitfield_replace(value, 6, 1, 1); /* sdautorsp */
	} else {
		value = bitfield_replace(value, 6, 1, 0);
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
		return;
	}
	/*
	 * Currently, host is not capable of checking R2's CRC7,
	 * thus, enable crc7 check only for 48 bit response commands
	 */
	if (likely(cmd->flags & MMC_RSP_CRC && !(cmd->flags & MMC_RSP_136)))
		value = bitfield_replace(value, 8, 1, 1); /* sdrspchk_en */
	else
		value = bitfield_replace(value, 8, 1, 0);

	if (unlikely(cmd->flags & MMC_RSP_136))
		value = bitfield_replace(value, 15, 1, 1); /* sdrsptype */
	else
		value = bitfield_replace(value, 15, 1, 0);
	writel(value, host->base + SPMMC_SD_CONFIG0_REG);
}

static void spmmc_prepare_data(struct spmmc_host *host, struct mmc_data *data)
{
	u32 value, srcdst;
	//struct mmc_command *cmd = data->mrq->cmd;

	writel(data->blocks - 1, host->base + SPMMC_SD_PAGE_NUM_REG);
	writel(data->blksz - 1, host->base + SPMMC_SD_BLOCKSIZE_REG);
	value = readl(host->base + SPMMC_SD_CONFIG0_REG);
	if (data->flags & MMC_DATA_READ) {
		value = bitfield_replace(value, 4, 2, 2); /* sd_trans_mode */
		value = bitfield_replace(value, 6, 1, 0); /* sdautorsp */
		value = bitfield_replace(value, 7, 1, 0); /* sdcmddummy */
		srcdst = readl(host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
		srcdst = bitfield_replace(srcdst, 4, 7, 0x12);
		writel(srcdst, host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
	} else {
		value = bitfield_replace(value, 4, 2, 1);
		srcdst = readl(host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
		srcdst = bitfield_replace(srcdst, 4, 7, 0x21);
		writel(srcdst, host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
	}
	value = bitfield_replace(value, 2, 1, 1);
	if (likely(host->dmapio_mode == SPMMC_DMA_MODE)) {
		struct scatterlist *sg;
		dma_addr_t dma_addr;
		unsigned int dma_size;
		u32 *reg_addr;
		int dma_direction = data->flags & MMC_DATA_READ ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
		int i, count = 1;

		count = dma_map_sg(host->mmc->parent, data->sg, data->sg_len, dma_direction);
		if (unlikely(!count || count > SPMMC_MAX_DMA_MEMORY_SECTORS)) {
			data->error = -EINVAL;
			return;
		}
		for_each_sg(data->sg, sg, count, i) {
			dma_addr = sg_dma_address(sg);
			dma_size = sg_dma_len(sg) / 512 - 1;
			if (i == 0) {
				writel(dma_addr, host->base + SPMMC_DMA_BASE_ADDR_REG);
				writel(dma_size, host->base + SPMMC_SDRAM_SECTOR_0_SIZE_REG);
			} else {
				reg_addr = host->base + SPMMC_SDRAM_SECTOR_1_ADDR_REG + (i - 1) * 2;
				writel(dma_addr, reg_addr);
				writel(dma_size, reg_addr + 1);
			}
		}
		value = bitfield_replace(value, 0, 1, 0); /* sdpiomode */
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
		/* enable interrupt if needed */
		if (!host->use_int && data->blksz * data->blocks > host->dma_int_threshold) {
			host->dma_use_int = 1;
			value = readl(host->base + SPMMC_SD_INT_REG);
			value = bitfield_replace(value, 0, 1, 1); /* sdcmpen */
			writel(value, host->base + SPMMC_SD_INT_REG);
		}
	} else {
		value = bitfield_replace(value, 0, 1, 1);
		value = bitfield_replace(value, 14, 1, 1); /* rx4_en */
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
	}
}

static inline void spmmc_trigger_transaction(struct spmmc_host *host)
{
	u32 value = readl(host->base + SPMMC_SD_CTRL_REG);

	value = bitfield_replace(value, 0, 1, 1); /* trigger transaction */
	writel(value, host->base + SPMMC_SD_CTRL_REG);
}

static void __send_stop_cmd(struct spmmc_host *host)
{
	struct mmc_command stop = {};
	u32 value;

	stop.opcode = MMC_STOP_TRANSMISSION;
	stop.arg = 0;
	stop.flags = MMC_RSP_R1B;
	spmmc_prepare_cmd(host, &stop);
	value = readl(host->base + SPMMC_SD_INT_REG);
	value = bitfield_replace(value, 0, 1, 0); /* sdcmpen */
	writel(value, host->base + SPMMC_SD_INT_REG);
	spmmc_trigger_transaction(host);
	spmmc_wait_finish(host);
}

/**
 * check if error occurred during transaction.
 * @host -  host
 * @mrq - the mrq
 * @return 0 if no error otherwise the error number.
 */
static int spmmc_check_error(struct spmmc_host *host, struct mmc_request *mrq)
{
	int ret = 0;
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;

	u32 value = readl(host->base + SPMMC_SD_STATE_REG);
	u32 crc_token = bitfield_extract(value, 4, 3);

	if (unlikely(value & SPMMC_SDSTATE_ERROR)) {
		u32 timing_cfg0 = 0;

		value = readl(host->base + SPMMC_SD_STATUS_REG);

		if (host->tuning_info.enable_tuning) {
			timing_cfg0 = readl(host->base + SPMMC_SD_TIMING_CONFIG0_REG);
			host->tuning_info.rd_crc_dly = bitfield_extract(timing_cfg0, 20, 3);
			host->tuning_info.rd_dat_dly = bitfield_extract(timing_cfg0, 16, 3);
			host->tuning_info.rd_rsp_dly = bitfield_extract(timing_cfg0, 12, 3);
			host->tuning_info.wr_cmd_dly = bitfield_extract(timing_cfg0, 8, 3);
			host->tuning_info.wr_dat_dly = bitfield_extract(timing_cfg0, 4, 3);
		}

		if (value & SPMMC_SDSTATUS_RSP_TIMEOUT) {
			ret = -ETIMEDOUT;
			host->tuning_info.wr_cmd_dly++;
		} else if (value & SPMMC_SDSTATUS_RSP_CRC7_ERROR) {
			ret = -EILSEQ;
			host->tuning_info.rd_rsp_dly++;
		} else if (data) {
			if ((value & SPMMC_SDSTATUS_STB_TIMEOUT)) {
				ret = -ETIMEDOUT;
				host->tuning_info.rd_dat_dly++;
			} else if (value & SPMMC_SDSTATUS_RDATA_CRC16_ERROR) {
				ret = -EILSEQ;
				host->tuning_info.rd_dat_dly++;
			} else if (value & SPMMC_SDSTATUS_CARD_CRC_CHECK_TIMEOUT) {
				ret = -ETIMEDOUT;
				host->tuning_info.rd_crc_dly++;
			} else if (value & SPMMC_SDSTATUS_CRC_TOKEN_CHECK_ERROR) {
				ret = -EILSEQ;
				if (crc_token == 0x5)
					host->tuning_info.wr_dat_dly++;
				else
					host->tuning_info.rd_crc_dly++;
			}
		}
		cmd->error = ret;
		if (data) {
			data->error = ret;
			data->bytes_xfered = 0;
		}
		if (!host->tuning_info.need_tuning && host->tuning_info.enable_tuning)
			cmd->retries = SPMMC_MAX_RETRIES; /* retry it */
		spmmc_sw_reset(host);
		mdelay(5);

		if (host->tuning_info.enable_tuning) {
			timing_cfg0 = bitfield_replace(timing_cfg0, 20, 3,
						       host->tuning_info.rd_crc_dly);
			timing_cfg0 = bitfield_replace(timing_cfg0, 16, 3,
						       host->tuning_info.rd_dat_dly);
			timing_cfg0 = bitfield_replace(timing_cfg0, 12, 3,
						       host->tuning_info.rd_rsp_dly);
			timing_cfg0 = bitfield_replace(timing_cfg0, 8, 3,
						       host->tuning_info.wr_cmd_dly);
			timing_cfg0 = bitfield_replace(timing_cfg0, 4, 3,
						       host->tuning_info.wr_dat_dly);
			writel(timing_cfg0, host->base + SPMMC_SD_TIMING_CONFIG0_REG);
		}
	} else if (data) {
		data->error = 0;
		data->bytes_xfered = data->blocks * data->blksz;
	}
	host->tuning_info.need_tuning = ret;
    /* controller will not send cmd 12 automatically if error occurred */
	if (cmd->opcode == MMC_READ_MULTIPLE_BLOCK ||
	    cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK) {
		__send_stop_cmd(host);
		spmmc_sw_reset(host);
	}
	return ret;
}

/**
 * the strategy is:
 * 1. if several continuous delays are acceptable, we choose a middle one;
 * 2. otherwise, we choose the first one.
 */
static inline int __find_best_delay(u8 candidate_dly)
{
	int f, w;

	if (!candidate_dly)
		return 0;
	f = ffs(candidate_dly) - 1;
	w = hweight8(candidate_dly);
	if (0xff == (bitfield_mask(f, w) & ~candidate_dly))
		return (f + w / 2);
	else
		return (f);
}

static inline __maybe_unused void spmmc_txdummy(struct spmmc_host *host, int count)
{
	u32 value;

	count &= 0x1ff;
	value = readl(host->base + SPMMC_SD_CONFIG1_REG);
	value = bitfield_replace(value, 0, 9, count);
	writel(value, host->base + SPMMC_SD_CONFIG1_REG);
	value = readl(host->base + SPMMC_SD_CTRL_REG);
	value = bitfield_replace(value, 1, 1, 1); /* trigger tx dummy */
	writel(value, host->base + SPMMC_SD_CTRL_REG);
}

static void spmmc_xfer_data_pio(struct spmmc_host *host, struct mmc_data *data)
{
	u32 *buf; /* tx/rx 4 bytes one time in pio mode */
	int data_left = data->blocks * data->blksz;
	int consumed, remain;

	struct sg_mapping_iter *sg_miter = &host->sg_miter;
	unsigned int flags = 0;

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
				if (spmmc_wait_txbuf_empty(host))
					goto done;
				writel(*buf, host->base + SPMMC_SD_PIODATATX_REG);
			} else {
				if (spmmc_wait_rxbuf_full(host))
					goto done;
				*buf = readl(host->base + SPMMC_SD_PIODATARX_REG);
			}
			buf++;
			consumed += 4;
			remain -= 4;
		} while (remain);
		sg_miter->consumed = consumed;
		data_left -= consumed;
	}
done:
	sg_miter_stop(sg_miter);
}

static void spmmc_controller_init(struct spmmc_host *host)
{
	u32 value;
	int ret = reset_control_assert(host->rstc);

	if (!ret) {
		mdelay(1);
		ret = reset_control_deassert(host->rstc);
	}

	value = readl(host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
	value = bitfield_replace(value, 0, 3, SPMMC_MEDIA_SD);
	writel(value, host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
	host->signal_voltage = MMC_SIGNAL_VOLTAGE_330;
}

static void spmmc_set_power_mode(struct spmmc_host *host, struct mmc_ios *ios)
{
	if (host->power_state == ios->power_mode)
		return;

	switch (ios->power_mode) {
		/* power off->up->on */
	case MMC_POWER_ON:
		spmmc_controller_init(host);
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
static void spmmc_finish_request(struct spmmc_host *host, struct mmc_request *mrq)
{
	struct mmc_command *cmd;
	struct mmc_data *data;

	if (!mrq)
		return;

	cmd = mrq->cmd;
	data = mrq->data;

	if (data && SPMMC_DMA_MODE == host->dmapio_mode) {
		int dma_direction = data->flags & MMC_DATA_READ ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

		dma_unmap_sg(host->mmc->parent, data->sg, data->sg_len, dma_direction);
		host->dma_use_int = 0;
	}
	spmmc_get_rsp(host, cmd);
	spmmc_check_error(host, mrq);
	host->mrq = NULL;
	mutex_unlock(&host->mrq_lock);
	mmc_request_done(host->mmc, mrq);
}

/* Interrupt Service Routine */
irqreturn_t spmmc_irq(int irq, void *dev_id)
{
	struct spmmc_host *host = dev_id;
	u32 value = readl(host->base + SPMMC_SD_INT_REG);

	spin_lock(&host->lock);
	if ((value & SPMMC_SDINT_SDCMP) && (value & SPMMC_SDINT_SDCMPEN)) {
		value = bitfield_replace(value, 0, 1, 0); /* disable sdcmp */
		value = bitfield_replace(value, 2, 1, 1); /* sd_cmp_clr */
		writel(value, host->base + SPMMC_SD_INT_REG);
		/* if error occurred, we my need send cmd 12 to stop data transaction,*/
		/* which is time consuming, so make use of tasklet to handle this.*/
		if (unlikely(readl(host->base + SPMMC_SD_STATE_REG) & SPMMC_SDSTATE_ERROR))
			tasklet_schedule(&host->tsklet_finish_req);
		else
			spmmc_finish_request(host, host->mrq);
	}
	if (value & SPMMC_SDINT_SDIO &&
	    (value & SPMMC_SDINT_SDIOEN)) {
		mmc_signal_sdio_irq(host->mmc);
	}
	spin_unlock(&host->lock);
	return IRQ_HANDLED;
}

static void spmmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct spmmc_host *host = mmc_priv(mmc);
	struct mmc_data *data;
	struct mmc_command *cmd;
	int ret;

	ret = mutex_lock_interruptible(&host->mrq_lock);
	if (ret)
		return;

	host->mrq = mrq;
	data = mrq->data;
	cmd = mrq->cmd;

	spmmc_prepare_cmd(host, cmd);
	/* we need manually read response R2. */
	if (unlikely(cmd->flags & MMC_RSP_136)) {
		spmmc_trigger_transaction(host);
		spmmc_get_rsp(host, cmd);
		spmmc_wait_finish(host);
		spmmc_check_error(host, mrq);
		host->mrq = NULL;
		mutex_unlock(&host->mrq_lock);
		mmc_request_done(host->mmc, mrq);
	} else {
		if (data)
			spmmc_prepare_data(host, data);

		if (unlikely(host->dmapio_mode == SPMMC_PIO_MODE && data)) {
			u32 value;
			/* pio data transfer do not use interrupt */
			value = readl(host->base + SPMMC_SD_INT_REG);
			value = bitfield_replace(value, 0, 1, 0); /* sdcmpen */
			writel(value, host->base + SPMMC_SD_INT_REG);
			spmmc_trigger_transaction(host);
			spmmc_xfer_data_pio(host, data);
			spmmc_wait_finish(host);
			spmmc_finish_request(host, mrq);
		} else {
			if (!(host->use_int || host->dma_use_int)) {
				spmmc_trigger_transaction(host);
				spmmc_wait_finish(host);
				spmmc_finish_request(host, mrq);
			} else {
				spmmc_trigger_transaction(host);
			}
		}
	}
}

static void spmmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct spmmc_host *host = (struct spmmc_host *)mmc_priv(mmc);

	mutex_lock(&host->mrq_lock);
	spmmc_set_power_mode(host, ios);
	spmmc_set_bus_clk(host, ios->clock);
	spmmc_set_bus_timing(host, ios->timing);
	spmmc_set_bus_width(host, ios->bus_width);
	/* ensure mode is correct, because we might have hw reset the controller */
	spmmc_select_mode(host, host->mode);
	mutex_unlock(&host->mrq_lock);
	//return;
}

/**
 * Return values for the get_cd callback should be:
 *   0 for a absent card
 *   1 for a present card
 *   -ENOSYS when not supported (equal to NULL callback)
 *   or a negative errno value when something bad happened
 */
int spmmc_get_cd(struct mmc_host *mmc)
{
	int ret = 0;

	if (mmc_can_gpio_cd(mmc))
		ret = mmc_gpio_get_cd(mmc);

	if (ret < 0)
		ret = 0;

	return ret;
}

static int spmmc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct spmmc_host *host = mmc_priv(mmc);
	const u8 *blk_pattern;
	u8 *blk_test;
	int blksz;
	u8 smpl_dly = 0, candidate_dly = 0;
	u32 value;

	if (mmc->ios.bus_width == MMC_BUS_WIDTH_8) {
		blk_pattern = tuning_blk_pattern_8bit;
		blksz = sizeof(tuning_blk_pattern_8bit);
	} else if (mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
		blk_pattern = tuning_blk_pattern_4bit;
		blksz = sizeof(tuning_blk_pattern_4bit);
	} else {
		return -EINVAL;
	}

	blk_test = kmalloc(blksz, GFP_KERNEL);
	if (!blk_test)
		return -ENOMEM;

	host->tuning_info.enable_tuning = 0;
	do {
		struct mmc_request mrq = {NULL};
		struct mmc_command cmd = {0};
		struct mmc_command stop = {0};
		struct mmc_data data = {0};
		struct scatterlist sg;

		cmd.opcode = opcode;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

		stop.opcode = MMC_STOP_TRANSMISSION;
		stop.arg = 0;
		stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		data.blksz = blksz;
		data.blocks = 1;
		data.flags = MMC_DATA_READ;
		data.sg = &sg;
		data.sg_len = 1;

		sg_init_one(&sg, blk_test, blksz);
		mrq.cmd = &cmd;
		mrq.stop = &stop;
		mrq.data = &data;
		host->mrq = &mrq;

		value = readl(host->base + SPMMC_SD_TIMING_CONFIG0_REG);
		value = bitfield_replace(value, 12, 3, smpl_dly); /* sd_rd_rsp_dly_sel */
		value = bitfield_replace(value, 16, 3, smpl_dly); /* sd_rd_dat_dly_sel */
		value = bitfield_replace(value, 20, 3, smpl_dly); /* sd_rd_crc_dly_sel */
		writel(value, host->base + SPMMC_SD_TIMING_CONFIG0_REG);

		mmc_wait_for_req(mmc, &mrq);
		if (!cmd.error && !data.error) {
			if (!memcmp(blk_pattern, blk_test, blksz))
				candidate_dly |= (1 << smpl_dly);
		}
	} while (smpl_dly++ <= SPMMC_MAX_TUNABLE_DLY);
	host->tuning_info.enable_tuning = 1;

	if (candidate_dly) {
		smpl_dly = __find_best_delay(candidate_dly);
		value = readl(host->base + SPMMC_SD_TIMING_CONFIG0_REG);
		value = bitfield_replace(value, 12, 3, smpl_dly); /* sd_rd_rsp_dly_sel */
		value = bitfield_replace(value, 16, 3, smpl_dly); /* sd_rd_dat_dly_sel */
		value = bitfield_replace(value, 20, 3, smpl_dly); /* sd_rd_crc_dly_sel */
		writel(value, host->base + SPMMC_SD_TIMING_CONFIG0_REG);
		return 0;
	}
	return -EIO;
}

static void spmmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct spmmc_host *host = mmc_priv(mmc);
	u32 value = readl(host->base + SPMMC_SD_INT_REG);

	value = bitfield_replace(value, 5, 1, 1); /* sdio_int_clr */
	if (enable)
		value = bitfield_replace(value, 3, 1, 1);
	else
		value = bitfield_replace(value, 3, 1, 0);
	writel(value, host->base + SPMMC_SD_INT_REG);
}

static const struct mmc_host_ops spmmc_ops = {
	.request = spmmc_request,
	.set_ios = spmmc_set_ios,
	.get_cd = spmmc_get_cd,
	.execute_tuning = spmmc_execute_tuning,
	.enable_sdio_irq = spmmc_enable_sdio_irq,
};

static void tsklet_func_finish_req(unsigned long data)
{
	struct spmmc_host *host = (struct spmmc_host *)data;

	spin_lock(&host->lock);
	spmmc_finish_request(host, host->mrq);
	spin_unlock(&host->lock);
}

static int spmmc_drv_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct mmc_host *mmc;
	struct resource *res;
	struct spmmc_host *host;
	unsigned int mode;

	mmc = mmc_alloc_host(sizeof(*host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto probe_free_host;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->power_state = MMC_POWER_OFF;
	host->dma_int_threshold = 1024;
	host->dmapio_mode = SPMMC_DMA_MODE;

	host->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(host->base))
		return PTR_ERR(host->base);

	host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk)) {
		dev_err(&pdev->dev, "Can not find clock source\n");
		ret = PTR_ERR(host->clk);
		goto probe_free_host;
	}

	host->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(host->rstc)) {
		dev_err(&pdev->dev, "Can not find reset controller\n");
		ret = PTR_ERR(host->rstc);
		goto probe_free_host;
	}

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq <= 0) {
		dev_err(&pdev->dev, "get sd irq resource fail\n");
		ret = -EINVAL;
		goto probe_free_host;
	}
	if (devm_request_irq(&pdev->dev, host->irq, spmmc_irq, IRQF_SHARED,
			     dev_name(&pdev->dev), host)) {
		dev_err(&pdev->dev, "Failed to request sd card interrupt.\n");
		ret = -ENOENT;
		goto probe_free_host;
	}

	ret = mmc_of_parse(mmc);
	if (ret)
		goto probe_free_host;

	ret = clk_prepare(host->clk);
	if (ret)
		goto probe_free_host;
	ret = clk_enable(host->clk);
	if (ret)
		goto probe_clk_unprepare;

	spin_lock_init(&host->lock);
	mutex_init(&host->mrq_lock);
	tasklet_init(&host->tsklet_finish_req, tsklet_func_finish_req, (unsigned long)host);
	mmc->ops = &spmmc_ops;
	mmc->f_min = SPMMC_MIN_CLK;
	if (mmc->f_max > SPMMC_MAX_CLK)
		mmc->f_max = SPMMC_MAX_CLK;

	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->max_seg_size = SPMMC_MAX_BLK_COUNT * 512;
	/* Host controller supports up to "SPMMC_MAX_DMA_MEMORY_SECTORS"*/
	/* a.k.a. max scattered memory segments per request*/
	mmc->max_segs = SPMMC_MAX_DMA_MEMORY_SECTORS;
	mmc->max_req_size = SPMMC_MAX_BLK_COUNT * 512;
	/* Limited by the max value of dma_size & data_length, set it to 512 bytes for now */
	mmc->max_blk_size = 512;
	mmc->max_blk_count = SPMMC_MAX_BLK_COUNT; /* Limited by sd_page_num */

	dev_set_drvdata(&pdev->dev, host);
	spmmc_controller_init(host);
	mode = (int)of_device_get_match_data(&pdev->dev);
	spmmc_select_mode(host, mode);
	mmc_add_host(mmc);
	host->tuning_info.enable_tuning = 1;
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

probe_clk_unprepare:
	dev_err(&pdev->dev, "unable to enable controller clock\n");
	clk_unprepare(host->clk);
probe_free_host:
	if (mmc)
		mmc_free_host(mmc);

	return ret;
}

static int spmmc_drv_remove(struct platform_device *dev)
{
	struct spmmc_host *host = platform_get_drvdata(dev);

	mmc_remove_host(host->mmc);
	clk_disable(host->clk);
	clk_unprepare(host->clk);
	pm_runtime_disable(&dev->dev);
	platform_set_drvdata(dev, NULL);
	mmc_free_host(host->mmc);
	return 0;
}

static int spmmc_drv_suspend(struct platform_device *dev, pm_message_t state)
{
	struct spmmc_host *host;

	host = platform_get_drvdata(dev);
	mutex_lock(&host->mrq_lock); /* Make sure that no one is holding the controller */
	mutex_unlock(&host->mrq_lock);
	clk_disable(host->clk);
	return 0;
}

static int spmmc_drv_resume(struct platform_device *dev)
{
	struct spmmc_host *host;

	host = platform_get_drvdata(dev);
	return clk_enable(host->clk);
}

#ifdef CONFIG_PM
#ifdef CONFIG_PM_SLEEP
static int spmmc_pm_suspend(struct device *dev)
{
	pm_runtime_force_suspend(dev);
	return 0;
}

static int spmmc_pm_resume(struct device *dev)
{
	pm_runtime_force_resume(dev);
	return 0;
}
#endif /* ifdef CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_RUNTIME
static int spmmc_pm_runtime_suspend(struct device *dev)
{
	struct spmmc_host *host;

	host = dev_get_drvdata(dev);
	clk_disable(host->clk);
	return 0;
}

static int spmmc_pm_runtime_resume(struct device *dev)
{
	struct spmmc_host *host;

	host = dev_get_drvdata(dev);
	return clk_enable(host->clk);
}
#endif /* ifdef CONFIG_PM_RUNTIME */

static const struct dev_pm_ops spmmc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(spmmc_pm_suspend, spmmc_pm_resume)
#ifdef CONFIG_PM_RUNTIME
	SET_RUNTIME_PM_OPS(spmmc_pm_runtime_suspend, spmmc_pm_runtime_resume, NULL)
#endif
};
#endif /* ifdef CONFIG_PM */

static const struct of_device_id spmmc_of_table[] = {
	{
		.compatible = "sunplus,sp7021-emmc",
		.data = (void *)SPMMC_MODE_EMMC,
	},
	{/* sentinel */}
};
MODULE_DEVICE_TABLE(of, spmmc_of_table);

static struct platform_driver spmmc_driver = {
	.probe = spmmc_drv_probe,
	.remove = spmmc_drv_remove,
	.suspend = spmmc_drv_suspend,
	.resume = spmmc_drv_resume,
	.driver = {
		.name = "spmmc",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &spmmc_pm_ops,
#endif
		.of_match_table = spmmc_of_table,
	},
};
module_platform_driver(spmmc_driver);

MODULE_AUTHOR("Tony Huang <tonyhuang.sunplus@gmail.com>");
MODULE_DESCRIPTION("Sunplus MMC controller driver");
MODULE_LICENSE("GPL v2");
