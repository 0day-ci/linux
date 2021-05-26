/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2020, NXP Semiconductors
 */
#ifndef _SJA1105_SGMII_H
#define _SJA1105_SGMII_H

/* DIGITAL_CONTROL_1 (address 1f8000h) */
#define SJA1105_DC1			0x8000
#define SJA1105_DC1_VS_RESET		BIT(15)
#define SJA1105_DC1_REMOTE_LOOPBACK	BIT(14)
#define SJA1105_DC1_EN_VSMMD1		BIT(13)
#define SJA1105_DC1_POWER_SAVE		BIT(11)
#define SJA1105_DC1_CLOCK_STOP_EN	BIT(10)
#define SJA1105_DC1_MAC_AUTO_SW		BIT(9)
#define SJA1105_DC1_INIT		BIT(8)
#define SJA1105_DC1_TX_DISABLE		BIT(4)
#define SJA1105_DC1_AUTONEG_TIMER_OVRR	BIT(3)
#define SJA1105_DC1_ENA_2500_MODE	BIT(2)
#define SJA1105_DC1_BYP_POWERUP		BIT(1)
#define SJA1105_DC1_PHY_MODE_CONTROL	BIT(0)

/* DIGITAL_CONTROL_2 register (address 1f80E1h) */
#define SJA1105_DC2			0x80e1
#define SJA1105_DC2_TX_POL_INV_DISABLE	BIT(4)
#define SJA1105_DC2_RX_POL_INV		BIT(0)

/* DIGITAL_ERROR_CNT register (address 1f80E2h) */
#define SJA1105_DEC			0x80e2
#define SJA1105_DEC_ICG_EC_ENA		BIT(4)
#define SJA1105_DEC_CLEAR_ON_READ	BIT(0)

/* AUTONEG_CONTROL register (address 1f8001h) */
#define SJA1105_AC			0x8001
#define SJA1105_AC_MII_CONTROL		BIT(8)
#define SJA1105_AC_SGMII_LINK		BIT(4)
#define SJA1105_AC_PHY_MODE		BIT(3)
#define SJA1105_AC_AUTONEG_MODE(x)	(((x) << 1) & GENMASK(2, 1))
#define SJA1105_AC_AUTONEG_MODE_SGMII	SJA1105_AC_AUTONEG_MODE(2)

/* AUTONEG_INTR_STATUS register (address 1f8002h) */
#define SJA1105_AIS			0x8002
#define SJA1105_AIS_LINK_STATUS(x)	(!!((x) & BIT(4)))
#define SJA1105_AIS_SPEED(x)		(((x) & GENMASK(3, 2)) >> 2)
#define SJA1105_AIS_DUPLEX_MODE(x)	(!!((x) & BIT(1)))
#define SJA1105_AIS_COMPLETE(x)		(!!((x) & BIT(0)))

/* DEBUG_CONTROL register (address 1f8005h) */
#define SJA1105_DC			0x8005
#define SJA1105_DC_SUPPRESS_LOS		BIT(4)
#define SJA1105_DC_RESTART_SYNC		BIT(0)

/* LANE_DRIVER1_0 register (address 0x1f8038) */
#define SJA1105_LANE_DRIVER1_0		0x8038
#define SJA1105_TXDRV(x)		(((x) << 12) & GENMASK(14, 12))

/* LANE_DRIVER2_0 register (address 0x1f803a) */
#define SJA1105_LANE_DRIVER2_0		0x803a
#define SJA1105_TXDRVTRIM_LSB(x)	((x) & GENMASK_ULL(15, 0))

/* LANE_DRIVER2_1 register (address 0x1f803b) */
#define SJA1105_LANE_DRIVER2_1		0x803b
#define SJA1105_LANE_DRIVER2_1_RSV	BIT(9)
#define SJA1105_TXDRVTRIM_MSB(x)	(((x) & GENMASK_ULL(23, 16)) >> 16)

/* LANE_TRIM register (address 0x1f8040) */
#define SJA1105_LANE_TRIM		0x8040
#define SJA1105_TXTEN			BIT(11)
#define SJA1105_TXRTRIM(x)		(((x) << 8) & GENMASK(10, 8))
#define SJA1105_TXPLL_BWSEL		BIT(7)
#define SJA1105_RXTEN			BIT(6)
#define SJA1105_RXRTRIM(x)		(((x) << 3) & GENMASK(5, 3))
#define SJA1105_CDR_GAIN		BIT(2)
#define SJA1105_ACCOUPLE_RXVCM_EN	BIT(0)

/* LANE_DATAPATH_1 register (address 0x1f8037) */
#define SJA1105_LANE_DATAPATH_1		0x8037

/* POWERDOWN_ENABLE register (address 0x1f8041) */
#define SJA1105_POWERDOWN_ENABLE	0x8041
#define SJA1105_TXPLL_PD		BIT(12)
#define SJA1105_TXPD			BIT(11)
#define SJA1105_RXPKDETEN		BIT(10)
#define SJA1105_RXCH_PD			BIT(9)
#define SJA1105_RXBIAS_PD		BIT(8)
#define SJA1105_RESET_SER_EN		BIT(7)
#define SJA1105_RESET_SER		BIT(6)
#define SJA1105_RESET_DES		BIT(5)
#define SJA1105_RCVEN			BIT(4)

/* RXPLL_CTRL0 register (address 0x1f8065) */
#define SJA1105_RXPLL_CTRL0		0x8065
#define SJA1105_RXPLL_FBDIV(x)		(((x) << 2) & GENMASK(9, 2))

/* RXPLL_CTRL1 register (address 0x1f8066) */
#define SJA1105_RXPLL_CTRL1		0x8066
#define SJA1105_RXPLL_REFDIV(x)		((x) & GENMASK(4, 0))

/* TXPLL_CTRL0 register (address 0x1f806d) */
#define SJA1105_TXPLL_CTRL0		0x806d
#define SJA1105_TXPLL_FBDIV(x)		((x) & GENMASK(11, 0))

/* TXPLL_CTRL1 register (address 0x1f806e) */
#define SJA1105_TXPLL_CTRL1		0x806e
#define SJA1105_TXPLL_REFDIV(x)		((x) & GENMASK(5, 0))

/* RX_DATA_DETECT register (address 0x1f8045) */
#define SJA1105_RX_DATA_DETECT		0x8045

/* RX_CDR_CTLE register (address 0x1f8042) */
#define SJA1105_RX_CDR_CTLE		0x8042

#endif
