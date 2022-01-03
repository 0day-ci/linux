// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Sunplus Inc.
 * Author: Li-hao Kuo <lhjeff911@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#define SP_I2C_STD_FREQ                      100
#define SP_I2C_FAST_FREQ                     400
#define SP_I2C_SLEEP_TIMEOUT                 200
#define SP_I2C_SCL_DELAY                     1
#define SP_CLK_SOURCE_FREQ                   27000
#define SP_BUFFER_SIZE                       1024
#define SP_I2C_EMP_THOLD                     4

#define SP_I2C_BURST_RDATA_BYTES             16
#define SP_I2C_BURST_RDATA_FLAG              (BIT(31) | BIT(15))
#define SP_I2C_BURST_RDATA_ALL_FLAG          GENMASK(31, 0)

#define SP_I2C_CTL0_REG 0x0000
//control0
#define SP_I2C_CTL0_FREQ_MASK                GENMASK(26, 24)
#define SP_I2C_CTL0_PREFETCH                 BIT(18)
#define SP_I2C_CTL0_RESTART_EN               BIT(17)
#define SP_I2C_CTL0_SUBADDR_EN               BIT(16)
#define SP_I2C_CTL0_SW_RESET                 BIT(15)
#define SP_I2C_CTL0_SLAVE_ADDR_MASK          GENMASK(7, 1)

#define SP_I2C_CTL1_REG 0x0004
//control1
#define SP_I2C_CTL1_ALL_CLR                  GENMASK(9, 0)
#define SP_I2C_CTL1_EMP_CLR                  BIT(9)
#define SP_I2C_CTL1_SCL_HOLD_TOO_LONG_CLR    BIT(8)
#define SP_I2C_CTL1_SCL_WAIT_CLR             BIT(7)
#define SP_I2C_CTL1_EMP_THOLD_CLR            BIT(6)
#define SP_I2C_CTL1_DATA_NACK_CLR            BIT(5)
#define SP_I2C_CTL1_ADDRESS_NACK_CLR         BIT(4)
#define SP_I2C_CTL1_BUSY_CLR                 BIT(3)
#define SP_I2C_CTL1_CLKERR_CLR               BIT(2)
#define SP_I2C_CTL1_DONE_CLR                 BIT(1)
#define SPI2C_CTL1_SIFBUSY_CLR               BIT(0)

#define SP_I2C_CTL2_REG 0x0008
//control2
#define SP_I2C_CTL2_FREQ_CUSTOM_MASK         GENMASK(10, 0)  // 11 bit
#define SP_I2C_CTL2_SCL_DELAY_MASK           GENMASK(25, 24)
#define SP_I2C_CTL2_SDA_HALF_ENABLE          BIT(31)

#define SP_I2C_INT_REG                       0x001c
#define SP_I2C_INT_EN0_REG                   0x0020
#define SP_I2C_MOD_REG                       0x0024
#define SP_I2C_CTL6_REG                      0x0030
#define SP_I2C_INT_EN1_REG                   0x0034
#define SP_I2C_STATUS3_REG                   0x0038
#define SP_I2C_STATUS4_REG                   0x0040
#define SP_I2C_INT_EN2_REG                   0x0040
#define SP_I2C_CTL7_REG                      0x0044
//control7
#define SP_I2C_CTL7_RD_MASK                  GENMASK(31, 16)
#define SP_I2C_CTL7_WR_MASK                  GENMASK(15, 0)  //bit[31:16]
//i2c master mode
#define SP_I2C_MODE_DMA_MODE                 BIT(2)
#define SP_I2C_MODE_MANUAL_MODE              BIT(1)
#define SP_I2C_MODE_MANUAL_TRIG              BIT(0)

#define SP_I2C_DATA0_REG                     0x0060
#define SP_I2C_DATA4_REG                     0x0064
#define SP_I2C_DATA8_REG                     0x0068
#define SP_I2C_DATA12_REG                    0x006c
#define SP_I2C_DATA16_REG                    0x0070
#define SP_I2C_DATA20_REG                    0x0074
#define SP_I2C_DATA24_REG                    0x0078
#define SP_I2C_DATA28_REG                    0x007c

#define SP_I2C_DMA_CONF_REG                  0x0004
#define SP_I2C_DMA_LEN_REG                   0x0008
#define SP_I2C_DMA_ADDR_REG                  0x000c
//dma config
#define SP_I2C_DMA_CFG_DMA_GO                BIT(8)
#define SP_I2C_DMA_CFG_NON_BUF_MODE          BIT(2)
#define SP_I2C_DMA_CFG_SAME_SLAVE            BIT(1)
#define SP_I2C_DMA_CFG_DMA_MODE              BIT(0)

#define SP_I2C_DMA_FLAG_REG 0x0014
//dma interrupt flag
#define SP_I2C_DMA_INT_LENGTH0_FLAG          BIT(6)
#define SP_I2C_DMA_INT_THRESHOLD_FLAG        BIT(5)
#define SP_I2C_DMA_INT_IP_TIMEOUT_FLAG       BIT(4)
#define SP_I2C_DMA_INT_GDMA_TIMEOUT_FLAG     BIT(3)
#define SP_I2C_DMA_INT_WB_EN_ERROR_FLAG      BIT(2)
#define SP_I2C_DMA_INT_WCNT_ERROR_FLAG       BIT(1)
#define SP_I2C_DMA_INT_DMA_DONE_FLAG         BIT(0)

#define SP_I2C_DMA_INT_EN_REG 0x0018
//dma interrupt enable
#define SP_I2C_DMA_EN_LENGTH0_INT            BIT(6)
#define SP_I2C_DMA_EN_THRESHOLD_INT          BIT(5)
#define SP_I2C_DMA_EN_IP_TIMEOUT_INT         BIT(4)
#define SP_I2C_DMA_EN_GDMA_TIMEOUT_INT       BIT(3)
#define SP_I2C_DMA_EN_WB_EN_ERROR_INT        BIT(2)
#define SP_I2C_DMA_EN_WCNT_ERROR_INT         BIT(1)
#define SP_I2C_DMA_EN_DMA_DONE_INT           BIT(0)
//interrupt
#define SP_I2C_INT_RINC_INDEX                GENMASK(20, 18) //bit[20:18]
#define SP_I2C_INT_WINC_INDEX                GENMASK(17, 15) //bit[17:15]
#define SP_I2C_INT_SCL_HOLD_TOO_LONG_FLAG    BIT(11)
#define SP_I2C_INT_WFIFO_ENABLE              BIT(10)
#define SP_I2C_INT_FULL_FLAG                 BIT(9)
#define SP_I2C_INT_EMPTY_FLAG                BIT(8)
#define SP_I2C_INT_SCL_WAIT_FLAG             BIT(7)
#define SP_I2C_INT_EMPTY_THRESHOLD_FLAG      BIT(6)
#define SP_I2C_INT_DATA_NACK_FLAG            BIT(5)
#define SP_I2C_INT_ADDRESS_NACK_FLAG         BIT(4)
#define SP_I2C_INT_BUSY_FLAG                 BIT(3)
#define SP_I2C_INT_CLKERR_FLAG               BIT(2)
#define SP_I2C_INT_DONE_FLAG                 BIT(1)
#define SP_I2C_INT_SIFBUSY_FLAG              BIT(0)
//interrupt enable0
#define SP_I2C_EN0_SCL_HOLD_TOO_LONG_INT     BIT(13)
#define SP_I2C_EN0_NACK_INT                  BIT(12)
#define SP_I2C_EN0_CTL_EMP_THOLD             GENMASK(11, 9)
#define SP_I2C_EN0_EMPTY_INT                 BIT(8)
#define SP_I2C_EN0_SCL_WAIT_INT              BIT(7)
#define SP_I2C_EN0_EMP_THOLD_INT             BIT(6)
#define SP_I2C_EN0_DATA_NACK_INT             BIT(5)
#define SP_I2C_EN0_ADDRESS_NACK_INT          BIT(4)
#define SP_I2C_EN0_BUSY_INT                  BIT(3)
#define SP_I2C_EN0_CLKERR_INT                BIT(2)
#define SP_I2C_EN0_DONE_INT                  BIT(1)
#define SP_I2C_EN0_SIFBUSY_INT               BIT(0)

#define SP_I2C_RESET(id, val)                (((1 << 16) | (val)) << (id))
#define SP_I2C_CLKEN(id, val)                (((1 << 16) | (val)) << (id))
#define SP_I2C_GCLKEN(id, val)               (((1 << 16) | (val)) << (id))

#define SP_I2C_POWER_CLKEN3                  0x0010
#define SP_I2C_POWER_GCLKEN3                 0x0038
#define SP_I2C_POWER_RESET3                  0x0060

enum sp_state_e_ {
	SPI2C_SUCCESS = 0,    /* successful */
	SPI2C_STATE_WR = 1,  /* i2c is write */
	SPI2C_STATE_RD = 2,   /* i2c is read */
	SPI2C_STATE_IDLE = 3,   /* i2c is idle */
	SPI2C_STATE_DMA_WR = 4,/* i2c is dma write */
	SPI2C_STATE_DMA_RD = 5, /* i2c is dma read */
};

enum sp_i2c_switch_e_ {
	SP_I2C_DMA_POWER_NO_SW = 0,
	SP_I2C_DMA_POWER_SW = 1,
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
	unsigned char emp_thold;
	unsigned char fifo_empty;
	unsigned char fifo_full;
	unsigned char scl_hold_too_long;
	unsigned char read_over_flow;
};

struct sp_i2c_irq_event {
	enum sp_state_e_ rw_state;
	struct sp_i2c_irq_flag irq_flag;
	struct sp_i2c_irq_dma_flag irq_dma_flag;
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
	int mode; /* dma power switch*/
	int total_port;
};

struct sp_i2c_dev {
	struct i2c_adapter adap;
	struct device *dev;
	struct sp_i2c_cmd spi2c_cmd;
	struct sp_i2c_irq_event spi2c_irq;
	struct clk *clk;
	struct reset_control *rstc;
	unsigned int i2c_clk_freq;
	unsigned int mode;
	unsigned int total_port;
	int irq;
	void __iomem *i2c_regs;
	void __iomem *i2c_dma_regs;
	void __iomem *i2c_power_regs;
	wait_queue_head_t wait;
};

static unsigned int sp_i2cm_get_int_flag(void __iomem *sr)
{
	return readl(sr + SP_I2C_INT_REG);
}

static void sp_i2cm_status_clear(void __iomem *sr, u32 flag)
{
	u32 ctl1;

	ctl1 = readl(sr + SP_I2C_CTL1_REG);
	ctl1 |= flag;
	writel(ctl1, sr + SP_I2C_CTL1_REG);

	ctl1 = readl(sr + SP_I2C_CTL1_REG);
	ctl1 &= (~flag);
	writel(ctl1, sr + SP_I2C_CTL1_REG);
}

static void sp_i2cm_reset(void __iomem *sr)
{
	u32 ctl0;

	ctl0 = readl(sr + SP_I2C_CTL0_REG);
	ctl0 |= SP_I2C_CTL0_SW_RESET;
	writel(ctl0, sr + SP_I2C_CTL0_REG);

	udelay(2);
}

static void sp_i2cm_data0_set(void __iomem *sr, void *wrdata)
{
	unsigned int *wdata = wrdata;

	writel(*wdata, sr + SP_I2C_DATA0_REG);
}

static void sp_i2cm_int_en0_disable(void __iomem *sr, unsigned int int0)
{
	u32 val;

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

static void sp_i2cm_data_get(void __iomem *sr, unsigned int index, void *rxdata)
{
	unsigned int *rdata = rxdata;

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
	u32 ctl0, ctl2;

	div = SP_CLK_SOURCE_FREQ / freq;
	div -= 1;
	if (SP_CLK_SOURCE_FREQ % freq != 0)
		div += 1;

	if (div > SP_I2C_CTL2_FREQ_CUSTOM_MASK)
		div = SP_I2C_CTL2_FREQ_CUSTOM_MASK;

	ctl0 = readl(sr + SP_I2C_CTL0_REG);
	ctl0 &= ~SP_I2C_CTL0_FREQ_MASK;
	writel(ctl0, sr + SP_I2C_CTL0_REG);

	ctl2 = readl(sr + SP_I2C_CTL2_REG);
	ctl2 &= ~SP_I2C_CTL2_FREQ_CUSTOM_MASK;
	ctl2 |= FIELD_PREP(SP_I2C_CTL2_FREQ_CUSTOM_MASK, div);
	writel(ctl2, sr + SP_I2C_CTL2_REG);
}

static void sp_i2cm_slave_addr_set(void __iomem *sr, unsigned int addr)
{
	u32 ctl0;

	ctl0 = readl(sr + SP_I2C_CTL0_REG);
	ctl0 &= ~SP_I2C_CTL0_SLAVE_ADDR_MASK;
	ctl0 |= FIELD_PREP(SP_I2C_CTL0_SLAVE_ADDR_MASK, addr);
	writel(ctl0, sr + SP_I2C_CTL0_REG);
}

static void sp_i2cm_scl_delay_set(void __iomem *sr, unsigned int delay)
{
	u32 ctl2;

	ctl2 = readl(sr + SP_I2C_CTL2_REG);
	ctl2 &= ~SP_I2C_CTL2_SCL_DELAY_MASK;
	ctl2 |= FIELD_PREP(SP_I2C_CTL2_SCL_DELAY_MASK, delay);
	ctl2 &= ~SP_I2C_CTL2_SDA_HALF_ENABLE;
	writel(ctl2, sr + SP_I2C_CTL2_REG);
}

static void sp_i2cm_trans_cnt_set(void __iomem *sr, unsigned int write_cnt,
				  unsigned int read_cnt)
{
	u32 ctl7 = 0;

	ctl7 = FIELD_PREP(SP_I2C_CTL7_RD_MASK, read_cnt) |
			  FIELD_PREP(SP_I2C_CTL7_WR_MASK, write_cnt);
	writel(ctl7, sr + SP_I2C_CTL7_REG);
}

static void sp_i2cm_active_mode_set(void __iomem *sr, enum sp_i2c_active_mode mode)
{
	u32 val;

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

static void sp_i2cm_data_tx(void __iomem *sr, void *wdata, unsigned int cnt)
{
	unsigned int *wrdata = wdata;
	unsigned int i;

	for (i = 0 ; i < cnt ; i++)
		writel(wrdata[i], sr + SP_I2C_DATA0_REG + (i * 4));
}

static void sp_i2cm_rw_mode_set(void __iomem *sr, enum sp_i2c_mode rw_mode)
{
	u32 ctl0;

	ctl0 = readl(sr + SP_I2C_CTL0_REG);
	switch (rw_mode) {
	default:
	case I2C_WRITE_MODE:
		ctl0 &= ~(SP_I2C_CTL0_PREFETCH |
				SP_I2C_CTL0_RESTART_EN | SP_I2C_CTL0_SUBADDR_EN);
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

static void sp_i2cm_int_en0_set(void __iomem *sr, u32 int0)
{
	writel(int0, sr + SP_I2C_INT_EN0_REG);
}

static void sp_i2cm_int_en1_set(void __iomem *sr, u32 rdata_en)
{
	writel(rdata_en, sr + SP_I2C_INT_EN1_REG);
}

static void sp_i2cm_int_en2_set(void __iomem *sr, u32 overflow_en)
{
	writel(overflow_en, sr + SP_I2C_INT_EN2_REG);
}

static void sp_i2cm_enable(unsigned int device_id, void *membase)
{
	writel(SP_I2C_CLKEN(device_id, 1),  membase + SP_I2C_POWER_CLKEN3);
	writel(SP_I2C_GCLKEN(device_id, 0), membase + SP_I2C_POWER_GCLKEN3);
	writel(SP_I2C_RESET(device_id, 0), membase + SP_I2C_POWER_RESET3);
}

static void sp_i2cm_manual_trigger(void __iomem *sr)
{
	u32 val;

	val = readl(sr + SP_I2C_MOD_REG);
	val |= SP_I2C_MODE_MANUAL_TRIG;
	writel(val, sr + SP_I2C_MOD_REG);
}

static void sp_i2cm_int_en0_with_thershold_set(void __iomem *sr, unsigned int int0,
					       unsigned char threshold)
{
	u32 val;

	int0 &= ~SP_I2C_EN0_CTL_EMP_THOLD;
	val = int0 | FIELD_PREP(SP_I2C_EN0_CTL_EMP_THOLD, threshold);
	writel(val, sr + SP_I2C_INT_EN0_REG);
}

static void sp_i2cm_dma_mode_enable(void __iomem *sr)
{
	u32 val;

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
	u32 val;

	val = readl(sr_dma + SP_I2C_DMA_FLAG_REG);
	val |= flag;
	writel(val, sr_dma + SP_I2C_DMA_FLAG_REG);
}

static void sp_i2cm_dma_addr_set(void __iomem *sr_dma, dma_addr_t addr)
{
	writel(addr, sr_dma + SP_I2C_DMA_ADDR_REG);
}

static void sp_i2cm_dma_length_set(void __iomem *sr_dma, unsigned int length)
{
	length &= (0xFFFF);  //only support 16 bit
	writel(length, sr_dma + SP_I2C_DMA_LEN_REG);
}

static void sp_i2cm_dma_rw_mode_set(void __iomem *sr_dma, enum sp_i2c_dma_mode rw_mode)
{
	u32 val;

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
	u32 val;

	val = readl(sr_dma + SP_I2C_DMA_CONF_REG);
	val |= SP_I2C_DMA_CFG_DMA_GO;
	writel(val, sr_dma + SP_I2C_DMA_CONF_REG);
}

static void _sp_i2cm_intflag_check(struct sp_i2c_dev *spi2c, struct sp_i2c_irq_event *spi2c_irq)
{
	void __iomem *sr = spi2c->i2c_regs;
	unsigned int int_flag = 0;
	unsigned int overflow_flag = 0;

	int_flag = sp_i2cm_get_int_flag(sr);

	if (int_flag & SP_I2C_INT_DONE_FLAG)
		spi2c_irq->irq_flag.active_done = 1;
	else
		spi2c_irq->irq_flag.active_done = 0;

	if (int_flag & SP_I2C_INT_ADDRESS_NACK_FLAG)
		spi2c_irq->irq_flag.addr_nack = 1;
	else
		spi2c_irq->irq_flag.addr_nack = 0;

	if (int_flag & SP_I2C_INT_DATA_NACK_FLAG)
		spi2c_irq->irq_flag.data_nack = 1;
	else
		spi2c_irq->irq_flag.data_nack = 0;
	// write use
	if (int_flag & SP_I2C_INT_EMPTY_THRESHOLD_FLAG)
		spi2c_irq->irq_flag.emp_thold = 1;
	else
		spi2c_irq->irq_flag.emp_thold = 0;
	// write use
	if (int_flag & SP_I2C_INT_EMPTY_FLAG)
		spi2c_irq->irq_flag.fifo_empty = 1;
	else
		spi2c_irq->irq_flag.fifo_empty = 0;
	// write use (for debug)
	if (int_flag & SP_I2C_INT_FULL_FLAG)
		spi2c_irq->irq_flag.fifo_full = 1;
	else
		spi2c_irq->irq_flag.fifo_full = 0;

	if (int_flag & SP_I2C_INT_SCL_HOLD_TOO_LONG_FLAG)
		spi2c_irq->irq_flag.scl_hold_too_long = 1;
	else
		spi2c_irq->irq_flag.scl_hold_too_long = 0;

	sp_i2cm_status_clear(sr, SP_I2C_CTL1_ALL_CLR);

	overflow_flag = sp_i2cm_over_flag_get(sr);

	if (overflow_flag)
		spi2c_irq->irq_flag.read_over_flow = 1;
	else
		spi2c_irq->irq_flag.read_over_flow = 0;
}

static void _sp_i2cm_dma_intflag_check(struct sp_i2c_dev *spi2c,
				       struct sp_i2c_irq_event *spi2c_irq)
{
	void __iomem *sr_dma = spi2c->i2c_dma_regs;
	u32 int_flag = 0;

	int_flag = sp_i2cm_get_dma_int_flag(sr_dma);

	if (int_flag & SP_I2C_DMA_INT_DMA_DONE_FLAG)
		spi2c_irq->irq_dma_flag.dma_done = 1;
	else
		spi2c_irq->irq_dma_flag.dma_done = 0;

	if (int_flag & SP_I2C_DMA_INT_WCNT_ERROR_FLAG)
		spi2c_irq->irq_dma_flag.write_cnt_err = 1;
	else
		spi2c_irq->irq_dma_flag.write_cnt_err = 0;

	if (int_flag & SP_I2C_DMA_INT_WB_EN_ERROR_FLAG)
		spi2c_irq->irq_dma_flag.wb_en_err = 1;
	else
		spi2c_irq->irq_dma_flag.wb_en_err = 0;

	if (int_flag & SP_I2C_DMA_INT_GDMA_TIMEOUT_FLAG)
		spi2c_irq->irq_dma_flag.gdma_timeout = 1;
	else
		spi2c_irq->irq_dma_flag.gdma_timeout = 0;

	if (int_flag & SP_I2C_DMA_INT_IP_TIMEOUT_FLAG)
		spi2c_irq->irq_dma_flag.ipt_timerout = 1;
	else
		spi2c_irq->irq_dma_flag.ipt_timerout = 0;

	if (int_flag & SP_I2C_DMA_INT_LENGTH0_FLAG)
		spi2c_irq->irq_dma_flag.dma_ligth = 1;
	else
		spi2c_irq->irq_dma_flag.dma_ligth = 0;

	sp_i2cm_dma_int_flag_clear(sr_dma, 0x7F);  //write 1 to clear
}

static irqreturn_t _sp_i2cm_irqevent_handler(int irq, void *args)
{
	struct sp_i2c_dev *spi2c = args;
	struct sp_i2c_irq_event *spi2c_irq = &spi2c->spi2c_irq;
	void __iomem *sr = spi2c->i2c_regs;
	unsigned char r_data[SP_I2C_BURST_RDATA_BYTES] = {0};
	unsigned char w_data[32] = {0};
	unsigned int rdata_flag = 0;
	unsigned int bit_index = 0;
	int i = 0, j = 0, k = 0;

	_sp_i2cm_intflag_check(spi2c, spi2c_irq);

switch (spi2c_irq->rw_state) {
case SPI2C_STATE_WR:
case SPI2C_STATE_DMA_WR:
	if (spi2c_irq->irq_flag.active_done) {
		spi2c_irq->ret = SPI2C_SUCCESS;
		wake_up(&spi2c->wait);
	} else if (spi2c_irq->irq_flag.addr_nack || spi2c_irq->irq_flag.data_nack) {
		if (spi2c_irq->rw_state == SPI2C_STATE_DMA_WR)
			dev_err(spi2c->dev, "DMA wtire NACK!!\n");
		else
			dev_err(spi2c->dev, "wtire NACK!!\n");

		spi2c_irq->ret = -ENXIO;
		spi2c_irq->irq_flag.active_done = 1;
		wake_up(&spi2c->wait);
		sp_i2cm_reset(sr);
	} else if (spi2c_irq->irq_flag.scl_hold_too_long) {
		spi2c_irq->ret = -EINVAL;
		spi2c_irq->irq_flag.active_done = 1;
		wake_up(&spi2c->wait);
		sp_i2cm_reset(sr);
	} else if (spi2c_irq->irq_flag.fifo_empty) {
		spi2c_irq->ret = -ENXIO;
		spi2c_irq->irq_flag.active_done = 1;
		wake_up(&spi2c->wait);
		sp_i2cm_reset(sr);
	} else if ((spi2c_irq->burst_cnt > 0) &&
			(spi2c_irq->rw_state == SPI2C_STATE_WR)) {
		if (spi2c_irq->irq_flag.emp_thold) {
			for (i = 0; i < SP_I2C_EMP_THOLD; i++) {
				for (j = 0; j < 4; j++) {
					if (spi2c_irq->data_index >=
					    spi2c_irq->data_total_len)
						w_data[j] = 0;
					else
						w_data[j] =
					spi2c_irq->data_buf[spi2c_irq->data_index];
					spi2c_irq->data_index++;
				}
				sp_i2cm_data0_set(sr, w_data);
				spi2c_irq->burst_cnt--;
				if (spi2c_irq->burst_cnt == 0) {
					sp_i2cm_int_en0_disable(sr,
								SP_I2C_EN0_EMP_THOLD_INT |
								SP_I2C_EN0_EMPTY_INT);
					break;
				}
			}
				sp_i2cm_status_clear(sr, SP_I2C_CTL1_EMP_THOLD_CLR);
		}
	}
	break;

case SPI2C_STATE_RD:
case SPI2C_STATE_DMA_RD:
		if (spi2c_irq->irq_flag.addr_nack || spi2c_irq->irq_flag.data_nack) {
			if (spi2c_irq->rw_state == SPI2C_STATE_DMA_RD)
				dev_err(spi2c->dev, "DMA read NACK!!\n");
			else
				dev_err(spi2c->dev, "read NACK!!\n");

			spi2c_irq->ret = -ENXIO;
			spi2c_irq->irq_flag.active_done = 1;
			wake_up(&spi2c->wait);
			sp_i2cm_reset(sr);
		} else if (spi2c_irq->irq_flag.scl_hold_too_long) {
			spi2c_irq->ret = -EINVAL;
			spi2c_irq->irq_flag.active_done = 1;
			wake_up(&spi2c->wait);
			sp_i2cm_reset(sr);
		} else if (spi2c_irq->irq_flag.read_over_flow) {
			spi2c_irq->ret = -EINVAL;
			spi2c_irq->irq_flag.active_done = 1;
			wake_up(&spi2c->wait);
			sp_i2cm_reset(sr);
} else {
	if (spi2c_irq->burst_cnt > 0 && spi2c_irq->rw_state == SPI2C_STATE_RD) {
		sp_i2cm_rdata_flag_get(sr, &rdata_flag);
		for (i = 0; i < (32 / SP_I2C_BURST_RDATA_BYTES); i++) {
			bit_index = (SP_I2C_BURST_RDATA_BYTES - 1) + (SP_I2C_BURST_RDATA_BYTES * i);
			if (rdata_flag & (1 << bit_index)) {
				for (j = 0; j < (SP_I2C_BURST_RDATA_BYTES / 4); j++) {
					k = spi2c_irq->reg_data_index + j;
					if (k >= 8)
						k -= 8;

					sp_i2cm_data_get(sr, k, &spi2c_irq->data_buf
							 [spi2c_irq->data_index]);
					spi2c_irq->data_index += 4;
				}
				sp_i2cm_rdata_flag_clear(sr, (((1 << SP_I2C_BURST_RDATA_BYTES) -
							 1) << (SP_I2C_BURST_RDATA_BYTES * i)));
				spi2c_irq->reg_data_index += (SP_I2C_BURST_RDATA_BYTES / 4);
				if (spi2c_irq->reg_data_index >= 8)
					spi2c_irq->burst_cnt--;
			}
		}
	}
		if (spi2c_irq->irq_flag.active_done) {
			if (spi2c_irq->burst_remainder && spi2c_irq->rw_state == SPI2C_STATE_RD) {
				j = 0;
			for (i = 0; i < (SP_I2C_BURST_RDATA_BYTES / 4); i++) {
				k = spi2c_irq->reg_data_index + i;
				if (k >= 8)
					k -= 8;
				sp_i2cm_data_get(sr, k, &r_data[j]);
				j += 4;
			}

				for (i = 0; i < spi2c_irq->burst_remainder; i++)
					spi2c_irq->data_buf[spi2c_irq->data_index + i] = r_data[i];
			}
				spi2c_irq->ret = SPI2C_SUCCESS;
				wake_up(&spi2c->wait);
		}
	}
	break;

default:
	break;
}

	_sp_i2cm_dma_intflag_check(spi2c, spi2c_irq);
	switch (spi2c_irq->rw_state) {
	case SPI2C_STATE_DMA_WR:
		if (spi2c_irq->irq_dma_flag.dma_done) {
			spi2c_irq->ret = SPI2C_SUCCESS;
			wake_up(&spi2c->wait);
		}
		break;
	case SPI2C_STATE_DMA_RD:
		if (spi2c_irq->irq_dma_flag.dma_done) {
			spi2c_irq->ret = SPI2C_SUCCESS;
			wake_up(&spi2c->wait);
		}
		break;

	default:
		break;
	}

	return IRQ_HANDLED;
}

static int _sp_i2cm_init(unsigned int device_id, struct sp_i2c_dev *spi2c)
{
	void __iomem *sr = spi2c->i2c_regs;

	if (device_id >= spi2c->total_port)
		return -ENXIO;

	sp_i2cm_reset(sr);
	return SPI2C_SUCCESS;
}

static int _sp_i2cm_get_resources(struct platform_device *pdev, struct sp_i2c_dev *spi2c)
{
	int ret;
	struct resource *res;

	spi2c->i2c_regs = devm_platform_ioremap_resource_byname(pdev, "i2cm");
	if (IS_ERR(spi2c->i2c_regs))
		return dev_err_probe(&pdev->dev, PTR_ERR(spi2c->i2c_regs), "regs get fail\n");

	spi2c->i2c_dma_regs = devm_platform_ioremap_resource_byname(pdev, "i2cmdma");
	if (IS_ERR(spi2c->i2c_dma_regs))
		return dev_err_probe(&pdev->dev, PTR_ERR(spi2c->i2c_dma_regs), "regs get fail\n");

	spi2c->i2c_power_regs = devm_platform_ioremap_resource_byname(pdev, "i2cdmapower");
	if (IS_ERR(spi2c->i2c_power_regs))
		spi2c->i2c_power_regs = 0;

	spi2c->irq = platform_get_irq(pdev, 0);
	if (spi2c->irq < 0)
		return spi2c->irq;

	ret = devm_request_irq(&pdev->dev, spi2c->irq, _sp_i2cm_irqevent_handler,
			       IRQF_TRIGGER_HIGH, pdev->name, spi2c);
	if (ret)
		return ret;

	return SPI2C_SUCCESS;
}

static int sp_i2cm_read(struct sp_i2c_cmd *spi2c_cmd, struct sp_i2c_dev *spi2c)
{
	void __iomem *sr = spi2c->i2c_regs;
	struct sp_i2c_irq_event *spi2c_irq = &spi2c->spi2c_irq;
	unsigned int int0 = 0, int1 = 0, int2 = 0;
	unsigned int burst_cnt = 0, burst_r = 0;
	unsigned int write_cnt = 0;
	unsigned int read_cnt = 0;
	int ret = SPI2C_SUCCESS;

	if (spi2c_irq->busy || spi2c_cmd->dev_id > spi2c->total_port) {
		dev_err(spi2c->dev, "IO error !!\n");
		return -ENXIO;
	}

	memset(spi2c_irq, 0, sizeof(*spi2c_irq));
	spi2c_irq->busy = 1;

	write_cnt = spi2c_cmd->write_cnt;
	read_cnt = spi2c_cmd->read_cnt;

	if (spi2c_cmd->restart_en) {
		if (write_cnt > 32) {
			spi2c_irq->busy = 0;
			dev_err(spi2c->dev,
				"I2C write count is invalid !! write count=%d\n", write_cnt);
			return -EINVAL;
		}
	}

	if (read_cnt > 0xFFFF  || read_cnt == 0) {
		spi2c_irq->busy = 0;
		dev_err(spi2c->dev,
			"I2C read count is invalid !! read count=%d\n", read_cnt);
		return -EINVAL;
	}

	burst_cnt = read_cnt / SP_I2C_BURST_RDATA_BYTES;
	burst_r = read_cnt % SP_I2C_BURST_RDATA_BYTES;

	int0 = (SP_I2C_EN0_SCL_HOLD_TOO_LONG_INT | SP_I2C_EN0_EMPTY_INT | SP_I2C_EN0_DATA_NACK_INT
			| SP_I2C_EN0_ADDRESS_NACK_INT | SP_I2C_EN0_DONE_INT);
	if (burst_cnt) {
		int1 = SP_I2C_BURST_RDATA_FLAG;
		int2 = SP_I2C_BURST_RDATA_ALL_FLAG;
	}

	spi2c_irq->rw_state = SPI2C_STATE_RD;
	spi2c_irq->burst_cnt = burst_cnt;
	spi2c_irq->burst_remainder = burst_r;
	spi2c_irq->data_index = 0;
	spi2c_irq->reg_data_index = 0;
	spi2c_irq->data_total_len = read_cnt;
	spi2c_irq->data_buf = spi2c_cmd->read_data;

	sp_i2cm_reset(sr);
	sp_i2cm_clock_freq_set(sr, spi2c_cmd->freq);
	sp_i2cm_slave_addr_set(sr, spi2c_cmd->slave_addr);
	sp_i2cm_scl_delay_set(sr, SP_I2C_SCL_DELAY);
	sp_i2cm_trans_cnt_set(sr, write_cnt, read_cnt);
	sp_i2cm_active_mode_set(sr, I2C_TRIGGER);

	if (spi2c_cmd->restart_en) {
		write_cnt = spi2c_cmd->write_cnt  / 4;
		if (spi2c_cmd->write_cnt % 4)
			write_cnt += 1;

		sp_i2cm_data_tx(sr, spi2c_cmd->write_data, write_cnt);
		sp_i2cm_rw_mode_set(sr, I2C_RESTART_MODE);
	} else {
		sp_i2cm_rw_mode_set(sr, I2C_READ_MODE);
	}

	sp_i2cm_int_en0_set(sr, int0);
	sp_i2cm_int_en1_set(sr, int1);
	sp_i2cm_int_en2_set(sr, int2);
	sp_i2cm_manual_trigger(sr);	//start send data

	ret = wait_event_timeout(spi2c->wait, spi2c_irq->irq_flag.active_done,
				 (SP_I2C_SLEEP_TIMEOUT * HZ) / 500);
	if (ret == 0) {
		dev_err(spi2c->dev, "I2C read timeout !!\n");
		ret = -ETIMEDOUT;
	} else {
		ret = spi2c_irq->ret;
	}
	sp_i2cm_reset(sr);
	spi2c_irq->rw_state = SPI2C_STATE_IDLE;
	spi2c_irq->busy = 0;

	return ret;
}

static int sp_i2cm_write(struct sp_i2c_cmd *spi2c_cmd, struct sp_i2c_dev *spi2c)
{
	struct sp_i2c_irq_event *spi2c_irq = &spi2c->spi2c_irq;
	void __iomem *sr = spi2c->i2c_regs;
	unsigned int write_cnt = 0;
	unsigned int burst_cnt = 0;
	unsigned int int0 = 0;
	int ret = SPI2C_SUCCESS;
	int i = 0;

	if (spi2c_irq->busy || spi2c_cmd->dev_id > spi2c->total_port) {
		dev_err(spi2c->dev, "IO error !!\n");
		return -ENXIO;
	}

	memset(spi2c_irq, 0, sizeof(*spi2c_irq));
	spi2c_irq->busy = 1;

	write_cnt = spi2c_cmd->write_cnt;

	if (write_cnt > 0xFFFF) {
		spi2c_irq->busy = 0;
		dev_err(spi2c->dev,
			"I2C write count is invalid !! write count=%d\n", write_cnt);
		return -EINVAL;
	}

	burst_cnt = write_cnt  / 4;
	if (write_cnt % 4)
		burst_cnt += 1;

	int0 = (SP_I2C_EN0_SCL_HOLD_TOO_LONG_INT | SP_I2C_EN0_EMPTY_INT | SP_I2C_EN0_DATA_NACK_INT
			| SP_I2C_EN0_ADDRESS_NACK_INT | SP_I2C_EN0_DONE_INT);

	if (burst_cnt)
		int0 |= SP_I2C_EN0_EMP_THOLD_INT;

	spi2c_irq->rw_state = SPI2C_STATE_WR;
	spi2c_irq->burst_cnt = burst_cnt;
	spi2c_irq->data_index = i;
	spi2c_irq->data_total_len = write_cnt;
	spi2c_irq->data_buf = spi2c_cmd->write_data;

	sp_i2cm_reset(sr);
	sp_i2cm_clock_freq_set(sr, spi2c_cmd->freq);
	sp_i2cm_slave_addr_set(sr, spi2c_cmd->slave_addr);
	sp_i2cm_scl_delay_set(sr, SP_I2C_SCL_DELAY);
	sp_i2cm_trans_cnt_set(sr, write_cnt, 0);
	sp_i2cm_active_mode_set(sr, I2C_TRIGGER);
	sp_i2cm_rw_mode_set(sr, I2C_WRITE_MODE);
	sp_i2cm_data_tx(sr, spi2c_cmd->write_data, burst_cnt);

	if (burst_cnt)
		sp_i2cm_int_en0_with_thershold_set(sr, int0, SP_I2C_EMP_THOLD);
	else
		sp_i2cm_int_en0_set(sr, int0);

	sp_i2cm_manual_trigger(sr);	//start send data

	ret = wait_event_timeout(spi2c->wait, spi2c_irq->irq_flag.active_done,
				 (SP_I2C_SLEEP_TIMEOUT * HZ) / 500);
	if (ret == 0) {
		dev_err(spi2c->dev, "I2C write timeout !!\n");
		ret = -ETIMEDOUT;
	} else {
		ret = spi2c_irq->ret;
	}
	sp_i2cm_reset(sr);
	spi2c_irq->rw_state = SPI2C_STATE_IDLE;
	spi2c_irq->busy = 0;

	return ret;
}

static int sp_i2cm_dma_write(struct sp_i2c_cmd *spi2c_cmd, struct sp_i2c_dev *spi2c)
{
	struct sp_i2c_irq_event *spi2c_irq = &spi2c->spi2c_irq;
	void __iomem *sr_dma = spi2c->i2c_dma_regs;
	void __iomem *sr = spi2c->i2c_regs;
	unsigned int int0 = 0;
	unsigned int dma_int = 0;
	int ret = SPI2C_SUCCESS;
	dma_addr_t dma_w_addr = 0;

	if (spi2c_irq->busy || spi2c_cmd->dev_id > spi2c->total_port) {
		dev_err(spi2c->dev, "IO error !!\n");
		return -ENXIO;
	}

	if (spi2c->mode == SP_I2C_DMA_POWER_SW && spi2c->i2c_power_regs != 0)
		sp_i2cm_enable(0, spi2c->i2c_power_regs);

	memset(spi2c_irq, 0, sizeof(*spi2c_irq));
	spi2c_irq->busy = 1;

	if (spi2c_cmd->write_cnt > 0xFFFF) {
		spi2c_irq->busy = 0;
		dev_err(spi2c->dev, "write count = %d is invalid!\n", spi2c_cmd->write_cnt);
		return -EINVAL;
	}

	spi2c_irq->rw_state = SPI2C_STATE_DMA_WR;

	dma_w_addr = dma_map_single(spi2c->dev, spi2c_cmd->write_data,
				    spi2c_cmd->write_cnt, DMA_TO_DEVICE);

	if (dma_mapping_error(spi2c->dev, dma_w_addr))
		return -ENOMEM;

	int0 = (SP_I2C_EN0_SCL_HOLD_TOO_LONG_INT | SP_I2C_EN0_EMPTY_INT
	 | SP_I2C_EN0_DATA_NACK_INT | SP_I2C_EN0_ADDRESS_NACK_INT | SP_I2C_EN0_DONE_INT);

	dma_int = SP_I2C_DMA_EN_DMA_DONE_INT;

	sp_i2cm_reset(sr);
	sp_i2cm_dma_mode_enable(sr);
	sp_i2cm_clock_freq_set(sr, spi2c_cmd->freq);
	sp_i2cm_slave_addr_set(sr, spi2c_cmd->slave_addr);
	sp_i2cm_scl_delay_set(sr, SP_I2C_SCL_DELAY);
	sp_i2cm_active_mode_set(sr, I2C_AUTO);
	sp_i2cm_rw_mode_set(sr, I2C_WRITE_MODE);
	sp_i2cm_int_en0_set(sr, int0);

	sp_i2cm_dma_addr_set(sr_dma, dma_w_addr);
	sp_i2cm_dma_length_set(sr_dma, spi2c_cmd->write_cnt);
	sp_i2cm_dma_rw_mode_set(sr_dma, I2C_DMA_READ_MODE);
	sp_i2cm_dma_int_en_set(sr_dma, dma_int);
	sp_i2cm_dma_go_set(sr_dma);

	ret = wait_event_timeout(spi2c->wait, spi2c_irq->irq_dma_flag.dma_done,
				 (SP_I2C_SLEEP_TIMEOUT * HZ) / 200);
	if (ret == 0) {
		dev_err(spi2c->dev, "I2C DMA write timeout !!\n");
		ret = -ETIMEDOUT;
	} else {
		ret = spi2c_irq->ret;
	}
	sp_i2cm_status_clear(sr, 0xFFFFFFFF);

	spi2c_irq->rw_state = SPI2C_STATE_IDLE;
	spi2c_irq->busy = 0;

	sp_i2cm_reset(sr);

	return ret;
}

static int sp_i2cm_dma_read(struct sp_i2c_cmd *spi2c_cmd, struct sp_i2c_dev *spi2c)
{
	struct sp_i2c_irq_event *spi2c_irq = &spi2c->spi2c_irq;
	void __iomem *sr_dma = spi2c->i2c_dma_regs;
	void __iomem *sr = spi2c->i2c_regs;
	unsigned int int0 = 0, int1 = 0, int2 = 0;
	unsigned int write_cnt = 0;
	unsigned int read_cnt = 0;
	unsigned int dma_int = 0;
	int ret = SPI2C_SUCCESS;
	dma_addr_t dma_r_addr = 0;

	if (spi2c_irq->busy || spi2c_cmd->dev_id > spi2c->total_port) {
		dev_err(spi2c->dev, "IO error !!\n");
		return -ENXIO;
	}

	if (spi2c->mode == SP_I2C_DMA_POWER_SW && spi2c->i2c_power_regs != 0)
		sp_i2cm_enable(0, spi2c->i2c_power_regs);

	memset(spi2c_irq, 0, sizeof(*spi2c_irq));
	spi2c_irq->busy = 1;

	write_cnt = spi2c_cmd->write_cnt;
	read_cnt = spi2c_cmd->read_cnt;

	if (spi2c_cmd->restart_en) {
		if (write_cnt > 32) {
			spi2c_irq->busy = 0;
			dev_err(spi2c->dev,
				"I2C write count is invalid !! write count=%d\n", write_cnt);
			return -EINVAL;
		}
	}

	if (read_cnt > 0xFFFF  || read_cnt == 0) {
		spi2c_irq->busy = 0;
		dev_err(spi2c->dev,
			"I2C read count is invalid !! read count=%d\n", read_cnt);
		return -EINVAL;
	}

	dma_r_addr = dma_map_single(spi2c->dev, spi2c_cmd->read_data,
				    spi2c_cmd->read_cnt, DMA_FROM_DEVICE);

	if (dma_mapping_error(spi2c->dev, dma_r_addr))
		return -ENOMEM;

	int0 = (SP_I2C_EN0_SCL_HOLD_TOO_LONG_INT | SP_I2C_EN0_EMPTY_INT | SP_I2C_EN0_DATA_NACK_INT
		| SP_I2C_EN0_ADDRESS_NACK_INT | SP_I2C_EN0_DONE_INT);

	dma_int = SP_I2C_DMA_EN_DMA_DONE_INT;

	spi2c_irq->rw_state = SPI2C_STATE_DMA_RD;
	spi2c_irq->data_index = 0;
	spi2c_irq->reg_data_index = 0;
	spi2c_irq->data_total_len = read_cnt;

	sp_i2cm_reset(sr);
	sp_i2cm_dma_mode_enable(sr);
	sp_i2cm_clock_freq_set(sr, spi2c_cmd->freq);
	sp_i2cm_slave_addr_set(sr, spi2c_cmd->slave_addr);
	sp_i2cm_scl_delay_set(sr, SP_I2C_SCL_DELAY);

	if (spi2c_cmd->restart_en) {
		sp_i2cm_active_mode_set(sr, I2C_TRIGGER);
		sp_i2cm_rw_mode_set(sr, I2C_RESTART_MODE);
		sp_i2cm_trans_cnt_set(sr, write_cnt, read_cnt);
		write_cnt = spi2c_cmd->write_cnt  / 4;
		if (spi2c_cmd->write_cnt % 4)
			write_cnt += 1;

		sp_i2cm_data_tx(sr, spi2c_cmd->write_data, write_cnt);

	} else {
		sp_i2cm_active_mode_set(sr, I2C_AUTO);
		sp_i2cm_rw_mode_set(sr, I2C_READ_MODE);
	}

	sp_i2cm_int_en0_set(sr, int0);
	sp_i2cm_int_en1_set(sr, int1);
	sp_i2cm_int_en2_set(sr, int2);

	sp_i2cm_dma_addr_set(sr_dma, dma_r_addr);
	sp_i2cm_dma_length_set(sr_dma, spi2c_cmd->read_cnt);
	sp_i2cm_dma_rw_mode_set(sr_dma, I2C_DMA_WRITE_MODE);
	sp_i2cm_dma_int_en_set(sr_dma, dma_int);
	sp_i2cm_dma_go_set(sr_dma);

	if (spi2c_cmd->restart_en)
		sp_i2cm_manual_trigger(sr); //start send data

	ret = wait_event_timeout(spi2c->wait, spi2c_irq->irq_dma_flag.dma_done,
				 (SP_I2C_SLEEP_TIMEOUT * HZ) / 200);
	if (ret == 0) {
		dev_err(spi2c->dev, "I2C DMA read timeout !!\n");
		ret = -ETIMEDOUT;
	} else {
		ret = spi2c_irq->ret;
	}
	sp_i2cm_status_clear(sr, 0xFFFFFFFF);

	//copy data from virtual addr to spi2c_cmd->read_data
	spi2c_irq->rw_state = SPI2C_STATE_IDLE;
	spi2c_irq->busy = 0;

	sp_i2cm_reset(sr);
	return ret;
}

static int sp_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct sp_i2c_dev *spi2c = adap->algo_data;
	struct sp_i2c_cmd *spi2c_cmd = &spi2c->spi2c_cmd;
	unsigned char restart_w_data[32] = {0};
	unsigned int  restart_write_cnt = 0;
	unsigned int  restart_en = 0;
	int ret = SPI2C_SUCCESS;
	int i = 0;

	ret = pm_runtime_get_sync(spi2c->dev);

	if (num == 0)
		return -EINVAL;

	memset(spi2c_cmd, 0, sizeof(*spi2c_cmd));
	spi2c_cmd->dev_id = adap->nr;

	if (spi2c_cmd->freq > SP_I2C_FAST_FREQ)
		spi2c_cmd->freq = SP_I2C_FAST_FREQ;
	else
		spi2c_cmd->freq = spi2c->i2c_clk_freq / 1000;

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_TEN)
			return -EINVAL;

		spi2c_cmd->slave_addr = msgs[i].addr;
		if (msgs[i].flags & I2C_M_NOSTART) {
			restart_write_cnt = msgs[i].len;
			for (i = 0; i < restart_write_cnt; i++)
				restart_w_data[i] = msgs[i].buf[i];

			restart_en = 1;
			continue;
		}
		if (msgs[i].flags & I2C_M_RD) {
			if (restart_en == 1) {
				spi2c_cmd->write_cnt = restart_write_cnt;
				spi2c_cmd->write_data = restart_w_data;
				restart_en = 0;
				spi2c_cmd->restart_en = 1;
			}
			spi2c_cmd->read_cnt = msgs[i].len;
			spi2c_cmd->read_data = i2c_get_dma_safe_msg_buf(&msgs[i], 4);

			if (spi2c_cmd->read_cnt < 4 || !spi2c_cmd->read_data) {
				spi2c_cmd->read_data = msgs[i].buf;
				ret = sp_i2cm_read(spi2c_cmd, spi2c);
			} else {
				ret = sp_i2cm_dma_read(spi2c_cmd, spi2c);
				i2c_put_dma_safe_msg_buf(spi2c_cmd->read_data, &msgs[i], true);
			}
		} else {
			spi2c_cmd->write_cnt = msgs[i].len;
			spi2c_cmd->write_data = i2c_get_dma_safe_msg_buf(&msgs[i], 4);
				if (spi2c_cmd->write_cnt < 4 || !spi2c_cmd->write_data) {
					spi2c_cmd->write_data = msgs[i].buf;
					ret = sp_i2cm_write(spi2c_cmd, spi2c);
				} else {
					ret = sp_i2cm_dma_write(spi2c_cmd, spi2c);
					i2c_put_dma_safe_msg_buf(spi2c_cmd->write_data,
								 &msgs[i], true);
				}
		}
		if (ret != SPI2C_SUCCESS)
			return -EIO;
	}

	pm_runtime_put(spi2c->dev);
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
	.mode = SP_I2C_DMA_POWER_SW,
	.total_port = 4,
};

static const struct i2c_compatible i2c_645_compat = {
	.mode = SP_I2C_DMA_POWER_NO_SW,
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

static void sp_i2c_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static void sp_i2c_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

static int sp_i2c_probe(struct platform_device *pdev)
{
	struct sp_i2c_dev *spi2c;
	struct i2c_adapter *p_adap;
	unsigned int i2c_clk_freq;
	int device_id = 0;
	int ret = SPI2C_SUCCESS;
	struct device *dev = &pdev->dev;
	const struct i2c_compatible *dev_mode;

	if (pdev->dev.of_node) {
		pdev->id = of_alias_get_id(pdev->dev.of_node, "i2c");
		device_id = pdev->id;
	}

	spi2c = devm_kzalloc(&pdev->dev, sizeof(*spi2c), GFP_KERNEL);
	if (!spi2c)
		return -ENOMEM;

	if (!of_property_read_u32(pdev->dev.of_node, "clock-frequency", &i2c_clk_freq))
		spi2c->i2c_clk_freq = i2c_clk_freq;
	else
		spi2c->i2c_clk_freq = SP_I2C_STD_FREQ * 1000;

	spi2c->dev = &pdev->dev;
	ret = _sp_i2cm_get_resources(pdev, spi2c);
	if (ret != SPI2C_SUCCESS)
		return ret;

	spi2c->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(spi2c->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(spi2c->clk), "err get clock\n");

	spi2c->rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(spi2c->rstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(spi2c->rstc), "err get reset\n");

	ret = clk_prepare_enable(spi2c->clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to enable clk\n");

	ret = devm_add_action_or_reset(dev, sp_i2c_disable_unprepare, spi2c->clk);
	if (ret)
		return ret;

	ret = reset_control_deassert(spi2c->rstc);
	if (ret)
		dev_err_probe(&pdev->dev, ret, "failed to deassert reset\n");

	ret = devm_add_action_or_reset(dev, sp_i2c_reset_control_assert, spi2c->rstc);
	if (ret)
		return ret;

	init_waitqueue_head(&spi2c->wait);

	dev_mode = of_device_get_match_data(&pdev->dev);
	spi2c->mode = dev_mode->mode;
	spi2c->total_port = dev_mode->total_port;
	p_adap = &spi2c->adap;
	sprintf(p_adap->name, "%s%d", "sunplus-i2cm", device_id);
	p_adap->algo = &sp_algorithm;
	p_adap->algo_data = spi2c;
	p_adap->nr = device_id;
	p_adap->class = 0;
	p_adap->retries = 5;
	p_adap->dev.parent = &pdev->dev;
	p_adap->dev.of_node = pdev->dev.of_node;

	ret = i2c_add_numbered_adapter(p_adap);
	ret = _sp_i2cm_init(device_id, spi2c);
	platform_set_drvdata(pdev, spi2c);

	pm_runtime_set_autosuspend_delay(&pdev->dev, 5000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return ret;
}

static int sp_i2c_remove(struct platform_device *pdev)
{
	struct sp_i2c_dev *spi2c = platform_get_drvdata(pdev);
	struct i2c_adapter *p_adap = &spi2c->adap;

	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	i2c_del_adapter(p_adap);

	return 0;
}

static int __maybe_unused sp_i2c_suspend(struct device *dev)
{
	struct sp_i2c_dev *spi2c = dev_get_drvdata(dev);
	struct i2c_adapter *p_adap = &spi2c->adap;

	if (p_adap->nr < spi2c->total_port)
		reset_control_assert(spi2c->rstc);

	return 0;
}

static int __maybe_unused sp_i2c_resume(struct device *dev)
{
	struct sp_i2c_dev *spi2c = dev_get_drvdata(dev);
	struct i2c_adapter *p_adap = &spi2c->adap;

	if (p_adap->nr < spi2c->total_port) {
		reset_control_deassert(spi2c->rstc);
		clk_prepare_enable(spi2c->clk);
	}
	return 0;
}

static int sp_i2c_runtime_suspend(struct device *dev)
{
	struct sp_i2c_dev *spi2c = dev_get_drvdata(dev);
	struct i2c_adapter *p_adap = &spi2c->adap;

	if (p_adap->nr < spi2c->total_port)
		reset_control_assert(spi2c->rstc);

	return 0;
}

static int sp_i2c_runtime_resume(struct device *dev)
{
	struct sp_i2c_dev *spi2c = dev_get_drvdata(dev);
	struct i2c_adapter *p_adap = &spi2c->adap;

	if (p_adap->nr < spi2c->total_port) {
		reset_control_deassert(spi2c->rstc);   //release reset
		clk_prepare_enable(spi2c->clk);        //enable clken and disable gclken
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
		.name		= "sunplus-i2cm",
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
MODULE_AUTHOR("Li-hao Kuo <lhjeff911@gmail.com>");
MODULE_DESCRIPTION("Sunplus I2C Master Driver");
