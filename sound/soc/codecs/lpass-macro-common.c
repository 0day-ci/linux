// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022, The Linux Foundation. All rights reserved.

#include <linux/export.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include "lpass-macro-common.h"

int lpass_macro_pds_init(struct platform_device *pdev, struct lpass_macro **pds)
{
	struct device *dev = &pdev->dev;
	struct lpass_macro *l_pds;
	int ret;

	const struct property *prop = of_find_property(dev->of_node, "power-domains", NULL);

	if (!prop)
		return 0;

	l_pds = devm_kzalloc(dev, sizeof(*l_pds), GFP_KERNEL);
	if (!l_pds)
		return -ENOMEM;

	l_pds->macro_pd = dev_pm_domain_attach_by_name(dev,  "macro");
	if (IS_ERR_OR_NULL(l_pds->macro_pd)) {
		ret = PTR_ERR(l_pds->macro_pd) ? : -ENODATA;
		return ret;
	}
	ret = pm_runtime_get_sync(l_pds->macro_pd);
	if (ret < 0) {
		dev_err(dev, "%s failed for macro_pd, ret %d\n", __func__, ret);
		dev_pm_domain_detach(l_pds->macro_pd, false);
		pm_runtime_put_noidle(l_pds->macro_pd);
		return ret;
	}

	l_pds->dcodec_pd = dev_pm_domain_attach_by_name(dev, "dcodec");
	if (IS_ERR_OR_NULL(l_pds->dcodec_pd)) {
		ret = PTR_ERR(l_pds->dcodec_pd) ? : -ENODATA;
		dev_pm_domain_detach(l_pds->macro_pd, false);
		return ret;
	}

	ret = pm_runtime_get_sync(l_pds->dcodec_pd);
	if (ret < 0) {
		dev_err(dev, "%s failed for dcodec_pd, ret %d\n", __func__, ret);

		dev_pm_domain_detach(l_pds->dcodec_pd, false);
		pm_runtime_put_noidle(l_pds->dcodec_pd);
		return ret;
	}
	*pds = l_pds;
	return ret;
}
EXPORT_SYMBOL_GPL(lpass_macro_pds_init);

void lpass_macro_pds_exit(struct platform_device *pdev, struct lpass_macro *pds)
{
	pm_runtime_put(pds->macro_pd);
	pm_runtime_put(pds->dcodec_pd);
	dev_pm_domain_detach(pds->macro_pd, false);
	dev_pm_domain_detach(pds->dcodec_pd, false);
}
EXPORT_SYMBOL_GPL(lpass_macro_pds_exit);

MODULE_DESCRIPTION("QTI SC7280 LPI GPIO pin control driver");
MODULE_LICENSE("GPL");
