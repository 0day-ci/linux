// SPDX-License-Identifier: GPL-2.0
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
 */

#include "sppctl_gpio.h"

const char * const sppctlgpio_list_s[] = {
	D_PIS(0, 0),  D_PIS(0, 1),  D_PIS(0, 2),  D_PIS(0, 3),
	D_PIS(0, 4),  D_PIS(0, 5),  D_PIS(0, 6),  D_PIS(0, 7),
	D_PIS(1, 0),  D_PIS(1, 1),  D_PIS(1, 2),  D_PIS(1, 3),
	D_PIS(1, 4),  D_PIS(1, 5),  D_PIS(1, 6),  D_PIS(1, 7),
	D_PIS(2, 0),  D_PIS(2, 1),  D_PIS(2, 2),  D_PIS(2, 3),
	D_PIS(2, 4),  D_PIS(2, 5),  D_PIS(2, 6),  D_PIS(2, 7),
	D_PIS(3, 0),  D_PIS(3, 1),  D_PIS(3, 2),  D_PIS(3, 3),
	D_PIS(3, 4),  D_PIS(3, 5),  D_PIS(3, 6),  D_PIS(3, 7),
	D_PIS(4, 0),  D_PIS(4, 1),  D_PIS(4, 2),  D_PIS(4, 3),
	D_PIS(4, 4),  D_PIS(4, 5),  D_PIS(4, 6),  D_PIS(4, 7),
	D_PIS(5, 0),  D_PIS(5, 1),  D_PIS(5, 2),  D_PIS(5, 3),
	D_PIS(5, 4),  D_PIS(5, 5),  D_PIS(5, 6),  D_PIS(5, 7),
	D_PIS(6, 0),  D_PIS(6, 1),  D_PIS(6, 2),  D_PIS(6, 3),
	D_PIS(6, 4),  D_PIS(6, 5),  D_PIS(6, 6),  D_PIS(6, 7),
	D_PIS(7, 0),  D_PIS(7, 1),  D_PIS(7, 2),  D_PIS(7, 3),
	D_PIS(7, 4),  D_PIS(7, 5),  D_PIS(7, 6),  D_PIS(7, 7),
	D_PIS(8, 0),  D_PIS(8, 1),  D_PIS(8, 2),  D_PIS(8, 3),
	D_PIS(8, 4),  D_PIS(8, 5),  D_PIS(8, 6),  D_PIS(8, 7),
	D_PIS(9, 0),  D_PIS(9, 1),  D_PIS(9, 2),  D_PIS(9, 3),
	D_PIS(9, 4),  D_PIS(9, 5),  D_PIS(9, 6),  D_PIS(9, 7),
	D_PIS(10, 0), D_PIS(10, 1), D_PIS(10, 2), D_PIS(10, 3),
	D_PIS(10, 4), D_PIS(10, 5), D_PIS(10, 6), D_PIS(10, 7),
	D_PIS(11, 0), D_PIS(11, 1), D_PIS(11, 2), D_PIS(11, 3),
	D_PIS(11, 4), D_PIS(11, 5), D_PIS(11, 6), D_PIS(11, 7),
	D_PIS(12, 0), D_PIS(12, 1), D_PIS(12, 2)
};

const size_t GPIS_listSZ = sizeof(sppctlgpio_list_s)/sizeof(*(sppctlgpio_list_s));
