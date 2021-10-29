/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */
#ifndef _DT_BINDINGS_CLOCK_SUNPLUS_SP7021_H
#define _DT_BINDINGS_CLOCK_SUNPLUS_Sp7021_H

#define XTAL		27000000

/* plls */
#define PLL_A		0
#define PLL_E		1
#define PLL_E_2P5	2
#define PLL_E_25	3
#define PLL_E_112P5	4
#define PLL_F		5
#define PLL_TV		6
#define PLL_TV_A	7
#define PLL_SYS		8

/* gates: mo_clken0 ~ mo_clken9 */
#define SYSTEM		0x10
#define RTC			0x12
#define IOCTL		0x13
#define IOP			0x14
#define OTPRX		0x15
#define NOC			0x16
#define BR			0x17
#define RBUS_L00	0x18
#define SPIFL		0x19
#define SDCTRL0		0x1a
#define PERI0		0x1b
#define A926		0x1d
#define UMCTL2		0x1e
#define PERI1		0x1f

#define DDR_PHY0	0x20
#define ACHIP		0x22
#define STC0		0x24
#define STC_AV0		0x25
#define STC_AV1		0x26
#define STC_AV2		0x27
#define UA0			0x28
#define UA1			0x29
#define UA2			0x2a
#define UA3			0x2b
#define UA4			0x2c
#define HWUA		0x2d
#define DDC0		0x2e
#define UADMA		0x2f

#define CBDMA0		0x30
#define CBDMA1		0x31
#define SPI_COMBO_0	0x32
#define SPI_COMBO_1	0x33
#define SPI_COMBO_2	0x34
#define SPI_COMBO_3	0x35
#define AUD			0x36
#define USBC0		0x3a
#define USBC1		0x3b
#define UPHY0		0x3d
#define UPHY1		0x3e

#define I2CM0		0x40
#define I2CM1		0x41
#define I2CM2		0x42
#define I2CM3		0x43
#define PMC			0x4d
#define CARD_CTL0	0x4e
#define CARD_CTL1	0x4f

#define CARD_CTL4	0x52
#define BCH			0x54
#define DDFCH		0x5b
#define CSIIW0		0x5c
#define CSIIW1		0x5d
#define MIPICSI0	0x5e
#define MIPICSI1	0x5f

#define HDMI_TX		0x60
#define VPOST		0x65

#define TGEN		0x70
#define DMIX		0x71
#define TCON		0x7a
#define INTERRUPT	0x7f

#define RGST		0x80
#define GPIO		0x83
#define RBUS_TOP	0x84

#define MAILBOX		0x96
#define SPIND		0x9a
#define I2C2CBUS	0x9b
#define SEC			0x9d
#define DVE			0x9e
#define GPOST0		0x9f

#define OSD0		0xa0
#define DISP_PWM	0xa2
#define UADBG		0xa3
#define DUMMY_MASTER	0xa4
#define FIO_CTL		0xa5
#define FPGA		0xa6
#define L2SW		0xa7
#define ICM			0xa8
#define AXI_GLOBAL	0xa9

#define CLK_MAX		0xb0

#endif
