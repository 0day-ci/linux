/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Linaro Ltd.
 */

#ifndef __LINUX_PWRSEQ_FALLBACK_H__
#define __LINUX_PWRSEQ_FALLBACK_H__

#include <linux/list.h>

struct pwrseq;

struct device;
struct module;
struct of_device_id;

struct pwrseq_fallback {
	struct list_head list;
	struct module *owner;

	const struct of_device_id *of_match_table;

	struct pwrseq *(*get)(struct device *dev, const char *id);
};

/* provider interface */

int __pwrseq_fallback_register(struct pwrseq_fallback *fallback, struct module *owner);
#define pwrseq_fallback_register(fallback) __pwrseq_fallback_register(fallback, THIS_MODULE)

void pwrseq_fallback_unregister(struct pwrseq_fallback *fallback);

/* internal interface */
struct pwrseq *pwrseq_fallback_get(struct device *dev, const char *id);

#endif /* __LINUX_PWRSEQ_DRIVER_H__ */
