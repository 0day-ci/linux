// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 (c) Linaro Ltd.
 * Author: Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 *
 * Based on phy-core.c:
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 */

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/pwrseq/consumer.h>
#include <linux/pwrseq/driver.h>
#include <linux/pwrseq/fallback.h>
#include <linux/slab.h>

#define	to_pwrseq(a)	(container_of((a), struct pwrseq, dev))

static DEFINE_IDA(pwrseq_ida);
static DEFINE_MUTEX(pwrseq_provider_mutex);
static LIST_HEAD(pwrseq_provider_list);

struct pwrseq_provider {
	struct device		*dev;
	struct module		*owner;
	struct list_head	list;
	void			*data;
	struct pwrseq * (*of_xlate)(void *data, struct of_phandle_args *args);
};

void pwrseq_put(struct device *dev, struct pwrseq *pwrseq)
{
	device_link_remove(dev, &pwrseq->dev);

	module_put(pwrseq->owner);
	put_device(&pwrseq->dev);
}
EXPORT_SYMBOL_GPL(pwrseq_put);

static struct pwrseq_provider *of_pwrseq_provider_lookup(struct device_node *node)
{
	struct pwrseq_provider *pwrseq_provider;

	list_for_each_entry(pwrseq_provider, &pwrseq_provider_list, list) {
		if (pwrseq_provider->dev->of_node == node)
			return pwrseq_provider;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

static struct pwrseq *_of_pwrseq_get(struct device *dev, const char *id)
{
	struct pwrseq_provider *pwrseq_provider;
	struct pwrseq *pwrseq;
	struct of_phandle_args args;
	char prop_name[64]; /* 64 is max size of property name */
	int ret;

	snprintf(prop_name, 64, "%s-pwrseq", id);
	ret = of_parse_phandle_with_args(dev->of_node, prop_name, "#pwrseq-cells", 0, &args);
	if (ret) {
		/*
		 * Parsing failed. Try locating old bindings for mmc-pwrseq,
		 * which did not use #pwrseq-cells.
		 */
		if (strcmp(id, "mmc"))
			return NULL;

		ret = of_parse_phandle_with_args(dev->of_node, prop_name, NULL, 0, &args);
		if (ret)
			return NULL;

		dev_warn(dev, "old mmc-pwrseq binding used, add #pwrseq-cells to the provider\n");
	}

	mutex_lock(&pwrseq_provider_mutex);
	pwrseq_provider = of_pwrseq_provider_lookup(args.np);
	if (IS_ERR(pwrseq_provider) || !try_module_get(pwrseq_provider->owner)) {
		pwrseq = ERR_PTR(-EPROBE_DEFER);
		goto out_unlock;
	}

	if (!of_device_is_available(args.np)) {
		dev_warn(pwrseq_provider->dev, "Requested pwrseq is disabled\n");
		pwrseq = ERR_PTR(-ENODEV);
		goto out_put_module;
	}

	pwrseq = pwrseq_provider->of_xlate(pwrseq_provider->data, &args);

out_put_module:
	module_put(pwrseq_provider->owner);

out_unlock:
	mutex_unlock(&pwrseq_provider_mutex);
	of_node_put(args.np);

	return pwrseq;
}

struct pwrseq * __pwrseq_get(struct device *dev, const char *id, bool optional)
{
	struct pwrseq *pwrseq;
	struct device_link *link;

	pwrseq = _of_pwrseq_get(dev, id);
	if (pwrseq == NULL)
		pwrseq = pwrseq_fallback_get(dev, id);
	if (pwrseq == NULL)
		return optional ? NULL : ERR_PTR(-ENODEV);
	else if (IS_ERR(pwrseq))
		return pwrseq;

	if (!try_module_get(pwrseq->owner))
		return ERR_PTR(-EPROBE_DEFER);

	get_device(&pwrseq->dev);
	link = device_link_add(dev, &pwrseq->dev, DL_FLAG_STATELESS);
	if (!link)
		dev_dbg(dev, "failed to create device link to %s\n",
			dev_name(pwrseq->dev.parent));

	return pwrseq;
}

struct pwrseq * pwrseq_get(struct device *dev, const char *id)
{
	return __pwrseq_get(dev, id, false);
}
EXPORT_SYMBOL_GPL(pwrseq_get);

static void devm_pwrseq_release(struct device *dev, void *res)
{
	struct pwrseq *pwrseq = *(struct pwrseq **)res;

	pwrseq_put(dev, pwrseq);
}

struct pwrseq * devm_pwrseq_get(struct device *dev, const char *id)
{
	struct pwrseq **ptr, *pwrseq;

	ptr = devres_alloc(devm_pwrseq_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pwrseq = pwrseq_get(dev, id);
	if (!IS_ERR(pwrseq)) {
		*ptr = pwrseq;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return pwrseq;
}
EXPORT_SYMBOL_GPL(devm_pwrseq_get);

struct pwrseq * pwrseq_get_optional(struct device *dev, const char *id)
{
	return __pwrseq_get(dev, id, true);
}
EXPORT_SYMBOL_GPL(pwrseq_get_optional);

struct pwrseq * devm_pwrseq_get_optional(struct device *dev, const char *id)
{
	struct pwrseq **ptr, *pwrseq;

	ptr = devres_alloc(devm_pwrseq_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pwrseq = pwrseq_get_optional(dev, id);
	if (!IS_ERR_OR_NULL(pwrseq)) {
		*ptr = pwrseq;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return pwrseq;
}
EXPORT_SYMBOL_GPL(devm_pwrseq_get_optional);

int pwrseq_pre_power_on(struct pwrseq *pwrseq)
{
	if (pwrseq && pwrseq->ops->pre_power_on)
		return pwrseq->ops->pre_power_on(pwrseq);

	return 0;
}
EXPORT_SYMBOL_GPL(pwrseq_pre_power_on);

int pwrseq_power_on(struct pwrseq *pwrseq)
{
	if (pwrseq && pwrseq->ops->power_on)
		return pwrseq->ops->power_on(pwrseq);

	return 0;
}
EXPORT_SYMBOL_GPL(pwrseq_power_on);

void pwrseq_power_off(struct pwrseq *pwrseq)
{
	if (pwrseq && pwrseq->ops->power_off)
		pwrseq->ops->power_off(pwrseq);
}
EXPORT_SYMBOL_GPL(pwrseq_power_off);

void pwrseq_reset(struct pwrseq *pwrseq)
{
	if (pwrseq && pwrseq->ops->reset)
		pwrseq->ops->reset(pwrseq);
}
EXPORT_SYMBOL_GPL(pwrseq_reset);

static void pwrseq_dev_release(struct device *dev)
{
	struct pwrseq *pwrseq = to_pwrseq(dev);

	ida_free(&pwrseq_ida, pwrseq->id);
	of_node_put(dev->of_node);
	kfree(pwrseq);
}

static struct class pwrseq_class = {
	.name = "pwrseq",
	.dev_release = pwrseq_dev_release,
};

struct pwrseq *__pwrseq_create(struct device *dev, struct module *owner, const struct pwrseq_ops *ops, void *data)
{
	struct pwrseq *pwrseq;
	int ret;

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	pwrseq = kzalloc(sizeof(*pwrseq), GFP_KERNEL);
	if (!pwrseq)
		return ERR_PTR(-ENOMEM);

	ret = ida_alloc(&pwrseq_ida, GFP_KERNEL);
	if (ret < 0)
		goto free_pwrseq;

	pwrseq->id = ret;

	device_initialize(&pwrseq->dev);

	pwrseq->dev.class = &pwrseq_class;
	pwrseq->dev.parent = dev;
	pwrseq->dev.of_node = of_node_get(dev->of_node);
	pwrseq->ops = ops;
	pwrseq->owner = owner;

	dev_set_drvdata(&pwrseq->dev, data);

	ret = dev_set_name(&pwrseq->dev, "pwrseq-%s.%u", dev_name(dev), pwrseq->id);
	if (ret)
		goto put_dev;

	ret = device_add(&pwrseq->dev);
	if (ret)
		goto put_dev;

	if (pm_runtime_enabled(dev)) {
		pm_runtime_enable(&pwrseq->dev);
		pm_runtime_no_callbacks(&pwrseq->dev);
	}

	return pwrseq;

put_dev:
	/* will call pwrseq_dev_release() to free resources */
	put_device(&pwrseq->dev);

	return ERR_PTR(ret);

free_pwrseq:
	kfree(pwrseq);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(__pwrseq_create);

void pwrseq_destroy(struct pwrseq *pwrseq)
{
	pm_runtime_disable(&pwrseq->dev);
	device_unregister(&pwrseq->dev);
}
EXPORT_SYMBOL_GPL(pwrseq_destroy);

static void devm_pwrseq_destroy(struct device *dev, void *res)
{
	struct pwrseq *pwrseq = *(struct pwrseq **)res;

	pwrseq_destroy(pwrseq);
}

struct pwrseq *__devm_pwrseq_create(struct device *dev, struct module *owner, const struct pwrseq_ops *ops, void *data)
{
	struct pwrseq **ptr, *pwrseq;

	ptr = devres_alloc(devm_pwrseq_destroy, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pwrseq = __pwrseq_create(dev, owner, ops, data);
	if (!IS_ERR(pwrseq)) {
		*ptr = pwrseq;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return pwrseq;
}
EXPORT_SYMBOL_GPL(__devm_pwrseq_create);

struct pwrseq_provider *__of_pwrseq_provider_register(struct device *dev,
	struct module *owner,
	struct pwrseq * (*of_xlate)(void *data,
				    struct of_phandle_args *args),
	void *data)
{
	struct pwrseq_provider *pwrseq_provider;

	pwrseq_provider = kzalloc(sizeof(*pwrseq_provider), GFP_KERNEL);
	if (!pwrseq_provider)
		return ERR_PTR(-ENOMEM);

	pwrseq_provider->dev = dev;
	pwrseq_provider->owner = owner;
	pwrseq_provider->of_xlate = of_xlate;
	pwrseq_provider->data = data;

	mutex_lock(&pwrseq_provider_mutex);
	list_add_tail(&pwrseq_provider->list, &pwrseq_provider_list);
	mutex_unlock(&pwrseq_provider_mutex);

	return pwrseq_provider;
}
EXPORT_SYMBOL_GPL(__of_pwrseq_provider_register);

void of_pwrseq_provider_unregister(struct pwrseq_provider *pwrseq_provider)
{
	if (IS_ERR(pwrseq_provider))
		return;

	mutex_lock(&pwrseq_provider_mutex);
	list_del(&pwrseq_provider->list);
	kfree(pwrseq_provider);
	mutex_unlock(&pwrseq_provider_mutex);
}
EXPORT_SYMBOL_GPL(of_pwrseq_provider_unregister);

static void devm_pwrseq_provider_unregister(struct device *dev, void *res)
{
	struct pwrseq_provider *pwrseq_provider = *(struct pwrseq_provider **)res;

	of_pwrseq_provider_unregister(pwrseq_provider);
}

struct pwrseq_provider *__devm_of_pwrseq_provider_register(struct device *dev,
	struct module *owner,
	struct pwrseq * (*of_xlate)(void *data,
				    struct of_phandle_args *args),
	void *data)
{
	struct pwrseq_provider **ptr, *pwrseq_provider;

	ptr = devres_alloc(devm_pwrseq_provider_unregister, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pwrseq_provider = __of_pwrseq_provider_register(dev, owner, of_xlate, data);
	if (!IS_ERR(pwrseq_provider)) {
		*ptr = pwrseq_provider;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return pwrseq_provider;
}
EXPORT_SYMBOL_GPL(__devm_of_pwrseq_provider_register);

struct pwrseq *of_pwrseq_xlate_onecell(void *data, struct of_phandle_args *args)
{
	struct pwrseq_onecell_data *pwrseq_data = data;
	unsigned int idx;

	if (args->args_count != 1)
		return ERR_PTR(-EINVAL);

	idx = args->args[0];
	if (idx >= pwrseq_data->num) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return pwrseq_data->pwrseqs[idx];
}

static int __init pwrseq_core_init(void)
{
	return class_register(&pwrseq_class);
}
device_initcall(pwrseq_core_init);
