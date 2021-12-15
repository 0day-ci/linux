/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright(c) 2021 Intel Corporation.
 */

#ifndef HID_USI_H_
#define HID_USI_H_

#include <linux/bits.h>

enum {
	USI_PEN_ID = 0,
	USI_PEN_IN_RANGE,
	USI_PEN_TOUCHING,
	USI_PEN_COLOR,
	USI_PEN_LINE_WIDTH,
	USI_PEN_LINE_STYLE,
	USI_NUM_PARAMS
};

#endif /* HID_USI_H */
