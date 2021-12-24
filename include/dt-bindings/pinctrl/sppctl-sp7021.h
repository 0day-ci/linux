/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Sunplus SP7021 dt-bindings Pinctrl header file
 * Copyright (C) Sunplus Tech/Tibbo Tech.
 * Author: Dvorkin Dmitry <dvorkin@tibbo.com>
 */

#ifndef	__DT_BINDINGS_PINCTRL_SPPCTL_SP7021_H__
#define	__DT_BINDINGS_PINCTRL_SPPCTL_SP7021_H__

#include <dt-bindings/pinctrl/sppctl.h>

/* Please don't change the order of the
 * following defines. They are based on
 * order of control register define of
 * MOON1 ~ MOON4 registers.
 */
#define MUXF_L2SW_CLK_OUT		0
#define MUXF_L2SW_MAC_SMI_MDC		1
#define MUXF_L2SW_LED_FLASH0		2
#define MUXF_L2SW_LED_FLASH1		3
#define MUXF_L2SW_LED_ON0		4
#define MUXF_L2SW_LED_ON1		5
#define MUXF_L2SW_MAC_SMI_MDIO		6
#define MUXF_L2SW_P0_MAC_RMII_TXEN	7
#define MUXF_L2SW_P0_MAC_RMII_TXD0	8
#define MUXF_L2SW_P0_MAC_RMII_TXD1	9
#define MUXF_L2SW_P0_MAC_RMII_CRSDV	10
#define MUXF_L2SW_P0_MAC_RMII_RXD0	11
#define MUXF_L2SW_P0_MAC_RMII_RXD1	12
#define MUXF_L2SW_P0_MAC_RMII_RXER	13
#define MUXF_L2SW_P1_MAC_RMII_TXEN	14
#define MUXF_L2SW_P1_MAC_RMII_TXD0	15
#define MUXF_L2SW_P1_MAC_RMII_TXD1	16
#define MUXF_L2SW_P1_MAC_RMII_CRSDV	17
#define MUXF_L2SW_P1_MAC_RMII_RXD0	18
#define MUXF_L2SW_P1_MAC_RMII_RXD1	19
#define MUXF_L2SW_P1_MAC_RMII_RXER	20
#define MUXF_DAISY_MODE			21
#define MUXF_SDIO_CLK			22
#define MUXF_SDIO_CMD			23
#define MUXF_SDIO_D0			24
#define MUXF_SDIO_D1			25
#define MUXF_SDIO_D2			26
#define MUXF_SDIO_D3			27
#define MUXF_PWM0			28
#define MUXF_PWM1			29
#define MUXF_PWM2			30
#define MUXF_PWM3			31
#define MUXF_PWM4			32
#define MUXF_PWM5			33
#define MUXF_PWM6			34
#define MUXF_PWM7			35
#define MUXF_ICM0_D			36
#define MUXF_ICM1_D			37
#define MUXF_ICM2_D			38
#define MUXF_ICM3_D			39
#define MUXF_ICM0_CLK			40
#define MUXF_ICM1_CLK			41
#define MUXF_ICM2_CLK			42
#define MUXF_ICM3_CLK			43
#define MUXF_SPIM0_INT			44
#define MUXF_SPIM0_CLK			45
#define MUXF_SPIM0_EN			46
#define MUXF_SPIM0_DO			47
#define MUXF_SPIM0_DI			48
#define MUXF_SPIM1_INT			49
#define MUXF_SPIM1_CLK			50
#define MUXF_SPIM1_EN			51
#define MUXF_SPIM1_DO			52
#define MUXF_SPIM1_DI			53
#define MUXF_SPIM2_INT			54
#define MUXF_SPIM2_CLK			55
#define MUXF_SPIM2_EN			56
#define MUXF_SPIM2_DO			57
#define MUXF_SPIM2_DI			58
#define MUXF_SPIM3_INT			59
#define MUXF_SPIM3_CLK			60
#define MUXF_SPIM3_EN			61
#define MUXF_SPIM3_DO			62
#define MUXF_SPIM3_DI			63
#define MUXF_SPI0S_INT			64
#define MUXF_SPI0S_CLK			65
#define MUXF_SPI0S_EN			66
#define MUXF_SPI0S_DO			67
#define MUXF_SPI0S_DI			68
#define MUXF_SPI1S_INT			69
#define MUXF_SPI1S_CLK			70
#define MUXF_SPI1S_EN			71
#define MUXF_SPI1S_DO			72
#define MUXF_SPI1S_DI			73
#define MUXF_SPI2S_INT			74
#define MUXF_SPI2S_CLK			75
#define MUXF_SPI2S_EN			76
#define MUXF_SPI2S_DO			77
#define MUXF_SPI2S_DI			78
#define MUXF_SPI3S_INT			79
#define MUXF_SPI3S_CLK			80
#define MUXF_SPI3S_EN			81
#define MUXF_SPI3S_DO			82
#define MUXF_SPI3S_DI			83
#define MUXF_I2CM0_CLK			84
#define MUXF_I2CM0_DAT			85
#define MUXF_I2CM1_CLK			86
#define MUXF_I2CM1_DAT			87
#define MUXF_I2CM2_CLK			88
#define MUXF_I2CM2_DAT			89
#define MUXF_I2CM3_CLK			90
#define MUXF_I2CM3_DAT			91
#define MUXF_UA1_TX			92
#define MUXF_UA1_RX			93
#define MUXF_UA1_CTS			94
#define MUXF_UA1_RTS			95
#define MUXF_UA2_TX			96
#define MUXF_UA2_RX			97
#define MUXF_UA2_CTS			98
#define MUXF_UA2_RTS			99
#define MUXF_UA3_TX			100
#define MUXF_UA3_RX			101
#define MUXF_UA3_CTS			102
#define MUXF_UA3_RTS			103
#define MUXF_UA4_TX			104
#define MUXF_UA4_RX			105
#define MUXF_UA4_CTS			106
#define MUXF_UA4_RTS			107
#define MUXF_TIMER0_INT			108
#define MUXF_TIMER1_INT			109
#define MUXF_TIMER2_INT			110
#define MUXF_TIMER3_INT			111
#define MUXF_GPIO_INT0			112
#define MUXF_GPIO_INT1			113
#define MUXF_GPIO_INT2			114
#define MUXF_GPIO_INT3			115
#define MUXF_GPIO_INT4			116
#define MUXF_GPIO_INT5			117
#define MUXF_GPIO_INT6			118
#define MUXF_GPIO_INT7			119

#define GROP_SPI_FLASH			120
#define GROP_SPI_FLASH_4BIT		121
#define GROP_SPI_NAND			122
#define GROP_CARD0_EMMC			123
#define GROP_SD_CARD			124
#define GROP_UA0			125
#define GROP_ACHIP_DEBUG		126
#define GROP_ACHIP_UA2AXI		127
#define GROP_FPGA_IFX			128
#define GROP_HDMI_TX			129
#define GROP_AUD_EXT_ADC_IFX0		130
#define GROP_AUD_EXT_DAC_IFX0		131
#define GROP_SPDIF_RX			132
#define GROP_SPDIF_TX			133
#define GROP_TDMTX_IFX0			134
#define GROP_TDMRX_IFX0			135
#define GROP_PDMRX_IFX0			136
#define GROP_PCM_IEC_TX			137
#define GROP_LCDIF			138
#define GROP_DVD_DSP_DEBUG		139
#define GROP_I2C_DEBUG			140
#define GROP_I2C_SLAVE			141
#define GROP_WAKEUP			142
#define GROP_UART2AXI			143
#define GROP_USB0_I2C			144
#define GROP_USB1_I2C			145
#define GROP_USB0_OTG			146
#define GROP_USB1_OTG			147
#define GROP_UPHY0_DEBUG		148
#define GROP_UPHY1_DEBUG		149
#define GROP_UPHY0_EXT			150
#define GROP_PROBE_PORT			151

#endif
