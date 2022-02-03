/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2020 Loongson Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */


#ifndef __LSDC_CONNECTOR_H__
#define __LSDC_CONNECTOR_H__

#include <drm/drm_device.h>
#include <drm/drm_connector.h>

struct lsdc_connector {
	struct drm_connector base;

	struct i2c_adapter *ddc;

	/* pass EDID from dtb support */
	unsigned char edid_data[EDID_LENGTH];
	bool has_edid;

	/* pass display timmings from dtb support */
	struct display_timings *disp_tim;
	bool has_disp_tim;

	int index;
};

#define to_lsdc_connector(x)        \
		container_of(x, struct lsdc_connector, base)

struct lsdc_connector *lsdc_connector_init(struct lsdc_device *ldev,
					   unsigned int index);

#endif
