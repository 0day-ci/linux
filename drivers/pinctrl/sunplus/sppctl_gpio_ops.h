/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GPIO Driver for Sunplus/Tibbo SP7021 controller
 * Copyright (C) 2020 Sunplus Tech./Tibbo Tech.
 * Author: Dvorkin Dmitry <dvorkin@tibbo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef SPPCTL_GPIO_OPS_H
#define SPPCTL_GPIO_OPS_H

#include "sppctl_gpio.h"

// who is first: GPIO(1) | MUX(0)
int sppctlgpio_u_gfrst(struct gpio_chip *_c, unsigned int _n);

// who is master: GPIO(1) | IOP(0)
int sppctlgpio_u_magpi(struct gpio_chip *_c, unsigned int _n);

// set MASTER and FIRST
void sppctlgpio_u_magpi_set(struct gpio_chip *_c, unsigned int _n,
			    enum muxF_MG_t _f, enum muxM_IG_t _m);

// is inv: INVERTED(1) | NORMAL(0)
int sppctlgpio_u_isinv(struct gpio_chip *_c, unsigned int _n);
// set (I|O)inv
void sppctlgpio_u_siinv(struct gpio_chip *_c, unsigned int _n);
void sppctlgpio_u_soinv(struct gpio_chip *_c, unsigned int _n);

// is open-drain: YES(1) | NON(0)
int sppctlgpio_u_isodr(struct gpio_chip *_c, unsigned int _n);
void sppctlgpio_u_seodr(struct gpio_chip *_c, unsigned int _n, unsigned int _v);

// get dir: 0=out, 1=in, -E =err (-EINVAL for ex): OE inverted on ret
int sppctlgpio_f_gdi(struct gpio_chip *_c, unsigned int _n);

// set to input: 0:ok: OE=0
int sppctlgpio_f_sin(struct gpio_chip *_c, unsigned int _n);

// set to output: 0:ok: OE=1,O=_v
int sppctlgpio_f_sou(struct gpio_chip *_c, unsigned int _n, int _v);

// get value for signal: 0=low | 1=high | -err
int sppctlgpio_f_get(struct gpio_chip *_c, unsigned int _n);

// OUT only: can't call set on IN pin: protected by gpio_chip layer
void sppctlgpio_f_set(struct gpio_chip *_c, unsigned int _n, int _v);

// FIX: test in-depth
int sppctlgpio_f_scf(struct gpio_chip *_c, unsigned int _n, unsigned long _conf);

#ifdef CONFIG_DEBUG_FS
void sppctlgpio_f_dsh(struct seq_file *_s, struct gpio_chip *_c);
#else
#define sppctlgpio_f_dsh NULL
#endif

#ifdef CONFIG_OF_GPIO
int sppctlgpio_xlate(struct gpio_chip *_c, const struct of_phandle_args *_a,
		     u32 *_flags);
#endif

int sppctlgpio_i_map(struct gpio_chip *_c, unsigned int _off);

#endif // SPPCTL_GPIO_OPS_H
