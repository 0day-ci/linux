/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Sipeed
 */

#ifndef _CCU_SUN50I_R329_H_
#define _CCU_SUN50I_R329_H_

#include <dt-bindings/clock/sun50i-r329-ccu.h>
#include <dt-bindings/reset/sun50i-r329-ccu.h>

#define CLK_OSC12M		0

/* CPUX exported for DVFS */

#define CLK_AXI			2
#define CLK_CPUX_APB		3
#define CLK_AHB			4

/* APB1 exported for PIO */

#define CLK_APB2		6

/* Peripheral module and gate clock exported except for DRAM ones */

#define CLK_DRAM		18

#define CLK_BUS_DRAM		24

#define CLK_NUMBER		(CLK_BUS_LEDC + 1)

#endif /* _CCU_SUN50I_R329_H_ */
