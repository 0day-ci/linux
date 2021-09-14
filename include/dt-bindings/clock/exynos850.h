/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Linaro Ltd.
 * Author: Sam Protsenko <semen.protsenko@linaro.org>
 *
 * Device Tree binding constants for Exynos850 clock controller.
 */

#ifndef _DT_BINDINGS_CLOCK_EXYNOS_850_H
#define _DT_BINDINGS_CLOCK_EXYNOS_850_H

/* CMU_TOP */
#define DOUT_HSI_BUS			1
#define DOUT_HSI_MMC_CARD		2
#define DOUT_HSI_USB20DRD		3
#define DOUT_PERI_BUS			4
#define DOUT_PERI_UART			5
#define DOUT_PERI_IP			6
#define DOUT_CORE_BUS			7
#define DOUT_CORE_CCI			8
#define DOUT_CORE_MMC_EMBD		9
#define DOUT_CORE_SSS			10
#define TOP_NR_CLK			11

/* CMU_HSI */
#define GOUT_USB_RTC_CLK		1
#define GOUT_USB_REF_CLK		2
#define GOUT_USB_PHY_REF_CLK		3
#define GOUT_USB_PHY_ACLK		4
#define GOUT_USB_BUS_EARLY_CLK		5
#define GOUT_GPIO_HSI_PCLK		6
#define GOUT_MMC_CARD_ACLK		7
#define GOUT_MMC_CARD_SDCLKIN		8
#define GOUT_SYSREG_HSI_PCLK		9
#define HSI_NR_CLK			10

/* CMU_PERI */
#define GOUT_GPIO_PERI_PCLK		1
#define GOUT_HSI2C0_IPCLK		2
#define GOUT_HSI2C0_PCLK		3
#define GOUT_HSI2C1_IPCLK		4
#define GOUT_HSI2C1_PCLK		5
#define GOUT_HSI2C2_IPCLK		6
#define GOUT_HSI2C2_PCLK		7
#define GOUT_I2C0_PCLK			8
#define GOUT_I2C1_PCLK			9
#define GOUT_I2C2_PCLK			10
#define GOUT_I2C3_PCLK			11
#define GOUT_I2C4_PCLK			12
#define GOUT_I2C5_PCLK			13
#define GOUT_I2C6_PCLK			14
#define GOUT_MCT_PCLK			15
#define GOUT_PWM_MOTOR_PCLK		16
#define GOUT_SPI0_IPCLK			17
#define GOUT_SPI0_PCLK			18
#define GOUT_SYSREG_PERI_PCLK		19
#define GOUT_UART_IPCLK			20
#define GOUT_UART_PCLK			21
#define GOUT_WDT0_PCLK			22
#define GOUT_WDT1_PCLK			23
#define PERI_NR_CLK			24

/* CMU_CORE */
#define GOUT_CCI_ACLK			1
#define GOUT_GIC_CLK			2
#define GOUT_MMC_EMBD_ACLK		3
#define GOUT_MMC_EMBD_SDCLKIN		4
#define GOUT_SSS_ACLK			5
#define GOUT_SSS_PCLK			6
#define CORE_NR_CLK			7

#endif /* _DT_BINDINGS_CLOCK_EXYNOS_850_H */
