/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) Sunplus Technology Co., Ltd.
 *       All rights reserved.
 *
 *
 * This header provides constants for the SP7021 INTC
 */

#ifndef _DT_BINDINGS_INTERRUPT_CONTROLLER_SP7021_INTC_H
#define _DT_BINDINGS_INTERRUPT_CONTROLLER_SP7021_INTC_H

#include <dt-bindings/interrupt-controller/irq.h>

/*
 * Interrupt specifier cell 1.
 * The flags in irq.h are valid, plus those below.
 */
#define SP_INTC_EXT_INT0		0x00000
#define SP_INTC_EXT_INT1		0x01000
#define SP_INTC_EXT_INT_MASK	0xff000
#define SP_INTC_EXT_INT_SHFIT	12

#endif
