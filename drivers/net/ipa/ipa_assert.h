/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Linaro Ltd.
 */
#ifndef _IPA_ASSERT_H_
#define _IPA_ASSERT_H_

#include <linux/compiler.h>
#include <linux/printk.h>
#include <linux/device.h>

/* Verify the expression yields true, and fail at build time if possible */
#define ipa_assert(dev, expr) \
	do { \
		if (__builtin_constant_p(expr)) \
			compiletime_assert(expr, __ipa_failure_msg(expr)); \
		else \
			__ipa_assert_runtime(dev, expr); \
	} while (0)

/* Report an error if the given expression evaluates to false at runtime */
#define ipa_assert_always(dev, expr) \
	do { \
		if (unlikely(!(expr))) { \
			struct device *__dev = (dev); \
			\
			if (__dev) \
				dev_err(__dev, __ipa_failure_msg(expr)); \
			else  \
				pr_err(__ipa_failure_msg(expr)); \
		} \
	} while (0)

/* Constant message used when an assertion fails */
#define __ipa_failure_msg(expr)	"IPA assertion failed: " #expr "\n"

#ifdef IPA_VALIDATION

/* Only do runtime checks for "normal" assertions if validating the code */
#define __ipa_assert_runtime(dev, expr)	ipa_assert_always(dev, expr)

#else /* !IPA_VALIDATION */

/* "Normal" assertions aren't checked when validation is disabled */
#define __ipa_assert_runtime(dev, expr)	\
	do { (void)(dev); (void)(expr); } while (0)

#endif /* !IPA_VALIDATION */

#endif /* _IPA_ASSERT_H_ */
