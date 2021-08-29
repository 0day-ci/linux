/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Linaro Ltd.
 */

#ifndef __LINUX_PWRSEQ_DRIVER_H__
#define __LINUX_PWRSEQ_DRIVER_H__

#include <linux/device.h>

struct pwrseq;

struct pwrseq_ops {
	int (*pre_power_on)(struct pwrseq *pwrseq);
	int (*power_on)(struct pwrseq *pwrseq);
	void (*power_off)(struct pwrseq *pwrseq);
	void (*reset)(struct pwrseq *pwrseq);
};

struct module;

struct pwrseq {
	struct device dev;
	const struct pwrseq_ops *ops;
	unsigned int id;
	struct module *owner;
};

struct pwrseq *__pwrseq_create(struct device *dev, struct module *owner, const struct pwrseq_ops *ops, void *data);
struct pwrseq *__devm_pwrseq_create(struct device *dev, struct module *owner, const struct pwrseq_ops *ops, void *data);

#define pwrseq_create(dev, ops, data) __pwrseq_create((dev), THIS_MODULE, (ops), (data))
#define devm_pwrseq_create(dev, ops, data) __devm_pwrseq_create((dev), THIS_MODULE, (ops), (data))

void pwrseq_destroy(struct pwrseq *pwrseq);

static inline void *pwrseq_get_data(struct pwrseq *pwrseq)
{
	return dev_get_drvdata(&pwrseq->dev);
}

#define	of_pwrseq_provider_register(dev, xlate, data)	\
	__of_pwrseq_provider_register((dev), THIS_MODULE, (xlate), (data))

#define	devm_of_pwrseq_provider_register(dev, xlate, data)	\
	__devm_of_pwrseq_provider_register((dev), THIS_MODULE, (xlate), (data))

struct of_phandle_args;

struct pwrseq_provider *__of_pwrseq_provider_register(struct device *dev,
	struct module *owner,
	struct pwrseq * (*of_xlate)(void *data,
				    struct of_phandle_args *args),
	void *data);
struct pwrseq_provider *__devm_of_pwrseq_provider_register(struct device *dev,
	struct module *owner,
	struct pwrseq * (*of_xlate)(void *data,
				    struct of_phandle_args *args),
	void *data);
void of_pwrseq_provider_unregister(struct pwrseq_provider *pwrseq_provider);

static inline struct pwrseq *of_pwrseq_xlate_single(void *data,
						    struct of_phandle_args *args)
{
	return data;
}

struct pwrseq_onecell_data {
	unsigned int num;
	struct pwrseq *pwrseqs[];
};

struct pwrseq *of_pwrseq_xlate_onecell(void *data, struct of_phandle_args *args);

#endif /* __LINUX_PWRSEQ_DRIVER_H__ */
