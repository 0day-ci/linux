/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, The Linux Foundation. All rights reserved.
 */

#ifndef __LPASS_MACRO_COMMON_H__
#define __LPASS_MACRO_COMMON_H__


struct lpass_macro {
	struct device *macro_pd;
	struct device *dcodec_pd;
};

int lpass_macro_pds_init(struct platform_device *pdev, struct lpass_macro **pds);
void lpass_macro_pds_exit(struct platform_device *pdev, struct lpass_macro *pds);

#endif /* __LPASS_MACRO_COMMON_H__ */
