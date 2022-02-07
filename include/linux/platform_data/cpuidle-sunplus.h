/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CPUIDLE_SP7021_H
#define __CPUIDLE_SP7021_H

struct cpuidle_sp7021_data {
	int (*cpu23_powerdown)(void);
	void (*pre_enter_aftr)(void);
	void (*post_enter_aftr)(void);
};

extern int cpu_v7_do_idle(void);

#endif
