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
 * 2021/09/03: Porting to Linux 5.4, with enhancements (see tlv320adc3xxx.c).
 * Copyright (C) 2021 Axis Communications AB
 *
 */

#ifndef _ADC3xxx_H
#define _ADC3xxx_H

/* 8 bit mask value */
#define ADC3xxx_8BITS_MASK           0xFF

/* Enable slave / master mode for codec */
#define ADC3xxx_MCBSP_SLAVE //codec master
//#undef ADC3xxx_MCBSP_SLAVE

#define ADC3xxx_PAGE_SIZE		128
#define ADC3xxx_REG(page, reg)		((page * ADC3xxx_PAGE_SIZE) + reg)


/****************************************************************************/
/*			Page 0 Registers				    */
/****************************************************************************/

/* Page select register */
#define PAGE_SELECT			ADC3xxx_REG(0, 0)
/* Software reset register */
#define RESET				ADC3xxx_REG(0, 1)

/* 2-3 Reserved */

/* PLL programming register B */
#define CLKGEN_MUX			ADC3xxx_REG(0, 4)
/* PLL P and R-Val */
#define PLL_PROG_PR			ADC3xxx_REG(0, 5)
/* PLL J-Val */
#define PLL_PROG_J			ADC3xxx_REG(0, 6)
/* PLL D-Val MSB */
#define PLL_PROG_D_MSB			ADC3xxx_REG(0, 7)
/* PLL D-Val LSB */
#define PLL_PROG_D_LSB			ADC3xxx_REG(0, 8)

/* 9-17 Reserved */

/* ADC NADC */
#define ADC_NADC			ADC3xxx_REG(0, 18)
/* ADC MADC */
#define ADC_MADC			ADC3xxx_REG(0, 19)
/* ADC AOSR */
#define ADC_AOSR			ADC3xxx_REG(0, 20)
/* ADC IADC */
#define ADC_IADC			ADC3xxx_REG(0, 21)

/* 23-24 Reserved */

/* CLKOUT MUX */
#define CLKOUT_MUX			ADC3xxx_REG(0, 25)
/* CLOCKOUT M divider value */
#define CLKOUT_M_DIV			ADC3xxx_REG(0, 26)
/*Audio Interface Setting Register 1*/
#define INTERFACE_CTRL_1		ADC3xxx_REG(0, 27)
/* Data Slot Offset (Ch_Offset_1) */
#define CH_OFFSET_1			ADC3xxx_REG(0, 28)
/* ADC interface control 2 */
#define INTERFACE_CTRL_2		ADC3xxx_REG(0, 29)
/* BCLK N Divider */
#define BCLK_N_DIV			ADC3xxx_REG(0, 30)
/* Secondary audio interface control 1 */
#define INTERFACE_CTRL_3		ADC3xxx_REG(0, 31)
/* Secondary audio interface control 2 */
#define INTERFACE_CTRL_4		ADC3xxx_REG(0, 32)
/* Secondary audio interface control 3 */
#define INTERFACE_CTRL_5		ADC3xxx_REG(0, 33)
/* I2S sync */
#define I2S_SYNC			ADC3xxx_REG(0, 34)

/* 35 Reserved */

/* ADC flag register */
#define ADC_FLAG			ADC3xxx_REG(0, 36)
/* Data slot offset 2 (Ch_Offset_2) */
#define CH_OFFSET_2			ADC3xxx_REG(0, 37)
/* I2S TDM control register */
#define I2S_TDM_CTRL			ADC3xxx_REG(0, 38)

/* 39-41 Reserved */

/* Interrupt flags (overflow) */
#define INTR_FLAG_1			ADC3xxx_REG(0, 42)
/* Interrupt flags (overflow) */
#define INTR_FLAG_2			ADC3xxx_REG(0, 43)

/* 44 Reserved */

/* Interrupt flags ADC */
#define INTR_FLAG_ADC1			ADC3xxx_REG(0, 45)

/* 46 Reserved */

/* Interrupt flags ADC */
#define INTR_FLAG_ADC2			ADC3xxx_REG(0, 47)
/* INT1 interrupt control */
#define INT1_CTRL			ADC3xxx_REG(0, 48)
/* INT2 interrupt control */
#define INT2_CTRL			ADC3xxx_REG(0, 49)

/* 50 Reserved */

/* DMCLK/GPIO2 control */
#define GPIO2_CTRL			ADC3xxx_REG(0, 51)
/* DMDIN/GPIO1 control */
#define GPIO1_CTRL			ADC3xxx_REG(0, 52)
/* DOUT Control */
#define DOUT_CTRL			ADC3xxx_REG(0, 53)

/* 54-56 Reserved */

/* ADC sync control 1 */
#define SYNC_CTRL_1			ADC3xxx_REG(0, 57)
/* ADC sync control 2 */
#define SYNC_CTRL_2			ADC3xxx_REG(0, 58)
/* ADC CIC filter gain control */
#define CIC_GAIN_CTRL			ADC3xxx_REG(0, 59)

/* 60 Reserved */

/* ADC processing block selection  */
#define PRB_SELECT			ADC3xxx_REG(0, 61)
/* Programmable instruction mode control bits */
#define INST_MODE_CTRL			ADC3xxx_REG(0, 62)

/* 63-79 Reserved */

/* Digital microphone polarity control */
#define MIC_POLARITY_CTRL		ADC3xxx_REG(0, 80)
/* ADC Digital */
#define ADC_DIGITAL			ADC3xxx_REG(0, 81)
/* ADC Fine Gain Adjust */
#define	ADC_FGA				ADC3xxx_REG(0, 82)
/* Left ADC Channel Volume Control */
#define LADC_VOL			ADC3xxx_REG(0, 83)
/* Right ADC Channel Volume Control */
#define RADC_VOL			ADC3xxx_REG(0, 84)
/* ADC phase compensation */
#define ADC_PHASE_COMP			ADC3xxx_REG(0, 85)
/* Left Channel AGC Control Register 1 */
#define LEFT_CHN_AGC_1			ADC3xxx_REG(0, 86)
/* Left Channel AGC Control Register 2 */
#define LEFT_CHN_AGC_2			ADC3xxx_REG(0, 87)
/* Left Channel AGC Control Register 3 */
#define LEFT_CHN_AGC_3			ADC3xxx_REG(0, 88)
/* Left Channel AGC Control Register 4 */
#define LEFT_CHN_AGC_4			ADC3xxx_REG(0, 89)
/* Left Channel AGC Control Register 5 */
#define LEFT_CHN_AGC_5			ADC3xxx_REG(0, 90)
/* Left Channel AGC Control Register 6 */
#define LEFT_CHN_AGC_6			ADC3xxx_REG(0, 91)
/* Left Channel AGC Control Register 7 */
#define LEFT_CHN_AGC_7			ADC3xxx_REG(0, 92)
/* Left AGC gain */
#define LEFT_AGC_GAIN			ADC3xxx_REG(0, 93)
/* Right Channel AGC Control Register 1 */
#define RIGHT_CHN_AGC_1			ADC3xxx_REG(0, 94)
/* Right Channel AGC Control Register 2 */
#define RIGHT_CHN_AGC_2			ADC3xxx_REG(0, 95)
/* Right Channel AGC Control Register 3 */
#define RIGHT_CHN_AGC_3			ADC3xxx_REG(0, 96)
/* Right Channel AGC Control Register 4 */
#define RIGHT_CHN_AGC_4			ADC3xxx_REG(0, 97)
/* Right Channel AGC Control Register 5 */
#define RIGHT_CHN_AGC_5			ADC3xxx_REG(0, 98)
/* Right Channel AGC Control Register 6 */
#define RIGHT_CHN_AGC_6			ADC3xxx_REG(0, 99)
/* Right Channel AGC Control Register 7 */
#define RIGHT_CHN_AGC_7			ADC3xxx_REG(0, 100)
/* Right AGC gain */
#define RIGHT_AGC_GAIN			ADC3xxx_REG(0, 101)

/* 102-127 Reserved */

/****************************************************************************/
/*			Page 1 Registers				    */
/****************************************************************************/
/* 1-25 Reserved */

/* Dither control */
#define DITHER_CTRL			ADC3xxx_REG(1, 26)

/* 27-50 Reserved */

/* MICBIAS Configuration Register */
#define MICBIAS_CTRL			ADC3xxx_REG(1, 51)
/* Left ADC input selection for Left PGA */
#define LEFT_PGA_SEL_1			ADC3xxx_REG(1, 52)

/* 53 Reserved */

/* Right ADC input selection for Left PGA */
#define LEFT_PGA_SEL_2			ADC3xxx_REG(1, 54)
/* Right ADC input selection for right PGA */
#define RIGHT_PGA_SEL_1			ADC3xxx_REG(1, 55)

/* 56 Reserved */

/* Right ADC input selection for right PGA */
#define RIGHT_PGA_SEL_2			ADC3xxx_REG(1, 57)

/* 58 Reserved */

/* Left analog PGA settings */
#define LEFT_APGA_CTRL			ADC3xxx_REG(1, 59)
/* Right analog PGA settings */
#define RIGHT_APGA_CTRL			ADC3xxx_REG(1, 60)
/* ADC Low current Modes */
#define LOW_CURRENT_MODES		ADC3xxx_REG(1, 61)
/* ADC analog PGA flags */
#define ANALOG_PGA_FLAGS		ADC3xxx_REG(1, 62)

/* 63-127 Reserved */

/****************************************************************************/
/*			Macros and definitions				    */
/****************************************************************************/

#define ADC3xxx_RATES	SNDRV_PCM_RATE_8000_96000
#define ADC3xxx_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

/* bits defined for easy usage */
#define D7			(0x01 << 7)
#define D6			(0x01 << 6)
#define D5			(0x01 << 5)
#define D4			(0x01 << 4)
#define D3			(0x01 << 3)
#define D2			(0x01 << 2)
#define D1			(0x01 << 1)
#define D0			(0x01 << 0)

/****************************************************************************/
/*			ADC3xxx Register bits				    */
/****************************************************************************/
/* PLL Enable bits */
#define ENABLE_PLL              D7
#define ENABLE_NADC             D7
#define ENABLE_MADC             D7
#define ENABLE_BCLK             D7

/* Power bits */
#define LADC_PWR_ON		D7
#define RADC_PWR_ON		D6

#define SOFT_RESET              D0
#define BCLK_MASTER		D3
#define WCLK_MASTER		D2

/* Interface register masks */
#define FORMAT_MASK		(D7|D6)
#define FORMAT_SHIFT		6
#define WLENGTH_MASK		(D5|D4)
#define WLENGTH_SHIFT		4
#define CLKDIR_MASK		(D3|D2)
#define CLKDIR_SHIFT		2

/* Interface register bit patterns */
#define FORMAT_I2S		(0 << FORMAT_SHIFT)
#define FORMAT_DSP		(1 << FORMAT_SHIFT)
#define FORMAT_RJF		(2 << FORMAT_SHIFT)
#define FORMAT_LJF		(3 << FORMAT_SHIFT)

#define IFACE_16BITS		(0 << WLENGTH_SHIFT)
#define IFACE_20BITS		(1 << WLENGTH_SHIFT)
#define IFACE_24BITS		(2 << WLENGTH_SHIFT)
#define IFACE_32BITS		(3 << WLENGTH_SHIFT)

/* PLL P/R bit offsets */
#define PLLP_SHIFT		4
#define PLLR_SHIFT		0
#define PLL_PR_MASK		0x7F
#define PLLJ_MASK		0x3F
#define PLLD_MSB_MASK		0x3F
#define PLLD_LSB_MASK		0xFF
#define NADC_MASK		0x7F
#define MADC_MASK		0x7F
#define AOSR_MASK		0xFF
#define IADC_MASK		0xFF
#define BDIV_MASK		0x7F

/* PLL_CLKIN bits */
#define PLL_CLKIN_SHIFT		2
#define PLL_CLKIN_MCLK		0x0
#define PLL_CLKIN_BCLK		0x1
#define PLL_CLKIN_ZERO		0x3

/* CODEC_CLKIN bits */
#define CODEC_CLKIN_SHIFT	0
#define CODEC_CLKIN_MCLK	0x0
#define CODEC_CLKIN_BCLK	0x1
#define CODEC_CLKIN_PLL_CLK	0x3

#define USE_PLL		((PLL_CLKIN_MCLK << PLL_CLKIN_SHIFT) | \
			 (CODEC_CLKIN_PLL_CLK << CODEC_CLKIN_SHIFT))
#define NO_PLL		((PLL_CLKIN_ZERO << PLL_CLKIN_SHIFT) | \
			 (CODEC_CLKIN_MCLK << CODEC_CLKIN_SHIFT))

/*  Analog PGA control bits */
#define LPGA_MUTE		D7
#define RPGA_MUTE		D7

#define LPGA_GAIN_MASK		0x7F
#define RPGA_GAIN_MASK		0x7F

/* ADC current modes */
#define ADC_LOW_CURR_MODE	D0

/* Left ADC Input selection bits */
#define LCH_SEL1_SHIFT		0
#define LCH_SEL2_SHIFT		2
#define LCH_SEL3_SHIFT		4
#define LCH_SEL4_SHIFT		6

#define LCH_SEL1X_SHIFT		0
#define LCH_SEL2X_SHIFT		2
#define LCH_SEL3X_SHIFT		4
#define LCH_COMMON_MODE		D6
#define BYPASS_LPGA		D7

/* Right ADC Input selection bits */
#define RCH_SEL1_SHIFT		0
#define RCH_SEL2_SHIFT		2
#define RCH_SEL3_SHIFT		4
#define RCH_SEL4_SHIFT		6

#define RCH_SEL1X_SHIFT		0
#define RCH_SEL2X_SHIFT		2
#define RCH_SEL3X_SHIFT		4
#define RCH_COMMON_MODE		D6
#define BYPASS_RPGA		D7

/* MICBIAS control bits */
#define MICBIAS1_SHIFT		5
#define MICBIAS2_SHIFT		3

#define ADC_MAX_VOLUME		64
#define ADC_POS_VOL		24

/* GPIO control bits (GPIO1_CTRL and GPIO2_CTRL) */
#define GPIO_CTRL_CFG_MASK		(D2 | D3 | D4 | D5)
#define GPIO_CTRL_CFG_SHIFT		2
#define GPIO_CTRL_OUTPUT_CTRL		D0
#define GPIO_CTRL_OUTPUT_CTRL_SHIFT	0
#define GPIO_CTRL_INPUT_VALUE		D1
#define GPIO_CTRL_INPUT_VALUE_SHIFT	1

/****************** RATES TABLE FOR ADC3xxx ************************/
struct adc3xxx_rate_divs {
	u32 mclk;
	u32 rate;
	u8 pll_p;
	u8 pll_r;
	u8 pll_j;
	u16 pll_d;
	u8 nadc;
	u8 madc;
	u8 aosr;
};

#endif /* _ADC3xxx_H */
