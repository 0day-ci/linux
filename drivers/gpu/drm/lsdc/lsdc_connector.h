/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KMS driver for Loongson display controller
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

	/* Read display timmings from dtb support */
	struct display_timings *disp_tim;
	bool has_disp_tim;

	int index;
};

#define to_lsdc_connector(x)        \
		container_of(x, struct lsdc_connector, base)

struct lsdc_connector *lsdc_connector_init(struct lsdc_device *ldev,
					   unsigned int index);

#endif
