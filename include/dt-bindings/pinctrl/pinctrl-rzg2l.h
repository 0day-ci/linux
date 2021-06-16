/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for Renesas RZ/G2{L,LC} pinctrl bindings.
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 */

#ifndef __DT_BINDINGS_PINCTRL_RZG2L_H
#define __DT_BINDINGS_PINCTRL_RZG2L_H

#define RZG2L_PINS_PER_PORT	8

#define RZG2L_GPIO(port, pos)	((port) * RZG2L_PINS_PER_PORT + (pos))

#endif /* __DT_BINDINGS_PINCTRL_RZG2L_H */
