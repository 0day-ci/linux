/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Sipeed
 */

#ifndef _CCU_SUN50I_R329_R_H
#define _CCU_SUN50I_R329_R_H

#include <dt-bindings/clock/sun50i-r329-r-ccu.h>
#include <dt-bindings/reset/sun50i-r329-r-ccu.h>

#define CLK_PLL_CPUX		0
#define CLK_PLL_PERIPH_BASE	1
#define CLK_PLL_PERIPH_2X	2
#define CLK_PLL_PERIPH_800M	3
#define CLK_PLL_PERIPH		4
#define CLK_PLL_AUDIO0		5
#define CLK_PLL_AUDIO0_DIV2	6
#define CLK_PLL_AUDIO0_DIV5	7
#define CLK_PLL_AUDIO1_4X	8
#define CLK_PLL_AUDIO1_2X	9
#define CLK_PLL_AUDIO1		10
#define CLK_R_AHB		11

/* R_APB1 exported for PIO */

#define CLK_R_APB2		13

/* All module / bus gate clocks exported */

#define CLK_NUMBER	(CLK_R_BUS_RTC + 1)

#endif /* _CCU_SUN50I_R329_R_H */
