/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved. */

#ifndef QCOM_BAM_DMA_H
#define QCOM_BAM_DMA_H

struct bam_desc_hw {
	__le32 addr;		/* Buffer physical address */
	__le16 size;		/* Buffer size in bytes */
	__le16 flags;
};

#define BAM_DMA_AUTOSUSPEND_DELAY 100

#define DESC_FLAG_INT BIT(15)
#define DESC_FLAG_EOT BIT(14)
#define DESC_FLAG_EOB BIT(13)
#define DESC_FLAG_NWD BIT(12)
#define DESC_FLAG_CMD BIT(11)

enum bam_reg {
	BAM_CTRL,
	BAM_REVISION,
	BAM_NUM_PIPES,
	BAM_DESC_CNT_TRSHLD,
	BAM_IRQ_SRCS,
	BAM_IRQ_SRCS_MSK,
	BAM_IRQ_SRCS_UNMASKED,
	BAM_IRQ_STTS,
	BAM_IRQ_CLR,
	BAM_IRQ_EN,
	BAM_CNFG_BITS,
	BAM_IRQ_SRCS_EE,
	BAM_IRQ_SRCS_MSK_EE,
	BAM_P_CTRL,
	BAM_P_RST,
	BAM_P_HALT,
	BAM_P_IRQ_STTS,
	BAM_P_IRQ_CLR,
	BAM_P_IRQ_EN,
	BAM_P_EVNT_DEST_ADDR,
	BAM_P_EVNT_REG,
	BAM_P_SW_OFSTS,
	BAM_P_DATA_FIFO_ADDR,
	BAM_P_DESC_FIFO_ADDR,
	BAM_P_EVNT_GEN_TRSHLD,
	BAM_P_FIFO_SIZES,
};

struct reg_offset_data {
	u32 base_offset;
	unsigned int pipe_mult, evnt_mult, ee_mult;
};

static const struct reg_offset_data bam_v1_3_reg_info[] = {
	[BAM_CTRL]		= { 0x0F80, 0x00, 0x00, 0x00 },
	[BAM_REVISION]		= { 0x0F84, 0x00, 0x00, 0x00 },
	[BAM_NUM_PIPES]		= { 0x0FBC, 0x00, 0x00, 0x00 },
	[BAM_DESC_CNT_TRSHLD]	= { 0x0F88, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS]		= { 0x0F8C, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_MSK]	= { 0x0F90, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_UNMASKED]	= { 0x0FB0, 0x00, 0x00, 0x00 },
	[BAM_IRQ_STTS]		= { 0x0F94, 0x00, 0x00, 0x00 },
	[BAM_IRQ_CLR]		= { 0x0F98, 0x00, 0x00, 0x00 },
	[BAM_IRQ_EN]		= { 0x0F9C, 0x00, 0x00, 0x00 },
	[BAM_CNFG_BITS]		= { 0x0FFC, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_EE]	= { 0x1800, 0x00, 0x00, 0x80 },
	[BAM_IRQ_SRCS_MSK_EE]	= { 0x1804, 0x00, 0x00, 0x80 },
	[BAM_P_CTRL]		= { 0x0000, 0x80, 0x00, 0x00 },
	[BAM_P_RST]		= { 0x0004, 0x80, 0x00, 0x00 },
	[BAM_P_HALT]		= { 0x0008, 0x80, 0x00, 0x00 },
	[BAM_P_IRQ_STTS]	= { 0x0010, 0x80, 0x00, 0x00 },
	[BAM_P_IRQ_CLR]		= { 0x0014, 0x80, 0x00, 0x00 },
	[BAM_P_IRQ_EN]		= { 0x0018, 0x80, 0x00, 0x00 },
	[BAM_P_EVNT_DEST_ADDR]	= { 0x102C, 0x00, 0x40, 0x00 },
	[BAM_P_EVNT_REG]	= { 0x1018, 0x00, 0x40, 0x00 },
	[BAM_P_SW_OFSTS]	= { 0x1000, 0x00, 0x40, 0x00 },
	[BAM_P_DATA_FIFO_ADDR]	= { 0x1024, 0x00, 0x40, 0x00 },
	[BAM_P_DESC_FIFO_ADDR]	= { 0x101C, 0x00, 0x40, 0x00 },
	[BAM_P_EVNT_GEN_TRSHLD]	= { 0x1028, 0x00, 0x40, 0x00 },
	[BAM_P_FIFO_SIZES]	= { 0x1020, 0x00, 0x40, 0x00 },
};

static const struct reg_offset_data bam_v1_4_reg_info[] = {
	[BAM_CTRL]		= { 0x0000, 0x00, 0x00, 0x00 },
	[BAM_REVISION]		= { 0x0004, 0x00, 0x00, 0x00 },
	[BAM_NUM_PIPES]		= { 0x003C, 0x00, 0x00, 0x00 },
	[BAM_DESC_CNT_TRSHLD]	= { 0x0008, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS]		= { 0x000C, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_MSK]	= { 0x0010, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_UNMASKED]	= { 0x0030, 0x00, 0x00, 0x00 },
	[BAM_IRQ_STTS]		= { 0x0014, 0x00, 0x00, 0x00 },
	[BAM_IRQ_CLR]		= { 0x0018, 0x00, 0x00, 0x00 },
	[BAM_IRQ_EN]		= { 0x001C, 0x00, 0x00, 0x00 },
	[BAM_CNFG_BITS]		= { 0x007C, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_EE]	= { 0x0800, 0x00, 0x00, 0x80 },
	[BAM_IRQ_SRCS_MSK_EE]	= { 0x0804, 0x00, 0x00, 0x80 },
	[BAM_P_CTRL]		= { 0x1000, 0x1000, 0x00, 0x00 },
	[BAM_P_RST]		= { 0x1004, 0x1000, 0x00, 0x00 },
	[BAM_P_HALT]		= { 0x1008, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_STTS]	= { 0x1010, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_CLR]		= { 0x1014, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_EN]		= { 0x1018, 0x1000, 0x00, 0x00 },
	[BAM_P_EVNT_DEST_ADDR]	= { 0x182C, 0x00, 0x1000, 0x00 },
	[BAM_P_EVNT_REG]	= { 0x1818, 0x00, 0x1000, 0x00 },
	[BAM_P_SW_OFSTS]	= { 0x1800, 0x00, 0x1000, 0x00 },
	[BAM_P_DATA_FIFO_ADDR]	= { 0x1824, 0x00, 0x1000, 0x00 },
	[BAM_P_DESC_FIFO_ADDR]	= { 0x181C, 0x00, 0x1000, 0x00 },
	[BAM_P_EVNT_GEN_TRSHLD]	= { 0x1828, 0x00, 0x1000, 0x00 },
	[BAM_P_FIFO_SIZES]	= { 0x1820, 0x00, 0x1000, 0x00 },
};

static const struct reg_offset_data bam_v1_7_reg_info[] = {
	[BAM_CTRL]		= { 0x00000, 0x00, 0x00, 0x00 },
	[BAM_REVISION]		= { 0x01000, 0x00, 0x00, 0x00 },
	[BAM_NUM_PIPES]		= { 0x01008, 0x00, 0x00, 0x00 },
	[BAM_DESC_CNT_TRSHLD]	= { 0x00008, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS]		= { 0x03010, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_MSK]	= { 0x03014, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_UNMASKED]	= { 0x03018, 0x00, 0x00, 0x00 },
	[BAM_IRQ_STTS]		= { 0x00014, 0x00, 0x00, 0x00 },
	[BAM_IRQ_CLR]		= { 0x00018, 0x00, 0x00, 0x00 },
	[BAM_IRQ_EN]		= { 0x0001C, 0x00, 0x00, 0x00 },
	[BAM_CNFG_BITS]		= { 0x0007C, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_EE]	= { 0x03000, 0x00, 0x00, 0x1000 },
	[BAM_IRQ_SRCS_MSK_EE]	= { 0x03004, 0x00, 0x00, 0x1000 },
	[BAM_P_CTRL]		= { 0x13000, 0x1000, 0x00, 0x00 },
	[BAM_P_RST]		= { 0x13004, 0x1000, 0x00, 0x00 },
	[BAM_P_HALT]		= { 0x13008, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_STTS]	= { 0x13010, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_CLR]		= { 0x13014, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_EN]		= { 0x13018, 0x1000, 0x00, 0x00 },
	[BAM_P_EVNT_DEST_ADDR]	= { 0x1382C, 0x00, 0x1000, 0x00 },
	[BAM_P_EVNT_REG]	= { 0x13818, 0x00, 0x1000, 0x00 },
	[BAM_P_SW_OFSTS]	= { 0x13800, 0x00, 0x1000, 0x00 },
	[BAM_P_DATA_FIFO_ADDR]	= { 0x13824, 0x00, 0x1000, 0x00 },
	[BAM_P_DESC_FIFO_ADDR]	= { 0x1381C, 0x00, 0x1000, 0x00 },
	[BAM_P_EVNT_GEN_TRSHLD]	= { 0x13828, 0x00, 0x1000, 0x00 },
	[BAM_P_FIFO_SIZES]	= { 0x13820, 0x00, 0x1000, 0x00 },
};

/* BAM CTRL */
#define BAM_SW_RST			BIT(0)
#define BAM_EN				BIT(1)
#define BAM_EN_ACCUM			BIT(4)
#define BAM_TESTBUS_SEL_SHIFT		5
#define BAM_TESTBUS_SEL_MASK		0x3F
#define BAM_DESC_CACHE_SEL_SHIFT	13
#define BAM_DESC_CACHE_SEL_MASK		0x3
#define BAM_CACHED_DESC_STORE		BIT(15)
#define IBC_DISABLE			BIT(16)

/* BAM REVISION */
#define REVISION_SHIFT		0
#define REVISION_MASK		0xFF
#define NUM_EES_SHIFT		8
#define NUM_EES_MASK		0xF
#define CE_BUFFER_SIZE		BIT(13)
#define AXI_ACTIVE		BIT(14)
#define USE_VMIDMT		BIT(15)
#define SECURED			BIT(16)
#define BAM_HAS_NO_BYPASS	BIT(17)
#define HIGH_FREQUENCY_BAM	BIT(18)
#define INACTIV_TMRS_EXST	BIT(19)
#define NUM_INACTIV_TMRS	BIT(20)
#define DESC_CACHE_DEPTH_SHIFT	21
#define DESC_CACHE_DEPTH_1	(0 << DESC_CACHE_DEPTH_SHIFT)
#define DESC_CACHE_DEPTH_2	(1 << DESC_CACHE_DEPTH_SHIFT)
#define DESC_CACHE_DEPTH_3	(2 << DESC_CACHE_DEPTH_SHIFT)
#define DESC_CACHE_DEPTH_4	(3 << DESC_CACHE_DEPTH_SHIFT)
#define CMD_DESC_EN		BIT(23)
#define INACTIV_TMR_BASE_SHIFT	24
#define INACTIV_TMR_BASE_MASK	0xFF

/* BAM NUM PIPES */
#define BAM_NUM_PIPES_SHIFT		0
#define BAM_NUM_PIPES_MASK		0xFF
#define PERIPH_NON_PIPE_GRP_SHIFT	16
#define PERIPH_NON_PIP_GRP_MASK		0xFF
#define BAM_NON_PIPE_GRP_SHIFT		24
#define BAM_NON_PIPE_GRP_MASK		0xFF

/* BAM CNFG BITS */
#define BAM_PIPE_CNFG		BIT(2)
#define BAM_FULL_PIPE		BIT(11)
#define BAM_NO_EXT_P_RST	BIT(12)
#define BAM_IBC_DISABLE		BIT(13)
#define BAM_SB_CLK_REQ		BIT(14)
#define BAM_PSM_CSW_REQ		BIT(15)
#define BAM_PSM_P_RES		BIT(16)
#define BAM_AU_P_RES		BIT(17)
#define BAM_SI_P_RES		BIT(18)
#define BAM_WB_P_RES		BIT(19)
#define BAM_WB_BLK_CSW		BIT(20)
#define BAM_WB_CSW_ACK_IDL	BIT(21)
#define BAM_WB_RETR_SVPNT	BIT(22)
#define BAM_WB_DSC_AVL_P_RST	BIT(23)
#define BAM_REG_P_EN		BIT(24)
#define BAM_PSM_P_HD_DATA	BIT(25)
#define BAM_AU_ACCUMED		BIT(26)
#define BAM_CMD_ENABLE		BIT(27)

#define BAM_CNFG_BITS_DEFAULT	(BAM_PIPE_CNFG |	\
				 BAM_NO_EXT_P_RST |	\
				 BAM_IBC_DISABLE |	\
				 BAM_SB_CLK_REQ |	\
				 BAM_PSM_CSW_REQ |	\
				 BAM_PSM_P_RES |	\
				 BAM_AU_P_RES |		\
				 BAM_SI_P_RES |		\
				 BAM_WB_P_RES |		\
				 BAM_WB_BLK_CSW |	\
				 BAM_WB_CSW_ACK_IDL |	\
				 BAM_WB_RETR_SVPNT |	\
				 BAM_WB_DSC_AVL_P_RST |	\
				 BAM_REG_P_EN |		\
				 BAM_PSM_P_HD_DATA |	\
				 BAM_AU_ACCUMED |	\
				 BAM_CMD_ENABLE)

/* PIPE CTRL */
#define P_EN			BIT(1)
#define P_DIRECTION		BIT(3)
#define P_SYS_STRM		BIT(4)
#define P_SYS_MODE		BIT(5)
#define P_AUTO_EOB		BIT(6)
#define P_AUTO_EOB_SEL_SHIFT	7
#define P_AUTO_EOB_SEL_512	(0 << P_AUTO_EOB_SEL_SHIFT)
#define P_AUTO_EOB_SEL_256	(1 << P_AUTO_EOB_SEL_SHIFT)
#define P_AUTO_EOB_SEL_128	(2 << P_AUTO_EOB_SEL_SHIFT)
#define P_AUTO_EOB_SEL_64	(3 << P_AUTO_EOB_SEL_SHIFT)
#define P_PREFETCH_LIMIT_SHIFT	9
#define P_PREFETCH_LIMIT_32	(0 << P_PREFETCH_LIMIT_SHIFT)
#define P_PREFETCH_LIMIT_16	(1 << P_PREFETCH_LIMIT_SHIFT)
#define P_PREFETCH_LIMIT_4	(2 << P_PREFETCH_LIMIT_SHIFT)
#define P_WRITE_NWD		BIT(11)
#define P_LOCK_GROUP_SHIFT	16
#define P_LOCK_GROUP_MASK	0x1F

/* BAM_DESC_CNT_TRSHLD */
#define CNT_TRSHLD		0xffff
#define DEFAULT_CNT_THRSHLD	0x4

/* BAM_IRQ_SRCS */
#define BAM_IRQ			BIT(31)
#define P_IRQ			0x7fffffff

/* BAM_IRQ_SRCS_MSK */
#define BAM_IRQ_MSK		BAM_IRQ
#define P_IRQ_MSK		P_IRQ

/* BAM_IRQ_STTS */
#define BAM_TIMER_IRQ		BIT(4)
#define BAM_EMPTY_IRQ		BIT(3)
#define BAM_ERROR_IRQ		BIT(2)
#define BAM_HRESP_ERR_IRQ	BIT(1)

/* BAM_IRQ_CLR */
#define BAM_TIMER_CLR		BIT(4)
#define BAM_EMPTY_CLR		BIT(3)
#define BAM_ERROR_CLR		BIT(2)
#define BAM_HRESP_ERR_CLR	BIT(1)

/* BAM_IRQ_EN */
#define BAM_TIMER_EN		BIT(4)
#define BAM_EMPTY_EN		BIT(3)
#define BAM_ERROR_EN		BIT(2)
#define BAM_HRESP_ERR_EN	BIT(1)

/* BAM_P_IRQ_EN */
#define P_PRCSD_DESC_EN		BIT(0)
#define P_TIMER_EN		BIT(1)
#define P_WAKE_EN		BIT(2)
#define P_OUT_OF_DESC_EN	BIT(3)
#define P_ERR_EN		BIT(4)
#define P_TRNSFR_END_EN		BIT(5)
#define P_DEFAULT_IRQS_EN	(P_PRCSD_DESC_EN | P_ERR_EN | P_TRNSFR_END_EN)

/* BAM_P_SW_OFSTS */
#define P_SW_OFSTS_MASK		0xffff

#define BAM_DESC_FIFO_SIZE	SZ_32K
#define MAX_DESCRIPTORS (BAM_DESC_FIFO_SIZE / sizeof(struct bam_desc_hw) - 1)
#define BAM_FIFO_SIZE	(SZ_32K - 8)
#define IS_BUSY(chan)	(CIRC_SPACE(bchan->tail, bchan->head,\
			 MAX_DESCRIPTORS + 1) == 0)

bool bam_is_probed(void);

#endif
