/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Based on sound/soc/codecs/tlv320aic3x.c by  Vladimir Barinov
 *
 * History:
 *
 * Author:  "Shahina Shaik" < shahina.s@mistralsolutions.com >
 * Copyright:   (C) 2010 Mistral Solutions Pvt Ltd.
 *
 * Author: Dongge wu <dgwu@ambarella.com>
 *      2015/10/28 - [Dongge wu] Created file
 * Copyright (C) 2014-2018, Ambarella, Inc.
 *
 * Author: Ricard Wanderlof <ricardw@axis.com>
 * 2020/11/05: Fixing driver for Linux 4.14: more clocking modes, etc.
 * 2021/09/03: Porting to Linux 5.4, with enhancements.
 * Copyright (C) 2021 Axis Communications AB
 *
 */

#ifndef _ADC3XXX_H
#define _ADC3XXX_H

/* 8 bit mask value */
#define ADC3XXX_8BITS_MASK		0xFF

#define ADC3XXX_PAGE_SIZE		128
#define ADC3XXX_REG(page, reg)		((page * ADC3XXX_PAGE_SIZE) + reg)

#define ADC3XXX_MICBIAS_PINS		2

/* Number of GPIO pins exposed via the gpiolib interface */
#define ADC3XXX_GPIOS_MAX		2

/*
 * PLL modes, to be used for clk_id for set_sysclk callback.
 *
 * The default behavior (AUTO) is to take the first matching entry in the clock
 * table, which is intended to be the PLL based one if there is more than one.
 *
 * Setting the clock source using simple-card (clocks or
 * system-clock-frequency property) sets clk_id = 0 = ADC3XXX_PLL_AUTO.
 */
#define ADC3XXX_PLL_AUTO	0 /* Use first available mode */
#define ADC3XXX_PLL_ENABLE	1 /* Use PLL for clock generation */
#define ADC3XXX_PLL_BYPASS	2 /* Don't use PLL for clock generation */


/****************************************************************************/
/*			Page 0 Registers				    */
/****************************************************************************/

/* Page select register */
#define ADC3XXX_PAGE_SELECT			ADC3XXX_REG(0, 0)
/* Software reset register */
#define ADC3XXX_RESET				ADC3XXX_REG(0, 1)

/* 2-3 Reserved */

/* PLL programming register B */
#define ADC3XXX_CLKGEN_MUX			ADC3XXX_REG(0, 4)
/* PLL P and R-Val */
#define ADC3XXX_PLL_PROG_PR			ADC3XXX_REG(0, 5)
/* PLL J-Val */
#define ADC3XXX_PLL_PROG_J			ADC3XXX_REG(0, 6)
/* PLL D-Val MSB */
#define ADC3XXX_PLL_PROG_D_MSB			ADC3XXX_REG(0, 7)
/* PLL D-Val LSB */
#define ADC3XXX_PLL_PROG_D_LSB			ADC3XXX_REG(0, 8)

/* 9-17 Reserved */

/* ADC NADC */
#define ADC3XXX_ADC_NADC			ADC3XXX_REG(0, 18)
/* ADC MADC */
#define ADC3XXX_ADC_MADC			ADC3XXX_REG(0, 19)
/* ADC AOSR */
#define ADC3XXX_ADC_AOSR			ADC3XXX_REG(0, 20)
/* ADC IADC */
#define ADC3XXX_ADC_IADC			ADC3XXX_REG(0, 21)

/* 23-24 Reserved */

/* CLKOUT MUX */
#define ADC3XXX_CLKOUT_MUX			ADC3XXX_REG(0, 25)
/* CLOCKOUT M divider value */
#define ADC3XXX_CLKOUT_M_DIV			ADC3XXX_REG(0, 26)
/*Audio Interface Setting Register 1*/
#define ADC3XXX_INTERFACE_CTRL_1		ADC3XXX_REG(0, 27)
/* Data Slot Offset (Ch_Offset_1) */
#define ADC3XXX_CH_OFFSET_1			ADC3XXX_REG(0, 28)
/* ADC interface control 2 */
#define ADC3XXX_INTERFACE_CTRL_2		ADC3XXX_REG(0, 29)
/* BCLK N Divider */
#define ADC3XXX_BCLK_N_DIV			ADC3XXX_REG(0, 30)
/* Secondary audio interface control 1 */
#define ADC3XXX_INTERFACE_CTRL_3		ADC3XXX_REG(0, 31)
/* Secondary audio interface control 2 */
#define ADC3XXX_INTERFACE_CTRL_4		ADC3XXX_REG(0, 32)
/* Secondary audio interface control 3 */
#define ADC3XXX_INTERFACE_CTRL_5		ADC3XXX_REG(0, 33)
/* I2S sync */
#define ADC3XXX_I2S_SYNC			ADC3XXX_REG(0, 34)

/* 35 Reserved */

/* ADC flag register */
#define ADC3XXX_ADC_FLAG			ADC3XXX_REG(0, 36)
/* Data slot offset 2 (Ch_Offset_2) */
#define ADC3XXX_CH_OFFSET_2			ADC3XXX_REG(0, 37)
/* I2S TDM control register */
#define ADC3XXX_I2S_TDM_CTRL			ADC3XXX_REG(0, 38)

/* 39-41 Reserved */

/* Interrupt flags (overflow) */
#define ADC3XXX_INTR_FLAG_1			ADC3XXX_REG(0, 42)
/* Interrupt flags (overflow) */
#define ADC3XXX_INTR_FLAG_2			ADC3XXX_REG(0, 43)

/* 44 Reserved */

/* Interrupt flags ADC */
#define ADC3XXX_INTR_FLAG_ADC1			ADC3XXX_REG(0, 45)

/* 46 Reserved */

/* Interrupt flags ADC */
#define ADC3XXX_INTR_FLAG_ADC2			ADC3XXX_REG(0, 47)
/* INT1 interrupt control */
#define ADC3XXX_INT1_CTRL			ADC3XXX_REG(0, 48)
/* INT2 interrupt control */
#define ADC3XXX_INT2_CTRL			ADC3XXX_REG(0, 49)

/* 50 Reserved */

/* DMCLK/GPIO2 control */
#define ADC3XXX_GPIO2_CTRL			ADC3XXX_REG(0, 51)
/* DMDIN/GPIO1 control */
#define ADC3XXX_GPIO1_CTRL			ADC3XXX_REG(0, 52)
/* DOUT Control */
#define ADC3XXX_DOUT_CTRL			ADC3XXX_REG(0, 53)

/* 54-56 Reserved */

/* ADC sync control 1 */
#define ADC3XXX_SYNC_CTRL_1			ADC3XXX_REG(0, 57)
/* ADC sync control 2 */
#define ADC3XXX_SYNC_CTRL_2			ADC3XXX_REG(0, 58)
/* ADC CIC filter gain control */
#define ADC3XXX_CIC_GAIN_CTRL			ADC3XXX_REG(0, 59)

/* 60 Reserved */

/* ADC processing block selection  */
#define ADC3XXX_PRB_SELECT			ADC3XXX_REG(0, 61)
/* Programmable instruction mode control bits */
#define ADC3XXX_INST_MODE_CTRL			ADC3XXX_REG(0, 62)

/* 63-79 Reserved */

/* Digital microphone polarity control */
#define ADC3XXX_MIC_POLARITY_CTRL		ADC3XXX_REG(0, 80)
/* ADC Digital */
#define ADC3XXX_ADC_DIGITAL			ADC3XXX_REG(0, 81)
/* ADC Fine Gain Adjust */
#define	ADC3XXX_ADC_FGA				ADC3XXX_REG(0, 82)
/* Left ADC Channel Volume Control */
#define ADC3XXX_LADC_VOL			ADC3XXX_REG(0, 83)
/* Right ADC Channel Volume Control */
#define ADC3XXX_RADC_VOL			ADC3XXX_REG(0, 84)
/* ADC phase compensation */
#define ADC3XXX_ADC_PHASE_COMP			ADC3XXX_REG(0, 85)
/* Left Channel AGC Control Register 1 */
#define ADC3XXX_LEFT_CHN_AGC_1			ADC3XXX_REG(0, 86)
/* Left Channel AGC Control Register 2 */
#define ADC3XXX_LEFT_CHN_AGC_2			ADC3XXX_REG(0, 87)
/* Left Channel AGC Control Register 3 */
#define ADC3XXX_LEFT_CHN_AGC_3			ADC3XXX_REG(0, 88)
/* Left Channel AGC Control Register 4 */
#define ADC3XXX_LEFT_CHN_AGC_4			ADC3XXX_REG(0, 89)
/* Left Channel AGC Control Register 5 */
#define ADC3XXX_LEFT_CHN_AGC_5			ADC3XXX_REG(0, 90)
/* Left Channel AGC Control Register 6 */
#define ADC3XXX_LEFT_CHN_AGC_6			ADC3XXX_REG(0, 91)
/* Left Channel AGC Control Register 7 */
#define ADC3XXX_LEFT_CHN_AGC_7			ADC3XXX_REG(0, 92)
/* Left AGC gain */
#define ADC3XXX_LEFT_AGC_GAIN			ADC3XXX_REG(0, 93)
/* Right Channel AGC Control Register 1 */
#define ADC3XXX_RIGHT_CHN_AGC_1			ADC3XXX_REG(0, 94)
/* Right Channel AGC Control Register 2 */
#define ADC3XXX_RIGHT_CHN_AGC_2			ADC3XXX_REG(0, 95)
/* Right Channel AGC Control Register 3 */
#define ADC3XXX_RIGHT_CHN_AGC_3			ADC3XXX_REG(0, 96)
/* Right Channel AGC Control Register 4 */
#define ADC3XXX_RIGHT_CHN_AGC_4			ADC3XXX_REG(0, 97)
/* Right Channel AGC Control Register 5 */
#define ADC3XXX_RIGHT_CHN_AGC_5			ADC3XXX_REG(0, 98)
/* Right Channel AGC Control Register 6 */
#define ADC3XXX_RIGHT_CHN_AGC_6			ADC3XXX_REG(0, 99)
/* Right Channel AGC Control Register 7 */
#define ADC3XXX_RIGHT_CHN_AGC_7			ADC3XXX_REG(0, 100)
/* Right AGC gain */
#define ADC3XXX_RIGHT_AGC_GAIN			ADC3XXX_REG(0, 101)

/* 102-127 Reserved */

/****************************************************************************/
/*			Page 1 Registers				    */
/****************************************************************************/
/* 1-25 Reserved */

/* Dither control */
#define ADC3XXX_DITHER_CTRL			ADC3XXX_REG(1, 26)

/* 27-50 Reserved */

/* MICBIAS Configuration Register */
#define ADC3XXX_MICBIAS_CTRL			ADC3XXX_REG(1, 51)
/* Left ADC input selection for Left PGA */
#define ADC3XXX_LEFT_PGA_SEL_1			ADC3XXX_REG(1, 52)

/* 53 Reserved */

/* Right ADC input selection for Left PGA */
#define ADC3XXX_LEFT_PGA_SEL_2			ADC3XXX_REG(1, 54)
/* Right ADC input selection for right PGA */
#define ADC3XXX_RIGHT_PGA_SEL_1			ADC3XXX_REG(1, 55)

/* 56 Reserved */

/* Right ADC input selection for right PGA */
#define ADC3XXX_RIGHT_PGA_SEL_2			ADC3XXX_REG(1, 57)

/* 58 Reserved */

/* Left analog PGA settings */
#define ADC3XXX_LEFT_APGA_CTRL			ADC3XXX_REG(1, 59)
/* Right analog PGA settings */
#define ADC3XXX_RIGHT_APGA_CTRL			ADC3XXX_REG(1, 60)
/* ADC Low current Modes */
#define ADC3XXX_LOW_CURRENT_MODES		ADC3XXX_REG(1, 61)
/* ADC analog PGA flags */
#define ADC3XXX_ANALOG_PGA_FLAGS		ADC3XXX_REG(1, 62)

/* 63-127 Reserved */

/****************************************************************************/
/*			Macros and definitions				    */
/****************************************************************************/

#define ADC3XXX_RATES		SNDRV_PCM_RATE_8000_96000
#define ADC3XXX_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | \
				 SNDRV_PCM_FMTBIT_S20_3LE | \
				 SNDRV_PCM_FMTBIT_S24_3LE | \
				 SNDRV_PCM_FMTBIT_S32_LE)

/****************************************************************************/
/*			ADC3XXX Register bits				    */
/****************************************************************************/
/* PLL Enable bits */
#define ADC3XXX_ENABLE_PLL_SHIFT	7
#define ADC3XXX_ENABLE_PLL		(1 << ADC3XXX_ENABLE_PLL_SHIFT)
#define ADC3XXX_ENABLE_NADC_SHIFT	7
#define ADC3XXX_ENABLE_NADC		(1 << ADC3XXX_ENABLE_NADC_SHIFT)
#define ADC3XXX_ENABLE_MADC_SHIFT	7
#define ADC3XXX_ENABLE_MADC		(1 << ADC3XXX_ENABLE_MADC_SHIFT)
#define ADC3XXX_ENABLE_BCLK_SHIFT	7
#define ADC3XXX_ENABLE_BCLK		(1 << ADC3XXX_ENABLE_BCLK_SHIFT)

/* Power bits */
#define ADC3XXX_LADC_PWR_ON		0x80
#define ADC3XXX_RADC_PWR_ON		0x40

#define ADC3XXX_SOFT_RESET		0x01
#define ADC3XXX_BCLK_MASTER		0x08
#define ADC3XXX_WCLK_MASTER		0x04

/* Interface register masks */
#define ADC3XXX_FORMAT_MASK		0xc0
#define ADC3XXX_FORMAT_SHIFT		6
#define ADC3XXX_WLENGTH_MASK		0x30
#define ADC3XXX_WLENGTH_SHIFT		4
#define ADC3XXX_CLKDIR_MASK		0x0c
#define ADC3XXX_CLKDIR_SHIFT		2

/* Interface register bit patterns */
#define ADC3XXX_FORMAT_I2S		(0 << ADC3XXX_FORMAT_SHIFT)
#define ADC3XXX_FORMAT_DSP		(1 << ADC3XXX_FORMAT_SHIFT)
#define ADC3XXX_FORMAT_RJF		(2 << ADC3XXX_FORMAT_SHIFT)
#define ADC3XXX_FORMAT_LJF		(3 << ADC3XXX_FORMAT_SHIFT)

#define ADC3XXX_IFACE_16BITS		(0 << ADC3XXX_WLENGTH_SHIFT)
#define ADC3XXX_IFACE_20BITS		(1 << ADC3XXX_WLENGTH_SHIFT)
#define ADC3XXX_IFACE_24BITS		(2 << ADC3XXX_WLENGTH_SHIFT)
#define ADC3XXX_IFACE_32BITS		(3 << ADC3XXX_WLENGTH_SHIFT)

/* PLL P/R bit offsets */
#define ADC3XXX_PLLP_SHIFT		4
#define ADC3XXX_PLLR_SHIFT		0
#define ADC3XXX_PLL_PR_MASK		0x7f
#define ADC3XXX_PLLJ_MASK		0x3f
#define ADC3XXX_PLLD_MSB_MASK		0x3f
#define ADC3XXX_PLLD_LSB_MASK		0xff
#define ADC3XXX_NADC_MASK		0x7f
#define ADC3XXX_MADC_MASK		0x7f
#define ADC3XXX_AOSR_MASK		0xff
#define ADC3XXX_IADC_MASK		0xff
#define ADC3XXX_BDIV_MASK		0x7f

/* PLL_CLKIN bits */
#define ADC3XXX_PLL_CLKIN_SHIFT		2
#define ADC3XXX_PLL_CLKIN_MCLK		0x0
#define ADC3XXX_PLL_CLKIN_BCLK		0x1
#define ADC3XXX_PLL_CLKIN_ZERO		0x3

/* CODEC_CLKIN bits */
#define ADC3XXX_CODEC_CLKIN_SHIFT	0
#define ADC3XXX_CODEC_CLKIN_MCLK	0x0
#define ADC3XXX_CODEC_CLKIN_BCLK	0x1
#define ADC3XXX_CODEC_CLKIN_PLL_CLK	0x3

#define ADC3XXX_USE_PLL	((ADC3XXX_PLL_CLKIN_MCLK << ADC3XXX_PLL_CLKIN_SHIFT) | \
			 (ADC3XXX_CODEC_CLKIN_PLL_CLK << ADC3XXX_CODEC_CLKIN_SHIFT))
#define ADC3XXX_NO_PLL	((ADC3XXX_PLL_CLKIN_ZERO << ADC3XXX_PLL_CLKIN_SHIFT) | \
			 (ADC3XXX_CODEC_CLKIN_MCLK << ADC3XXX_CODEC_CLKIN_SHIFT))

/*  Analog PGA control bits */
#define ADC3XXX_LPGA_MUTE		0x80
#define ADC3XXX_RPGA_MUTE		0x80

#define ADC3XXX_LPGA_GAIN_MASK		0x7f
#define ADC3XXX_RPGA_GAIN_MASK		0x7f

/* ADC current modes */
#define ADC3XXX_ADC_LOW_CURR_MODE	0x01

/* Left ADC Input selection bits */
#define ADC3XXX_LCH_SEL1_SHIFT		0
#define ADC3XXX_LCH_SEL2_SHIFT		2
#define ADC3XXX_LCH_SEL3_SHIFT		4
#define ADC3XXX_LCH_SEL4_SHIFT		6

#define ADC3XXX_LCH_SEL1X_SHIFT		0
#define ADC3XXX_LCH_SEL2X_SHIFT		2
#define ADC3XXX_LCH_SEL3X_SHIFT		4
#define ADC3XXX_LCH_COMMON_MODE		0x40
#define ADC3XXX_BYPASS_LPGA		0x80

/* Right ADC Input selection bits */
#define ADC3XXX_RCH_SEL1_SHIFT		0
#define ADC3XXX_RCH_SEL2_SHIFT		2
#define ADC3XXX_RCH_SEL3_SHIFT		4
#define ADC3XXX_RCH_SEL4_SHIFT		6

#define ADC3XXX_RCH_SEL1X_SHIFT		0
#define ADC3XXX_RCH_SEL2X_SHIFT		2
#define ADC3XXX_RCH_SEL3X_SHIFT		4
#define ADC3XXX_RCH_COMMON_MODE		0x40
#define ADC3XXX_BYPASS_RPGA		0x80

/* MICBIAS control bits */
#define ADC3XXX_MICBIAS_MASK		0x2
#define ADC3XXX_MICBIAS1_SHIFT		5
#define ADC3XXX_MICBIAS2_SHIFT		3

#define ADC3XXX_ADC_MAX_VOLUME		64
#define ADC3XXX_ADC_POS_VOL		24

/* GPIO control bits (GPIO1_CTRL and GPIO2_CTRL) */
#define ADC3XXX_GPIO_CTRL_CFG_MASK		0x3c
#define ADC3XXX_GPIO_CTRL_CFG_SHIFT		2
#define ADC3XXX_GPIO_CTRL_OUTPUT_CTRL_MASK	0x01
#define ADC3XXX_GPIO_CTRL_OUTPUT_CTRL_SHIFT	0
#define ADC3XXX_GPIO_CTRL_INPUT_VALUE_MASK	0x02
#define ADC3XXX_GPIO_CTRL_INPUT_VALUE_SHIFT	1

#endif /* _ADC3XXX_H */
