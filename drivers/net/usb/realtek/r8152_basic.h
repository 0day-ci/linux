/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (c) 2021 Realtek Semiconductor Corp. All rights reserved.
 */

#ifndef LINUX_R8152_BASIC_H
#define LINUX_R8152_BASIC_H

#define PLA_IDR			0xc000
#define PLA_RCR			0xc010
#define PLA_RCR1		0xc012
#define PLA_RMS			0xc016
#define PLA_RXFIFO_CTRL0	0xc0a0
#define PLA_RXFIFO_FULL		0xc0a2
#define PLA_RXFIFO_CTRL1	0xc0a4
#define PLA_RX_FIFO_FULL	0xc0a6
#define PLA_RXFIFO_CTRL2	0xc0a8
#define PLA_RX_FIFO_EMPTY	0xc0aa
#define PLA_DMY_REG0		0xc0b0
#define PLA_FMC			0xc0b4
#define PLA_CFG_WOL		0xc0b6
#define PLA_TEREDO_CFG		0xc0bc
#define PLA_TEREDO_WAKE_BASE	0xc0c4
#define PLA_MAR			0xcd00
#define PLA_BACKUP		0xd000
#define PLA_BDC_CR		0xd1a0
#define PLA_TEREDO_TIMER	0xd2cc
#define PLA_REALWOW_TIMER	0xd2e8
#define PLA_UPHY_TIMER		0xd388
#define PLA_SUSPEND_FLAG	0xd38a
#define PLA_INDICATE_FALG	0xd38c
#define PLA_MACDBG_PRE		0xd38c	/* RTL_VER_04 only */
#define PLA_MACDBG_POST		0xd38e	/* RTL_VER_04 only */
#define PLA_EXTRA_STATUS	0xd398
#define PLA_GPHY_CTRL		0xd3ae
#define PLA_POL_GPIO_CTRL	0xdc6a
#define PLA_EFUSE_DATA		0xdd00
#define PLA_EFUSE_CMD		0xdd02
#define PLA_LEDSEL		0xdd90
#define PLA_LED_FEATURE		0xdd92
#define PLA_PHYAR		0xde00
#define PLA_BOOT_CTRL		0xe004
#define PLA_LWAKE_CTRL_REG	0xe007
#define PLA_GPHY_INTR_IMR	0xe022
#define PLA_EEE_CR		0xe040
#define PLA_EEE_TXTWSYS		0xe04c
#define PLA_EEE_TXTWSYS_2P5G	0xe058
#define PLA_EEEP_CR		0xe080
#define PLA_MAC_PWR_CTRL	0xe0c0
#define PLA_MAC_PWR_CTRL2	0xe0ca
#define PLA_MAC_PWR_CTRL3	0xe0cc
#define PLA_MAC_PWR_CTRL4	0xe0ce
#define PLA_WDT6_CTRL		0xe428
#define PLA_TCR0		0xe610
#define PLA_TCR1		0xe612
#define PLA_MTPS		0xe615
#define PLA_TXFIFO_CTRL		0xe618
#define PLA_TXFIFO_FULL		0xe61a
#define PLA_RSTTALLY		0xe800
#define PLA_CR			0xe813
#define PLA_CRWECR		0xe81c
#define PLA_CONFIG12		0xe81e	/* CONFIG1, CONFIG2 */
#define PLA_CONFIG34		0xe820	/* CONFIG3, CONFIG4 */
#define PLA_CONFIG5		0xe822
#define PLA_PHY_PWR		0xe84c
#define PLA_OOB_CTRL		0xe84f
#define PLA_CPCR		0xe854
#define PLA_MISC_0		0xe858
#define PLA_MISC_1		0xe85a
#define PLA_OCP_GPHY_BASE	0xe86c
#define PLA_TALLYCNT		0xe890
#define PLA_SFF_STS_7		0xe8de
#define PLA_PHYSTATUS		0xe908
#define PLA_CONFIG6		0xe90a /* CONFIG6 */
#define PLA_USB_CFG		0xe952
#define PLA_BP_BA		0xfc26
#define PLA_BP_0		0xfc28
#define PLA_BP_1		0xfc2a
#define PLA_BP_2		0xfc2c
#define PLA_BP_3		0xfc2e
#define PLA_BP_4		0xfc30
#define PLA_BP_5		0xfc32
#define PLA_BP_6		0xfc34
#define PLA_BP_7		0xfc36
#define PLA_BP_EN		0xfc38

#define USB_USB2PHY		0xb41e
#define USB_SSPHYLINK1		0xb426
#define USB_SSPHYLINK2		0xb428
#define USB_L1_CTRL		0xb45e
#define USB_U2P3_CTRL		0xb460
#define USB_CSR_DUMMY1		0xb464
#define USB_CSR_DUMMY2		0xb466
#define USB_DEV_STAT		0xb808
#define USB_CONNECT_TIMER	0xcbf8
#define USB_MSC_TIMER		0xcbfc
#define USB_BURST_SIZE		0xcfc0
#define USB_FW_FIX_EN0		0xcfca
#define USB_FW_FIX_EN1		0xcfcc
#define USB_LPM_CONFIG		0xcfd8
#define USB_EFUSE		0xcfdb
#define USB_ECM_OPTION		0xcfee
#define USB_CSTMR		0xcfef	/* RTL8153A */
#define USB_MISC_2		0xcfff
#define USB_ECM_OP		0xd26b
#define USB_GPHY_CTRL		0xd284
#define USB_SPEED_OPTION	0xd32a
#define USB_FW_CTRL		0xd334	/* RTL8153B */
#define USB_FC_TIMER		0xd340
#define USB_USB_CTRL		0xd406
#define USB_PHY_CTRL		0xd408
#define USB_TX_AGG		0xd40a
#define USB_RX_BUF_TH		0xd40c
#define USB_USB_TIMER		0xd428
#define USB_RX_EARLY_TIMEOUT	0xd42c
#define USB_RX_EARLY_SIZE	0xd42e
#define USB_PM_CTRL_STATUS	0xd432	/* RTL8153A */
#define USB_RX_EXTRA_AGGR_TMR	0xd432	/* RTL8153B */
#define USB_TX_DMA		0xd434
#define USB_UPT_RXDMA_OWN	0xd437
#define USB_UPHY3_MDCMDIO	0xd480
#define USB_TOLERANCE		0xd490
#define USB_LPM_CTRL		0xd41a
#define USB_BMU_RESET		0xd4b0
#define USB_BMU_CONFIG		0xd4b4
#define USB_U1U2_TIMER		0xd4da
#define USB_FW_TASK		0xd4e8	/* RTL8153B */
#define USB_RX_AGGR_NUM		0xd4ee
#define USB_UPS_CTRL		0xd800
#define USB_POWER_CUT		0xd80a
#define USB_MISC_0		0xd81a
#define USB_MISC_1		0xd81f
#define USB_AFE_CTRL2		0xd824
#define USB_UPHY_XTAL		0xd826
#define USB_UPS_CFG		0xd842
#define USB_UPS_FLAGS		0xd848
#define USB_WDT1_CTRL		0xe404
#define USB_WDT11_CTRL		0xe43c
#define USB_BP_BA		PLA_BP_BA
#define USB_BP_0		PLA_BP_0
#define USB_BP_1		PLA_BP_1
#define USB_BP_2		PLA_BP_2
#define USB_BP_3		PLA_BP_3
#define USB_BP_4		PLA_BP_4
#define USB_BP_5		PLA_BP_5
#define USB_BP_6		PLA_BP_6
#define USB_BP_7		PLA_BP_7
#define USB_BP_EN		PLA_BP_EN	/* RTL8153A */
#define USB_BP_8		0xfc38		/* RTL8153B */
#define USB_BP_9		0xfc3a
#define USB_BP_10		0xfc3c
#define USB_BP_11		0xfc3e
#define USB_BP_12		0xfc40
#define USB_BP_13		0xfc42
#define USB_BP_14		0xfc44
#define USB_BP_15		0xfc46
#define USB_BP2_EN		0xfc48

/* OCP Registers */
#define OCP_ALDPS_CONFIG	0x2010
#define OCP_EEE_CONFIG1		0x2080
#define OCP_EEE_CONFIG2		0x2092
#define OCP_EEE_CONFIG3		0x2094
#define OCP_BASE_MII		0xa400
#define OCP_EEE_AR		0xa41a
#define OCP_EEE_DATA		0xa41c
#define OCP_PHY_STATUS		0xa420
#define OCP_NCTL_CFG		0xa42c
#define OCP_POWER_CFG		0xa430
#define OCP_EEE_CFG		0xa432
#define OCP_SRAM_ADDR		0xa436
#define OCP_SRAM_DATA		0xa438
#define OCP_DOWN_SPEED		0xa442
#define OCP_EEE_ABLE		0xa5c4
#define OCP_EEE_ADV		0xa5d0
#define OCP_EEE_LPABLE		0xa5d2
#define OCP_10GBT_CTRL		0xa5d4
#define OCP_10GBT_STAT		0xa5d6
#define OCP_EEE_ADV2		0xa6d4
#define OCP_PHY_STATE		0xa708		/* nway state for 8153 */
#define OCP_PHY_PATCH_STAT	0xb800
#define OCP_PHY_PATCH_CMD	0xb820
#define OCP_PHY_LOCK		0xb82e
#define OCP_ADC_IOFFSET		0xbcfc
#define OCP_ADC_CFG		0xbc06
#define OCP_SYSCLK_CFG		0xc416

/* SRAM Register */
#define SRAM_GREEN_CFG		0x8011
#define SRAM_LPF_CFG		0x8012
#define SRAM_GPHY_FW_VER	0x801e
#define SRAM_10M_AMP1		0x8080
#define SRAM_10M_AMP2		0x8082
#define SRAM_IMPEDANCE		0x8084
#define SRAM_PHY_LOCK		0xb82e

/* PLA_RCR */
#define RCR_AAP			0x00000001
#define RCR_APM			0x00000002
#define RCR_AM			0x00000004
#define RCR_AB			0x00000008
#define RCR_ACPT_ALL		(RCR_AAP | RCR_APM | RCR_AM | RCR_AB)
#define SLOT_EN			BIT(11)

/* PLA_RCR1 */
#define OUTER_VLAN		BIT(7)
#define INNER_VLAN		BIT(6)

/* PLA_RXFIFO_CTRL0 */
#define RXFIFO_THR1_NORMAL	0x00080002
#define RXFIFO_THR1_OOB		0x01800003

/* PLA_RXFIFO_FULL */
#define RXFIFO_FULL_MASK	0xfff

/* PLA_RXFIFO_CTRL1 */
#define RXFIFO_THR2_FULL	0x00000060
#define RXFIFO_THR2_HIGH	0x00000038
#define RXFIFO_THR2_OOB		0x0000004a
#define RXFIFO_THR2_NORMAL	0x00a0

/* PLA_RXFIFO_CTRL2 */
#define RXFIFO_THR3_FULL	0x00000078
#define RXFIFO_THR3_HIGH	0x00000048
#define RXFIFO_THR3_OOB		0x0000005a
#define RXFIFO_THR3_NORMAL	0x0110

/* PLA_TXFIFO_CTRL */
#define TXFIFO_THR_NORMAL	0x00400008
#define TXFIFO_THR_NORMAL2	0x01000008

/* PLA_DMY_REG0 */
#define ECM_ALDPS		0x0002

/* PLA_FMC */
#define FMC_FCR_MCU_EN		0x0001

/* PLA_EEEP_CR */
#define EEEP_CR_EEEP_TX		0x0002

/* PLA_WDT6_CTRL */
#define WDT6_SET_MODE		0x0010

/* PLA_TCR0 */
#define TCR0_TX_EMPTY		0x0800
#define TCR0_AUTO_FIFO		0x0080

/* PLA_TCR1 */
#define VERSION_MASK		0x7cf0
#define IFG_MASK		(BIT(3) | BIT(9) | BIT(8))
#define IFG_144NS		BIT(9)
#define IFG_96NS		(BIT(9) | BIT(8))

/* PLA_MTPS */
#define MTPS_JUMBO		(12 * 1024 / 64)
#define MTPS_DEFAULT		(6 * 1024 / 64)

/* PLA_RSTTALLY */
#define TALLY_RESET		0x0001

/* PLA_CR */
#define CR_RST			0x10
#define CR_RE			0x08
#define CR_TE			0x04

/* PLA_CRWECR */
#define CRWECR_NORAML		0x00
#define CRWECR_CONFIG		0xc0

/* PLA_OOB_CTRL */
#define NOW_IS_OOB		0x80
#define TXFIFO_EMPTY		0x20
#define RXFIFO_EMPTY		0x10
#define LINK_LIST_READY		0x02
#define DIS_MCU_CLROOB		0x01
#define FIFO_EMPTY		(TXFIFO_EMPTY | RXFIFO_EMPTY)

/* PLA_MISC_1 */
#define RXDY_GATED_EN		0x0008

/* PLA_SFF_STS_7 */
#define RE_INIT_LL		0x8000
#define MCU_BORW_EN		0x4000

/* PLA_CPCR */
#define FLOW_CTRL_EN		BIT(0)
#define CPCR_RX_VLAN		0x0040

/* PLA_CFG_WOL */
#define MAGIC_EN		0x0001

/* PLA_TEREDO_CFG */
#define TEREDO_SEL		0x8000
#define TEREDO_WAKE_MASK	0x7f00
#define TEREDO_RS_EVENT_MASK	0x00fe
#define OOB_TEREDO_EN		0x0001

/* PLA_BDC_CR */
#define ALDPS_PROXY_MODE	0x0001

/* PLA_EFUSE_CMD */
#define EFUSE_READ_CMD		BIT(15)
#define EFUSE_DATA_BIT16	BIT(7)

/* PLA_CONFIG34 */
#define LINK_ON_WAKE_EN		0x0010
#define LINK_OFF_WAKE_EN	0x0008

/* PLA_CONFIG6 */
#define LANWAKE_CLR_EN		BIT(0)

/* PLA_USB_CFG */
#define EN_XG_LIP		BIT(1)
#define EN_G_LIP		BIT(2)

/* PLA_CONFIG5 */
#define BWF_EN			0x0040
#define MWF_EN			0x0020
#define UWF_EN			0x0010
#define LAN_WAKE_EN		0x0002

/* PLA_LED_FEATURE */
#define LED_MODE_MASK		0x0700

/* PLA_PHY_PWR */
#define TX_10M_IDLE_EN		0x0080
#define PFM_PWM_SWITCH		0x0040
#define TEST_IO_OFF		BIT(4)

/* PLA_MAC_PWR_CTRL */
#define D3_CLK_GATED_EN		0x00004000
#define MCU_CLK_RATIO		0x07010f07
#define MCU_CLK_RATIO_MASK	0x0f0f0f0f
#define ALDPS_SPDWN_RATIO	0x0f87

/* PLA_MAC_PWR_CTRL2 */
#define EEE_SPDWN_RATIO		0x8007
#define MAC_CLK_SPDWN_EN	BIT(15)
#define EEE_SPDWN_RATIO_MASK	0xff

/* PLA_MAC_PWR_CTRL3 */
#define PLA_MCU_SPDWN_EN	BIT(14)
#define PKT_AVAIL_SPDWN_EN	0x0100
#define SUSPEND_SPDWN_EN	0x0004
#define U1U2_SPDWN_EN		0x0002
#define L1_SPDWN_EN		0x0001

/* PLA_MAC_PWR_CTRL4 */
#define PWRSAVE_SPDWN_EN	0x1000
#define RXDV_SPDWN_EN		0x0800
#define TX10MIDLE_EN		0x0100
#define IDLE_SPDWN_EN		BIT(6)
#define TP100_SPDWN_EN		0x0020
#define TP500_SPDWN_EN		0x0010
#define TP1000_SPDWN_EN		0x0008
#define EEE_SPDWN_EN		0x0001

/* PLA_GPHY_INTR_IMR */
#define GPHY_STS_MSK		0x0001
#define SPEED_DOWN_MSK		0x0002
#define SPDWN_RXDV_MSK		0x0004
#define SPDWN_LINKCHG_MSK	0x0008

/* PLA_PHYAR */
#define PHYAR_FLAG		0x80000000

/* PLA_EEE_CR */
#define EEE_RX_EN		0x0001
#define EEE_TX_EN		0x0002

/* PLA_BOOT_CTRL */
#define AUTOLOAD_DONE		0x0002

/* PLA_LWAKE_CTRL_REG */
#define LANWAKE_PIN		BIT(7)

/* PLA_SUSPEND_FLAG */
#define LINK_CHG_EVENT		BIT(0)

/* PLA_INDICATE_FALG */
#define UPCOMING_RUNTIME_D3	BIT(0)

/* PLA_MACDBG_PRE and PLA_MACDBG_POST */
#define DEBUG_OE		BIT(0)
#define DEBUG_LTSSM		0x0082

/* PLA_EXTRA_STATUS */
#define CUR_LINK_OK		BIT(15)
#define U3P3_CHECK_EN		BIT(7)	/* RTL_VER_05 only */
#define LINK_CHANGE_FLAG	BIT(8)
#define POLL_LINK_CHG		BIT(0)

/* PLA_GPHY_CTRL */
#define GPHY_FLASH		BIT(1)

/* PLA_POL_GPIO_CTRL */
#define DACK_DET_EN		BIT(15)
#define POL_GPHY_PATCH		BIT(4)

/* USB_USB2PHY */
#define USB2PHY_SUSPEND		0x0001
#define USB2PHY_L1		0x0002

/* USB_SSPHYLINK1 */
#define DELAY_PHY_PWR_CHG	BIT(1)

/* USB_SSPHYLINK2 */
#define pwd_dn_scale_mask	0x3ffe
#define pwd_dn_scale(x)		((x) << 1)

/* USB_CSR_DUMMY1 */
#define DYNAMIC_BURST		0x0001

/* USB_CSR_DUMMY2 */
#define EP4_FULL_FC		0x0001

/* USB_DEV_STAT */
#define STAT_SPEED_MASK		0x0006
#define STAT_SPEED_HIGH		0x0000
#define STAT_SPEED_FULL		0x0002

/* USB_FW_FIX_EN0 */
#define FW_FIX_SUSPEND		BIT(14)

/* USB_FW_FIX_EN1 */
#define FW_IP_RESET_EN		BIT(9)

/* USB_LPM_CONFIG */
#define LPM_U1U2_EN		BIT(0)

/* USB_EFUSE */
#define PASS_THRU_MASK		BIT(0)

/* USB_TX_AGG */
#define TX_AGG_MAX_THRESHOLD	0x03

/* USB_RX_BUF_TH */
#define RX_THR_SUPPER		0x0c350180
#define RX_THR_HIGH		0x7a120180
#define RX_THR_SLOW		0xffff0180
#define RX_THR_B		0x00010001

/* USB_TX_DMA */
#define TEST_MODE_DISABLE	0x00000001
#define TX_SIZE_ADJUST1		0x00000100

/* USB_BMU_RESET */
#define BMU_RESET_EP_IN		0x01
#define BMU_RESET_EP_OUT	0x02

/* USB_BMU_CONFIG */
#define ACT_ODMA		BIT(1)

/* USB_UPT_RXDMA_OWN */
#define OWN_UPDATE		BIT(0)
#define OWN_CLEAR		BIT(1)

/* USB_FW_TASK */
#define FC_PATCH_TASK		BIT(1)

/* USB_RX_AGGR_NUM */
#define RX_AGGR_NUM_MASK	0x1ff

/* USB_UPS_CTRL */
#define POWER_CUT		0x0100

/* USB_PM_CTRL_STATUS */
#define RESUME_INDICATE		0x0001

/* USB_ECM_OPTION */
#define BYPASS_MAC_RESET	BIT(5)

/* USB_CSTMR */
#define FORCE_SUPER		BIT(0)

/* USB_MISC_2 */
#define UPS_FORCE_PWR_DOWN	BIT(0)

/* USB_ECM_OP */
#define	EN_ALL_SPEED		BIT(0)

/* USB_GPHY_CTRL */
#define GPHY_PATCH_DONE		BIT(2)
#define BYPASS_FLASH		BIT(5)
#define BACKUP_RESTRORE		BIT(6)

/* USB_SPEED_OPTION */
#define RG_PWRDN_EN		BIT(8)
#define ALL_SPEED_OFF		BIT(9)

/* USB_FW_CTRL */
#define FLOW_CTRL_PATCH_OPT	BIT(1)
#define AUTO_SPEEDUP		BIT(3)
#define FLOW_CTRL_PATCH_2	BIT(8)

/* USB_FC_TIMER */
#define CTRL_TIMER_EN		BIT(15)

/* USB_USB_CTRL */
#define CDC_ECM_EN		BIT(3)
#define RX_AGG_DISABLE		0x0010
#define RX_ZERO_EN		0x0080

/* USB_U2P3_CTRL */
#define U2P3_ENABLE		0x0001
#define RX_DETECT8		BIT(3)

/* USB_POWER_CUT */
#define PWR_EN			0x0001
#define PHASE2_EN		0x0008
#define UPS_EN			BIT(4)
#define USP_PREWAKE		BIT(5)

/* USB_MISC_0 */
#define PCUT_STATUS		0x0001
#define AD_MASK			0xfee0

/* USB_MISC_1 */
#define BD_MASK			BIT(0)
#define BND_MASK		BIT(2)
#define BL_MASK			BIT(3)

/* USB_RX_EARLY_TIMEOUT */
#define COALESCE_SUPER		 85000U
#define COALESCE_HIGH		250000U
#define COALESCE_SLOW		524280U

/* USB_WDT1_CTRL */
#define WTD1_EN			BIT(0)

/* USB_WDT11_CTRL */
#define TIMER11_EN		0x0001

/* USB_LPM_CTRL */
/* bit 4 ~ 5: fifo empty boundary */
#define FIFO_EMPTY_1FB		0x30	/* 0x1fb * 64 = 32448 bytes */
/* bit 2 ~ 3: LMP timer */
#define LPM_TIMER_MASK		0x0c
#define LPM_TIMER_500MS		0x04	/* 500 ms */
#define LPM_TIMER_500US		0x0c	/* 500 us */
#define ROK_EXIT_LPM		0x02

/* USB_AFE_CTRL2 */
#define SEN_VAL_MASK		0xf800
#define SEN_VAL_NORMAL		0xa000
#define SEL_RXIDLE		0x0100

/* USB_UPHY_XTAL */
#define OOBS_POLLING		BIT(8)

/* USB_UPS_CFG */
#define SAW_CNT_1MS_MASK	0x0fff
#define MID_REVERSE		BIT(5)	/* RTL8156A */

/* USB_UPS_FLAGS */
#define UPS_FLAGS_R_TUNE		BIT(0)
#define UPS_FLAGS_EN_10M_CKDIV		BIT(1)
#define UPS_FLAGS_250M_CKDIV		BIT(2)
#define UPS_FLAGS_EN_ALDPS		BIT(3)
#define UPS_FLAGS_CTAP_SHORT_DIS	BIT(4)
#define UPS_FLAGS_SPEED_MASK		(0xf << 16)
#define ups_flags_speed(x)		((x) << 16)
#define UPS_FLAGS_EN_EEE		BIT(20)
#define UPS_FLAGS_EN_500M_EEE		BIT(21)
#define UPS_FLAGS_EN_EEE_CKDIV		BIT(22)
#define UPS_FLAGS_EEE_PLLOFF_100	BIT(23)
#define UPS_FLAGS_EEE_PLLOFF_GIGA	BIT(24)
#define UPS_FLAGS_EEE_CMOD_LV_EN	BIT(25)
#define UPS_FLAGS_EN_GREEN		BIT(26)
#define UPS_FLAGS_EN_FLOW_CTR		BIT(27)

enum spd_duplex {
	NWAY_10M_HALF,
	NWAY_10M_FULL,
	NWAY_100M_HALF,
	NWAY_100M_FULL,
	NWAY_1000M_FULL,
	FORCE_10M_HALF,
	FORCE_10M_FULL,
	FORCE_100M_HALF,
	FORCE_100M_FULL,
	FORCE_1000M_FULL,
	NWAY_2500M_FULL,
};

/* OCP_ALDPS_CONFIG */
#define ENPWRSAVE		0x8000
#define ENPDNPS			0x0200
#define LINKENA			0x0100
#define DIS_SDSAVE		0x0010

/* OCP_PHY_STATUS */
#define PHY_STAT_MASK		0x0007
#define PHY_STAT_EXT_INIT	2
#define PHY_STAT_LAN_ON		3
#define PHY_STAT_PWRDN		5

/* OCP_NCTL_CFG */
#define PGA_RETURN_EN		BIT(1)

/* OCP_POWER_CFG */
#define EEE_CLKDIV_EN		0x8000
#define EN_ALDPS		0x0004
#define EN_10M_PLLOFF		0x0001

/* OCP_EEE_CONFIG1 */
#define RG_TXLPI_MSK_HFDUP	0x8000
#define RG_MATCLR_EN		0x4000
#define EEE_10_CAP		0x2000
#define EEE_NWAY_EN		0x1000
#define TX_QUIET_EN		0x0200
#define RX_QUIET_EN		0x0100
#define sd_rise_time_mask	0x0070
#define sd_rise_time(x)		(min(x, 7) << 4)	/* bit 4 ~ 6 */
#define RG_RXLPI_MSK_HFDUP	0x0008
#define SDFALLTIME		0x0007	/* bit 0 ~ 2 */

/* OCP_EEE_CONFIG2 */
#define RG_LPIHYS_NUM		0x7000	/* bit 12 ~ 15 */
#define RG_DACQUIET_EN		0x0400
#define RG_LDVQUIET_EN		0x0200
#define RG_CKRSEL		0x0020
#define RG_EEEPRG_EN		0x0010

/* OCP_EEE_CONFIG3 */
#define fast_snr_mask		0xff80
#define fast_snr(x)		(min(x, 0x1ff) << 7)	/* bit 7 ~ 15 */
#define RG_LFS_SEL		0x0060	/* bit 6 ~ 5 */
#define MSK_PH			0x0006	/* bit 0 ~ 3 */

/* OCP_EEE_AR */
/* bit[15:14] function */
#define FUN_ADDR		0x0000
#define FUN_DATA		0x4000
/* bit[4:0] device addr */

/* OCP_EEE_CFG */
#define CTAP_SHORT_EN		0x0040
#define EEE10_EN		0x0010

/* OCP_DOWN_SPEED */
#define EN_EEE_CMODE		BIT(14)
#define EN_EEE_1000		BIT(13)
#define EN_EEE_100		BIT(12)
#define EN_10M_CLKDIV		BIT(11)
#define EN_10M_BGOFF		0x0080

/* OCP_10GBT_CTRL */
#define RTL_ADV2_5G_F_R		BIT(5)	/* Advertise 2.5GBASE-T fast-retrain */

/* OCP_PHY_STATE */
#define TXDIS_STATE		0x01
#define ABD_STATE		0x02

/* OCP_PHY_PATCH_STAT */
#define PATCH_READY		BIT(6)

/* OCP_PHY_PATCH_CMD */
#define PATCH_REQUEST		BIT(4)

/* OCP_PHY_LOCK */
#define PATCH_LOCK		BIT(0)

/* OCP_ADC_CFG */
#define CKADSEL_L		0x0100
#define ADC_EN			0x0080
#define EN_EMI_L		0x0040

/* OCP_SYSCLK_CFG */
#define sysclk_div_expo(x)	(min(x, 5) << 8)
#define clk_div_expo(x)		(min(x, 5) << 4)

/* SRAM_GREEN_CFG */
#define GREEN_ETH_EN		BIT(15)
#define R_TUNE_EN		BIT(11)

/* SRAM_LPF_CFG */
#define LPF_AUTO_TUNE		0x8000

/* SRAM_10M_AMP1 */
#define GDAC_IB_UPALL		0x0008

/* SRAM_10M_AMP2 */
#define AMP_DN			0x0200

/* SRAM_IMPEDANCE */
#define RX_DRIVING_MASK		0x6000

/* SRAM_PHY_LOCK */
#define PHY_PATCH_LOCK		0x0001

#define RTL8152_MAX_TX		4
#define RTL8152_MAX_RX		10

struct r8152;

struct rx_agg {
	struct list_head list, info_list;
	struct urb *urb;
	struct r8152 *context;
	struct page *page;
	void *buffer;
};

struct tx_agg {
	struct list_head list;
	struct urb *urb;
	struct r8152 *context;
	void *buffer;
	void *head;
	u32 skb_num;
	u32 skb_len;
};

struct r8152 {
	unsigned long flags;
	struct usb_device *udev;
	struct napi_struct napi;
	struct usb_interface *intf;
	struct net_device *netdev;
	struct urb *intr_urb;
	struct tx_agg tx_info[RTL8152_MAX_TX];
	struct list_head rx_info, rx_used;
	struct list_head rx_done, tx_free;
	struct sk_buff_head tx_queue, rx_queue;
	spinlock_t rx_lock, tx_lock;
	struct delayed_work schedule, hw_phy_work;
	struct mii_if_info mii;
	struct mutex control;	/* use for hw setting */
#ifdef CONFIG_PM_SLEEP
	struct notifier_block pm_notifier;
#endif
	struct tasklet_struct tx_tl;

	struct rtl_ops {
		void (*init)(struct r8152 *tp);
		int (*enable)(struct r8152 *tp);
		void (*disable)(struct r8152 *tp);
		void (*up)(struct r8152 *tp);
		void (*down)(struct r8152 *tp);
		void (*unload)(struct r8152 *tp);
		int (*eee_get)(struct r8152 *tp, struct ethtool_eee *eee);
		int (*eee_set)(struct r8152 *tp, struct ethtool_eee *eee);
		bool (*in_nway)(struct r8152 *tp);
		void (*hw_phy_cfg)(struct r8152 *tp);
		void (*autosuspend_en)(struct r8152 *tp, bool enable);
		void (*change_mtu)(struct r8152 *tp);
	} rtl_ops;

	struct ups_info {
		u32 r_tune:1;
		u32 _10m_ckdiv:1;
		u32 _250m_ckdiv:1;
		u32 aldps:1;
		u32 lite_mode:2;
		u32 speed_duplex:4;
		u32 eee:1;
		u32 eee_lite:1;
		u32 eee_ckdiv:1;
		u32 eee_plloff_100:1;
		u32 eee_plloff_giga:1;
		u32 eee_cmod_lv:1;
		u32 green:1;
		u32 flow_control:1;
		u32 ctap_short_off:1;
	} ups_info;

#define RTL_VER_SIZE		32

	struct rtl_fw {
		const char *fw_name;
		const struct firmware *fw;

		char version[RTL_VER_SIZE];
		int (*pre_fw)(struct r8152 *tp);
		int (*post_fw)(struct r8152 *tp);

		bool retry;
	} rtl_fw;

	atomic_t rx_count;

	bool eee_en;
	int intr_interval;
	u32 saved_wolopts;
	u32 msg_enable;
	u32 tx_qlen;
	u32 coalesce;
	u32 advertising;
	u32 rx_buf_sz;
	u32 rx_copybreak;
	u32 rx_pending;
	u32 fc_pause_on, fc_pause_off;

	unsigned int pipe_in, pipe_out, pipe_intr, pipe_ctrl_in, pipe_ctrl_out;

	u32 support_2500full:1;
	u32 lenovo_macpassthru:1;
	u32 dell_tb_rx_agg_bug:1;
	u16 ocp_base;
	u16 speed;
	u16 eee_adv;
	u8 *intr_buff;
	u8 version;
	u8 duplex;
	u8 autoneg;
};

enum rtl_version {
	RTL_VER_UNKNOWN = 0,
	RTL_VER_01,
	RTL_VER_02,
	RTL_VER_03,
	RTL_VER_04,
	RTL_VER_05,
	RTL_VER_06,
	RTL_VER_07,
	RTL_VER_08,
	RTL_VER_09,

	RTL_TEST_01,
	RTL_VER_10,
	RTL_VER_11,
	RTL_VER_12,
	RTL_VER_13,
	RTL_VER_14,
	RTL_VER_15,

	RTL_VER_MAX
};

#define FIRMWARE_8153A_2	"rtl_nic/rtl8153a-2.fw"
#define FIRMWARE_8153A_3	"rtl_nic/rtl8153a-3.fw"
#define FIRMWARE_8153A_4	"rtl_nic/rtl8153a-4.fw"
#define FIRMWARE_8153B_2	"rtl_nic/rtl8153b-2.fw"
#define FIRMWARE_8153C_1	"rtl_nic/rtl8153c-1.fw"
#define FIRMWARE_8156A_2	"rtl_nic/rtl8156a-2.fw"
#define FIRMWARE_8156B_2	"rtl_nic/rtl8156b-2.fw"

int generic_ocp_read(struct r8152 *tp, u16 index, u16 size, void *data, u16 type);
int generic_ocp_write(struct r8152 *tp, u16 index, u16 byteen, u16 size, void *data, u16 type);
u32 ocp_read_dword(struct r8152 *tp, u16 type, u16 index);
u16 ocp_read_word(struct r8152 *tp, u16 type, u16 index);
u8 ocp_read_byte(struct r8152 *tp, u16 type, u16 index);
void ocp_write_dword(struct r8152 *tp, u16 type, u16 index, u32 data);
void ocp_write_word(struct r8152 *tp, u16 type, u16 index, u32 data);
void ocp_write_byte(struct r8152 *tp, u16 type, u16 index, u32 data);

u16 ocp_reg_read(struct r8152 *tp, u16 addr);
void ocp_reg_write(struct r8152 *tp, u16 addr, u16 data);
void sram_write(struct r8152 *tp, u16 addr, u16 data);
u16 sram_read(struct r8152 *tp, u16 addr);

int rtl_phy_patch_request(struct r8152 *tp, bool request, bool wait);

void rtl8152_apply_firmware(struct r8152 *tp, bool power_cut);
void rtl8152_release_firmware(struct r8152 *tp);
int rtl8152_request_firmware(struct r8152 *tp);
int rtl_fw_init(struct r8152 *tp);

#endif /* LINUX_R8152_BASIC_H */
