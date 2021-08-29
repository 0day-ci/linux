// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 (c) Linaro Ltd.
 * Author: Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/pwrseq/fallback.h>
#include <linux/slab.h>

static DEFINE_MUTEX(pwrseq_fallback_mutex);
static LIST_HEAD(pwrseq_fallback_list);

int __pwrseq_fallback_register(struct pwrseq_fallback *fallback, struct module *owner)
{
	if (!try_module_get(owner))
		return -EPROBE_DEFER;

	fallback->owner = owner;

	mutex_lock(&pwrseq_fallback_mutex);
	list_add_tail(&fallback->list, &pwrseq_fallback_list);
	mutex_unlock(&pwrseq_fallback_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(__pwrseq_fallback_register);

void pwrseq_fallback_unregister(struct pwrseq_fallback *fallback)
{
	mutex_lock(&pwrseq_fallback_mutex);
	list_del(&fallback->list);
	mutex_unlock(&pwrseq_fallback_mutex);

	module_put(fallback->owner);

	kfree(fallback);
}
EXPORT_SYMBOL_GPL(pwrseq_fallback_unregister);

static bool pwrseq_fallback_match(struct device *dev, struct pwrseq_fallback *fallback)
{
	if (of_match_device(fallback->of_match_table, dev) != NULL)
		return true;

	/* We might add support for other matching options later */

	return false;
}

struct pwrseq *pwrseq_fallback_get(struct device *dev, const char *id)
{
	struct pwrseq_fallback *fallback;
	struct pwrseq *pwrseq = ERR_PTR(-ENODEV);

	mutex_lock(&pwrseq_fallback_mutex);

	list_for_each_entry(fallback, &pwrseq_fallback_list, list) {
		if (!pwrseq_fallback_match(dev, fallback))
			continue;

		pwrseq = fallback->get(dev, id);
		break;
	}

	mutex_unlock(&pwrseq_fallback_mutex);

	if (!IS_ERR_OR_NULL(pwrseq))
		dev_warn(dev, "legacy pwrseq support used for the device\n");

	return pwrseq;
}
