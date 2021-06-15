/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2021 Hisilicon Limited Permission is hereby granted, free of
 * charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Code herein communicates with and accesses proprietary hardware which is
 * licensed intellectual property (IP) belonging to Siemens Digital Industries
 * Software Ltd.
 *
 * Siemens Digital Industries Software Ltd. asserts and reserves all rights to
 * their intellectual property. This paragraph may not be removed or modified
 * in any way without permission from Siemens Digital Industries Software Ltd.
 */
#ifndef ULTRASOC_AXI_COM_H
#define ULTRASOC_AXI_COM_H

#include "ultrasoc.h"

#define AXIC_US_CTL 0X0 /* Upstream general control */
#define AXIC_US_DATA 0XC /* Upstream message data */
#define AXIC_US_BUF_STS 0X10 /* Upstream buffer status */

#define AXIC_DS_CTL 0X80 /* Downstream general contral */
#define AXIC_DS_DATA 0X8C /* Downstream message data */
#define AXIC_DS_BUF_STS 0X90 /* Downstream buffer status */
#define AXIC_DS_RD_STS 0X94 /* Downstream read status */

#define AXIC_MSG_LEN_PER_SEND		4
#define AXIC_MSG_LEN_PER_REC		4
#define AXIC_US_CTL_EN 0x1
#define AXIC_DS_CTL_EN 0x1

struct axi_com_drv_data {
	void __iomem *base;

	struct device *dev;
	struct ultrasoc_com *com;

	u32 ds_msg_counter;

	u32 us_msg_cur;
	spinlock_t us_msg_list_lock;
	struct list_head us_msg_head;

	u32 ds_msg_cur;
	spinlock_t ds_msg_list_lock;
	struct list_head ds_msg_head;
};

#endif
