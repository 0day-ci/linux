/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SP7021 pinmux pinctrl bindings.
 * Copyright (C) Sunplus Tech/Tibbo Tech. 2020
 * Author: Dvorkin Dmitry <dvorkin@tibbo.com>
 */

#ifndef _DT_BINDINGS_PINCTRL_SPPCTL_H
#define _DT_BINDINGS_PINCTRL_SPPCTL_H

#define IOP_G_MASTE             (0x01<<0)
#define IOP_G_FIRST             (0x01<<1)

#define SPPCTL_PCTL_G_PMUX      (0x00|IOP_G_MASTE)
#define SPPCTL_PCTL_G_GPIO      (IOP_G_FIRST|IOP_G_MASTE)
#define SPPCTL_PCTL_G_IOPP      (IOP_G_FIRST|0x00)

#define SPPCTL_PCTL_L_OUT       (0x01<<0)
#define SPPCTL_PCTL_L_OU1       (0x01<<1)
#define SPPCTL_PCTL_L_INV       (0x01<<2)
#define SPPCTL_PCTL_L_ONV       (0x01<<3)
#define SPPCTL_PCTL_L_ODR       (0x01<<4)

#define SPPCTL_PCTLE_P(v)       ((v)<<24)
#define SPPCTL_PCTLE_G(v)       ((v)<<16)
#define SPPCTL_PCTLE_F(v)       ((v)<<8)
#define SPPCTL_PCTLE_L(v)       ((v)<<0)

#define SPPCTL_PCTLD_P(v)       (((v)>>24) & 0xFF)
#define SPPCTL_PCTLD_G(v)       (((v)>>16) & 0xFF)
#define SPPCTL_PCTLD_F(v)       (((v) >> 8) & 0xFF)
#define SPPCTL_PCTLD_L(v)       (((v) >> 0) & 0xFF)

/*
 * pack into 32-bit value:
 * pin#{8bit}, typ{8bit}, function{8bit}, flags{8bit}
 */
#define SPPCTL_IOPAD(pin, typ, fun, fls) (((pin)<<24)|((typ)<<16)|((fun)<<8)|(fls))

#endif
