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

#ifdef CONFIG_PM_RUNTIME_I2C
#include <linux/pm_runtime.h>
#endif


//#define I2C_RETEST

//#define I2C_FUNC_DEBUG
//#define I2C_DBG_INFO
#define I2C_DBG_ERR

#ifdef I2C_FUNC_DEBUG
	#define FUNC_DEBUG()    pr_info("[I2C] Debug: %s(%d)\n", __func__, __LINE__)
#else
	#define FUNC_DEBUG()
#endif

#ifdef I2C_DBG_INFO
	#define DBG_INFO(fmt, args ...)    pr_info("[I2C] Info (%d):  "  fmt"\n", __LINE__, ## args)
#else
	#define DBG_INFO(fmt, args ...)
#endif

#ifdef I2C_DBG_ERR
	#define DBG_ERR(fmt, args ...)    pr_info("[I2C] Error (%d):  "  fmt"\n", __LINE__, ## args)
#else
	#define DBG_ERR(fmt, args ...)
#endif

#define I2C_FREQ             400
#define I2C_SLEEP_TIMEOUT    200
#define I2C_SCL_DELAY        1  //SCl dalay xT

#define I2C_CLK_SOURCE_FREQ         27000  // KHz(27MHz)
#define I2C_BUFFER_SIZE             1024   // Byte



#define I2CM_REG_NAME        "i2cm"

#define DEVICE_NAME          "sp7021-i2cm"

#define I2CM_DMA_REG_NAME    "i2cmdma"

#define I2C_MASTER_NUM    (4)

//burst write use
#define I2C_EMPTY_THRESHOLD_VALUE    4

//burst read use
#define I2C_IS_READ16BYTE

#ifdef I2C_IS_READ16BYTE
#define I2C_BURST_RDATA_BYTES        16
#define I2C_BURST_RDATA_FLAG         0x80008000
#else
#define I2C_BURST_RDATA_BYTES        4
#define I2C_BURST_RDATA_FLAG         0x88888888
#endif

#define I2C_BURST_RDATA_ALL_FLAG     0xFFFFFFFF


//control0
#define I2C_CTL0_FREQ(x)                  (x<<24)  //bit[26:24]
#define I2C_CTL0_PREFETCH                 (1<<18)
#define I2C_CTL0_RESTART_EN               (1<<17)  //0:disable 1:enable
#define I2C_CTL0_SUBADDR_EN               (1<<16)  //For restart mode need to set high
#define I2C_CTL0_SW_RESET                 (1<<15)
#define I2C_CTL0_SLAVE_ADDR(x)            (x<<1)   //bit[7:1]

//control1
#define I2C_CTL1_ALL_CLR                  (0x3FF)
#define I2C_CTL1_EMPTY_CLR                (1<<9)
#define I2C_CTL1_SCL_HOLD_TOO_LONG_CLR    (1<<8)
#define I2C_CTL1_SCL_WAIT_CLR             (1<<7)
#define I2C_CTL1_EMPTY_THRESHOLD_CLR      (1<<6)
#define I2C_CTL1_DATA_NACK_CLR            (1<<5)
#define I2C_CTL1_ADDRESS_NACK_CLR         (1<<4)
#define I2C_CTL1_BUSY_CLR                 (1<<3)
#define I2C_CTL1_CLKERR_CLR               (1<<2)
#define I2C_CTL1_DONE_CLR                 (1<<1)
#define I2C_CTL1_SIFBUSY_CLR              (1<<0)

//control2
#define I2C_CTL2_FREQ_CUSTOM(x)           (x<<0)   //bit[10:0]
#define I2C_CTL2_SCL_DELAY(x)             (x<<24)  //bit[25:24]
#define I2C_CTL2_SDA_HALF_ENABLE          (1<<31)

//control7
#define I2C_CTL7_RDCOUNT(x)               (x<<16)  //bit[31:16]
#define I2C_CTL7_WRCOUNT(x)               (x<<0)   //bit[15:0]


#define I2C_CTL0_FREQ_MASK                  (0x7)     // 3 bit
#define I2C_CTL0_SLAVE_ADDR_MASK            (0x7F)    // 7 bit
#define I2C_CTL2_FREQ_CUSTOM_MASK           (0x7FF)   // 11 bit
#define I2C_CTL2_SCL_DELAY_MASK             (0x3)     // 2 bit
#define I2C_CTL7_RW_COUNT_MASK              (0xFFFF)  // 16 bit
#define I2C_EN0_CTL_EMPTY_THRESHOLD_MASK    (0x7)     // 3 bit
#define I2C_SG_DMA_LLI_INDEX_MASK           (0x1F)    // 5 bit

//interrupt enable1
#define I2C_EN1_BURST_RDATA_INT           (0x80008000)  //must sync with GET_BYTES_EACHTIME

//interrupt enable2
#define I2C_EN2_BURST_RDATA_OVERFLOW_INT  (0xFFFFFFFF)

//i2c master mode
#define I2C_MODE_DMA_MODE                 (1<<2)
#define I2C_MODE_MANUAL_MODE              (1<<1)  //0:trigger mode 1:auto mode
#define I2C_MODE_MANUAL_TRIG              (1<<0)


//dma config
#define I2C_DMA_CFG_DMA_GO                (1<<8)
#define I2C_DMA_CFG_NON_BUF_MODE          (1<<2)
#define I2C_DMA_CFG_SAME_SLAVE            (1<<1)
#define I2C_DMA_CFG_DMA_MODE              (1<<0)

//dma interrupt flag
#define I2C_DMA_INT_LENGTH0_FLAG          (1<<6)
#define I2C_DMA_INT_THRESHOLD_FLAG        (1<<5)
#define I2C_DMA_INT_IP_TIMEOUT_FLAG       (1<<4)
#define I2C_DMA_INT_GDMA_TIMEOUT_FLAG     (1<<3)
#define I2C_DMA_INT_WB_EN_ERROR_FLAG      (1<<2)
#define I2C_DMA_INT_WCNT_ERROR_FLAG       (1<<1)
#define I2C_DMA_INT_DMA_DONE_FLAG         (1<<0)

//dma interrupt enable
#define I2C_DMA_EN_LENGTH0_INT            (1<<6)
#define I2C_DMA_EN_THRESHOLD_INT          (1<<5)
#define I2C_DMA_EN_IP_TIMEOUT_INT         (1<<4)
#define I2C_DMA_EN_GDMA_TIMEOUT_INT       (1<<3)
#define I2C_DMA_EN_WB_EN_ERROR_INT        (1<<2)
#define I2C_DMA_EN_WCNT_ERROR_INT         (1<<1)
#define I2C_DMA_EN_DMA_DONE_INT           (1<<0)

//interrupt
#define I2C_INT_RINC_INDEX(x)             (x<<18)  //bit[20:18]
#define I2C_INT_WINC_INDEX(x)             (x<<15)  //bit[17:15]
#define I2C_INT_SCL_HOLD_TOO_LONG_FLAG    (1<<11)
#define I2C_INT_WFIFO_ENABLE              (1<<10)
#define I2C_INT_FULL_FLAG                 (1<<9)
#define I2C_INT_EMPTY_FLAG                (1<<8)
#define I2C_INT_SCL_WAIT_FLAG             (1<<7)
#define I2C_INT_EMPTY_THRESHOLD_FLAG      (1<<6)
#define I2C_INT_DATA_NACK_FLAG            (1<<5)
#define I2C_INT_ADDRESS_NACK_FLAG         (1<<4)
#define I2C_INT_BUSY_FLAG                 (1<<3)
#define I2C_INT_CLKERR_FLAG               (1<<2)
#define I2C_INT_DONE_FLAG                 (1<<1)
#define I2C_INT_SIFBUSY_FLAG              (1<<0)


//interrupt enable0
#define I2C_EN0_SCL_HOLD_TOO_LONG_INT     (1<<13)
#define I2C_EN0_NACK_INT                  (1<<12)
#define I2C_EN0_CTL_EMPTY_THRESHOLD(x)    (x<<9)  //bit[11:9]
#define I2C_EN0_EMPTY_INT                 (1<<8)
#define I2C_EN0_SCL_WAIT_INT              (1<<7)
#define I2C_EN0_EMPTY_THRESHOLD_INT       (1<<6)
#define I2C_EN0_DATA_NACK_INT             (1<<5)
#define I2C_EN0_ADDRESS_NACK_INT          (1<<4)
#define I2C_EN0_BUSY_INT                  (1<<3)
#define I2C_EN0_CLKERR_INT                (1<<2)
#define I2C_EN0_DONE_INT                  (1<<1)
#define I2C_EN0_SIFBUSY_INT               (1<<0)


#define I2C_RESET(id, val)          ((1 << (16 + id)) | (val << id))
#define I2C_CLKEN(id, val)          ((1 << (16 + id)) | (val << id))
#define I2C_GCLKEN(id, val)         ((1 << (16 + id)) | (val << id))


struct regs_i2cm_s {
	unsigned int control0;      /* 00 */
	unsigned int control1;      /* 01 */
	unsigned int control2;      /* 02 */
	unsigned int control3;      /* 03 */
	unsigned int control4;      /* 04 */
	unsigned int control5;      /* 05 */
	unsigned int i2cm_status0;  /* 06 */
	unsigned int interrupt;     /* 07 */
	unsigned int int_en0;       /* 08 */
	unsigned int i2cm_mode;     /* 09 */
	unsigned int i2cm_status1;  /* 10 */
	unsigned int i2cm_status2;  /* 11 */
	unsigned int control6;      /* 12 */
	unsigned int int_en1;       /* 13 */
	unsigned int i2cm_status3;  /* 14 */
	unsigned int i2cm_status4;  /* 15 */
	unsigned int int_en2;       /* 16 */
	unsigned int control7;      /* 17 */
	unsigned int control8;      /* 18 */
	unsigned int control9;      /* 19 */
	unsigned int reserved[3];   /* 20~22 */
	unsigned int version;       /* 23 */
	unsigned int data00_03;     /* 24 */
	unsigned int data04_07;     /* 25 */
	unsigned int data08_11;     /* 26 */
	unsigned int data12_15;     /* 27 */
	unsigned int data16_19;     /* 28 */
	unsigned int data20_23;     /* 29 */
	unsigned int data24_27;     /* 30 */
	unsigned int data28_31;     /* 31 */
};

struct regs_i2cm_dma_s {
	unsigned int hw_version;                /* 00 */
	unsigned int dma_config;                /* 01 */
	unsigned int dma_length;                /* 02 */
	unsigned int dma_addr;                  /* 03 */
	unsigned int port_mux;                  /* 04 */
	unsigned int int_flag;                  /* 05 */
	unsigned int int_en;                    /* 06 */
	unsigned int sw_reset_state;            /* 07 */
	unsigned int reserved[2];               /* 08~09 */
	unsigned int sg_dma_index;              /* 10 */
	unsigned int sg_dma_config;             /* 11 */
	unsigned int sg_dma_length;             /* 12 */
	unsigned int sg_dma_addr;               /* 13 */
	unsigned int reserved2;                 /* 14 */
	unsigned int sg_setting;                /* 15 */
	unsigned int threshold;                 /* 16 */
	unsigned int reserved3;                 /* 17 */
	unsigned int gdma_read_timeout;         /* 18 */
	unsigned int gdma_write_timeout;        /* 19 */
	unsigned int ip_read_timeout;           /* 20 */
	unsigned int ip_write_timeout;          /* 21 */
	unsigned int write_cnt_debug;           /* 22 */
	unsigned int w_byte_en_debug;           /* 23 */
	unsigned int sw_reset_write_cnt_debug;  /* 24 */
	unsigned int reserved4[7];              /* 25~31 */
};

enum I2C_Status_e_ {
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

enum I2C_State_e_ {
	I2C_WRITE_STATE,  /* i2c is write */
	I2C_READ_STATE,   /* i2c is read */
	I2C_IDLE_STATE,   /* i2c is idle */
	I2C_DMA_WRITE_STATE,/* i2c is dma write */
	I2C_DMA_READ_STATE, /* i2c is dma read */
};

enum I2C_switch_e_ {
	I2C_POWER_ALL_SWITCH,
	I2C_POWER_NO_SWITCH,
};

struct I2C_Cmd_t_ {
	unsigned int dDevId;
	unsigned int dFreq;
	unsigned int dSlaveAddr;
	unsigned int dRestartEn;
	unsigned int dWrDataCnt;
	unsigned int dRdDataCnt;
	unsigned char *pWrData;
	unsigned char *pRdData;
};

struct I2C_Irq_Dma_Flag_t_ {
	unsigned char bDmaDone;
	unsigned char bWCntError;
	unsigned char bWBEnError;
	unsigned char bGDmaTimeout;
	unsigned char bIPTimeout;
	unsigned char bThreshold;
	unsigned char bLength0;
};

struct I2C_Irq_Flag_t_ {
	unsigned char bActiveDone;
	unsigned char bAddrNack;
	unsigned char bDataNack;
	unsigned char bEmptyThreshold;
	unsigned char bFiFoEmpty;
	unsigned char bFiFoFull;
	unsigned char bSCLHoldTooLong;
	unsigned char bRdOverflow;
};

struct I2C_Irq_Event_t_ {
	enum I2C_State_e_ eRWState;
	struct I2C_Irq_Flag_t_ stIrqFlag;
	struct I2C_Irq_Dma_Flag_t_ stIrqDmaFlag;
	unsigned int dDevId;
	unsigned int dBurstCount;
	unsigned int dBurstRemainder;
	unsigned int dDataIndex;
	unsigned int dDataTotalLen;
	unsigned int dRegDataIndex;
	unsigned char bI2CBusy;
	unsigned char bRet;
	unsigned char *pDataBuf;
};


enum I2C_DMA_RW_Mode_e_ {
	I2C_DMA_WRITE_MODE,
	I2C_DMA_READ_MODE,
};

enum I2C_RW_Mode_e_ {
	I2C_WRITE_MODE,
	I2C_READ_MODE,
	I2C_RESTART_MODE,
};

enum I2C_Active_Mode_e_ {
	I2C_TRIGGER,
	I2C_AUTO,
};


struct i2c_compatible {
	int mode; /* clk source switch*/
};

struct SpI2C_If_t_ {
	struct i2c_msg *msgs;  /* messages currently handled */
	struct i2c_adapter adap;
	struct device *dev;
	struct I2C_Cmd_t_ stCmdInfo;
	struct I2C_Irq_Event_t_ stIrqEvent;
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
};



#ifdef I2C_RETEST
int test_count;
#endif



void sp_i2cm_status_clear(struct regs_i2cm_s *sr, unsigned int flag)
{
	unsigned int ctl1;

		ctl1 = readl(&sr->control1);
		ctl1 |= flag;
		writel(ctl1, &sr->control1);

		ctl1 = readl(&sr->control1);
		ctl1 &= (~flag);
		writel(ctl1, &sr->control1);
}

void sp_i2cm_dma_int_flag_clear(struct regs_i2cm_dma_s *sr_dma, unsigned int flag)
{
	unsigned int val;

	val = readl(&sr_dma->int_flag);
	val |= flag;
	writel(val, &sr_dma->int_flag);
}

void sp_i2cm_reset(struct regs_i2cm_s *sr)
{
	unsigned int ctl0;

	ctl0 = readl(&sr->control0);
	ctl0 |= I2C_CTL0_SW_RESET;
	writel(ctl0, &sr->control0);

	udelay(2);
}

void sp_i2cm_data0_set(struct regs_i2cm_s *sr, unsigned int *wdata)
{
	writel(*wdata, &sr->data00_03);
}


void sp_i2cm_int_en0_disable(struct regs_i2cm_s *sr, unsigned int int0)
{
	unsigned int val;

	val = readl(&sr->int_en0);
	val &= (~int0);
	writel(val, &sr->int_en0);

}

void sp_i2cm_rdata_flag_get(struct regs_i2cm_s *sr, unsigned int *flag)
{
		*flag = readl(&sr->i2cm_status3);
}

void sp_i2cm_data_get(struct regs_i2cm_s *sr, unsigned int index, unsigned int *rdata)
{
		switch (index) {
		case 0:
			*rdata = readl(&sr->data00_03);
			break;

		case 1:
			*rdata = readl(&sr->data04_07);
			break;

		case 2:
			*rdata = readl(&sr->data08_11);
			break;

		case 3:
			*rdata = readl(&sr->data12_15);
			break;

		case 4:
			*rdata = readl(&sr->data16_19);
			break;

		case 5:
			*rdata = readl(&sr->data20_23);
			break;

		case 6:
			*rdata = readl(&sr->data24_27);
			break;

		case 7:
			*rdata = readl(&sr->data28_31);
			break;

		default:
			break;
		}
}

void sp_i2cm_rdata_flag_clear(struct regs_i2cm_s *sr, unsigned int flag)
{
		writel(flag, &sr->control6);
		writel(0, &sr->control6);
}

void sp_i2cm_clock_freq_set(struct regs_i2cm_s *sr,  unsigned int freq)
{
	unsigned int div;
	unsigned int ctl0, ctl2;

		div = I2C_CLK_SOURCE_FREQ / freq;
		div -= 1;
		if (I2C_CLK_SOURCE_FREQ % freq != 0)
			div += 1;

		if (div > I2C_CTL2_FREQ_CUSTOM_MASK)
			div = I2C_CTL2_FREQ_CUSTOM_MASK;

		ctl0 = readl(&sr->control0);
		ctl0 &= (~I2C_CTL0_FREQ(I2C_CTL0_FREQ_MASK));
		writel(ctl0, &sr->control0);

		ctl2 = readl(&sr->control2);
		ctl2 &= (~I2C_CTL2_FREQ_CUSTOM(I2C_CTL2_FREQ_CUSTOM_MASK));
		ctl2 |= I2C_CTL2_FREQ_CUSTOM(div);
		writel(ctl2, &sr->control2);

}

void sp_i2cm_slave_addr_set(struct regs_i2cm_s *sr, unsigned int addr)
{
	unsigned int t_addr = addr & I2C_CTL0_SLAVE_ADDR_MASK;
	unsigned int ctl0;

		ctl0 = readl(&sr->control0);
		ctl0 &= (~I2C_CTL0_SLAVE_ADDR(I2C_CTL0_SLAVE_ADDR_MASK));
		ctl0 |= I2C_CTL0_SLAVE_ADDR(t_addr);
		writel(ctl0, &sr->control0);
}

void sp_i2cm_scl_delay_set(struct regs_i2cm_s *sr, unsigned int delay)
{
	unsigned int ctl2;

		ctl2 = readl(&sr->control2);
		ctl2 &= (~I2C_CTL2_SCL_DELAY(I2C_CTL2_SCL_DELAY_MASK));
		ctl2 |= I2C_CTL2_SCL_DELAY(delay);
		ctl2 &= (~(I2C_CTL2_SDA_HALF_ENABLE));
		writel(ctl2, &sr->control2);
}

void sp_i2cm_trans_cnt_set(struct regs_i2cm_s *sr, unsigned int write_cnt,
		unsigned int read_cnt)
{
	unsigned int t_write = write_cnt & I2C_CTL7_RW_COUNT_MASK;
	unsigned int t_read = read_cnt & I2C_CTL7_RW_COUNT_MASK;
	unsigned int ctl7;

		ctl7 = I2C_CTL7_WRCOUNT(t_write) | I2C_CTL7_RDCOUNT(t_read);
		writel(ctl7, &sr->control7);
}

void sp_i2cm_active_mode_set(struct regs_i2cm_s *sr, enum I2C_Active_Mode_e_ mode)
{
	unsigned int val;

		val = readl(&sr->i2cm_mode);
		val &= (~(I2C_MODE_MANUAL_MODE | I2C_MODE_MANUAL_TRIG));
		switch (mode) {
		default:
		case I2C_TRIGGER:
			break;

		case I2C_AUTO:
			val |= I2C_MODE_MANUAL_MODE;
			break;
		}
		writel(val, &sr->i2cm_mode);
}

void sp_i2cm_data_set(struct regs_i2cm_s *sr, unsigned int *wdata)
{
		writel(wdata[0], &sr->data00_03);
		writel(wdata[1], &sr->data04_07);
		writel(wdata[2], &sr->data08_11);
		writel(wdata[3], &sr->data12_15);
		writel(wdata[4], &sr->data16_19);
		writel(wdata[5], &sr->data20_23);
		writel(wdata[6], &sr->data24_27);
		writel(wdata[7], &sr->data28_31);
}

void sp_i2cm_rw_mode_set(struct regs_i2cm_s *sr, enum I2C_RW_Mode_e_ rw_mode)
{
	unsigned int ctl0;

		ctl0 = readl(&sr->control0);
		switch (rw_mode) {
		default:
		case I2C_WRITE_MODE:
			ctl0 &= (~(I2C_CTL0_PREFETCH | I2C_CTL0_RESTART_EN | I2C_CTL0_SUBADDR_EN));
			break;

		case I2C_READ_MODE:
			ctl0 &= (~(I2C_CTL0_RESTART_EN | I2C_CTL0_SUBADDR_EN));
			ctl0 |= I2C_CTL0_PREFETCH;
			break;

		case I2C_RESTART_MODE:
			ctl0 |= (I2C_CTL0_PREFETCH | I2C_CTL0_RESTART_EN | I2C_CTL0_SUBADDR_EN);
			break;
		}
		writel(ctl0, &sr->control0);
}


void sp_i2cm_int_en0_set(struct regs_i2cm_s *sr, unsigned int int0)
{
		writel(int0, &sr->int_en0);
}

void sp_i2cm_int_en1_set(struct regs_i2cm_s *sr, unsigned int rdata_en)
{
		writel(rdata_en, &sr->int_en1);
}

void sp_i2cm_int_en2_set(struct regs_i2cm_s *sr, unsigned int overflow_en)
{
		writel(overflow_en, &sr->int_en2);
}


#define MOON0_BASE           0xF8000000

struct Moon_RegBase_s {
	void __iomem *moon0_regs;
} Moon_RegBase;

struct Moon_RegBase_s stMoonRegBase;

struct regs_moon0_s {
	unsigned int stamp;         /* 00 */
	unsigned int clken[10];     /* 01~10 */
	unsigned int gclken[10];    /* 11~20 */
	unsigned int reset[10];     /* 21~30 */
	unsigned int sfg_cfg_mode;  /* 31 */
} regs_moon0;


void sp_i2cm_enable(unsigned int device_id, void __iomem *membase)
{
	struct regs_moon0_s *pMoon0Reg = (struct regs_moon0_s *)membase;

		writel(I2C_RESET(device_id, 0), &(pMoon0Reg->reset[3]));
		writel(I2C_CLKEN(device_id, 1), &(pMoon0Reg->clken[3]));
		writel(I2C_GCLKEN(device_id, 0), &(pMoon0Reg->gclken[3]));
}

void sp_i2cm_manual_trigger(struct regs_i2cm_s *sr)
{
	unsigned int val;

		val = readl(&sr->i2cm_mode);
		val |= I2C_MODE_MANUAL_TRIG;
		writel(val, &sr->i2cm_mode);
}

void sp_i2cm_int_en0_with_thershold_set(struct regs_i2cm_s *sr,
			unsigned int int0, unsigned char threshold)
{
	unsigned int val;

		val = (int0 | I2C_EN0_CTL_EMPTY_THRESHOLD(threshold));
		writel(val, &sr->int_en0);
}

void sp_i2cm_dma_mode_enable(struct regs_i2cm_s *sr)
{
	unsigned int val;

		val = readl(&sr->i2cm_mode);
		val |= I2C_MODE_DMA_MODE;
		writel(val, &sr->i2cm_mode);
}

void sp_i2cm_dma_addr_set(struct regs_i2cm_dma_s *sr_dma, unsigned int addr)
{
		writel(addr, &sr_dma->dma_addr);
}

void sp_i2cm_dma_length_set(struct regs_i2cm_dma_s *sr_dma, unsigned int length)
{
		length &= (0xFFFF);  //only support 16 bit
		writel(length, &sr_dma->dma_length);
}

void sp_i2cm_dma_rw_mode_set(struct regs_i2cm_dma_s *sr_dma,
		enum I2C_DMA_RW_Mode_e_ rw_mode)
{
	unsigned int val;

		val = readl(&sr_dma->dma_config);
		switch (rw_mode) {
		default:
		case I2C_DMA_WRITE_MODE:
			val |= I2C_DMA_CFG_DMA_MODE;
			break;

		case I2C_DMA_READ_MODE:
			val &= (~I2C_DMA_CFG_DMA_MODE);
			break;
		}
		writel(val, &sr_dma->dma_config);

}

void sp_i2cm_dma_int_en_set(struct regs_i2cm_dma_s *sr_dma, unsigned int dma_int)
{
		writel(dma_int, &sr_dma->int_en);
}

void sp_i2cm_dma_go_set(struct regs_i2cm_dma_s *sr_dma)
{
	unsigned int val;

		val = readl(&sr_dma->dma_config);
		val |= I2C_DMA_CFG_DMA_GO;
		writel(val, &sr_dma->dma_config);
}


static void _sp_i2cm_intflag_check(struct SpI2C_If_t_ *pstSpI2CInfo,
		struct I2C_Irq_Event_t_ *pstIrqEvent)
{

	struct regs_i2cm_s *sr = (struct regs_i2cm_s *)pstSpI2CInfo->i2c_regs;
	unsigned int int_flag = 0;
	unsigned int overflow_flag = 0;
	#ifdef I2C_RETEST
	unsigned int scl_belay = 0;
    #endif


	int_flag = readl(&sr->interrupt);

	if (int_flag & I2C_INT_DONE_FLAG) {
		DBG_INFO("I2C is done !!\n");
		pstIrqEvent->stIrqFlag.bActiveDone = 1;
	} else {
		pstIrqEvent->stIrqFlag.bActiveDone = 0;
	}

	if (int_flag & I2C_INT_ADDRESS_NACK_FLAG) {
		DBG_INFO("I2C slave address NACK !!\n");
	#ifdef I2C_RETEST

		scl_belay = readl(&sr->control2);
		scl_belay &= I2C_CTL2_SCL_DELAY(I2C_CTL2_SCL_DELAY_MASK);

		if (scl_belay == 0x00)
			test_count++;
		else if (test_count > 9)
			test_count = 1;
	#endif
		pstIrqEvent->stIrqFlag.bAddrNack = 1;
	} else {
		pstIrqEvent->stIrqFlag.bAddrNack = 0;
	}

	if (int_flag & I2C_INT_DATA_NACK_FLAG) {
		DBG_INFO("I2C slave data NACK !!\n");
		pstIrqEvent->stIrqFlag.bDataNack = 1;
	} else {
		pstIrqEvent->stIrqFlag.bDataNack = 0;
	}

	// write use
	if (int_flag & I2C_INT_EMPTY_THRESHOLD_FLAG) {
		DBG_INFO("I2C empty threshold occur !!\n");
		pstIrqEvent->stIrqFlag.bEmptyThreshold = 1;
	} else {
		pstIrqEvent->stIrqFlag.bEmptyThreshold = 0;
	}

	// write use
	if (int_flag & I2C_INT_EMPTY_FLAG) {
		DBG_INFO("I2C FIFO empty occur !!\n");
		pstIrqEvent->stIrqFlag.bFiFoEmpty = 1;
	} else {
		pstIrqEvent->stIrqFlag.bFiFoEmpty = 0;
	}

	// write use (for debug)
	if (int_flag & I2C_INT_FULL_FLAG) {
		DBG_INFO("I2C FIFO full occur !!\n");
		pstIrqEvent->stIrqFlag.bFiFoFull = 1;
	} else {
		pstIrqEvent->stIrqFlag.bFiFoFull = 0;
	}

	if (int_flag & I2C_INT_SCL_HOLD_TOO_LONG_FLAG) {
		DBG_INFO("I2C SCL hold too long occur !!\n");
		pstIrqEvent->stIrqFlag.bSCLHoldTooLong = 1;
	} else {
		pstIrqEvent->stIrqFlag.bSCLHoldTooLong = 0;
	}
	sp_i2cm_status_clear(sr, I2C_CTL1_ALL_CLR);

	// read use
	overflow_flag = readl(&sr->i2cm_status4);

	if (overflow_flag) {
		DBG_ERR("I2C burst read data overflow !! overflow_flag = 0x%x\n", overflow_flag);
		pstIrqEvent->stIrqFlag.bRdOverflow = 1;
	} else {
		pstIrqEvent->stIrqFlag.bRdOverflow = 0;
	}
}

static void _sp_i2cm_dma_intflag_check(struct SpI2C_If_t_ *pstSpI2CInfo,
		struct I2C_Irq_Event_t_ *pstIrqEvent)
{
	struct regs_i2cm_dma_s *sr_dma = (struct regs_i2cm_dma_s *)pstSpI2CInfo->i2c_dma_regs;
	unsigned int int_flag = 0;

	int_flag = readl(&sr_dma->int_flag);

	if (int_flag & I2C_DMA_INT_DMA_DONE_FLAG) {
		DBG_INFO("I2C DMA is done !!\n");
		pstIrqEvent->stIrqDmaFlag.bDmaDone = 1;
	} else {
		pstIrqEvent->stIrqDmaFlag.bDmaDone = 0;
	}

	if (int_flag & I2C_DMA_INT_WCNT_ERROR_FLAG) {
		DBG_INFO("I2C DMA WCNT ERR !!\n");
		pstIrqEvent->stIrqDmaFlag.bWCntError = 1;
	} else {
		pstIrqEvent->stIrqDmaFlag.bWCntError = 0;
	}
	if (int_flag & I2C_DMA_INT_WB_EN_ERROR_FLAG) {
		DBG_INFO("I2C DMA WB EN ERR !!\n");
		pstIrqEvent->stIrqDmaFlag.bWBEnError = 1;
	} else {
		pstIrqEvent->stIrqDmaFlag.bWBEnError = 0;
	}
	if (int_flag & I2C_DMA_INT_GDMA_TIMEOUT_FLAG) {
		DBG_INFO("I2C DMA timeout !!\n");
		pstIrqEvent->stIrqDmaFlag.bGDmaTimeout = 1;
	} else {
		pstIrqEvent->stIrqDmaFlag.bGDmaTimeout = 0;
	}
	if (int_flag & I2C_DMA_INT_IP_TIMEOUT_FLAG) {
		DBG_INFO("I2C IP timeout !!\n");
		pstIrqEvent->stIrqDmaFlag.bIPTimeout = 1;
	} else {
		pstIrqEvent->stIrqDmaFlag.bIPTimeout = 0;
	}
	if (int_flag & I2C_DMA_INT_LENGTH0_FLAG) {
		DBG_INFO("I2C Length is zero !!\n");
		pstIrqEvent->stIrqDmaFlag.bLength0 = 1;
	} else {
		pstIrqEvent->stIrqDmaFlag.bLength0 = 0;
	}

	sp_i2cm_dma_int_flag_clear(sr_dma, 0x7F);  //write 1 to clear

}

static irqreturn_t _sp_i2cm_irqevent_handler(int irq, void *args)
{
	struct SpI2C_If_t_ *pstSpI2CInfo = args;
	struct I2C_Irq_Event_t_ *pstIrqEvent = &(pstSpI2CInfo->stIrqEvent);
	struct regs_i2cm_s *sr = (struct regs_i2cm_s *)pstSpI2CInfo->i2c_regs;
	unsigned char w_data[32] = {0};
	unsigned char r_data[I2C_BURST_RDATA_BYTES] = {0};
	unsigned int rdata_flag = 0;
	unsigned int bit_index = 0;
	int i = 0, j = 0, k = 0;

	_sp_i2cm_intflag_check(pstSpI2CInfo, pstIrqEvent);

switch (pstIrqEvent->eRWState) {
case I2C_WRITE_STATE:
case I2C_DMA_WRITE_STATE:
	if (pstIrqEvent->stIrqFlag.bActiveDone) {
		DBG_INFO("I2C write success !!\n");
		pstIrqEvent->bRet = I2C_SUCCESS;
		wake_up(&pstSpI2CInfo->wait);
	} else if (pstIrqEvent->stIrqFlag.bAddrNack || pstIrqEvent->stIrqFlag.bDataNack) {

		if (pstIrqEvent->eRWState == I2C_DMA_WRITE_STATE)
			DBG_ERR("DMA wtire NACK!!\n");
		else
			DBG_ERR("wtire NACK!!\n");

		pstIrqEvent->bRet = I2C_ERR_RECEIVE_NACK;
		pstIrqEvent->stIrqFlag.bActiveDone = 1;
		wake_up(&pstSpI2CInfo->wait);
		sp_i2cm_reset(sr);
	} else if (pstIrqEvent->stIrqFlag.bSCLHoldTooLong) {
		DBG_ERR("I2C SCL hold too long !!\n");
		pstIrqEvent->bRet = I2C_ERR_SCL_HOLD_TOO_LONG;
		pstIrqEvent->stIrqFlag.bActiveDone = 1;
		wake_up(&pstSpI2CInfo->wait);
		sp_i2cm_reset(sr);
	} else if (pstIrqEvent->stIrqFlag.bFiFoEmpty) {
		DBG_ERR("I2C FIFO empty !!\n");
		pstIrqEvent->bRet = I2C_ERR_FIFO_EMPTY;
		pstIrqEvent->stIrqFlag.bActiveDone = 1;
		wake_up(&pstSpI2CInfo->wait);
		sp_i2cm_reset(sr);
	} else if ((pstIrqEvent->dBurstCount > 0) &&
			(pstIrqEvent->eRWState == I2C_WRITE_STATE)) {
		if (pstIrqEvent->stIrqFlag.bEmptyThreshold) {
			for (i = 0; i < I2C_EMPTY_THRESHOLD_VALUE; i++) {
				for (j = 0; j < 4; j++) {

					if (pstIrqEvent->dDataIndex >= pstIrqEvent->dDataTotalLen)
						w_data[j] = 0;
					else
						w_data[j] =
						pstIrqEvent->pDataBuf[pstIrqEvent->dDataIndex];

					pstIrqEvent->dDataIndex++;
					}
					    sp_i2cm_data0_set(sr, (unsigned int *)w_data);
					    pstIrqEvent->dBurstCount--;
					if (pstIrqEvent->dBurstCount == 0) {
						sp_i2cm_int_en0_disable(sr,
						(I2C_EN0_EMPTY_THRESHOLD_INT | I2C_EN0_EMPTY_INT));
						break;
					}
				}
				sp_i2cm_status_clear(sr, I2C_CTL1_EMPTY_THRESHOLD_CLR);
			}
	}
	break;

case I2C_READ_STATE:
case I2C_DMA_READ_STATE:
		if (pstIrqEvent->stIrqFlag.bAddrNack || pstIrqEvent->stIrqFlag.bDataNack) {

			if (pstIrqEvent->eRWState == I2C_DMA_READ_STATE)
				DBG_ERR("DMA read NACK!!\n");
			else
				DBG_ERR("read NACK!!\n");

			pstIrqEvent->bRet = I2C_ERR_RECEIVE_NACK;
			pstIrqEvent->stIrqFlag.bActiveDone = 1;
			wake_up(&pstSpI2CInfo->wait);
			sp_i2cm_reset(sr);
		} else if (pstIrqEvent->stIrqFlag.bSCLHoldTooLong) {
			DBG_ERR("I2C SCL hold too long !!\n");
			pstIrqEvent->bRet = I2C_ERR_SCL_HOLD_TOO_LONG;
			pstIrqEvent->stIrqFlag.bActiveDone = 1;
			wake_up(&pstSpI2CInfo->wait);
			sp_i2cm_reset(sr);
		} else if (pstIrqEvent->stIrqFlag.bRdOverflow) {
			DBG_ERR("I2C read data overflow !!\n");
			pstIrqEvent->bRet = I2C_ERR_RDATA_OVERFLOW;
			pstIrqEvent->stIrqFlag.bActiveDone = 1;
			wake_up(&pstSpI2CInfo->wait);
			sp_i2cm_reset(sr);
} else {
	if ((pstIrqEvent->dBurstCount > 0) && (pstIrqEvent->eRWState == I2C_READ_STATE)) {
		sp_i2cm_rdata_flag_get(sr, &rdata_flag);
		for (i = 0; i < (32 / I2C_BURST_RDATA_BYTES); i++) {
			bit_index = (I2C_BURST_RDATA_BYTES - 1) + (I2C_BURST_RDATA_BYTES * i);
			if (rdata_flag & (1 << bit_index)) {
				for (j = 0; j < (I2C_BURST_RDATA_BYTES/4); j++) {
					k = pstIrqEvent->dRegDataIndex + j;
					if (k >= 8)
						k -= 8;

					sp_i2cm_data_get(sr,
		k, (unsigned int *)(&pstIrqEvent->pDataBuf[pstIrqEvent->dDataIndex]));
					pstIrqEvent->dDataIndex += 4;
				}
				sp_i2cm_rdata_flag_clear(sr,
		(((1 << I2C_BURST_RDATA_BYTES) - 1) << (I2C_BURST_RDATA_BYTES * i)));
				pstIrqEvent->dRegDataIndex += (I2C_BURST_RDATA_BYTES / 4);
				if (pstIrqEvent->dRegDataIndex >= 8)
					pstIrqEvent->dBurstCount--;
			}
		}
	}
		if (pstIrqEvent->stIrqFlag.bActiveDone) {
			if ((pstIrqEvent->dBurstRemainder) &&
				(pstIrqEvent->eRWState == I2C_READ_STATE)) {
				j = 0;
			for (i = 0; i < (I2C_BURST_RDATA_BYTES/4); i++) {
				k = pstIrqEvent->dRegDataIndex + i;
				if (k >= 8)
					k -= 8;

				sp_i2cm_data_get(sr, k, (unsigned int *)(&r_data[j]));
				j += 4;
			}

				for (i = 0; i < pstIrqEvent->dBurstRemainder; i++)
					pstIrqEvent->pDataBuf[pstIrqEvent->dDataIndex + i]
							= r_data[i];
			}

				DBG_INFO("I2C read success !!\n");
				pstIrqEvent->bRet = I2C_SUCCESS;
				wake_up(&pstSpI2CInfo->wait);
		}
	}
	break;

default:
	break;
}
	//switch case

	_sp_i2cm_dma_intflag_check(pstSpI2CInfo, pstIrqEvent);

	switch (pstIrqEvent->eRWState) {
	case I2C_DMA_WRITE_STATE:
			DBG_INFO("I2C_DMA_WRITE_STATE !!\n");
			if (pstIrqEvent->stIrqDmaFlag.bDmaDone) {
				DBG_INFO("I2C dma write success !!\n");
				pstIrqEvent->bRet = I2C_SUCCESS;
				wake_up(&pstSpI2CInfo->wait);
			}
			break;

	case I2C_DMA_READ_STATE:
			DBG_INFO("I2C_DMA_READ_STATE !!\n");
			if (pstIrqEvent->stIrqDmaFlag.bDmaDone) {
				DBG_INFO("I2C dma read success !!\n");
				pstIrqEvent->bRet = I2C_SUCCESS;
				wake_up(&pstSpI2CInfo->wait);
			}
			break;

	default:
			break;
	}

	return IRQ_HANDLED;
}


static int _sp_i2cm_init(unsigned int device_id, struct SpI2C_If_t_ *pstSpI2CInfo)
{
	struct regs_i2cm_s *sr = (struct regs_i2cm_s *)pstSpI2CInfo->i2c_regs;

	FUNC_DEBUG();

	if (device_id >= I2C_MASTER_NUM) {
		DBG_ERR("I2C device id is not correct !! device_id=%d\n", device_id);
		return I2C_ERR_INVALID_DEVID;
	}

	DBG_INFO("[I2C adapter] i2c_regs= 0x%x\n", (unsigned int)pstSpI2CInfo->i2c_regs);
	DBG_INFO("[I2C adapter] i2c_dma_regs= 0x%x\n", (unsigned int)pstSpI2CInfo->i2c_dma_regs);

	sp_i2cm_reset(sr);

	return I2C_SUCCESS;
}

static int _sp_i2cm_get_irq(struct platform_device *pdev, struct SpI2C_If_t_ *pstSpI2CInfo)
{
	int irq;

	FUNC_DEBUG();

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		DBG_ERR("[I2C adapter] get irq number fail, irq = %d\n", irq);
		return -ENODEV;
	}

	pstSpI2CInfo->irq = irq;
	return I2C_SUCCESS;
}

static int _sp_i2cm_get_resources(struct platform_device *pdev,
		struct SpI2C_If_t_ *pstSpI2CInfo)
{
	int ret;
	struct resource *res;

	FUNC_DEBUG();

	/* find and map our resources */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, I2CM_REG_NAME);
	if (res) {
		pstSpI2CInfo->i2c_regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pstSpI2CInfo->i2c_regs))
			DBG_INFO("[I2C adapter] platform_get_resource_byname fail\n");
	} else {
		DBG_ERR("[I2C adapter] %s (%d)\n", __func__, __LINE__);
		return -ENODEV;
	}


	/* find DMA and map our resources */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, I2CM_DMA_REG_NAME);
	if (res) {
		pstSpI2CInfo->i2c_dma_regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pstSpI2CInfo->i2c_dma_regs))
			DBG_INFO("[I2C adapter] platform_get_resource_byname fail\n");
	} else {
		DBG_ERR("[I2C adapter] %s (%d)\n", __func__, __LINE__);
		return -ENODEV;
	}

	ret = _sp_i2cm_get_irq(pdev, pstSpI2CInfo);
	if (ret) {
		DBG_ERR("[I2C adapter] %s (%d) ret = %d\n", __func__, __LINE__, ret);
		return ret;
	}

	return I2C_SUCCESS;
}

int sp_i2cm_read(struct I2C_Cmd_t_ *pstCmdInfo, struct SpI2C_If_t_ *pstSpI2CInfo)
{
	struct regs_i2cm_s *sr = (struct regs_i2cm_s *)pstSpI2CInfo->i2c_regs;
	struct I2C_Irq_Event_t_ *pstIrqEvent = &(pstSpI2CInfo->stIrqEvent);
	unsigned char w_data[32] = {0};
	unsigned int read_cnt = 0;
	unsigned int write_cnt = 0;
	unsigned int burst_cnt = 0, burst_r = 0;
	unsigned int int0 = 0, int1 = 0, int2 = 0;
	int ret = I2C_SUCCESS;
	int i = 0;

	FUNC_DEBUG();

	if (pstCmdInfo->dDevId > I2C_MASTER_NUM)
		return I2C_ERR_INVALID_DEVID;

	if (pstIrqEvent->bI2CBusy) {
		DBG_ERR("I2C is busy !!\n");
		return I2C_ERR_I2C_BUSY;
	}

	memset(pstIrqEvent, 0, sizeof(*pstIrqEvent));
	pstIrqEvent->bI2CBusy = 1;

	write_cnt = pstCmdInfo->dWrDataCnt;
	read_cnt = pstCmdInfo->dRdDataCnt;

	if (pstCmdInfo->dRestartEn) {
		//if ((write_cnt > 32) || (write_cnt == 0)) {
		if (write_cnt > 32) {
			pstIrqEvent->bI2CBusy = 0;
			DBG_ERR("I2C write count is invalid !! write count=%d\n", write_cnt);
			return I2C_ERR_INVALID_CNT;
		}
	}

	if ((read_cnt > 0xFFFF)  || (read_cnt == 0)) {
		pstIrqEvent->bI2CBusy = 0;
		DBG_ERR("I2C read count is invalid !! read count=%d\n", read_cnt);
		return I2C_ERR_INVALID_CNT;
	}

	burst_cnt = read_cnt / I2C_BURST_RDATA_BYTES;
	burst_r = read_cnt % I2C_BURST_RDATA_BYTES;
	DBG_INFO("write_cnt = %d, read_cnt = %d, burst_cnt = %d, burst_r = %d\n",
			write_cnt, read_cnt, burst_cnt, burst_r);

	int0 = (I2C_EN0_SCL_HOLD_TOO_LONG_INT | I2C_EN0_EMPTY_INT | I2C_EN0_DATA_NACK_INT
			| I2C_EN0_ADDRESS_NACK_INT | I2C_EN0_DONE_INT);
	if (burst_cnt) {
		int1 = I2C_BURST_RDATA_FLAG;
		int2 = I2C_BURST_RDATA_ALL_FLAG;
	}

	pstIrqEvent->eRWState = I2C_READ_STATE;
	pstIrqEvent->dBurstCount = burst_cnt;
	pstIrqEvent->dBurstRemainder = burst_r;
	pstIrqEvent->dDataIndex = 0;
	pstIrqEvent->dRegDataIndex = 0;
	pstIrqEvent->dDataTotalLen = read_cnt;
	pstIrqEvent->pDataBuf = pstCmdInfo->pRdData;

	//hal_i2cm_reset(pstCmdInfo->dDevId);
	sp_i2cm_reset(sr);
	sp_i2cm_clock_freq_set(sr, pstCmdInfo->dFreq);
	sp_i2cm_slave_addr_set(sr, pstCmdInfo->dSlaveAddr);
	#ifdef I2C_RETEST
	if ((test_count > 1) && (test_count%3 == 0)) {
		sp_i2cm_scl_delay_set(sr, 0x01);
		DBG_INFO("test_count = %d", test_count);
	} else {
		sp_i2cm_scl_delay_set(sr, I2C_SCL_DELAY);
	}
	#endif
	sp_i2cm_trans_cnt_set(sr, write_cnt, read_cnt);
	sp_i2cm_active_mode_set(sr, I2C_TRIGGER);

	if (pstCmdInfo->dRestartEn) {
		DBG_INFO("I2C_RESTART_MODE\n");
		for (i = 0; i < write_cnt; i++)
			w_data[i] = pstCmdInfo->pWrData[i];

		sp_i2cm_data_set(sr, (unsigned int *)w_data);
		sp_i2cm_rw_mode_set(sr, I2C_RESTART_MODE);
	} else {
		DBG_INFO("I2C_READ_MODE\n");
		sp_i2cm_rw_mode_set(sr, I2C_READ_MODE);
	}

	sp_i2cm_int_en0_set(sr, int0);
	sp_i2cm_int_en1_set(sr, int1);
	sp_i2cm_int_en2_set(sr, int2);
	sp_i2cm_manual_trigger(sr);	//start send data

	ret = wait_event_timeout(pstSpI2CInfo->wait,
		pstIrqEvent->stIrqFlag.bActiveDone, (I2C_SLEEP_TIMEOUT * HZ) / 500);
	if (ret == 0) {
		DBG_ERR("I2C read timeout !!\n");
		ret = I2C_ERR_TIMEOUT_OUT;
	} else {
		ret = pstIrqEvent->bRet;
	}
	sp_i2cm_reset(sr);
	pstIrqEvent->eRWState = I2C_IDLE_STATE;
	pstIrqEvent->bI2CBusy = 0;

	return ret;
}

int sp_i2cm_write(struct I2C_Cmd_t_ *pstCmdInfo, struct SpI2C_If_t_ *pstSpI2CInfo)
{
	struct regs_i2cm_s *sr = (struct regs_i2cm_s *)pstSpI2CInfo->i2c_regs;
	struct I2C_Irq_Event_t_ *pstIrqEvent = &(pstSpI2CInfo->stIrqEvent);
	unsigned char w_data[32] = {0};
	unsigned int write_cnt = 0;
	unsigned int burst_cnt = 0;
	unsigned int int0 = 0;
	int ret = I2C_SUCCESS;
	int i = 0;

	FUNC_DEBUG();

	if (pstCmdInfo->dDevId > I2C_MASTER_NUM)
		return I2C_ERR_INVALID_DEVID;

	if (pstIrqEvent->bI2CBusy) {
		DBG_ERR("I2C is busy !!\n");
		return I2C_ERR_I2C_BUSY;
	}

	memset(pstIrqEvent, 0, sizeof(*pstIrqEvent));
	pstIrqEvent->bI2CBusy = 1;

	write_cnt = pstCmdInfo->dWrDataCnt;

	if (write_cnt > 0xFFFF) {
		pstIrqEvent->bI2CBusy = 0;
		DBG_ERR("I2C write count is invalid !! write count=%d\n", write_cnt);
		return I2C_ERR_INVALID_CNT;
	}

	if (write_cnt > 32) {
		burst_cnt = (write_cnt - 32) / 4;
		if ((write_cnt - 32) % 4)
			burst_cnt += 1;
	for (i = 0; i < 32; i++)
		w_data[i] = pstCmdInfo->pWrData[i];
	} else {
		for (i = 0; i < write_cnt; i++)
			w_data[i] = pstCmdInfo->pWrData[i];
	}
	DBG_INFO("write_cnt = %d, burst_cnt = %d\n", write_cnt, burst_cnt);

	int0 = (I2C_EN0_SCL_HOLD_TOO_LONG_INT | I2C_EN0_EMPTY_INT | I2C_EN0_DATA_NACK_INT
			| I2C_EN0_ADDRESS_NACK_INT | I2C_EN0_DONE_INT);

	if (burst_cnt)
		int0 |= I2C_EN0_EMPTY_THRESHOLD_INT;

	pstIrqEvent->eRWState = I2C_WRITE_STATE;
	pstIrqEvent->dBurstCount = burst_cnt;
	pstIrqEvent->dDataIndex = i;
	pstIrqEvent->dDataTotalLen = write_cnt;
	pstIrqEvent->pDataBuf = pstCmdInfo->pWrData;

	sp_i2cm_reset(sr);
	sp_i2cm_clock_freq_set(sr, pstCmdInfo->dFreq);
	sp_i2cm_slave_addr_set(sr, pstCmdInfo->dSlaveAddr);
	#ifdef I2C_RETEST
	if ((test_count > 1) && (test_count%3 == 0)) {
		DBG_INFO("test_count = %d", test_count);
		sp_i2cm_scl_delay_set(sr, 0x01);
	} else {
		sp_i2cm_scl_delay_set(sr, I2C_SCL_DELAY);
	}
	#endif
	sp_i2cm_trans_cnt_set(sr, write_cnt, 0);
	sp_i2cm_active_mode_set(sr, I2C_TRIGGER);
	sp_i2cm_rw_mode_set(sr, I2C_WRITE_MODE);
	sp_i2cm_data_set(sr, (unsigned int *)w_data);

	if (burst_cnt)
		sp_i2cm_int_en0_with_thershold_set(sr, int0, I2C_EMPTY_THRESHOLD_VALUE);
	else
		sp_i2cm_int_en0_set(sr, int0);

	sp_i2cm_manual_trigger(sr);	//start send data

	ret = wait_event_timeout(pstSpI2CInfo->wait,
	pstIrqEvent->stIrqFlag.bActiveDone, (I2C_SLEEP_TIMEOUT * HZ) / 500);
	if (ret == 0) {
		DBG_ERR("I2C write timeout !!\n");
		ret = I2C_ERR_TIMEOUT_OUT;
	} else {
		ret = pstIrqEvent->bRet;
	}
	sp_i2cm_reset(sr);
	pstIrqEvent->eRWState = I2C_IDLE_STATE;
	pstIrqEvent->bI2CBusy = 0;

	return ret;
}


int sp_i2cm_dma_write(struct I2C_Cmd_t_ *pstCmdInfo, struct SpI2C_If_t_ *pstSpI2CInfo)
{
	struct regs_i2cm_s *sr = (struct regs_i2cm_s *)pstSpI2CInfo->i2c_regs;
	struct regs_i2cm_dma_s *sr_dma = (struct regs_i2cm_dma_s *)pstSpI2CInfo->i2c_dma_regs;
	struct I2C_Irq_Event_t_ *pstIrqEvent = &(pstSpI2CInfo->stIrqEvent);
	struct Moon_RegBase_s *pstMoonRegBase = &stMoonRegBase;
	unsigned int int0 = 0;
	int ret = I2C_SUCCESS;
	unsigned int dma_int = 0;
	dma_addr_t dma_w_addr = 0;

	FUNC_DEBUG();


	if (pstCmdInfo->dDevId > I2C_MASTER_NUM)
		return I2C_ERR_INVALID_DEVID;

	if (pstSpI2CInfo->mode == I2C_POWER_ALL_SWITCH) {
		pstMoonRegBase->moon0_regs = (void __iomem *)MOON0_BASE;
		sp_i2cm_enable(0, pstMoonRegBase->moon0_regs);
	}

	if (pstIrqEvent->bI2CBusy) {
		DBG_ERR("I2C is busy !!\n");
		return I2C_ERR_I2C_BUSY;
	}

	memset(pstIrqEvent, 0, sizeof(*pstIrqEvent));
	pstIrqEvent->bI2CBusy = 1;

	if (pstCmdInfo->dWrDataCnt > 0xFFFF) {
		pstIrqEvent->bI2CBusy = 0;
		DBG_ERR("I2C write count is invalid !! write count=%d\n", pstCmdInfo->dWrDataCnt);
		return I2C_ERR_INVALID_CNT;
	}


	DBG_INFO(" DMA DataCnt = %d\n", pstCmdInfo->dWrDataCnt);

	pstIrqEvent->eRWState = I2C_DMA_WRITE_STATE;

	dma_w_addr = dma_map_single(pstSpI2CInfo->dev, pstCmdInfo->pWrData,
			pstCmdInfo->dWrDataCnt, DMA_TO_DEVICE);

	if (dma_mapping_error(pstSpI2CInfo->dev, dma_w_addr)) {
		DBG_ERR("I2C dma_w_addr fail\n");
		dma_w_addr = pstSpI2CInfo->dma_phy_base;
		memcpy(pstSpI2CInfo->dma_vir_base, pstCmdInfo->pWrData, pstCmdInfo->dWrDataCnt);
	}

	int0 = (I2C_EN0_SCL_HOLD_TOO_LONG_INT | I2C_EN0_EMPTY_INT
	 | I2C_EN0_DATA_NACK_INT | I2C_EN0_ADDRESS_NACK_INT | I2C_EN0_DONE_INT);


	dma_int = I2C_DMA_EN_DMA_DONE_INT;


	sp_i2cm_reset(sr);
	sp_i2cm_dma_mode_enable(sr);
	sp_i2cm_clock_freq_set(sr, pstCmdInfo->dFreq);
	sp_i2cm_slave_addr_set(sr, pstCmdInfo->dSlaveAddr);

	#ifdef I2C_RETEST
	if ((test_count > 1) && (test_count%3 == 0)) {
		DBG_INFO("test_count = %d", test_count);
		sp_i2cm_scl_delay_set(sr, 0x01);
	} else {
		sp_i2cm_scl_delay_set(sr, I2C_SCL_DELAY);
	}
	#endif
	sp_i2cm_active_mode_set(sr, I2C_AUTO);
	sp_i2cm_rw_mode_set(sr, I2C_WRITE_MODE);
	sp_i2cm_int_en0_set(sr, int0);

	sp_i2cm_dma_addr_set(sr_dma, (unsigned int)dma_w_addr);
	sp_i2cm_dma_length_set(sr_dma, pstCmdInfo->dWrDataCnt);
	sp_i2cm_dma_rw_mode_set(sr_dma, I2C_DMA_READ_MODE);
	sp_i2cm_dma_int_en_set(sr_dma, dma_int);
	sp_i2cm_dma_go_set(sr_dma);


	ret = wait_event_timeout(pstSpI2CInfo->wait,
				pstIrqEvent->stIrqDmaFlag.bDmaDone, (I2C_SLEEP_TIMEOUT * HZ) / 200);
	if (ret == 0) {
		DBG_ERR("I2C DMA write timeout !!\n");
		ret = I2C_ERR_TIMEOUT_OUT;
	} else {
		ret = pstIrqEvent->bRet;
	}
	sp_i2cm_status_clear(sr, 0xFFFFFFFF);

	if (dma_w_addr != pstSpI2CInfo->dma_phy_base)
		dma_unmap_single(pstSpI2CInfo->dev, dma_w_addr,
		pstCmdInfo->dWrDataCnt, DMA_TO_DEVICE);



	pstIrqEvent->eRWState = I2C_IDLE_STATE;
	pstIrqEvent->bI2CBusy = 0;

	sp_i2cm_reset(sr);

	return ret;
}

int sp_i2cm_dma_read(struct I2C_Cmd_t_ *pstCmdInfo, struct SpI2C_If_t_ *pstSpI2CInfo)
{
	struct regs_i2cm_s *sr = (struct regs_i2cm_s *)pstSpI2CInfo->i2c_regs;
	struct regs_i2cm_dma_s *sr_dma = (struct regs_i2cm_dma_s *)pstSpI2CInfo->i2c_dma_regs;
	struct I2C_Irq_Event_t_ *pstIrqEvent = &(pstSpI2CInfo->stIrqEvent);

	struct Moon_RegBase_s *pstMoonRegBase = &stMoonRegBase;
	unsigned char w_data[32] = {0};
	unsigned int read_cnt = 0;
	unsigned int write_cnt = 0;
	unsigned int int0 = 0, int1 = 0, int2 = 0;
	unsigned int dma_int = 0;
	int ret = I2C_SUCCESS;
	int i = 0;
	dma_addr_t dma_r_addr = 0;

	FUNC_DEBUG();

	if (pstCmdInfo->dDevId > I2C_MASTER_NUM)
		return I2C_ERR_INVALID_DEVID;

	if (pstSpI2CInfo->mode == I2C_POWER_ALL_SWITCH) {
		pstMoonRegBase->moon0_regs = (void __iomem *)MOON0_BASE;
		sp_i2cm_enable(0, pstMoonRegBase->moon0_regs);
	}


	if (pstIrqEvent->bI2CBusy) {
		DBG_ERR("I2C is busy !!\n");
		return I2C_ERR_I2C_BUSY;
	}

	memset(pstIrqEvent, 0, sizeof(*pstIrqEvent));
	pstIrqEvent->bI2CBusy = 1;

	write_cnt = pstCmdInfo->dWrDataCnt;
	read_cnt = pstCmdInfo->dRdDataCnt;

	if (pstCmdInfo->dRestartEn) {
		if (write_cnt > 32) {
			pstIrqEvent->bI2CBusy = 0;
			DBG_ERR("I2C write count is invalid !! write count=%d\n", write_cnt);
			return I2C_ERR_INVALID_CNT;
		}
	}

	if ((read_cnt > 0xFFFF)  || (read_cnt == 0)) {
		pstIrqEvent->bI2CBusy = 0;
		DBG_ERR("I2C read count is invalid !! read count=%d\n", read_cnt);
		return I2C_ERR_INVALID_CNT;
	}

	DBG_INFO("write_cnt = %d, DMA read_cnt = %d\n",
			write_cnt, read_cnt);

	dma_r_addr = dma_map_single(pstSpI2CInfo->dev, pstCmdInfo->pRdData,
		pstCmdInfo->dRdDataCnt, DMA_FROM_DEVICE);


	if (dma_mapping_error(pstSpI2CInfo->dev, dma_r_addr)) {
		DBG_ERR("I2C dma_r_addr fail\n");
		dma_r_addr = pstSpI2CInfo->dma_phy_base;
	}


	int0 = (I2C_EN0_SCL_HOLD_TOO_LONG_INT | I2C_EN0_EMPTY_INT | I2C_EN0_DATA_NACK_INT
		| I2C_EN0_ADDRESS_NACK_INT | I2C_EN0_DONE_INT);

	dma_int = I2C_DMA_EN_DMA_DONE_INT;

	pstIrqEvent->eRWState = I2C_DMA_READ_STATE;

	pstIrqEvent->dDataIndex = 0;
	pstIrqEvent->dRegDataIndex = 0;
	pstIrqEvent->dDataTotalLen = read_cnt;

	sp_i2cm_reset(sr);
	sp_i2cm_dma_mode_enable(sr);
	sp_i2cm_clock_freq_set(sr, pstCmdInfo->dFreq);
	sp_i2cm_slave_addr_set(sr, pstCmdInfo->dSlaveAddr);

	#ifdef I2C_RETEST
	if ((test_count > 1) && (test_count%3 == 0)) {
		DBG_INFO("test_count = %d", test_count);
		sp_i2cm_scl_delay_set(sr, 0x01);
	} else {
		sp_i2cm_scl_delay_set(sr, I2C_SCL_DELAY);
	}
	#endif

	if (pstCmdInfo->dRestartEn) {
		DBG_INFO("I2C_RESTART_MODE\n");
		sp_i2cm_active_mode_set(sr, I2C_TRIGGER);
		sp_i2cm_rw_mode_set(sr, I2C_RESTART_MODE);
		sp_i2cm_trans_cnt_set(sr, write_cnt, read_cnt);
		for (i = 0; i < write_cnt; i++)
			w_data[i] = pstCmdInfo->pWrData[i];

		sp_i2cm_data_set(sr, (unsigned int *)w_data);
	} else {
		DBG_INFO("I2C_READ_MODE\n");
		sp_i2cm_active_mode_set(sr, I2C_AUTO);
		sp_i2cm_rw_mode_set(sr, I2C_READ_MODE);
	}

	sp_i2cm_int_en0_set(sr, int0);
	sp_i2cm_int_en1_set(sr, int1);
	sp_i2cm_int_en2_set(sr, int2);

	sp_i2cm_dma_addr_set(sr_dma, (unsigned int)dma_r_addr);
	sp_i2cm_dma_length_set(sr_dma, pstCmdInfo->dRdDataCnt);
	sp_i2cm_dma_rw_mode_set(sr_dma, I2C_DMA_WRITE_MODE);
	sp_i2cm_dma_int_en_set(sr_dma, dma_int);
	sp_i2cm_dma_go_set(sr_dma);


	if (pstCmdInfo->dRestartEn)
		sp_i2cm_manual_trigger(sr); //start send data


	ret = wait_event_timeout(pstSpI2CInfo->wait,
				pstIrqEvent->stIrqDmaFlag.bDmaDone, (I2C_SLEEP_TIMEOUT * HZ) / 200);

	if (ret == 0) {
		DBG_ERR("I2C DMA read timeout !!\n");
		ret = I2C_ERR_TIMEOUT_OUT;
	} else {
		ret = pstIrqEvent->bRet;
	}
	sp_i2cm_status_clear(sr, 0xFFFFFFFF);

	//copy data from virtual addr to pstCmdInfo->pRdData

	if (dma_r_addr == pstSpI2CInfo->dma_phy_base)
		memcpy(pstCmdInfo->pRdData, pstSpI2CInfo->dma_vir_base, pstCmdInfo->dRdDataCnt);
	else
		dma_unmap_single(pstSpI2CInfo->dev,
					dma_r_addr, pstCmdInfo->dRdDataCnt, DMA_FROM_DEVICE);

	pstIrqEvent->eRWState = I2C_IDLE_STATE;
	pstIrqEvent->bI2CBusy = 0;


	sp_i2cm_reset(sr);
	return ret;
}



static int sp_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct SpI2C_If_t_ *pstSpI2CInfo = adap->algo_data;
	struct I2C_Cmd_t_ *pstCmdInfo = &(pstSpI2CInfo->stCmdInfo);
	int ret = I2C_SUCCESS;
	int i = 0;
	unsigned char restart_w_data[32] = {0};
	unsigned int  restart_write_cnt = 0;
	unsigned int  restart_en = 0;

	FUNC_DEBUG();

#ifdef CONFIG_PM_RUNTIME_I2C
	ret = pm_runtime_get_sync(&adap->dev);
	if (ret < 0)
		goto out;
#endif
	if (num == 0)
		return -EINVAL;

	memset(pstCmdInfo, 0, sizeof(*pstCmdInfo));
	pstCmdInfo->dDevId = adap->nr;

	if (pstCmdInfo->dFreq > I2C_FREQ)
		pstCmdInfo->dFreq = I2C_FREQ;
	else
		pstCmdInfo->dFreq = pstSpI2CInfo->i2c_clk_freq/1000;

	DBG_INFO("[I2C] set freq : %d\n", pstCmdInfo->dFreq);

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_TEN)
			return -EINVAL;

		pstCmdInfo->dSlaveAddr = msgs[i].addr;

		if (msgs[i].flags & I2C_M_NOSTART) {

			restart_write_cnt = msgs[i].len;
			for (i = 0; i < restart_write_cnt; i++)
				restart_w_data[i] = msgs[i].buf[i];

			restart_en = 1;
			continue;
		}

		if (msgs[i].flags & I2C_M_RD) {
			if (restart_en == 1) {
				pstCmdInfo->dWrDataCnt = restart_write_cnt;
				pstCmdInfo->pWrData = restart_w_data;
				DBG_INFO("I2C_M_RD dWrDataCnt =%d ", pstCmdInfo->dWrDataCnt);
				DBG_INFO("I2C_M_RD pWrData[0] =%x ", pstCmdInfo->pWrData[0]);
				DBG_INFO("I2C_M_RD pWrData[1] =%x ", pstCmdInfo->pWrData[1]);
				restart_en = 0;
				pstCmdInfo->dRestartEn = 1;
			}
			pstCmdInfo->dRdDataCnt = msgs[i].len;
			pstCmdInfo->pRdData = i2c_get_dma_safe_msg_buf(&msgs[i], 4);

			if ((pstCmdInfo->dRdDataCnt < 4) || (!pstCmdInfo->pRdData)) {
				pstCmdInfo->pRdData = msgs[i].buf;
				ret = sp_i2cm_read(pstCmdInfo, pstSpI2CInfo);
			} else {
				ret = sp_i2cm_dma_read(pstCmdInfo, pstSpI2CInfo);
				i2c_put_dma_safe_msg_buf(pstCmdInfo->pRdData, &msgs[i], true);
			}

		} else {
			pstCmdInfo->dWrDataCnt = msgs[i].len;
			pstCmdInfo->pWrData = i2c_get_dma_safe_msg_buf(&msgs[i], 4);
				if ((pstCmdInfo->dWrDataCnt < 4) || (!pstCmdInfo->pWrData)) {
					pstCmdInfo->pWrData = msgs[i].buf;
					ret = sp_i2cm_write(pstCmdInfo, pstSpI2CInfo);
				} else {
					ret = sp_i2cm_dma_write(pstCmdInfo, pstSpI2CInfo);
					i2c_put_dma_safe_msg_buf(pstCmdInfo->pWrData,
							&msgs[i], true);
				}
		}

		if (ret != I2C_SUCCESS)
			return -EIO;
	}

#ifdef CONFIG_PM_RUNTIME_I2C
	   pm_runtime_put(&adap->dev);

#endif

	return num;

#ifdef CONFIG_PM_RUNTIME_I2C
out:
				pm_runtime_mark_last_busy(&adap->dev);
				pm_runtime_put_autosuspend(&adap->dev);
				return num;
#endif

}

static u32 sp_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm sp_algorithm = {
	.master_xfer	= sp_master_xfer,
	.functionality	= sp_functionality,
};

static int sp_i2c_probe(struct platform_device *pdev)
{
	struct SpI2C_If_t_ *pstSpI2CInfo;
	struct i2c_adapter *p_adap;
	unsigned int i2c_clk_freq;
	int device_id = 0;
	int ret = I2C_SUCCESS;
	struct device *dev = &pdev->dev;
	const struct i2c_compatible *dev_mode;

	FUNC_DEBUG();

	if (pdev->dev.of_node) {
		pdev->id = of_alias_get_id(pdev->dev.of_node, "i2c");
		DBG_INFO("[I2C adapter] pdev->id=%d\n", pdev->id);
		device_id = pdev->id;
	}

	pstSpI2CInfo = devm_kzalloc(&pdev->dev, sizeof(*pstSpI2CInfo), GFP_KERNEL);
	if (!pstSpI2CInfo)
		return -ENOMEM;

	if (!of_property_read_u32(pdev->dev.of_node, "clock-frequency", &i2c_clk_freq)) {
		dev_dbg(&pdev->dev, "clk_freq %d\n", i2c_clk_freq);
		pstSpI2CInfo->i2c_clk_freq = i2c_clk_freq;
	} else
		pstSpI2CInfo->i2c_clk_freq = I2C_FREQ*1000;

		DBG_INFO("[I2C adapter] get freq : %d\n", pstSpI2CInfo->i2c_clk_freq);

		pstSpI2CInfo->dev = &pdev->dev;

	ret = _sp_i2cm_get_resources(pdev, pstSpI2CInfo);
	if (ret != I2C_SUCCESS) {
		DBG_ERR("[I2C adapter] get resources fail !\n");
		return ret;
	}

	pstSpI2CInfo->clk = devm_clk_get(dev, NULL);

	if (IS_ERR(pstSpI2CInfo->clk)) {
		ret = PTR_ERR(pstSpI2CInfo->clk);
		dev_err(dev, "failed to retrieve clk: %d\n", ret);
		goto err_clk_disable;
	}
	ret = clk_prepare_enable(pstSpI2CInfo->clk);

	if (ret) {
		dev_err(dev, "failed to enable clk: %d\n", ret);
		goto err_clk_disable;
	}

	pstSpI2CInfo->rstc = devm_reset_control_get(dev, NULL);

	if (IS_ERR(pstSpI2CInfo->rstc)) {
		ret = PTR_ERR(pstSpI2CInfo->rstc);
		dev_err(dev, "failed to retrieve reset controller: %d\n", ret);
		goto err_reset_assert;
	}
	ret = reset_control_deassert(pstSpI2CInfo->rstc);

	if (ret) {
		dev_err(dev, "failed to deassert reset line: %d\n", ret);
		goto err_reset_assert;
	}

	/* dma alloc*/
	pstSpI2CInfo->dma_vir_base = dma_alloc_coherent(&pdev->dev, I2C_BUFFER_SIZE,
					&pstSpI2CInfo->dma_phy_base, GFP_ATOMIC);
	if (!pstSpI2CInfo->dma_vir_base)
		goto free_dma;

	ret = _sp_i2cm_init(device_id, pstSpI2CInfo);
	if (ret != 0) {
		DBG_ERR("[I2C adapter] i2c master %d init error\n", device_id);
		return ret;
	}

	init_waitqueue_head(&pstSpI2CInfo->wait);

	dev_mode = of_device_get_match_data(&pdev->dev);
	pstSpI2CInfo->mode = dev_mode->mode;
	p_adap = &pstSpI2CInfo->adap;
	sprintf(p_adap->name, "%s%d", DEVICE_NAME, device_id);
	p_adap->algo = &sp_algorithm;
	p_adap->algo_data = pstSpI2CInfo;
	p_adap->nr = device_id;
	p_adap->class = 0;
	p_adap->retries = 5;
	p_adap->dev.parent = &pdev->dev;
	p_adap->dev.of_node = pdev->dev.of_node;

	ret = i2c_add_numbered_adapter(p_adap);
	if (ret < 0) {
		DBG_ERR("[I2C adapter] error add adapter %s\n", p_adap->name);
		goto free_dma;
	} else {
		DBG_INFO("[I2C adapter] add adapter %s success\n", p_adap->name);
		platform_set_drvdata(pdev, pstSpI2CInfo);
	}

	ret = request_irq(pstSpI2CInfo->irq, _sp_i2cm_irqevent_handler, IRQF_TRIGGER_HIGH,
				p_adap->name, pstSpI2CInfo);
	if (ret) {
		DBG_ERR("request irq fail !!\n");
		return I2C_ERR_REQUESET_IRQ;
	}


#ifdef CONFIG_PM_RUNTIME_I2C
	pm_runtime_set_autosuspend_delay(&pdev->dev, 5000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
#endif

	return ret;

free_dma:
	dma_free_coherent(&pdev->dev, I2C_BUFFER_SIZE,
			pstSpI2CInfo->dma_vir_base, pstSpI2CInfo->dma_phy_base);

err_reset_assert:
	reset_control_assert(pstSpI2CInfo->rstc);

err_clk_disable:
	clk_disable_unprepare(pstSpI2CInfo->clk);

	return ret;
}

static int sp_i2c_remove(struct platform_device *pdev)
{
	struct SpI2C_If_t_ *pstSpI2CInfo = platform_get_drvdata(pdev);
	struct i2c_adapter *p_adap = &pstSpI2CInfo->adap;

	FUNC_DEBUG();

#ifdef CONFIG_PM_RUNTIME_I2C
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
#endif

	dma_free_coherent(&pdev->dev, I2C_BUFFER_SIZE,
			pstSpI2CInfo->dma_vir_base, pstSpI2CInfo->dma_phy_base);

	i2c_del_adapter(p_adap);
	if (p_adap->nr < I2C_MASTER_NUM) {
		clk_disable_unprepare(pstSpI2CInfo->clk);
		reset_control_assert(pstSpI2CInfo->rstc);
		free_irq(pstSpI2CInfo->irq, NULL);
	}

	return 0;
}

static int sp_i2c_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct SpI2C_If_t_ *pstSpI2CInfo = platform_get_drvdata(pdev);
	struct i2c_adapter *p_adap = &pstSpI2CInfo->adap;

	FUNC_DEBUG();

	if (p_adap->nr < I2C_MASTER_NUM)
		reset_control_assert(pstSpI2CInfo->rstc);

	return 0;
}

static int sp_i2c_resume(struct platform_device *pdev)
{
	struct SpI2C_If_t_ *pstSpI2CInfo = platform_get_drvdata(pdev);
	struct i2c_adapter *p_adap = &pstSpI2CInfo->adap;

	FUNC_DEBUG();

	if (p_adap->nr < I2C_MASTER_NUM) {
		reset_control_deassert(pstSpI2CInfo->rstc);   //release reset
		clk_prepare_enable(pstSpI2CInfo->clk);        //enable clken and disable gclken
	}

	return 0;
}

static const struct i2c_compatible i2c_7021_compat = {
	.mode = I2C_POWER_ALL_SWITCH,
};

static const struct i2c_compatible i2c_645_compat = {
	.mode = I2C_POWER_NO_SWITCH,
};


static const struct of_device_id sp_i2c_of_match[] = {
	{	.compatible = "sunplus,sp7021-i2cm",
		.data = &i2c_7021_compat, },
	{	.compatible = "sunplus,q645-i2cm",
		.data = &i2c_645_compat, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sp_i2c_of_match);

#ifdef CONFIG_PM_RUNTIME_I2C
static int sp_i2c_runtime_suspend(struct device *dev)
{
	struct SpI2C_If_t_ *pstSpI2CInfo = dev_get_drvdata(dev);
	struct i2c_adapter *p_adap = &pstSpI2CInfo->adap;

	FUNC_DEBUG();

	if (p_adap->nr < I2C_MASTER_NUM)
		reset_control_assert(pstSpI2CInfo->rstc);

	return 0;
}

static int sp_i2c_runtime_resume(struct device *dev)
{
	struct SpI2C_If_t_ *pstSpI2CInfo = dev_get_drvdata(dev);
	struct i2c_adapter *p_adap = &pstSpI2CInfo->adap;

	FUNC_DEBUG();

	if (p_adap->nr < I2C_MASTER_NUM) {
		reset_control_deassert(pstSpI2CInfo->rstc);   //release reset
		clk_prepare_enable(pstSpI2CInfo->clk);        //enable clken and disable gclken
	}

	return 0;
}
static const struct dev_pm_ops sp7021_i2c_pm_ops = {
	.runtime_suspend = sp_i2c_runtime_suspend,
	.runtime_resume  = sp_i2c_runtime_resume,
};

#define sp_i2c_pm_ops  (&sp7021_i2c_pm_ops)
#endif

static struct platform_driver sp_i2c_driver = {
	.probe		= sp_i2c_probe,
	.remove		= sp_i2c_remove,
	.suspend	= sp_i2c_suspend,
	.resume		= sp_i2c_resume,
	.driver		= {
		.owner		= THIS_MODULE,
		.name		= DEVICE_NAME,
		.of_match_table = sp_i2c_of_match,
#ifdef CONFIG_PM_RUNTIME_I2C
		.pm     = sp_i2c_pm_ops,
#endif
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

MODULE_AUTHOR("lH Kuo <lh.kuo@sunplus.com>");
MODULE_DESCRIPTION("Sunplus I2c controller driver");
MODULE_LICENSE("GPL v2");
