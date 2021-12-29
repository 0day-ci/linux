// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-pxa/pxa2xx.c
 *
 * code specific to pxa2xx
 *
 * Copyright (C) 2008 Dmitry Baryshkov
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/pxa2xx-regs.h>
#include "mfp-pxa25x.h"
#include <mach/reset.h>

void pxa2xx_clear_reset_status(unsigned int mask)
{
	/* RESET_STATUS_* has a 1:1 mapping with RCSR */
	RCSR = mask;
}
