/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Linaro Ltd.
 */

#ifndef __LINUX_PWRSEQ_CONSUMER_H__
#define __LINUX_PWRSEQ_CONSUMER_H__

struct pwrseq;
struct device;

#if defined(CONFIG_PWRSEQ)

struct pwrseq *__must_check pwrseq_get(struct device *dev, const char *id);
struct pwrseq *__must_check devm_pwrseq_get(struct device *dev, const char *id);

struct pwrseq *__must_check pwrseq_get_optional(struct device *dev, const char *id);
struct pwrseq *__must_check devm_pwrseq_get_optional(struct device *dev, const char *id);

void pwrseq_put(struct device *dev, struct pwrseq *pwrseq);

int pwrseq_pre_power_on(struct pwrseq *pwrseq);
int pwrseq_power_on(struct pwrseq *pwrseq);
void pwrseq_power_off(struct pwrseq *pwrseq);
void pwrseq_reset(struct pwrseq *pwrseq);

#else

static inline struct pwrseq *__must_check
pwrseq_get(struct device *dev, const char *id)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct pwrseq *__must_check
devm_pwrseq_get(struct device *dev, const char *id)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct pwrseq *__must_check
pwrseq_get_optional(struct device *dev, const char *id)
{
	return NULL;
}

static inline struct pwrseq *__must_check
devm_pwrseq_get_optional(struct device *dev, const char *id)
{
	return NULL;
}

static inline void pwrseq_put(struct device *dev, struct pwrseq *pwrseq)
{
}

static inline int pwrseq_pre_power_on(struct pwrseq *pwrseq)
{
	return -ENOSYS;
}

static inline int pwrseq_power_on(struct pwrseq *pwrseq)
{
	return -ENOSYS;
}

static inline void pwrseq_power_off(struct pwrseq *pwrseq)
{
}

static inline void pwrseq_reset(struct pwrseq *pwrseq)
{
}

#endif

static inline int pwrseq_full_power_on(struct pwrseq *pwrseq)
{
	int ret;

	ret = pwrseq_pre_power_on(pwrseq);
	if (ret)
		return ret;

	return pwrseq_power_on(pwrseq);
}

#endif /* __LINUX_PWRSEQ_CONSUMER_H__ */
