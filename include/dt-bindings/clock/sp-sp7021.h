/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */
#ifndef _DT_BINDINGS_CLOCK_SUNPLUS_SP7021_H
#define _DT_BINDINGS_CLOCK_SUNPLUS_Sp7021_H

#define XTAL			27000000

/* plls */
#define PLL_A			0
#define PLL_E			1
#define PLL_E_2P5		2
#define PLL_E_25		3
#define PLL_E_112P5		4
#define PLL_F			5
#define PLL_TV			6
#define PLL_TV_A		7
#define PLL_SYS			8

/* gates: mo_clken0 ~ mo_clken9 */
#define CLK_SYSTEM		0x10
#define CLK_RTC			0x12
#define CLK_IOCTL		0x13
#define CLK_IOP			0x14
#define CLK_OTPRX		0x15
#define CLK_NOC			0x16
#define CLK_BR			0x17
#define CLK_RBUS_L00	0x18
#define CLK_SPIFL		0x19
#define CLK_SDCTRL0		0x1a
#define CLK_PERI0		0x1b
#define CLK_A926		0x1d
#define CLK_UMCTL2		0x1e
#define CLK_PERI1		0x1f

#define CLK_DDR_PHY0	0x20
#define CLK_ACHIP		0x22
#define CLK_STC0		0x24
#define CLK_STC_AV0		0x25
#define CLK_STC_AV1		0x26
#define CLK_STC_AV2		0x27
#define CLK_UA0			0x28
#define CLK_UA1			0x29
#define CLK_UA2			0x2a
#define CLK_UA3			0x2b
#define CLK_UA4			0x2c
#define CLK_HWUA		0x2d
#define CLK_DDC0		0x2e
#define CLK_UADMA		0x2f

#define CLK_CBDMA0		0x30
#define CLK_CBDMA1		0x31
#define CLK_SPI_COMBO_0	0x32
#define CLK_SPI_COMBO_1	0x33
#define CLK_SPI_COMBO_2	0x34
#define CLK_SPI_COMBO_3	0x35
#define CLK_AUD			0x36
#define CLK_USBC0		0x3a
#define CLK_USBC1		0x3b
#define CLK_UPHY0		0x3d
#define CLK_UPHY1		0x3e

#define CLK_I2CM0		0x40
#define CLK_I2CM1		0x41
#define CLK_I2CM2		0x42
#define CLK_I2CM3		0x43
#define CLK_PMC			0x4d
#define CLK_CARD_CTL0	0x4e
#define CLK_CARD_CTL1	0x4f

#define CLK_CARD_CTL4	0x52
#define CLK_BCH			0x54
#define CLK_DDFCH		0x5b
#define CLK_CSIIW0		0x5c
#define CLK_CSIIW1		0x5d
#define CLK_MIPICSI0	0x5e
#define CLK_MIPICSI1	0x5f

#define CLK_HDMI_TX		0x60
#define CLK_VPOST		0x65

#define CLK_TGEN		0x70
#define CLK_DMIX		0x71
#define CLK_TCON		0x7a
#define CLK_INTERRUPT	0x7f

#define CLK_RGST		0x80
#define CLK_GPIO		0x83
#define CLK_RBUS_TOP	0x84

#define CLK_MAILBOX		0x96
#define CLK_SPIND		0x9a
#define CLK_I2C2CBUS	0x9b
#define CLK_SEC			0x9d
#define CLK_DVE			0x9e
#define CLK_GPOST0		0x9f

#define CLK_OSD0		0xa0
#define CLK_DISP_PWM	0xa2
#define CLK_UADBG		0xa3
#define CLK_DUMMY_MASTER	0xa4
#define CLK_FIO_CTL		0xa5
#define CLK_FPGA		0xa6
#define CLK_L2SW		0xa7
#define CLK_ICM			0xa8
#define CLK_AXI_GLOBAL	0xa9

#define CLK_MAX			0xb0

#endif
