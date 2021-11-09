// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Sunplus Inc.
 * Author: LH Kuo <lh.kuo@sunplus.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/rtc.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>

#define SP_I2C_FREQ             400
#define SP_I2C_SLEEP_TIMEOUT    200
#define SP_I2C_SCL_DELAY        1  //SCl dalay xT

#define SP_CLK_SOURCE_FREQ         27000  // KHz(27MHz)
#define SP_BUFFER_SIZE             1024   // Byte

#define SP_I2C_REG_NAME        "i2cm"
#define SP_DEVICE_NAME          "sunplus-i2cm"
#define SP_I2C_DMA_REG_NAME    "i2cmdma"

//burst write use
#define SP_I2C_EMPTY_THRESHOLD    4

#define SP_I2C_BURST_RDATA_BYTES        16
#define SP_I2C_BURST_RDATA_FLAG         0x80008000
#define SP_I2C_BURST_RDATA_ALL_FLAG     0xFFFFFFFF

#define SP_I2C_CTL0_REG 0x0000
//control0
#define SP_I2C_CTL0_FREQ(x)                  (x<<24)  //bit[26:24]
#define SP_I2C_CTL0_PREFETCH                 (1<<18)
#define SP_I2C_CTL0_RESTART_EN               (1<<17)  //0:disable 1:enable
#define SP_I2C_CTL0_SUBADDR_EN               (1<<16)  //For restart mode need to set high
#define SP_I2C_CTL0_SW_RESET                 (1<<15)
#define SP_I2C_CTL0_SLAVE_ADDR(x)            (x<<1)   //bit[7:1]

#define SP_I2C_CTL1_REG 0x0004
//control1
#define SP_I2C_CTL1_ALL_CLR                  (0x3FF)
#define SP_I2C_CTL1_EMPTY_CLR                (1<<9)
#define SP_I2C_CTL1_SCL_HOLD_TOO_LONG_CLR    (1<<8)
#define SP_I2C_CTL1_SCL_WAIT_CLR             (1<<7)
#define SP_I2C_CTL1_EMPTY_THRESHOLD_CLR      (1<<6)
#define SP_I2C_CTL1_DATA_NACK_CLR            (1<<5)
#define SP_I2C_CTL1_ADDRESS_NACK_CLR         (1<<4)
#define SP_I2C_CTL1_BUSY_CLR                 (1<<3)
#define SP_I2C_CTL1_CLKERR_CLR               (1<<2)
#define SP_I2C_CTL1_DONE_CLR                 (1<<1)
#define SPI2C_CTL1_SIFBUSY_CLR               (1<<0)

#define SP_I2C_CTL2_REG 0x0008
//control2
#define SP_I2C_CTL2_FREQ_CUSTOM(x)           (x<<0)   //bit[10:0]
#define SP_I2C_CTL2_SCL_DELAY(x)             (x<<24)  //bit[25:24]
#define SP_I2C_CTL2_SDA_HALF_ENABLE          (1<<31)

#define SP_I2C_INT_REG     0x001c
#define SP_I2C_INT_EN0_REG 0x0020
#define SP_I2C_MOD_REG     0x0024
#define SP_I2C_CTL6_REG    0x0030
#define SP_I2C_INT_EN1_REG 0x0034
#define SP_I2C_STATUS3_REG 0x0038
#define SP_I2C_STATUS4_REG 0x0040
#define SP_I2C_INT_EN2_REG 0x0040
#define SP_I2C_CTL7_REG    0x0044
//control7
#define SP_I2C_CTL7_RDCOUNT(x)               (x<<16)  //bit[31:16]
#define SP_I2C_CTL7_WRCOUNT(x)               (x<<0)   //bit[15:0]


#define SP_I2C_CTL0_FREQ_MASK                  (0x7)     // 3 bit
#define SP_I2C_CTL0_SLAVE_ADDR_MASK            (0x7F)    // 7 bit
#define SP_I2C_CTL2_FREQ_CUSTOM_MASK           (0x7FF)   // 11 bit
#define SP_I2C_CTL2_SCL_DELAY_MASK             (0x3)     // 2 bit
#define SP_I2C_CTL7_RW_COUNT_MASK              (0xFFFF)  // 16 bit
#define SP_I2C_EN0_CTL_EMPTY_THRESHOLD_MASK    (0x7)     // 3 bit
#define SP_I2C_SG_DMA_LLI_INDEX_MASK           (0x1F)    // 5 bit

//interrupt enable1
#define SP_I2C_EN1_BURST_RDATA_INT           (0x80008000)  //must sync with GET_BYTES_EACHTIME

//interrupt enable2
#define SP_I2C_EN2_BURST_RDATA_OVERFLOW_INT  (0xFFFFFFFF)

//i2c master mode
#define SP_I2C_MODE_DMA_MODE                 (1<<2)
#define SP_I2C_MODE_MANUAL_MODE              (1<<1)  //0:trigger mode 1:auto mode
#define SP_I2C_MODE_MANUAL_TRIG              (1<<0)

#define SP_I2C_DATA0_REG   0x0060
#define SP_I2C_DATA4_REG   0x0064
#define SP_I2C_DATA8_REG   0x0068
#define SP_I2C_DATA12_REG  0x006c
#define SP_I2C_DATA16_REG  0x0070
#define SP_I2C_DATA20_REG  0x0074
#define SP_I2C_DATA24_REG  0x0078
#define SP_I2C_DATA28_REG  0x007c

#define SP_I2C_DMA_CONF_REG   0x0004
#define SP_I2C_DMA_LEN_REG    0x0008
#define SP_I2C_DMA_ADDR_REG   0x000c

//dma config
#define SP_I2C_DMA_CFG_DMA_GO                (1<<8)
#define SP_I2C_DMA_CFG_NON_BUF_MODE          (1<<2)
#define SP_I2C_DMA_CFG_SAME_SLAVE            (1<<1)
#define SP_I2C_DMA_CFG_DMA_MODE              (1<<0)

#define SP_I2C_DMA_FLAG_REG 0x0014
//dma interrupt flag
#define SP_I2C_DMA_INT_LENGTH0_FLAG          (1<<6)
#define SP_I2C_DMA_INT_THRESHOLD_FLAG        (1<<5)
#define SP_I2C_DMA_INT_IP_TIMEOUT_FLAG       (1<<4)
#define SP_I2C_DMA_INT_GDMA_TIMEOUT_FLAG     (1<<3)
#define SP_I2C_DMA_INT_WB_EN_ERROR_FLAG      (1<<2)
#define SP_I2C_DMA_INT_WCNT_ERROR_FLAG       (1<<1)
#define SP_I2C_DMA_INT_DMA_DONE_FLAG         (1<<0)

#define SP_I2C_DMA_INT_EN_REG 0x0018
//dma interrupt enable
#define SP_I2C_DMA_EN_LENGTH0_INT            (1<<6)
#define SP_I2C_DMA_EN_THRESHOLD_INT          (1<<5)
#define SP_I2C_DMA_EN_IP_TIMEOUT_INT         (1<<4)
#define SP_I2C_DMA_EN_GDMA_TIMEOUT_INT       (1<<3)
#define SP_I2C_DMA_EN_WB_EN_ERROR_INT        (1<<2)
#define SP_I2C_DMA_EN_WCNT_ERROR_INT         (1<<1)
#define SP_I2C_DMA_EN_DMA_DONE_INT           (1<<0)

//interrupt
#define SP_I2C_INT_RINC_INDEX(x)             (x<<18)  //bit[20:18]
#define SP_I2C_INT_WINC_INDEX(x)             (x<<15)  //bit[17:15]
#define SP_I2C_INT_SCL_HOLD_TOO_LONG_FLAG    (1<<11)
#define SP_I2C_INT_WFIFO_ENABLE              (1<<10)
#define SP_I2C_INT_FULL_FLAG                 (1<<9)
#define SP_I2C_INT_EMPTY_FLAG                (1<<8)
#define SP_I2C_INT_SCL_WAIT_FLAG             (1<<7)
#define SP_I2C_INT_EMPTY_THRESHOLD_FLAG      (1<<6)
#define SP_I2C_INT_DATA_NACK_FLAG            (1<<5)
#define SP_I2C_INT_ADDRESS_NACK_FLAG         (1<<4)
#define SP_I2C_INT_BUSY_FLAG                 (1<<3)
#define SP_I2C_INT_CLKERR_FLAG               (1<<2)
#define SP_I2C_INT_DONE_FLAG                 (1<<1)
#define SP_I2C_INT_SIFBUSY_FLAG              (1<<0)


//interrupt enable0
#define SP_I2C_EN0_SCL_HOLD_TOO_LONG_INT     (1<<13)
#define SP_I2C_EN0_NACK_INT                  (1<<12)
#define SP_I2C_EN0_CTL_EMPTY_THRESHOLD(x)    (x<<9)  //bit[11:9]
#define SP_I2C_EN0_EMPTY_INT                 (1<<8)
#define SP_I2C_EN0_SCL_WAIT_INT              (1<<7)
#define SP_I2C_EN0_EMPTY_THRESHOLD_INT       (1<<6)
#define SP_I2C_EN0_DATA_NACK_INT             (1<<5)
#define SP_I2C_EN0_ADDRESS_NACK_INT          (1<<4)
#define SP_I2C_EN0_BUSY_INT                  (1<<3)
#define SP_I2C_EN0_CLKERR_INT                (1<<2)
#define SP_I2C_EN0_DONE_INT                  (1<<1)
#define SP_I2C_EN0_SIFBUSY_INT               (1<<0)


#define SP_I2C_RESET(id, val)          ((1 << (16 + id)) | (val << id))
#define SP_I2C_CLKEN(id, val)          ((1 << (16 + id)) | (val << id))
#define SP_I2C_GCLKEN(id, val)         ((1 << (16 + id)) | (val << id))

#define SP_I2C_POWER_BASE      0xF8000000
#define SP_I2C_POWER_CLKEN3    0x0010
#define SP_I2C_POWER_GCLKEN3   0x0038
#define SP_I2C_POWER_RESET3    0x0060

enum sp_ststus_e_ {
	I2C_SUCCESS,                /* successful */
	I2C_ERR_I2C_BUSY,           /* I2C is busy */
	I2C_ERR_INVALID_DEVID,      /* device id is invalid */
	I2C_ERR_INVALID_CNT,        /* read or write count is invalid */
	I2C_ERR_TIMEOUT_OUT,        /* wait timeout */
	I2C_ERR_RECEIVE_NACK,       /* receive NACK */
	I2C_ERR_FIFO_EMPTY,         /* FIFO empty */
	I2C_ERR_SCL_HOLD_TOO_LONG,  /* SCL hlod too long */
	I2C_ERR_RDATA_OVERFLOW,     /* rdata overflow */
	I2C_ERR_INVALID_STATE,      /* read write state is invalid */
	I2C_ERR_REQUESET_IRQ,       /* request irq failed */
};

enum sp_state_e_ {
	I2C_WRITE_STATE,  /* i2c is write */
	I2C_READ_STATE,   /* i2c is read */
	I2C_IDLE_STATE,   /* i2c is idle */
	I2C_DMA_WRITE_STATE,/* i2c is dma write */
	I2C_DMA_READ_STATE, /* i2c is dma read */
};

enum sp_i2c_switch_e_ {
	I2C_POWER_ALL_SWITCH,
	I2C_POWER_NO_SWITCH,
};

struct sp_i2c_cmd {
	unsigned int dev_id;
	unsigned int freq;
	unsigned int slave_addr;
	unsigned int restart_en;
	unsigned int write_cnt;
	unsigned int read_cnt;
	unsigned char *write_data;
	unsigned char *read_data;
};

struct sp_i2c_irq_dma_flag {
	unsigned char dma_done;
	unsigned char write_cnt_err;
	unsigned char wb_en_err;
	unsigned char gdma_timeout;
	unsigned char ipt_timerout;
	unsigned char threshold;
	unsigned char dma_ligth;
};

struct sp_i2c_irq_flag {
	unsigned char active_done;
	unsigned char addr_nack;
	unsigned char data_nack;
	unsigned char empty_threshold;
	unsigned char fifo_empty;
	unsigned char fifo_full;
	unsigned char scl_hold_too_long;
	unsigned char read_over_flow;
};

struct sp_i2c_irq_event {
	enum sp_state_e_ rw_state;
	struct sp_i2c_irq_flag irq_flag;
	struct sp_i2c_irq_dma_flag irq_dma_flag;
	unsigned int dev_id;
	unsigned int burst_cnt;
	unsigned int burst_remainder;
	unsigned int data_index;
	unsigned int data_total_len;
	unsigned int reg_data_index;
	unsigned char busy;
	unsigned char ret;
	unsigned char *data_buf;
};


enum sp_i2c_dma_mode {
	I2C_DMA_WRITE_MODE,
	I2C_DMA_READ_MODE,
};

enum sp_i2c_mode {
	I2C_WRITE_MODE,
	I2C_READ_MODE,
	I2C_RESTART_MODE,
};

enum sp_i2c_active_mode {
	I2C_TRIGGER,
	I2C_AUTO,
};


struct i2c_compatible {
	int mode; /* clk source switch*/
	int total_port; /* clk source switch*/
};

struct sp_i2c_dev {
	struct i2c_msg *msgs;  /* messages currently handled */
	struct i2c_adapter adap;
	struct device *dev;
	struct sp_i2c_cmd sp_i2c_cmd_info;
	struct sp_i2c_irq_event sp_i2c_irq_info;
	void __iomem *i2c_regs;

	struct clk *clk;
	struct reset_control *rstc;
	unsigned int i2c_clk_freq;
	int irq;
	wait_queue_head_t wait;

	void __iomem *i2c_dma_regs;
	dma_addr_t dma_phy_base;
	void *dma_vir_base;
	unsigned int mode;
	unsigned int total_port;
};

static unsigned int sp_i2cm_get_int_flag(void __iomem *sr)
{
	return readl(sr + SP_I2C_INT_REG);
}

static void sp_i2cm_status_clear(void __iomem *sr, unsigned int flag)
{
	unsigned int ctl1;

		ctl1 = readl(sr + SP_I2C_CTL1_REG);
		ctl1 |= flag;
		writel(ctl1, sr + SP_I2C_CTL1_REG);

		ctl1 = readl(sr + SP_I2C_CTL1_REG);
		ctl1 &= (~flag);
		writel(ctl1, sr + SP_I2C_CTL1_REG);
}


static void sp_i2cm_reset(void __iomem *sr)
{
	unsigned int ctl0;

	ctl0 = readl(sr + SP_I2C_CTL0_REG);
	ctl0 |= SP_I2C_CTL0_SW_RESET;
	writel(ctl0, sr + SP_I2C_CTL0_REG);

	udelay(2);
}

static void sp_i2cm_data0_set(void __iomem *sr, unsigned int *wdata)
{
	writel(*wdata, sr + SP_I2C_DATA0_REG);
}


static void sp_i2cm_int_en0_disable(void __iomem *sr, unsigned int int0)
{
	unsigned int val;

	val = readl(sr + SP_I2C_INT_EN0_REG);
	val &= (~int0);
	writel(val, sr + SP_I2C_INT_EN0_REG);

}

static void sp_i2cm_rdata_flag_get(void __iomem *sr, unsigned int *flag)
{
		*flag = readl(sr + SP_I2C_STATUS3_REG);
}

static unsigned int sp_i2cm_over_flag_get(void __iomem *sr)
{
	return readl(sr + SP_I2C_STATUS4_REG);
}

static void sp_i2cm_data_get(void __iomem *sr, unsigned int index, unsigned int *rdata)
{
		switch (index) {
		case 0:
			*rdata = readl(sr + SP_I2C_DATA0_REG);
			break;

		case 1:
			*rdata = readl(sr + SP_I2C_DATA4_REG);
			break;

		case 2:
			*rdata = readl(sr + SP_I2C_DATA8_REG);
			break;

		case 3:
			*rdata = readl(sr + SP_I2C_DATA12_REG);
			break;

		case 4:
			*rdata = readl(sr + SP_I2C_DATA16_REG);
			break;

		case 5:
			*rdata = readl(sr + SP_I2C_DATA20_REG);
			break;

		case 6:
			*rdata = readl(sr + SP_I2C_DATA24_REG);
			break;

		case 7:
			*rdata = readl(sr + SP_I2C_DATA28_REG);
			break;

		default:
			break;
		}
}

static void sp_i2cm_rdata_flag_clear(void __iomem *sr, unsigned int flag)
{
		writel(flag, sr + SP_I2C_CTL6_REG);
		writel(0, sr + SP_I2C_CTL6_REG);
}

static void sp_i2cm_clock_freq_set(void __iomem *sr,  unsigned int freq)
{
	unsigned int div;
	unsigned int ctl0, ctl2;

		div = SP_CLK_SOURCE_FREQ / freq;
		div -= 1;
		if (SP_CLK_SOURCE_FREQ % freq != 0)
			div += 1;

		if (div > SP_I2C_CTL2_FREQ_CUSTOM_MASK)
			div = SP_I2C_CTL2_FREQ_CUSTOM_MASK;

		ctl0 = readl(sr + SP_I2C_CTL0_REG);
		ctl0 &= (~SP_I2C_CTL0_FREQ(SP_I2C_CTL0_FREQ_MASK));
		writel(ctl0, sr + SP_I2C_CTL0_REG);

		ctl2 = readl(sr + SP_I2C_CTL2_REG);
		ctl2 &= (~SP_I2C_CTL2_FREQ_CUSTOM(SP_I2C_CTL2_FREQ_CUSTOM_MASK));
		ctl2 |= SP_I2C_CTL2_FREQ_CUSTOM(div);
		writel(ctl2, sr + SP_I2C_CTL2_REG);

}

static void sp_i2cm_slave_addr_set(void __iomem *sr, unsigned int addr)
{
	unsigned int t_addr = addr & SP_I2C_CTL0_SLAVE_ADDR_MASK;
	unsigned int ctl0;

		ctl0 = readl(sr + SP_I2C_CTL0_REG);
		ctl0 &= (~SP_I2C_CTL0_SLAVE_ADDR(SP_I2C_CTL0_SLAVE_ADDR_MASK));
		ctl0 |= SP_I2C_CTL0_SLAVE_ADDR(t_addr);
		writel(ctl0, sr + SP_I2C_CTL0_REG);
}

static void sp_i2cm_scl_delay_set(void __iomem *sr, unsigned int delay)
{
	unsigned int ctl2;

		ctl2 = readl(sr + SP_I2C_CTL2_REG);
		ctl2 &= (~SP_I2C_CTL2_SCL_DELAY(SP_I2C_CTL2_SCL_DELAY_MASK));
		ctl2 |= SP_I2C_CTL2_SCL_DELAY(delay);
		ctl2 &= (~(SP_I2C_CTL2_SDA_HALF_ENABLE));
		writel(ctl2, sr + SP_I2C_CTL2_REG);
}

static void sp_i2cm_trans_cnt_set(void __iomem *sr, unsigned int write_cnt,
		unsigned int read_cnt)
{
	unsigned int t_write = write_cnt & SP_I2C_CTL7_RW_COUNT_MASK;
	unsigned int t_read = read_cnt & SP_I2C_CTL7_RW_COUNT_MASK;
	unsigned int ctl7;

		ctl7 = SP_I2C_CTL7_WRCOUNT(t_write) | SP_I2C_CTL7_RDCOUNT(t_read);
		writel(ctl7, sr + SP_I2C_CTL7_REG);
}

static void sp_i2cm_active_mode_set(void __iomem *sr, enum sp_i2c_active_mode mode)
{
	unsigned int val;

		val = readl(sr + SP_I2C_MOD_REG);
		val &= (~(SP_I2C_MODE_MANUAL_MODE | SP_I2C_MODE_MANUAL_TRIG));
		switch (mode) {
		default:
		case I2C_TRIGGER:
			break;

		case I2C_AUTO:
			val |= SP_I2C_MODE_MANUAL_MODE;
			break;
		}
		writel(val, sr + SP_I2C_MOD_REG);
}

static void sp_i2cm_data_set(void __iomem *sr, unsigned int *wdata)
{
		writel(wdata[0], sr + SP_I2C_DATA0_REG);
		writel(wdata[1], sr + SP_I2C_DATA4_REG);
		writel(wdata[2], sr + SP_I2C_DATA8_REG);
		writel(wdata[3], sr + SP_I2C_DATA12_REG);
		writel(wdata[4], sr + SP_I2C_DATA16_REG);
		writel(wdata[5], sr + SP_I2C_DATA20_REG);
		writel(wdata[6], sr + SP_I2C_DATA24_REG);
		writel(wdata[7], sr + SP_I2C_DATA28_REG);
}

static void sp_i2cm_rw_mode_set(void __iomem *sr, enum sp_i2c_mode rw_mode)
{
	unsigned int ctl0;

		ctl0 = readl(sr + SP_I2C_CTL0_REG);
		switch (rw_mode) {
		default:
		case I2C_WRITE_MODE:
			ctl0 &= (~(SP_I2C_CTL0_PREFETCH |
				SP_I2C_CTL0_RESTART_EN | SP_I2C_CTL0_SUBADDR_EN));
			break;

		case I2C_READ_MODE:
			ctl0 &= (~(SP_I2C_CTL0_RESTART_EN | SP_I2C_CTL0_SUBADDR_EN));
			ctl0 |= SP_I2C_CTL0_PREFETCH;
			break;

		case I2C_RESTART_MODE:
			ctl0 |= (SP_I2C_CTL0_PREFETCH |
				SP_I2C_CTL0_RESTART_EN | SP_I2C_CTL0_SUBADDR_EN);
			break;
		}
		writel(ctl0, sr + SP_I2C_CTL0_REG);
}


static void sp_i2cm_int_en0_set(void __iomem *sr, unsigned int int0)
{
		writel(int0, sr + SP_I2C_INT_EN0_REG);
}

static void sp_i2cm_int_en1_set(void __iomem *sr, unsigned int rdata_en)
{
		writel(rdata_en, sr + SP_I2C_INT_EN1_REG);
}

static void sp_i2cm_int_en2_set(void __iomem *sr, unsigned int overflow_en)
{
		writel(overflow_en, sr + SP_I2C_INT_EN2_REG);
}

static void sp_i2cm_enable(unsigned int device_id, void __iomem *membase)
{
		writel(SP_I2C_RESET(device_id, 0),  membase + SP_I2C_POWER_CLKEN3);
		writel(SP_I2C_CLKEN(device_id, 1),  membase + SP_I2C_POWER_GCLKEN3);
		writel(SP_I2C_GCLKEN(device_id, 0), membase + SP_I2C_POWER_RESET3);
}

static void sp_i2cm_manual_trigger(void __iomem *sr)
{
	unsigned int val;

		val = readl(sr + SP_I2C_MOD_REG);
		val |= SP_I2C_MODE_MANUAL_TRIG;
		writel(val, sr + SP_I2C_MOD_REG);
}

static void sp_i2cm_int_en0_with_thershold_set(void __iomem *sr,
		unsigned int int0, unsigned char threshold)
{
	unsigned int val;

		val = (int0 | SP_I2C_EN0_CTL_EMPTY_THRESHOLD(threshold));
		writel(val, sr + SP_I2C_INT_EN0_REG);
}

static void sp_i2cm_dma_mode_enable(void __iomem *sr)
{
	unsigned int val;

		val = readl(sr + SP_I2C_MOD_REG);
		val |= SP_I2C_MODE_DMA_MODE;
		writel(val, sr + SP_I2C_MOD_REG);
}

static unsigned int sp_i2cm_get_dma_int_flag(void __iomem *sr_dma)
{
	return readl(sr_dma + SP_I2C_INT_REG);
}


static void sp_i2cm_dma_int_flag_clear(void __iomem *sr_dma, unsigned int flag)
{
	unsigned int val;

	val = readl(sr_dma + SP_I2C_DMA_FLAG_REG);
	val |= flag;
	writel(val, sr_dma + SP_I2C_DMA_FLAG_REG);
}


static void sp_i2cm_dma_addr_set(void __iomem *sr_dma, unsigned int addr)
{
		writel(addr, sr_dma + SP_I2C_DMA_ADDR_REG);
}

static void sp_i2cm_dma_length_set(void __iomem *sr_dma, unsigned int length)
{
		length &= (0xFFFF);  //only support 16 bit
		writel(length, sr_dma + SP_I2C_DMA_LEN_REG);
}

static void sp_i2cm_dma_rw_mode_set(void __iomem *sr_dma,
		enum sp_i2c_dma_mode rw_mode)
{
	unsigned int val;

	val = readl(sr_dma + SP_I2C_DMA_CONF_REG);
	switch (rw_mode) {
	default:
	case I2C_DMA_WRITE_MODE:
		val |= SP_I2C_DMA_CFG_DMA_MODE;
		break;

	case I2C_DMA_READ_MODE:
		val &= (~SP_I2C_DMA_CFG_DMA_MODE);
		break;
	}
	writel(val, sr_dma + SP_I2C_DMA_CONF_REG);
}

static void sp_i2cm_dma_int_en_set(void __iomem *sr_dma, unsigned int dma_int)
{
		writel(dma_int, sr_dma + SP_I2C_DMA_INT_EN_REG);
}

static void sp_i2cm_dma_go_set(void __iomem *sr_dma)
{
	unsigned int val;

	val = readl(sr_dma + SP_I2C_DMA_CONF_REG);
	val |= SP_I2C_DMA_CFG_DMA_GO;
	writel(val, sr_dma + SP_I2C_DMA_CONF_REG);
}


static void _sp_i2cm_intflag_check(struct sp_i2c_dev *sp_i2c_dev_info,
		struct sp_i2c_irq_event *sp_i2c_irq_event)
{
	void __iomem *sr = sp_i2c_dev_info->i2c_regs;
	unsigned int int_flag = 0;
	unsigned int overflow_flag = 0;

	int_flag = sp_i2cm_get_int_flag(sr);

	if (int_flag & SP_I2C_INT_DONE_FLAG)
		sp_i2c_irq_event->irq_flag.active_done = 1;
	else
		sp_i2c_irq_event->irq_flag.active_done = 0;

	if (int_flag & SP_I2C_INT_ADDRESS_NACK_FLAG)
		sp_i2c_irq_event->irq_flag.addr_nack = 1;
	else
		sp_i2c_irq_event->irq_flag.addr_nack = 0;

	if (int_flag & SP_I2C_INT_DATA_NACK_FLAG)
		sp_i2c_irq_event->irq_flag.data_nack = 1;
	else
		sp_i2c_irq_event->irq_flag.data_nack = 0;

	// write use
	if (int_flag & SP_I2C_INT_EMPTY_THRESHOLD_FLAG)
		sp_i2c_irq_event->irq_flag.empty_threshold = 1;
	else
		sp_i2c_irq_event->irq_flag.empty_threshold = 0;

	// write use
	if (int_flag & SP_I2C_INT_EMPTY_FLAG)
		sp_i2c_irq_event->irq_flag.fifo_empty = 1;
	else
		sp_i2c_irq_event->irq_flag.fifo_empty = 0;

	// write use (for debug)
	if (int_flag & SP_I2C_INT_FULL_FLAG)
		sp_i2c_irq_event->irq_flag.fifo_full = 1;
	else
		sp_i2c_irq_event->irq_flag.fifo_full = 0;

	if (int_flag & SP_I2C_INT_SCL_HOLD_TOO_LONG_FLAG)
		sp_i2c_irq_event->irq_flag.scl_hold_too_long = 1;
	else
		sp_i2c_irq_event->irq_flag.scl_hold_too_long = 0;

	sp_i2cm_status_clear(sr, SP_I2C_CTL1_ALL_CLR);

	overflow_flag = sp_i2cm_over_flag_get(sr);

	if (overflow_flag)
		sp_i2c_irq_event->irq_flag.read_over_flow = 1;
	else
		sp_i2c_irq_event->irq_flag.read_over_flow = 0;

}

static void _sp_i2cm_dma_intflag_check(struct sp_i2c_dev *sp_i2c_dev_info,
		struct sp_i2c_irq_event *sp_i2c_irq_event)
{
	void __iomem *sr_dma = sp_i2c_dev_info->i2c_dma_regs;
	unsigned int int_flag = 0;

	int_flag = sp_i2cm_get_dma_int_flag(sr_dma);

	if (int_flag & SP_I2C_DMA_INT_DMA_DONE_FLAG)
		sp_i2c_irq_event->irq_dma_flag.dma_done = 1;
	else
		sp_i2c_irq_event->irq_dma_flag.dma_done = 0;

	if (int_flag & SP_I2C_DMA_INT_WCNT_ERROR_FLAG)
		sp_i2c_irq_event->irq_dma_flag.write_cnt_err = 1;
	else
		sp_i2c_irq_event->irq_dma_flag.write_cnt_err = 0;

	if (int_flag & SP_I2C_DMA_INT_WB_EN_ERROR_FLAG)
		sp_i2c_irq_event->irq_dma_flag.wb_en_err = 1;
	else
		sp_i2c_irq_event->irq_dma_flag.wb_en_err = 0;

	if (int_flag & SP_I2C_DMA_INT_GDMA_TIMEOUT_FLAG)
		sp_i2c_irq_event->irq_dma_flag.gdma_timeout = 1;
	else
		sp_i2c_irq_event->irq_dma_flag.gdma_timeout = 0;

	if (int_flag & SP_I2C_DMA_INT_IP_TIMEOUT_FLAG)
		sp_i2c_irq_event->irq_dma_flag.ipt_timerout = 1;
	else
		sp_i2c_irq_event->irq_dma_flag.ipt_timerout = 0;

	if (int_flag & SP_I2C_DMA_INT_LENGTH0_FLAG)
		sp_i2c_irq_event->irq_dma_flag.dma_ligth = 1;
	else
		sp_i2c_irq_event->irq_dma_flag.dma_ligth = 0;

	sp_i2cm_dma_int_flag_clear(sr_dma, 0x7F);  //write 1 to clear

}

static irqreturn_t _sp_i2cm_irqevent_handler(int irq, void *args)
{
	struct sp_i2c_dev *sp_i2c_dev_info = args;
	struct sp_i2c_irq_event *sp_i2c_irq_event = &(sp_i2c_dev_info->sp_i2c_irq_info);
	void __iomem *sr = sp_i2c_dev_info->i2c_regs;
	unsigned char w_data[32] = {0};
	unsigned char r_data[SP_I2C_BURST_RDATA_BYTES] = {0};
	unsigned int rdata_flag = 0;
	unsigned int bit_index = 0;
	int i = 0, j = 0, k = 0;

	_sp_i2cm_intflag_check(sp_i2c_dev_info, sp_i2c_irq_event);

switch (sp_i2c_irq_event->rw_state) {
case I2C_WRITE_STATE:
case I2C_DMA_WRITE_STATE:
	if (sp_i2c_irq_event->irq_flag.active_done) {
		sp_i2c_irq_event->ret = I2C_SUCCESS;
		wake_up(&sp_i2c_dev_info->wait);
	} else if (sp_i2c_irq_event->irq_flag.addr_nack || sp_i2c_irq_event->irq_flag.data_nack) {

		if (sp_i2c_irq_event->rw_state == I2C_DMA_WRITE_STATE)
			dev_err(sp_i2c_dev_info->dev, "DMA wtire NACK!!\n");
		else
			dev_err(sp_i2c_dev_info->dev, "wtire NACK!!\n");

		sp_i2c_irq_event->ret = I2C_ERR_RECEIVE_NACK;
		sp_i2c_irq_event->irq_flag.active_done = 1;
		wake_up(&sp_i2c_dev_info->wait);
		sp_i2cm_reset(sr);
	} else if (sp_i2c_irq_event->irq_flag.scl_hold_too_long) {
		sp_i2c_irq_event->ret = I2C_ERR_SCL_HOLD_TOO_LONG;
		sp_i2c_irq_event->irq_flag.active_done = 1;
		wake_up(&sp_i2c_dev_info->wait);
		sp_i2cm_reset(sr);
	} else if (sp_i2c_irq_event->irq_flag.fifo_empty) {
		sp_i2c_irq_event->ret = I2C_ERR_FIFO_EMPTY;
		sp_i2c_irq_event->irq_flag.active_done = 1;
		wake_up(&sp_i2c_dev_info->wait);
		sp_i2cm_reset(sr);
	} else if ((sp_i2c_irq_event->burst_cnt > 0) &&
			(sp_i2c_irq_event->rw_state == I2C_WRITE_STATE)) {
		if (sp_i2c_irq_event->irq_flag.empty_threshold) {
			for (i = 0; i < SP_I2C_EMPTY_THRESHOLD; i++) {
				for (j = 0; j < 4; j++) {

					if (sp_i2c_irq_event->data_index >=
							sp_i2c_irq_event->data_total_len)
						w_data[j] = 0;
					else
						w_data[j] =
					sp_i2c_irq_event->data_buf[sp_i2c_irq_event->data_index];

					sp_i2c_irq_event->data_index++;
					}
					    sp_i2cm_data0_set(sr, (unsigned int *)w_data);
					    sp_i2c_irq_event->burst_cnt--;
					if (sp_i2c_irq_event->burst_cnt == 0) {
						sp_i2cm_int_en0_disable(sr,
					(SP_I2C_EN0_EMPTY_THRESHOLD_INT | SP_I2C_EN0_EMPTY_INT));
						break;
					}
				}
				sp_i2cm_status_clear(sr, SP_I2C_CTL1_EMPTY_THRESHOLD_CLR);
			}
	}
	break;

case I2C_READ_STATE:
case I2C_DMA_READ_STATE:
		if (sp_i2c_irq_event->irq_flag.addr_nack || sp_i2c_irq_event->irq_flag.data_nack) {

			if (sp_i2c_irq_event->rw_state == I2C_DMA_READ_STATE)
				dev_err(sp_i2c_dev_info->dev, "DMA read NACK!!\n");
			else
				dev_err(sp_i2c_dev_info->dev, "read NACK!!\n");

			sp_i2c_irq_event->ret = I2C_ERR_RECEIVE_NACK;
			sp_i2c_irq_event->irq_flag.active_done = 1;
			wake_up(&sp_i2c_dev_info->wait);
			sp_i2cm_reset(sr);
		} else if (sp_i2c_irq_event->irq_flag.scl_hold_too_long) {
			sp_i2c_irq_event->ret = I2C_ERR_SCL_HOLD_TOO_LONG;
			sp_i2c_irq_event->irq_flag.active_done = 1;
			wake_up(&sp_i2c_dev_info->wait);
			sp_i2cm_reset(sr);
		} else if (sp_i2c_irq_event->irq_flag.read_over_flow) {
			sp_i2c_irq_event->ret = I2C_ERR_RDATA_OVERFLOW;
			sp_i2c_irq_event->irq_flag.active_done = 1;
			wake_up(&sp_i2c_dev_info->wait);
			sp_i2cm_reset(sr);
} else {
	if ((sp_i2c_irq_event->burst_cnt > 0) && (sp_i2c_irq_event->rw_state == I2C_READ_STATE)) {
		sp_i2cm_rdata_flag_get(sr, &rdata_flag);
		for (i = 0; i < (32 / SP_I2C_BURST_RDATA_BYTES); i++) {
			bit_index = (SP_I2C_BURST_RDATA_BYTES - 1) + (SP_I2C_BURST_RDATA_BYTES * i);
			if (rdata_flag & (1 << bit_index)) {
				for (j = 0; j < (SP_I2C_BURST_RDATA_BYTES/4); j++) {
					k = sp_i2c_irq_event->reg_data_index + j;
					if (k >= 8)
						k -= 8;

					sp_i2cm_data_get(sr, k,
(unsigned int *)(&sp_i2c_irq_event->data_buf[sp_i2c_irq_event->data_index]));
					sp_i2c_irq_event->data_index += 4;
				}
				sp_i2cm_rdata_flag_clear(sr,
			(((1 << SP_I2C_BURST_RDATA_BYTES) - 1) << (SP_I2C_BURST_RDATA_BYTES * i)));
				sp_i2c_irq_event->reg_data_index += (SP_I2C_BURST_RDATA_BYTES / 4);
				if (sp_i2c_irq_event->reg_data_index >= 8)
					sp_i2c_irq_event->burst_cnt--;
			}
		}
	}
		if (sp_i2c_irq_event->irq_flag.active_done) {
			if ((sp_i2c_irq_event->burst_remainder) &&
				(sp_i2c_irq_event->rw_state == I2C_READ_STATE)) {
				j = 0;
			for (i = 0; i < (SP_I2C_BURST_RDATA_BYTES/4); i++) {
				k = sp_i2c_irq_event->reg_data_index + i;
				if (k >= 8)
					k -= 8;

				sp_i2cm_data_get(sr, k, (unsigned int *)(&r_data[j]));
				j += 4;
			}

				for (i = 0; i < sp_i2c_irq_event->burst_remainder; i++)
					sp_i2c_irq_event->data_buf[sp_i2c_irq_event->data_index + i]
					= r_data[i];
			}
				sp_i2c_irq_event->ret = I2C_SUCCESS;
				wake_up(&sp_i2c_dev_info->wait);
		}
	}
	break;

default:
	break;
}
	//switch case

	_sp_i2cm_dma_intflag_check(sp_i2c_dev_info, sp_i2c_irq_event);

	switch (sp_i2c_irq_event->rw_state) {
	case I2C_DMA_WRITE_STATE:
			if (sp_i2c_irq_event->irq_dma_flag.dma_done) {
				sp_i2c_irq_event->ret = I2C_SUCCESS;
				wake_up(&sp_i2c_dev_info->wait);
			}
			break;

	case I2C_DMA_READ_STATE:
			if (sp_i2c_irq_event->irq_dma_flag.dma_done) {
				sp_i2c_irq_event->ret = I2C_SUCCESS;
				wake_up(&sp_i2c_dev_info->wait);
			}
			break;

	default:
			break;
	}

	return IRQ_HANDLED;
}


static int _sp_i2cm_init(unsigned int device_id, struct sp_i2c_dev *sp_i2c_dev_info)
{
	void __iomem *sr = sp_i2c_dev_info->i2c_regs;

	if (device_id >= sp_i2c_dev_info->total_port)
		return I2C_ERR_INVALID_DEVID;

	sp_i2cm_reset(sr);

	return I2C_SUCCESS;
}

static int _sp_i2cm_get_irq(struct platform_device *pdev, struct sp_i2c_dev *sp_i2c_dev_info)
{
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(sp_i2c_dev_info->dev, " get irq number fail, irq = %d\n", irq);
		return -ENODEV;
	}

	sp_i2c_dev_info->irq = irq;
	return I2C_SUCCESS;
}

static int _sp_i2cm_get_resources(struct platform_device *pdev,
		struct sp_i2c_dev *sp_i2c_dev_info)
{
	int ret;
	struct resource *res;

	/* find and map our resources */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, SP_I2C_REG_NAME);
	if (res) {
		sp_i2c_dev_info->i2c_regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(sp_i2c_dev_info->i2c_regs))
			dev_err(&pdev->dev, " platform_get_resource_byname fail\n");
	} else {
		dev_err(sp_i2c_dev_info->dev, "adapter %s (%d)\n", __func__, __LINE__);
		return -ENODEV;
	}


	/* find DMA and map our resources */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, SP_I2C_DMA_REG_NAME);
	if (res) {
		sp_i2c_dev_info->i2c_dma_regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(sp_i2c_dev_info->i2c_dma_regs))
			dev_err(&pdev->dev, " platform_get_resource_byname fail\n");
	} else {
		dev_err(sp_i2c_dev_info->dev, "adapter %s (%d)\n", __func__, __LINE__);
		return -ENODEV;
	}

	ret = _sp_i2cm_get_irq(pdev, sp_i2c_dev_info);
	if (ret) {
		dev_err(sp_i2c_dev_info->dev, " line (%d) ret = %d\n", __LINE__, ret);
		return ret;
	}

	return I2C_SUCCESS;
}

static int sp_i2cm_read(struct sp_i2c_cmd *sp_i2c_cmd_info, struct sp_i2c_dev *sp_i2c_dev_info)
{
	void __iomem *sr = sp_i2c_dev_info->i2c_regs;
	struct sp_i2c_irq_event *sp_i2c_irq_event = &(sp_i2c_dev_info->sp_i2c_irq_info);
	unsigned char w_data[32] = {0};
	unsigned int read_cnt = 0;
	unsigned int write_cnt = 0;
	unsigned int burst_cnt = 0, burst_r = 0;
	unsigned int int0 = 0, int1 = 0, int2 = 0;
	int ret = I2C_SUCCESS;
	int i = 0;

	if (sp_i2c_cmd_info->dev_id > sp_i2c_dev_info->total_port)
		return I2C_ERR_INVALID_DEVID;

	if (sp_i2c_irq_event->busy) {
		dev_err(sp_i2c_dev_info->dev, "I2C is busy !!\n");
		return I2C_ERR_I2C_BUSY;
	}

	memset(sp_i2c_irq_event, 0, sizeof(*sp_i2c_irq_event));
	sp_i2c_irq_event->busy = 1;

	write_cnt = sp_i2c_cmd_info->write_cnt;
	read_cnt = sp_i2c_cmd_info->read_cnt;

	if (sp_i2c_cmd_info->restart_en) {
		//if ((write_cnt > 32) || (write_cnt == 0)) {
		if (write_cnt > 32) {
			sp_i2c_irq_event->busy = 0;
			dev_err(sp_i2c_dev_info->dev,
				"I2C write count is invalid !! write count=%d\n", write_cnt);
			return I2C_ERR_INVALID_CNT;
		}
	}

	if ((read_cnt > 0xFFFF)  || (read_cnt == 0)) {
		sp_i2c_irq_event->busy = 0;
		dev_err(sp_i2c_dev_info->dev,
			"I2C read count is invalid !! read count=%d\n", read_cnt);
		return I2C_ERR_INVALID_CNT;
	}

	burst_cnt = read_cnt / SP_I2C_BURST_RDATA_BYTES;
	burst_r = read_cnt % SP_I2C_BURST_RDATA_BYTES;

	int0 = (SP_I2C_EN0_SCL_HOLD_TOO_LONG_INT | SP_I2C_EN0_EMPTY_INT | SP_I2C_EN0_DATA_NACK_INT
			| SP_I2C_EN0_ADDRESS_NACK_INT | SP_I2C_EN0_DONE_INT);
	if (burst_cnt) {
		int1 = SP_I2C_BURST_RDATA_FLAG;
		int2 = SP_I2C_BURST_RDATA_ALL_FLAG;
	}

	sp_i2c_irq_event->rw_state = I2C_READ_STATE;
	sp_i2c_irq_event->burst_cnt = burst_cnt;
	sp_i2c_irq_event->burst_remainder = burst_r;
	sp_i2c_irq_event->data_index = 0;
	sp_i2c_irq_event->reg_data_index = 0;
	sp_i2c_irq_event->data_total_len = read_cnt;
	sp_i2c_irq_event->data_buf = sp_i2c_cmd_info->read_data;

	//hal_i2cm_reset(sp_i2c_cmd_info->dev_id);
	sp_i2cm_reset(sr);
	sp_i2cm_clock_freq_set(sr, sp_i2c_cmd_info->freq);
	sp_i2cm_slave_addr_set(sr, sp_i2c_cmd_info->slave_addr);
	sp_i2cm_scl_delay_set(sr, SP_I2C_SCL_DELAY);
	sp_i2cm_trans_cnt_set(sr, write_cnt, read_cnt);
	sp_i2cm_active_mode_set(sr, I2C_TRIGGER);

	if (sp_i2c_cmd_info->restart_en) {
		for (i = 0; i < write_cnt; i++)
			w_data[i] = sp_i2c_cmd_info->write_data[i];

		sp_i2cm_data_set(sr, (unsigned int *)w_data);
		sp_i2cm_rw_mode_set(sr, I2C_RESTART_MODE);
	} else {
		sp_i2cm_rw_mode_set(sr, I2C_READ_MODE);
	}

	sp_i2cm_int_en0_set(sr, int0);
	sp_i2cm_int_en1_set(sr, int1);
	sp_i2cm_int_en2_set(sr, int2);
	sp_i2cm_manual_trigger(sr);	//start send data

	ret = wait_event_timeout(sp_i2c_dev_info->wait,
			sp_i2c_irq_event->irq_flag.active_done, (SP_I2C_SLEEP_TIMEOUT * HZ) / 500);
	if (ret == 0) {
		dev_err(sp_i2c_dev_info->dev, "I2C read timeout !!\n");
		ret = I2C_ERR_TIMEOUT_OUT;
	} else {
		ret = sp_i2c_irq_event->ret;
	}
	sp_i2cm_reset(sr);
	sp_i2c_irq_event->rw_state = I2C_IDLE_STATE;
	sp_i2c_irq_event->busy = 0;

	return ret;
}

static int sp_i2cm_write(struct sp_i2c_cmd *sp_i2c_cmd_info, struct sp_i2c_dev *sp_i2c_dev_info)
{
	void __iomem *sr = sp_i2c_dev_info->i2c_regs;
	struct sp_i2c_irq_event *sp_i2c_irq_event = &(sp_i2c_dev_info->sp_i2c_irq_info);
	unsigned char w_data[32] = {0};
	unsigned int write_cnt = 0;
	unsigned int burst_cnt = 0;
	unsigned int int0 = 0;
	int ret = I2C_SUCCESS;
	int i = 0;

	if (sp_i2c_cmd_info->dev_id > sp_i2c_dev_info->total_port)
		return I2C_ERR_INVALID_DEVID;

	if (sp_i2c_irq_event->busy) {
		dev_err(sp_i2c_dev_info->dev, "I2C is busy !!\n");
		return I2C_ERR_I2C_BUSY;
	}

	memset(sp_i2c_irq_event, 0, sizeof(*sp_i2c_irq_event));
	sp_i2c_irq_event->busy = 1;

	write_cnt = sp_i2c_cmd_info->write_cnt;

	if (write_cnt > 0xFFFF) {
		sp_i2c_irq_event->busy = 0;
		dev_err(sp_i2c_dev_info->dev,
			"I2C write count is invalid !! write count=%d\n", write_cnt);
		return I2C_ERR_INVALID_CNT;
	}

	if (write_cnt > 32) {
		burst_cnt = (write_cnt - 32) / 4;
		if ((write_cnt - 32) % 4)
			burst_cnt += 1;
	for (i = 0; i < 32; i++)
		w_data[i] = sp_i2c_cmd_info->write_data[i];
	} else {
		for (i = 0; i < write_cnt; i++)
			w_data[i] = sp_i2c_cmd_info->write_data[i];
	}

	int0 = (SP_I2C_EN0_SCL_HOLD_TOO_LONG_INT | SP_I2C_EN0_EMPTY_INT | SP_I2C_EN0_DATA_NACK_INT
			| SP_I2C_EN0_ADDRESS_NACK_INT | SP_I2C_EN0_DONE_INT);

	if (burst_cnt)
		int0 |= SP_I2C_EN0_EMPTY_THRESHOLD_INT;

	sp_i2c_irq_event->rw_state = I2C_WRITE_STATE;
	sp_i2c_irq_event->burst_cnt = burst_cnt;
	sp_i2c_irq_event->data_index = i;
	sp_i2c_irq_event->data_total_len = write_cnt;
	sp_i2c_irq_event->data_buf = sp_i2c_cmd_info->write_data;

	sp_i2cm_reset(sr);
	sp_i2cm_clock_freq_set(sr, sp_i2c_cmd_info->freq);
	sp_i2cm_slave_addr_set(sr, sp_i2c_cmd_info->slave_addr);
	sp_i2cm_scl_delay_set(sr, SP_I2C_SCL_DELAY);
	sp_i2cm_trans_cnt_set(sr, write_cnt, 0);
	sp_i2cm_active_mode_set(sr, I2C_TRIGGER);
	sp_i2cm_rw_mode_set(sr, I2C_WRITE_MODE);
	sp_i2cm_data_set(sr, (unsigned int *)w_data);

	if (burst_cnt)
		sp_i2cm_int_en0_with_thershold_set(sr, int0, SP_I2C_EMPTY_THRESHOLD);
	else
		sp_i2cm_int_en0_set(sr, int0);

	sp_i2cm_manual_trigger(sr);	//start send data

	ret = wait_event_timeout(sp_i2c_dev_info->wait,
	sp_i2c_irq_event->irq_flag.active_done, (SP_I2C_SLEEP_TIMEOUT * HZ) / 500);
	if (ret == 0) {
		dev_err(sp_i2c_dev_info->dev, "I2C write timeout !!\n");
		ret = I2C_ERR_TIMEOUT_OUT;
	} else {
		ret = sp_i2c_irq_event->ret;
	}
	sp_i2cm_reset(sr);
	sp_i2c_irq_event->rw_state = I2C_IDLE_STATE;
	sp_i2c_irq_event->busy = 0;

	return ret;
}


static int sp_i2cm_dma_write(struct sp_i2c_cmd *sp_i2c_cmd_info, struct sp_i2c_dev *sp_i2c_dev_info)
{
	void __iomem *sr = sp_i2c_dev_info->i2c_regs;
	void __iomem *sr_dma = (struct regs_i2cm_dma_s *)sp_i2c_dev_info->i2c_dma_regs;
	struct sp_i2c_irq_event *sp_i2c_irq_event = &(sp_i2c_dev_info->sp_i2c_irq_info);
	unsigned int int0 = 0;
	int ret = I2C_SUCCESS;
	unsigned int dma_int = 0;
	dma_addr_t dma_w_addr = 0;

	if (sp_i2c_cmd_info->dev_id > sp_i2c_dev_info->total_port)
		return I2C_ERR_INVALID_DEVID;

	if (sp_i2c_dev_info->mode == I2C_POWER_ALL_SWITCH)
		sp_i2cm_enable(0, (void __iomem *)SP_I2C_POWER_BASE);

	if (sp_i2c_irq_event->busy) {
		dev_err(sp_i2c_dev_info->dev, "I2C is busy !!\n");
		return I2C_ERR_I2C_BUSY;
	}

	memset(sp_i2c_irq_event, 0, sizeof(*sp_i2c_irq_event));
	sp_i2c_irq_event->busy = 1;

	if (sp_i2c_cmd_info->write_cnt > 0xFFFF) {
		sp_i2c_irq_event->busy = 0;
		dev_err(sp_i2c_dev_info->dev, " I2C write count is invalid !! write count=%d\n",
			sp_i2c_cmd_info->write_cnt);
		return I2C_ERR_INVALID_CNT;
	}

	sp_i2c_irq_event->rw_state = I2C_DMA_WRITE_STATE;

	dma_w_addr = dma_map_single(sp_i2c_dev_info->dev, sp_i2c_cmd_info->write_data,
			sp_i2c_cmd_info->write_cnt, DMA_TO_DEVICE);

	if (dma_mapping_error(sp_i2c_dev_info->dev, dma_w_addr)) {
		dev_err(sp_i2c_dev_info->dev, "I2C dma_w_addr fail\n");
		dma_w_addr = sp_i2c_dev_info->dma_phy_base;
		memcpy(sp_i2c_dev_info->dma_vir_base,
			sp_i2c_cmd_info->write_data, sp_i2c_cmd_info->write_cnt);
	}

	int0 = (SP_I2C_EN0_SCL_HOLD_TOO_LONG_INT | SP_I2C_EN0_EMPTY_INT
	 | SP_I2C_EN0_DATA_NACK_INT | SP_I2C_EN0_ADDRESS_NACK_INT | SP_I2C_EN0_DONE_INT);


	dma_int = SP_I2C_DMA_EN_DMA_DONE_INT;


	sp_i2cm_reset(sr);
	sp_i2cm_dma_mode_enable(sr);
	sp_i2cm_clock_freq_set(sr, sp_i2c_cmd_info->freq);
	sp_i2cm_slave_addr_set(sr, sp_i2c_cmd_info->slave_addr);
	sp_i2cm_scl_delay_set(sr, SP_I2C_SCL_DELAY);
	sp_i2cm_active_mode_set(sr, I2C_AUTO);
	sp_i2cm_rw_mode_set(sr, I2C_WRITE_MODE);
	sp_i2cm_int_en0_set(sr, int0);

	sp_i2cm_dma_addr_set(sr_dma, (unsigned int)dma_w_addr);
	sp_i2cm_dma_length_set(sr_dma, sp_i2c_cmd_info->write_cnt);
	sp_i2cm_dma_rw_mode_set(sr_dma, I2C_DMA_READ_MODE);
	sp_i2cm_dma_int_en_set(sr_dma, dma_int);
	sp_i2cm_dma_go_set(sr_dma);


	ret = wait_event_timeout(sp_i2c_dev_info->wait,
			sp_i2c_irq_event->irq_dma_flag.dma_done, (SP_I2C_SLEEP_TIMEOUT * HZ) / 200);
	if (ret == 0) {
		dev_err(sp_i2c_dev_info->dev, "I2C DMA write timeout !!\n");
		ret = I2C_ERR_TIMEOUT_OUT;
	} else {
		ret = sp_i2c_irq_event->ret;
	}
	sp_i2cm_status_clear(sr, 0xFFFFFFFF);

	if (dma_w_addr != sp_i2c_dev_info->dma_phy_base)
		dma_unmap_single(sp_i2c_dev_info->dev, dma_w_addr,
		sp_i2c_cmd_info->write_cnt, DMA_TO_DEVICE);



	sp_i2c_irq_event->rw_state = I2C_IDLE_STATE;
	sp_i2c_irq_event->busy = 0;

	sp_i2cm_reset(sr);

	return ret;
}

static int sp_i2cm_dma_read(struct sp_i2c_cmd *sp_i2c_cmd_info, struct sp_i2c_dev *sp_i2c_dev_info)
{
	void __iomem *sr = sp_i2c_dev_info->i2c_regs;
	void __iomem *sr_dma = (struct regs_i2cm_dma_s *)sp_i2c_dev_info->i2c_dma_regs;
	struct sp_i2c_irq_event *sp_i2c_irq_event = &(sp_i2c_dev_info->sp_i2c_irq_info);
	unsigned char w_data[32] = {0};
	unsigned int read_cnt = 0;
	unsigned int write_cnt = 0;
	unsigned int int0 = 0, int1 = 0, int2 = 0;
	unsigned int dma_int = 0;
	int ret = I2C_SUCCESS;
	int i = 0;
	dma_addr_t dma_r_addr = 0;

	if (sp_i2c_cmd_info->dev_id > sp_i2c_dev_info->total_port)
		return I2C_ERR_INVALID_DEVID;

	if (sp_i2c_dev_info->mode == I2C_POWER_ALL_SWITCH)
		sp_i2cm_enable(0, (void __iomem *)SP_I2C_POWER_BASE);

	if (sp_i2c_irq_event->busy) {
		dev_err(sp_i2c_dev_info->dev, "I2C is busy !!\n");
		return I2C_ERR_I2C_BUSY;
	}

	memset(sp_i2c_irq_event, 0, sizeof(*sp_i2c_irq_event));
	sp_i2c_irq_event->busy = 1;

	write_cnt = sp_i2c_cmd_info->write_cnt;
	read_cnt = sp_i2c_cmd_info->read_cnt;

	if (sp_i2c_cmd_info->restart_en) {
		if (write_cnt > 32) {
			sp_i2c_irq_event->busy = 0;
			dev_err(sp_i2c_dev_info->dev,
				"I2C write count is invalid !! write count=%d\n", write_cnt);
			return I2C_ERR_INVALID_CNT;
		}
	}

	if ((read_cnt > 0xFFFF)  || (read_cnt == 0)) {
		sp_i2c_irq_event->busy = 0;
		dev_err(sp_i2c_dev_info->dev,
			"I2C read count is invalid !! read count=%d\n", read_cnt);
		return I2C_ERR_INVALID_CNT;
	}

	dma_r_addr = dma_map_single(sp_i2c_dev_info->dev, sp_i2c_cmd_info->read_data,
		sp_i2c_cmd_info->read_cnt, DMA_FROM_DEVICE);


	if (dma_mapping_error(sp_i2c_dev_info->dev, dma_r_addr)) {
		dev_err(sp_i2c_dev_info->dev, "I2C dma_r_addr fail\n");
		dma_r_addr = sp_i2c_dev_info->dma_phy_base;
	}


	int0 = (SP_I2C_EN0_SCL_HOLD_TOO_LONG_INT | SP_I2C_EN0_EMPTY_INT | SP_I2C_EN0_DATA_NACK_INT
		| SP_I2C_EN0_ADDRESS_NACK_INT | SP_I2C_EN0_DONE_INT);

	dma_int = SP_I2C_DMA_EN_DMA_DONE_INT;

	sp_i2c_irq_event->rw_state = I2C_DMA_READ_STATE;

	sp_i2c_irq_event->data_index = 0;
	sp_i2c_irq_event->reg_data_index = 0;
	sp_i2c_irq_event->data_total_len = read_cnt;

	sp_i2cm_reset(sr);
	sp_i2cm_dma_mode_enable(sr);
	sp_i2cm_clock_freq_set(sr, sp_i2c_cmd_info->freq);
	sp_i2cm_slave_addr_set(sr, sp_i2c_cmd_info->slave_addr);
	sp_i2cm_scl_delay_set(sr, SP_I2C_SCL_DELAY);

	if (sp_i2c_cmd_info->restart_en) {
		sp_i2cm_active_mode_set(sr, I2C_TRIGGER);
		sp_i2cm_rw_mode_set(sr, I2C_RESTART_MODE);
		sp_i2cm_trans_cnt_set(sr, write_cnt, read_cnt);
		for (i = 0; i < write_cnt; i++)
			w_data[i] = sp_i2c_cmd_info->write_data[i];

		sp_i2cm_data_set(sr, (unsigned int *)w_data);
	} else {
		sp_i2cm_active_mode_set(sr, I2C_AUTO);
		sp_i2cm_rw_mode_set(sr, I2C_READ_MODE);
	}

	sp_i2cm_int_en0_set(sr, int0);
	sp_i2cm_int_en1_set(sr, int1);
	sp_i2cm_int_en2_set(sr, int2);

	sp_i2cm_dma_addr_set(sr_dma, (unsigned int)dma_r_addr);
	sp_i2cm_dma_length_set(sr_dma, sp_i2c_cmd_info->read_cnt);
	sp_i2cm_dma_rw_mode_set(sr_dma, I2C_DMA_WRITE_MODE);
	sp_i2cm_dma_int_en_set(sr_dma, dma_int);
	sp_i2cm_dma_go_set(sr_dma);


	if (sp_i2c_cmd_info->restart_en)
		sp_i2cm_manual_trigger(sr); //start send data


	ret = wait_event_timeout(sp_i2c_dev_info->wait,
			sp_i2c_irq_event->irq_dma_flag.dma_done, (SP_I2C_SLEEP_TIMEOUT * HZ) / 200);
	if (ret == 0) {
		dev_err(sp_i2c_dev_info->dev, "I2C DMA read timeout !!\n");
		ret = I2C_ERR_TIMEOUT_OUT;
	} else {
		ret = sp_i2c_irq_event->ret;
	}
	sp_i2cm_status_clear(sr, 0xFFFFFFFF);

	//copy data from virtual addr to sp_i2c_cmd_info->read_data

	if (dma_r_addr == sp_i2c_dev_info->dma_phy_base)
		memcpy(sp_i2c_cmd_info->read_data,
		sp_i2c_dev_info->dma_vir_base, sp_i2c_cmd_info->read_cnt);
	else
		dma_unmap_single(sp_i2c_dev_info->dev, dma_r_addr,
			sp_i2c_cmd_info->read_cnt, DMA_FROM_DEVICE);

	sp_i2c_irq_event->rw_state = I2C_IDLE_STATE;
	sp_i2c_irq_event->busy = 0;


	sp_i2cm_reset(sr);
	return ret;
}

static int sp_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct sp_i2c_dev *sp_i2c_dev_info = adap->algo_data;
	struct sp_i2c_cmd *sp_i2c_cmd_info = &(sp_i2c_dev_info->sp_i2c_cmd_info);
	int ret = I2C_SUCCESS;
	int i = 0;
	unsigned char restart_w_data[32] = {0};
	unsigned int  restart_write_cnt = 0;
	unsigned int  restart_en = 0;

	ret = pm_runtime_get_sync(sp_i2c_dev_info->dev);

	if (num == 0)
		return -EINVAL;

	memset(sp_i2c_cmd_info, 0, sizeof(*sp_i2c_cmd_info));
	sp_i2c_cmd_info->dev_id = adap->nr;

	if (sp_i2c_cmd_info->freq > SP_I2C_FREQ)
		sp_i2c_cmd_info->freq = SP_I2C_FREQ;
	else
		sp_i2c_cmd_info->freq = sp_i2c_dev_info->i2c_clk_freq/1000;

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_TEN)
			return -EINVAL;

		sp_i2c_cmd_info->slave_addr = msgs[i].addr;

		if (msgs[i].flags & I2C_M_NOSTART) {

			restart_write_cnt = msgs[i].len;
			for (i = 0; i < restart_write_cnt; i++)
				restart_w_data[i] = msgs[i].buf[i];

			restart_en = 1;
			continue;
		}

		if (msgs[i].flags & I2C_M_RD) {
			if (restart_en == 1) {
				sp_i2c_cmd_info->write_cnt = restart_write_cnt;
				sp_i2c_cmd_info->write_data = restart_w_data;
				restart_en = 0;
				sp_i2c_cmd_info->restart_en = 1;
			}
			sp_i2c_cmd_info->read_cnt = msgs[i].len;
			sp_i2c_cmd_info->read_data = i2c_get_dma_safe_msg_buf(&msgs[i], 4);

			if ((sp_i2c_cmd_info->read_cnt < 4) || (!sp_i2c_cmd_info->read_data)) {
				sp_i2c_cmd_info->read_data = msgs[i].buf;
				ret = sp_i2cm_read(sp_i2c_cmd_info, sp_i2c_dev_info);
			} else {
				ret = sp_i2cm_dma_read(sp_i2c_cmd_info, sp_i2c_dev_info);
				i2c_put_dma_safe_msg_buf(sp_i2c_cmd_info->read_data,
					&msgs[i], true);
			}

		} else {
			sp_i2c_cmd_info->write_cnt = msgs[i].len;
			sp_i2c_cmd_info->write_data = i2c_get_dma_safe_msg_buf(&msgs[i], 4);
				if ((sp_i2c_cmd_info->write_cnt < 4) ||
					(!sp_i2c_cmd_info->write_data)) {
					sp_i2c_cmd_info->write_data = msgs[i].buf;
					ret = sp_i2cm_write(sp_i2c_cmd_info, sp_i2c_dev_info);
				} else {
					ret = sp_i2cm_dma_write(sp_i2c_cmd_info, sp_i2c_dev_info);
					i2c_put_dma_safe_msg_buf(sp_i2c_cmd_info->write_data,
						&msgs[i], true);
				}
		}

		if (ret != I2C_SUCCESS)
			return -EIO;
	}

	pm_runtime_put(sp_i2c_dev_info->dev);
	return num;

}

static u32 sp_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm sp_algorithm = {
	.master_xfer	= sp_master_xfer,
	.functionality	= sp_functionality,
};

static const struct i2c_compatible i2c_7021_compat = {
	.mode = I2C_POWER_ALL_SWITCH,
	.total_port = 4,
};

static const struct i2c_compatible i2c_645_compat = {
	.mode = I2C_POWER_NO_SWITCH,
	.total_port = 6,

};

static const struct of_device_id sp_i2c_of_match[] = {
	{	.compatible = "sunplus,sp7021-i2cm",
		.data = &i2c_7021_compat, },
	{	.compatible = "sunplus,q645-i2cm",
		.data = &i2c_645_compat, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sp_i2c_of_match);

static int sp_i2c_probe(struct platform_device *pdev)
{
	struct sp_i2c_dev *sp_i2c_dev_info;
	struct i2c_adapter *p_adap;
	unsigned int i2c_clk_freq;
	int device_id = 0;
	int ret = I2C_SUCCESS;
	struct device *dev = &pdev->dev;
	const struct i2c_compatible *dev_mode;

	if (pdev->dev.of_node) {
		pdev->id = of_alias_get_id(pdev->dev.of_node, "i2c");
		dev_err(&pdev->dev, "pdev->id=%d\n", pdev->id);
		device_id = pdev->id;
	}

	sp_i2c_dev_info = devm_kzalloc(&pdev->dev, sizeof(*sp_i2c_dev_info), GFP_KERNEL);
	if (!sp_i2c_dev_info)
		return -ENOMEM;

	if (!of_property_read_u32(pdev->dev.of_node, "clock-frequency", &i2c_clk_freq)) {
		dev_err(&pdev->dev, "clk_freq %d\n", i2c_clk_freq);
		sp_i2c_dev_info->i2c_clk_freq = i2c_clk_freq;
	} else {
		sp_i2c_dev_info->i2c_clk_freq = SP_I2C_FREQ*1000;
	}

	sp_i2c_dev_info->dev = &pdev->dev;

	ret = _sp_i2cm_get_resources(pdev, sp_i2c_dev_info);
	if (ret != I2C_SUCCESS) {
		dev_err(&pdev->dev, " get resources fail !\n");
		return ret;
	}

	/* dma alloc*/
	sp_i2c_dev_info->dma_vir_base = dma_alloc_coherent(&pdev->dev, SP_BUFFER_SIZE,
					&sp_i2c_dev_info->dma_phy_base, GFP_ATOMIC);
	if (!sp_i2c_dev_info->dma_vir_base)
		goto free_dma;

	sp_i2c_dev_info->clk = devm_clk_get(dev, NULL);

	if (IS_ERR(sp_i2c_dev_info->clk)) {
		ret = PTR_ERR(sp_i2c_dev_info->clk);
		dev_err(&pdev->dev, "failed to retrieve clk: %d\n", ret);
		goto err_clk_disable;
	}

	sp_i2c_dev_info->rstc = devm_reset_control_get_exclusive(dev, NULL);

	if (IS_ERR(sp_i2c_dev_info->rstc)) {
		ret = PTR_ERR(sp_i2c_dev_info->rstc);
		dev_err(&pdev->dev, "failed to retrieve reset controller: %d\n", ret);
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(sp_i2c_dev_info->clk);

	if (ret) {
		dev_err(&pdev->dev, "failed to enable clk: %d\n", ret);
		goto err_clk_disable;
	}

	ret = reset_control_deassert(sp_i2c_dev_info->rstc);

	if (ret) {
		dev_err(&pdev->dev, "failed to deassert reset line: %d\n", ret);
		goto err_reset_assert;
	}

	init_waitqueue_head(&sp_i2c_dev_info->wait);

	dev_mode = of_device_get_match_data(&pdev->dev);
	sp_i2c_dev_info->mode = dev_mode->mode;
	sp_i2c_dev_info->total_port = dev_mode->total_port;
	p_adap = &sp_i2c_dev_info->adap;
	sprintf(p_adap->name, "%s%d", SP_DEVICE_NAME, device_id);
	p_adap->algo = &sp_algorithm;
	p_adap->algo_data = sp_i2c_dev_info;
	p_adap->nr = device_id;
	p_adap->class = 0;
	p_adap->retries = 5;
	p_adap->dev.parent = &pdev->dev;
	p_adap->dev.of_node = pdev->dev.of_node;

	ret = i2c_add_numbered_adapter(p_adap);

	ret = _sp_i2cm_init(device_id, sp_i2c_dev_info);
	if (ret != 0) {
		dev_err(&pdev->dev, "i2c master %d init error\n", device_id);
		goto err_reset_assert;
	}

	if (ret < 0)
		goto err_reset_assert;
	else
		platform_set_drvdata(pdev, sp_i2c_dev_info);

	ret = request_irq(sp_i2c_dev_info->irq, _sp_i2cm_irqevent_handler, IRQF_TRIGGER_HIGH,
				p_adap->name, sp_i2c_dev_info);
	if (ret) {
		dev_err(&pdev->dev, "request irq fail !!\n");
		return I2C_ERR_REQUESET_IRQ;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, 5000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return ret;

err_reset_assert:
	reset_control_assert(sp_i2c_dev_info->rstc);
err_clk_disable:
	clk_disable_unprepare(sp_i2c_dev_info->clk);
free_dma:
	dma_free_coherent(&pdev->dev, SP_BUFFER_SIZE,
			sp_i2c_dev_info->dma_vir_base, sp_i2c_dev_info->dma_phy_base);

	return ret;
}

static int sp_i2c_remove(struct platform_device *pdev)
{
	struct sp_i2c_dev *sp_i2c_dev_info = platform_get_drvdata(pdev);
	struct i2c_adapter *p_adap = &sp_i2c_dev_info->adap;

	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	dma_free_coherent(&pdev->dev, SP_BUFFER_SIZE,
				sp_i2c_dev_info->dma_vir_base, sp_i2c_dev_info->dma_phy_base);

	i2c_del_adapter(p_adap);
	if (p_adap->nr < sp_i2c_dev_info->total_port) {
		clk_disable_unprepare(sp_i2c_dev_info->clk);
		reset_control_assert(sp_i2c_dev_info->rstc);
		free_irq(sp_i2c_dev_info->irq, NULL);
	}

	return 0;
}

static int __maybe_unused sp_i2c_suspend(struct device *dev)
{
	struct sp_i2c_dev *sp_i2c_dev_info = dev_get_drvdata(dev);
	struct i2c_adapter *p_adap = &sp_i2c_dev_info->adap;

	if (p_adap->nr < sp_i2c_dev_info->total_port)
		reset_control_assert(sp_i2c_dev_info->rstc);

	return 0;
}

static int __maybe_unused sp_i2c_resume(struct device *dev)
{
	struct sp_i2c_dev *sp_i2c_dev_info = dev_get_drvdata(dev);
	struct i2c_adapter *p_adap = &sp_i2c_dev_info->adap;

	if (p_adap->nr < sp_i2c_dev_info->total_port) {
		reset_control_deassert(sp_i2c_dev_info->rstc);   //release reset
		clk_prepare_enable(sp_i2c_dev_info->clk);        //enable clken and disable gclken
	}

	return 0;
}

static int sp_i2c_runtime_suspend(struct device *dev)
{
	struct sp_i2c_dev *sp_i2c_dev_info = dev_get_drvdata(dev);
	struct i2c_adapter *p_adap = &sp_i2c_dev_info->adap;

	if (p_adap->nr < sp_i2c_dev_info->total_port)
		reset_control_assert(sp_i2c_dev_info->rstc);

	return 0;
}

static int sp_i2c_runtime_resume(struct device *dev)
{
	struct sp_i2c_dev *sp_i2c_dev_info = dev_get_drvdata(dev);
	struct i2c_adapter *p_adap = &sp_i2c_dev_info->adap;

	if (p_adap->nr < sp_i2c_dev_info->total_port) {
		reset_control_deassert(sp_i2c_dev_info->rstc);   //release reset
		clk_prepare_enable(sp_i2c_dev_info->clk);        //enable clken and disable gclken
	}

	return 0;
}
static const struct dev_pm_ops sp7021_i2c_pm_ops = {
	SET_RUNTIME_PM_OPS(sp_i2c_runtime_suspend,
			   sp_i2c_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(sp_i2c_suspend, sp_i2c_resume)

};

#define sp_i2c_pm_ops  (&sp7021_i2c_pm_ops)

static struct platform_driver sp_i2c_driver = {
	.probe		= sp_i2c_probe,
	.remove		= sp_i2c_remove,
	.driver		= {
		.owner		= THIS_MODULE,
		.name		= SP_DEVICE_NAME,
		.of_match_table = sp_i2c_of_match,
		.pm     = sp_i2c_pm_ops,
	},
};

static int __init sp_i2c_adap_init(void)
{
	return platform_driver_register(&sp_i2c_driver);
}
module_init(sp_i2c_adap_init);

static void __exit sp_i2c_adap_exit(void)
{
	platform_driver_unregister(&sp_i2c_driver);
}
module_exit(sp_i2c_adap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sunplus Technology");
MODULE_DESCRIPTION("Sunplus I2C Master Driver");
