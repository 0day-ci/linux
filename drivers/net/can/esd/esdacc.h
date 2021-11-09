/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2015 - 2016 Thomas Körper, esd electronic system design gmbh
 * Copyright (C) 2017 - 2021 Stefan Mätje, esd electronics gmbh
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/can/dev.h>

#define ACC_CAN_EFF_FLAG			0x20000000
#define ACC_CAN_RTR_FLAG			0x10
#define ACC_CAN_DLC_MASK			0x0f

#define ACC_OV_OF_PROBE				0x0000
#define ACC_OV_OF_VERSION			0x0004
#define ACC_OV_OF_INFO				0x0008
#define ACC_OV_OF_CANCORE_FREQ			0x000c
#define ACC_OV_OF_TS_FREQ_LO			0x0010
#define ACC_OV_OF_TS_FREQ_HI			0x0014
#define ACC_OV_OF_IRQ_STATUS_CORES		0x0018
#define ACC_OV_OF_TS_CURR_LO			0x001c
#define ACC_OV_OF_TS_CURR_HI			0x0020
#define ACC_OV_OF_IRQ_STATUS			0x0028
#define ACC_OV_OF_MODE				0x002c
#define ACC_OV_OF_BM_IRQ_COUNTER		0x0070
#define ACC_OV_OF_BM_IRQ_MASK			0x0074
#define ACC_OV_OF_MSI_DATA			0x0080
#define ACC_OV_OF_MSI_ADDRESSOFFSET		0x0084

/* Feature flags are contained in the upper 16 bit of the version
 * register at ACC_OV_OF_VERSION but only used with these masks after
 * extraction into an extra variable => (xx - 16).
 */
#define ACC_OV_REG_FEAT_IDX_CANFD		(27 - 16)
#define ACC_OV_REG_FEAT_IDX_NEW_PSC		(28 - 16)
#define ACC_OV_REG_FEAT_MASK_CANFD		BIT(ACC_OV_REG_FEAT_IDX_CANFD)
#define ACC_OV_REG_FEAT_MASK_NEW_PSC		BIT(ACC_OV_REG_FEAT_IDX_NEW_PSC)

#define ACC_OV_REG_MODE_MASK_ENDIAN_LITTLE	0x00000001
#define ACC_OV_REG_MODE_MASK_BM_ENABLE		0x00000002
#define ACC_OV_REG_MODE_MASK_MODE_LED		0x00000004
#define ACC_OV_REG_MODE_MASK_TIMER		0x00000070
#define ACC_OV_REG_MODE_MASK_TIMER_ENABLE	0x00000010
#define ACC_OV_REG_MODE_MASK_TIMER_ONE_SHOT	0x00000020
#define ACC_OV_REG_MODE_MASK_TIMER_ABSOLUTE	0x00000040
#define ACC_OV_REG_MODE_MASK_TS_SRC		0x00000180
#define ACC_OV_REG_MODE_MASK_I2C_ENABLE		0x00000800
#define ACC_OV_REG_MODE_MASK_MSI_ENABLE		0x00004000
#define ACC_OV_REG_MODE_MASK_NEW_PSC_ENABLE	0x00008000
#define ACC_OV_REG_MODE_MASK_FPGA_RESET		0x80000000

#define ACC_CORE_OF_CTRL_MODE			0x0000
#define ACC_CORE_OF_STATUS_IRQ			0x0008
#define ACC_CORE_OF_BRP				0x000c
#define ACC_CORE_OF_BTR				0x0010
#define ACC_CORE_OF_FBTR			0x0014
#define ACC_CORE_OF_STATUS			0x0030
#define ACC_CORE_OF_TXFIFO_CONFIG		0x0048
#define ACC_CORE_OF_TXFIFO_STATUS		0x004c
#define ACC_CORE_OF_TX_STATUS_IRQ		0x0050
#define ACC_CORE_OF_TX_ABORT_MASK		0x0054
#define ACC_CORE_OF_BM_IRQ_COUNTER		0x0070
#define ACC_CORE_OF_TXFIFO_ID			0x00c0
#define ACC_CORE_OF_TXFIFO_DLC			0x00c4
#define ACC_CORE_OF_TXFIFO_DATA_0		0x00c8
#define ACC_CORE_OF_TXFIFO_DATA_1		0x00cc

#define ACC_REG_CONTROL_IDX_MODE_RESETMODE	0
#define ACC_REG_CONTROL_IDX_MODE_LOM		1
#define ACC_REG_CONTROL_IDX_MODE_STM		2
#define ACC_REG_CONTROL_IDX_MODE_TRANSEN	5
#define ACC_REG_CONTROL_IDX_MODE_TS		6
#define ACC_REG_CONTROL_IDX_MODE_SCHEDULE	7
#define ACC_REG_CONTROL_MASK_MODE_RESETMODE	\
				BIT(ACC_REG_CONTROL_IDX_MODE_RESETMODE)
#define ACC_REG_CONTROL_MASK_MODE_LOM		\
				BIT(ACC_REG_CONTROL_IDX_MODE_LOM)
#define ACC_REG_CONTROL_MASK_MODE_STM		\
				BIT(ACC_REG_CONTROL_IDX_MODE_STM)
#define ACC_REG_CONTROL_MASK_MODE_TRANSEN	\
				BIT(ACC_REG_CONTROL_IDX_MODE_TRANSEN)
#define ACC_REG_CONTROL_MASK_MODE_TS		\
				BIT(ACC_REG_CONTROL_IDX_MODE_TS)
#define ACC_REG_CONTROL_MASK_MODE_SCHEDULE	\
				BIT(ACC_REG_CONTROL_IDX_MODE_SCHEDULE)

#define ACC_REG_CONTROL_IDX_IE_RXTX	8
#define ACC_REG_CONTROL_IDX_IE_TXERROR	9
#define ACC_REG_CONTROL_IDX_IE_ERRWARN	10
#define ACC_REG_CONTROL_IDX_IE_OVERRUN	11
#define ACC_REG_CONTROL_IDX_IE_TSI	12
#define ACC_REG_CONTROL_IDX_IE_ERRPASS	13
#define ACC_REG_CONTROL_IDX_IE_BUSERR	15
#define ACC_REG_CONTROL_MASK_IE_RXTX	BIT(ACC_REG_CONTROL_IDX_IE_RXTX)
#define ACC_REG_CONTROL_MASK_IE_TXERROR BIT(ACC_REG_CONTROL_IDX_IE_TXERROR)
#define ACC_REG_CONTROL_MASK_IE_ERRWARN BIT(ACC_REG_CONTROL_IDX_IE_ERRWARN)
#define ACC_REG_CONTROL_MASK_IE_OVERRUN BIT(ACC_REG_CONTROL_IDX_IE_OVERRUN)
#define ACC_REG_CONTROL_MASK_IE_TSI	BIT(ACC_REG_CONTROL_IDX_IE_TSI)
#define ACC_REG_CONTROL_MASK_IE_ERRPASS BIT(ACC_REG_CONTROL_IDX_IE_ERRPASS)
#define ACC_REG_CONTROL_MASK_IE_BUSERR	BIT(ACC_REG_CONTROL_IDX_IE_BUSERR)

/* 256 BM_MSGs of 32 byte size */
#define ACC_CORE_DMAMSG_SIZE		32U
#define ACC_CORE_DMABUF_SIZE		(256U * ACC_CORE_DMAMSG_SIZE)

enum acc_bmmsg_id {
	BM_MSG_ID_RXTXDONE = 0x01,
	BM_MSG_ID_TXABORT = 0x02,
	BM_MSG_ID_OVERRUN = 0x03,
	BM_MSG_ID_BUSERR = 0x04,
	BM_MSG_ID_ERRPASSIVE = 0x05,
	BM_MSG_ID_ERRWARN = 0x06,
	BM_MSG_ID_TIMESLICE = 0x07,
	BM_MSG_ID_HWTIMER = 0x08,
	BM_MSG_ID_HOTPLUG = 0x09,
};

/* The struct acc_bmmsg* structure declarations that follow here provide
 * access to the ring buffer of bus master messages maintained by the FPGA
 * bus master engine. All bus master messages have the same size of
 * ACC_CORE_DMAMSG_SIZE and a minimum alignment of ACC_CORE_DMAMSG_SIZE in
 * memory.
 *
 * All structure members are natural aligned. Therefore we should not need
 * a __packed attribute. All struct acc_bmmsg* declarations have at least
 * reserved* members to fill the structure to the full ACC_CORE_DMAMSG_SIZE.
 *
 * A failure of this property due padding will be detected at compile time
 * by BUILD_BUG_ON(sizeof(struct acc_bmmsg) != ACC_CORE_DMAMSG_SIZE)
 */

struct acc_bmmsg_rxtxdone {
	u8 msg_id;
	u8 txfifo_level;
	u8 reserved1[2];
	u8 txtsfifo_level;
	u8 reserved2[3];
	u32 id;
	union {
		struct {
			u8 len;
			u8 reserved0;
			u8 bits;
			u8 state;
		} rxtx;
		struct {
			u8 len;
			u8 msg_lost;
			u8 bits;
			u8 state;
		} rx;
		struct {
			u8 len;
			u8 txfifo_idx;
			u8 bits;
			u8 state;
		} tx;
	} dlc;
	u8 data[8];
	/* Time stamps in struct acc_ov::timestamp_frequency ticks. */
	u64 ts;
};

struct acc_bmmsg_txabort {
	u8 msg_id;
	u8 txfifo_level;
	u16 abort_mask;
	u8 txtsfifo_level;
	u8 reserved2[1];
	u16 abort_mask_txts;
	u64 ts;
	u32 reserved3[4];
};

struct acc_bmmsg_overrun {
	u8 msg_id;
	u8 txfifo_level;
	u8 lost_cnt;
	u8 reserved1;
	u8 txtsfifo_level;
	u8 reserved2[3];
	u64 ts;
	u32 reserved3[4];
};

struct acc_bmmsg_buserr {
	u8 msg_id;
	u8 txfifo_level;
	u8 ecc;
	u8 reserved1;
	u8 txtsfifo_level;
	u8 reserved2[3];
	u64 ts;
	u32 reg_status;
	u32 reg_btr;
	u32 reserved3[2];
};

struct acc_bmmsg_errstatechange {
	u8 msg_id;
	u8 txfifo_level;
	u8 reserved1[2];
	u8 txtsfifo_level;
	u8 reserved2[3];
	u64 ts;
	u32 reg_status;
	u32 reserved3[3];
};

struct acc_bmmsg_timeslice {
	u8 msg_id;
	u8 txfifo_level;
	u8 reserved1[2];
	u8 txtsfifo_level;
	u8 reserved2[3];
	u64 ts;
	u32 reserved3[4];
};

struct acc_bmmsg_hwtimer {
	u8 msg_id;
	u8 reserved1[3];
	u32 reserved2[1];
	u64 timer;
	u32 reserved3[4];
};

struct acc_bmmsg_hotplug {
	u8 msg_id;
	u8 reserved1[3];
	u32 reserved2[7];
};

struct acc_bmmsg {
	union {
		u8 msg_id;
		struct acc_bmmsg_rxtxdone rxtxdone;
		struct acc_bmmsg_txabort txabort;
		struct acc_bmmsg_overrun overrun;
		struct acc_bmmsg_buserr buserr;
		struct acc_bmmsg_errstatechange errstatechange;
		struct acc_bmmsg_timeslice timeslice;
		struct acc_bmmsg_hwtimer hwtimer;
	} u;
};

/* Regarding Documentation/process/volatile-considered-harmful.rst the
 * forth exception applies to the "irq_cnt" member of the structure
 * below. The u32 variable "irq_cnt" points to is updated by the ESDACC
 * FPGA via DMA.
 */
struct acc_bmfifo {
	const struct acc_bmmsg *messages;
	/* Bits 0..7: bm_fifo head index */
	volatile const u32 *irq_cnt;
	u32 local_irq_cnt;
	u32 msg_fifo_tail;
};

struct acc_core {
	void __iomem *addr;
	struct net_device *netdev;
	struct acc_bmfifo bmfifo;
	u8 tx_fifo_size;
	u8 tx_fifo_head;
	u8 tx_fifo_tail;
};

struct acc_ov {
	void __iomem *addr;
	struct acc_bmfifo bmfifo;
	u32 timestamp_frequency;
	u32 ts2ns_numerator;
	u32 ts2ns_denominator;
	u32 core_frequency;
	u16 version;
	u16 features;
	u8 total_cores;
	u8 active_cores;
};

struct acc_net_priv {
	struct can_priv can; /* must be the first member! */
	struct acc_core *core;
	struct acc_ov *ov;
};

static inline u32 acc_read32(struct acc_core *core, unsigned short offs)
{
	return ioread32be(core->addr + offs);
}

static inline void acc_write32(struct acc_core *core,
			       unsigned short offs, u32 v)
{
	iowrite32be(v, core->addr + offs);
}

static inline void acc_write32_noswap(struct acc_core *core,
				      unsigned short offs, u32 v)
{
	iowrite32(v, core->addr + offs);
}

static inline void acc_set_bits(struct acc_core *core,
				unsigned short offs, u32 mask)
{
	u32 v = acc_read32(core, offs);

	v |= mask;
	acc_write32(core, offs, v);
}

static inline void acc_clear_bits(struct acc_core *core,
				  unsigned short offs, u32 mask)
{
	u32 v = acc_read32(core, offs);

	v &= ~mask;
	acc_write32(core, offs, v);
}

static inline int acc_resetmode_entered(struct acc_core *core)
{
	u32 ctrl = acc_read32(core, ACC_CORE_OF_CTRL_MODE);

	return (ctrl & ACC_REG_CONTROL_MASK_MODE_RESETMODE) != 0;
}

static inline u32 acc_ov_read32(struct acc_ov *ov, unsigned short offs)
{
	return ioread32be(ov->addr + offs);
}

static inline void acc_ov_write32(struct acc_ov *ov,
				  unsigned short offs, u32 v)
{
	iowrite32be(v, ov->addr + offs);
}

static inline void acc_ov_set_bits(struct acc_ov *ov,
				   unsigned short offs, u32 b)
{
	u32 v = acc_ov_read32(ov, offs);

	v |= b;
	acc_ov_write32(ov, offs, v);
}

static inline void acc_ov_clear_bits(struct acc_ov *ov,
				     unsigned short offs, u32 b)
{
	u32 v = acc_ov_read32(ov, offs);

	v &= ~b;
	acc_ov_write32(ov, offs, v);
}

static inline void acc_reset_fpga(struct acc_ov *ov)
{
	acc_ov_write32(ov, ACC_OV_OF_MODE, ACC_OV_REG_MODE_MASK_FPGA_RESET);

	/* Also reset I^2C, to re-detect card addons at every driver start: */
	acc_ov_clear_bits(ov, ACC_OV_OF_MODE, ACC_OV_REG_MODE_MASK_I2C_ENABLE);
	mdelay(2);
	acc_ov_set_bits(ov, ACC_OV_OF_MODE, ACC_OV_REG_MODE_MASK_I2C_ENABLE);
	mdelay(10);
}

void acc_init_ov(struct acc_ov *ov, struct device *dev);
void acc_init_bm_ptr(struct acc_ov *ov, struct acc_core *cores,
		     const void *mem);
int acc_open(struct net_device *netdev);
int acc_close(struct net_device *netdev);
netdev_tx_t acc_start_xmit(struct sk_buff *skb, struct net_device *netdev);
int acc_get_berr_counter(const struct net_device *netdev,
			 struct can_berr_counter *bec);
int acc_set_mode(struct net_device *netdev, enum can_mode mode);
int acc_set_bittiming(struct net_device *netdev);
irqreturn_t acc_card_interrupt(struct acc_ov *ov, struct acc_core *cores);
