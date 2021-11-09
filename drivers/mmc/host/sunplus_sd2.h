/* SPDX-License-Identifier: GPL-2.0 */
/**
 * (C) Copyright 2019 Sunplus Technology. <http://www.sunplus.com/>
 *
 * Sunplus SD host controller v2.0
 */
#ifndef __SPSDC_H__
#define __SPSDC_H__

#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>


#define SPSDC_WIDTH_SWITCH

#define SPSDC_MIN_CLK	400000
#define SPSDC_MAX_CLK	52000000
#define SPSDC_50M_CLK   50000000

#define SPSDC_MAX_BLK_COUNT 65536

#define __rsvd_regs(l) __append_suffix(l, __COUNTER__)
#define __append_suffix(l, s) _append_suffix(l, s)
#define _append_suffix(l, s) (reserved##s[l])

#define SPSD2_MEDIA_TYPE_REG 0x0000
#define SPSDC_MEDIA_NONE 0
#define SPSDC_MEDIA_SD 6
#define SPSDC_MEDIA_MS 7


#define SPSD2_SDRAM_SECTOR_SIZE_REG 0x0010

#define SPSDC_MAX_DMA_MEMORY_SECTORS 8 /* support up to 8 fragmented memory blocks */

#define SPSD2_SDRAM_SECTOR_ADDR_REG 0x001C

#define SPSD2_SD_INT_REG            0x00B0
#define SPSDC_SDINT_SDCMPEN	BIT(0)
#define SPSDC_SDINT_SDCMP	BIT(1)
#define SPSDC_SDINT_SDIOEN	BIT(4)
#define SPSDC_SDINT_SDIO	BIT(5)

#define SPSD2_SD_PAGE_NUM_REG       0x00B4
#define SPSD2_SD_CONF0_REG          0x00B8
#define SPSD2_SDIO_CTRL_REG         0x00Bc
#define SPSD2_SD_RST_REG            0x00C0

#define SPSD2_SD_CONF_REG           0x00C4
#define SPSDC_MODE_SDIO	2
#define SPSDC_MODE_EMMC	1
#define SPSDC_MODE_SD	0

#define SPSD2_SD_CTRL_REG           0x00C8
#define SPSDC_SDSTATUS_DUMMY_READY			BIT(0)
#define SPSDC_SDSTATUS_RSP_BUF_FULL			BIT(1)
#define SPSDC_SDSTATUS_TX_DATA_BUF_EMPTY		BIT(2)
#define SPSDC_SDSTATUS_RX_DATA_BUF_FULL			BIT(3)
#define SPSDC_SDSTATUS_CMD_PIN_STATUS			BIT(4)
#define SPSDC_SDSTATUS_DAT0_PIN_STATUS			BIT(5)
#define SPSDC_SDSTATUS_RSP_TIMEOUT			BIT(6)
#define SPSDC_SDSTATUS_CARD_CRC_CHECK_TIMEOUT		BIT(7)
#define SPSDC_SDSTATUS_STB_TIMEOUT			BIT(8)
#define SPSDC_SDSTATUS_RSP_CRC7_ERROR			BIT(9)
#define SPSDC_SDSTATUS_CRC_TOKEN_CHECK_ERROR		BIT(10)
#define SPSDC_SDSTATUS_RDATA_CRC16_ERROR		BIT(11)
#define SPSDC_SDSTATUS_SUSPEND_STATE_READY		BIT(12)
#define SPSDC_SDSTATUS_BUSY_CYCLE			BIT(13)

#define SPSD2_SD_STATUS_REG         0x00CC

#define SPSD2_SD_STATE_REG          0x00D0
#define SPSDC_SDSTATE_IDLE	(0x0)
#define SPSDC_SDSTATE_TXDUMMY	(0x1)
#define SPSDC_SDSTATE_TXCMD	(0x2)
#define SPSDC_SDSTATE_RXRSP	(0x3)
#define SPSDC_SDSTATE_TXDATA	(0x4)
#define SPSDC_SDSTATE_RXCRC	(0x5)
#define SPSDC_SDSTATE_RXDATA	(0x5)
#define SPSDC_SDSTATE_MASK	(0x7)
#define SPSDC_SDSTATE_BADCRC	(0x5)
#define SPSDC_SDSTATE_ERROR	BIT(13)
#define SPSDC_SDSTATE_FINISH	BIT(14)

#define SPSD2_BLOCKSIZE_REG         0x00D4
#define SPSD2_SD_TIMING_CONF0_REG   0x00Dc
#define SPSD2_SD_TIMING_CONF1_REG   0x00E0
#define SPSD2_SD_PIO_TX_REG         0x00E4
#define SPSD2_SD_PIO_RX_REG         0x00E8
#define SPSD2_SD_CMD_BUF0_REG       0x00Ec
#define SPSD2_SD_CMD_BUF1_REG       0x00F0
#define SPSD2_SD_CMD_BUF2_REG       0x00F4
#define SPSD2_SD_CMD_BUF3_REG       0x00F8
#define SPSD2_SD_CMD_BUF4_REG       0x00Fc

#define SPSD2_SD_RSP_BUF0_3_REG     0x0100
#define SPSD2_SD_RSP_BUF4_5_REG     0x0104

#define SPSD2_DMA_SRCDST_REG        0x0204
#define SPSD2_DMA_SIZE_REG          0x0208
#define SPSD2_DMA_STOP_RST_REG      0x020c
#define SPSD2_DMA_CTRL_REG          0x0210
#define SPSD2_DMA_BASE_ADDR0_REG    0x0214
#define SPSD2_DMA_BASE_ADDR16_REG   0x0218

struct spsdc_tuning_info {
	int need_tuning;
#define SPSDC_MAX_RETRIES (8 * 8)
	int retried; /* how many times has been retried */
	u32 wr_dly:3;
	u32 rd_dly:3;
	u32 clk_dly:3;
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


#ifdef SPSDC_WIDTH_SWITCH
	int restore_4bit_sdio_bus;
#endif

#define SPSDC_DMA_MODE 0
#define SPSDC_PIO_MODE 1
	int dmapio_mode;
	/* for purpose of reducing context switch, only when transfer data that
	 *  length is greater than `dma_int_threshold' should use interrupt
	 */
	int dma_int_threshold;
	int dma_use_int; /* should raise irq when dma done */
	struct sg_mapping_iter sg_miter; /* for pio mode to access sglist */
	struct spsdc_tuning_info tuning_info;
};

#endif /* #ifndef __SPSDC_H__ */
