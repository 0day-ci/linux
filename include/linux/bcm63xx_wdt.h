/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __BCM63XX_WDT_H
#define __BCM63XX_WDT_H

/* Watchdog default count register */
#define WDT_DEFVAL_REG			0x0

/* Watchdog control register */
#define WDT_CTL_REG			0x4

/* Watchdog control register constants */
#define WDT_START_1			(0xff00)
#define WDT_START_2			(0x00ff)
#define WDT_STOP_1			(0xee00)
#define WDT_STOP_2			(0x00ee)

/* Watchdog reset length register */
#define WDT_RSTLEN_REG			0x8

/* Watchdog soft reset register (BCM6328 only) */
#define WDT_SOFTRESET_REG		0xc

#endif /* __BCM63XX_WDT_H */
