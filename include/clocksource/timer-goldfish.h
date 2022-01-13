/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * goldfish-timer clocksource
 */

#ifndef _CLOCKSOURCE_GOLDFISH_TIMER_H
#define _CLOCKSOURCE_GOLDFISH_TIMER_H

extern void goldfish_timer_init(int irq, void __iomem *base);

#endif /* _CLOCKSOURCE_GOLDFISH_TIMER_H */

