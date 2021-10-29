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

struct spsdc_regs {
#define SPSDC_MEDIA_NONE 0
#define SPSDC_MEDIA_SD 6
#define SPSDC_MEDIA_MS 7
	u32 card_mediatype;

	u32 __rsvd_regs(1);
	u32 card_cpu_page_cnt;
	u32 card_ctl_page_cnt;
	u32 sdram_sector_0_size;
	u32 ring_buffer_on;
	u32 card_gclk_disable;
#define SPSDC_MAX_DMA_MEMORY_SECTORS 8 /* support up to 8 fragmented memory blocks */
	u32 sdram_sector_1_addr;
	u32 sdram_sector_1_size;
	u32 sdram_sector_2_addr;
	u32 sdram_sector_2_size;
	u32 sdram_sector_3_addr;
	u32 sdram_sector_3_size;
	u32 sdram_sector_4_addr;
	u32 sdram_sector_4_size;
	u32 sdram_sector_5_addr;
	u32 sdram_sector_5_size;
	u32 sdram_sector_6_addr;
	u32 sdram_sector_6_size;
	u32 sdram_sector_7_addr;
	u32 sdram_sector_7_size;
	u32 sdram_sector_cnt;

	u32 __rsvd_regs(10);

	u32 __rsvd_regs(11);
	u32 sd_vol_ctrl;
#define SPSDC_SDINT_SDCMPEN	BIT(0)
#define SPSDC_SDINT_SDCMP	BIT(1)
#define SPSDC_SDINT_SDIOEN	BIT(4)
#define SPSDC_SDINT_SDIO	BIT(5)
	u32 sd_int;
	u32 sd_page_num;
	u32 sd_config0;
	u32 sdio_ctrl;
	u32 sd_rst;
#define SPSDC_MODE_SDIO	2
#define SPSDC_MODE_EMMC	1
#define SPSDC_MODE_SD	0
	u32 sd_config;
	u32 sd_ctrl;
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
	u32 sd_status;
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
	u32 sd_state;
	u32 sd_blocksize;
	u32 sd_hwdma_config;
	u32 sd_timing_config0;
	u32 sd_timing_config1;
	u32 sd_piodatatx;
	u32 sd_piodatarx;
	u32 sd_cmdbuf0;
	u32 sd_cmdbuf1;
	u32 sd_cmdbuf2;
	u32 sd_cmdbuf3;
	u32 sd_cmdbuf4;

	u32 sd_rspbuf0_3;
	u32 sd_rspbuf4_5;
	u32 sd_crc16even0;
	u32 sd_crc16even1;
	u32 sd_crc16even2;
	u32 sd_crc16even3;
	u32 sd_crc7buf;
	u32 sd_crc16buf0;
	u32 sd_hw_state;
	u32 sd_crc16buf1;
	u32 sd_hw_cmd13_rca;
	u32 sd_crc16buf2;
	u32 sd_tx_dummy_num;
	u32 sd_crc16buf3;
	u32 sd_clk_dly;

	u32 __rsvd_regs(17);

	u32 __rsvd_regs(32);

	u32 dma_data;
	u32 dma_srcdst;
	u32 dma_size;
	u32 dma_hw_stop_rst;
	u32 dma_ctrl;
	u32 dma_base_addr15_0;
	u32 dma_base_addr31_16;
	u32 dma_hw_en;
	u32 dma_hw_page_addr_0_15_0;
	u32 dma_hw_page_addr_0_31_16;
	u32 dma_hw_page_addr_1_15_0;
	u32 dma_hw_page_addr_1_31_16;
	u32 dma_hw_page_addr_2_15_0;
	u32 dma_hw_page_addr_2_31_16;
	u32 dma_hw_page_addr_3_15_0;
	u32 dma_hw_page_addr_3_31_16;
	u32 dma_hw_page_num0;
	u32 dma_hw_page_num1;
	u32 dma_hw_page_num2;
	u32 dma_hw_page_num3;
	u32 dma_hw_block_num;
	u32 dma_start;
	u32 dma_hw_page_cnt;
	u32 dma_cmp;
	u32 dma_int_en;

	u32 __rsvd_regs(1);
	u32 dma_hw_wait_num15_0;
	u32 dma_hw_wait_num31_16;
	u32 dma_hw_delay_num;
	u32 dma_debug;

	u32 __rsvd_regs(2);
};

struct spsdc_tuning_info {
	int need_tuning;
#define SPSDC_MAX_RETRIES (8 * 8)
	int retried; /* how many times has been retried */
	u32 wr_dly:3;
	u32 rd_dly:3;
	u32 clk_dly:3;
};

struct spsdc_host {
	struct spsdc_regs *base;
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
