/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Nintendo Wii U "Latte" interrupt controller support
 *
 * Copyright (C) 2022 The linux-wiiu Team
 */

#ifndef __LATTE_PIC_H
#define __LATTE_PIC_H

struct lt_pic {
	__be32 ahball_icr;	/* Triggered AHB IRQs (all) */
	__be32 ahblt_icr;	/* Triggered AHB IRQs (latte only) */
	__be32 ahball_imr;	/* Allowed AHB IRQs (all) */
	__be32 ahblt_imr;	/* Allowed AHB IRQs (latte only) */
} __packed;

#define LATTE_AHBALL_NR_IRQS    32
#define LATTE_AHBLT_NR_IRQS     32

void latte_pic_init(void);

#endif
