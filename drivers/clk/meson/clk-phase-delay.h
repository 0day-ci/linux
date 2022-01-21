/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

#ifndef __MESON_CLK_PHASE_DELAY_H
#define __MESON_CLK_PHASE_DELAY_H

#include <linux/clk-provider.h>
#include "parm.h"

struct meson_clk_phase_delay_data {
	struct parm     phase;
	struct parm     delay;
	unsigned int    delay_step_ps;
};

extern const struct clk_ops meson_clk_phase_delay_ops;

#endif /* __MESON_CLK_PHASE_DELAY_H */
