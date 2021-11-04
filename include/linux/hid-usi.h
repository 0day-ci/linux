// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021, Intel Corporation
 * Authors: Tero Kristo <tero.kristo@linux.intel.com>
 */

#ifndef __HID_USI_H
#define __HID_USI_H

#include <linux/hid.h>
#include <linux/types.h>

struct usi_pen_info {
	__s32 index;
	__u32 code;
	__s32 value;
};

#define USIIOCGET _IOR('H', 0xf2, struct usi_pen_info)
#define USIIOCSET _IOW('H', 0xf3, struct usi_pen_info)

#endif
