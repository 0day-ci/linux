// SPDX-License-Identifier: GPL-2.0
/*
 * Simple KUnit suite for math helper funcs that are always enabled.
 *
 * Copyright (C) 2020, Google LLC.
 * Author: Daniel Latypov <dlatypov@google.com>
 */

#include <kunit/test.h>
#include <linux/gcd.h>
#include <linux/kernel.h>
#include <linux/lcm.h>
#include <linux/reciprocal_div.h>

/* Generic test case for unsigned long inputs. */
struct test_case {
	unsigned long a, b;
	unsigned long result;
};

static struct test_case gcd_cases[] = {
	{
		.a = 0, .b = 0,
		.result = 0,
	},
	{
		.a = 0, .b = 1,
		.result = 1,
	},
	{
		.a = 2, .b = 2,
		.result = 2,
	},
	{
		.a = 2, .b = 4,
		.result = 2,
	},
	{
		.a = 3, .b = 5,
		.result = 1,
	},
	{
		.a = 3 * 9, .b = 3 * 5,
		.result = 3,
	},
	{
		.a = 3 * 5 * 7, .b = 3 * 5 * 11,
		.result = 15,
	},
	{
		.a = 1 << 21,
		.b = (1 << 21) - 1,
		.result = 1,
	},
};

KUNIT_ARRAY_PARAM(gcd, gcd_cases, NULL);

static void gcd_test(struct kunit *test)
{
	const char *message_fmt = "gcd(%lu, %lu)";
	const struct test_case *test_param = test->param_value;

	KUNIT_EXPECT_EQ_MSG(test, test_param->result,
			    gcd(test_param->a, test_param->b),
			    message_fmt, test_param->a,
			    test_param->b);

	if (test_param->a == test_param->b)
		return;

	/* gcd(a,b) == gcd(b,a) */
	KUNIT_EXPECT_EQ_MSG(test, test_param->result,
			    gcd(test_param->b, test_param->a),
			    message_fmt, test_param->b,
			    test_param->a);
}

static struct test_case lcm_cases[] = {
	{
		.a = 0, .b = 0,
		.result = 0,
	},
	{
		.a = 0, .b = 1,
		.result = 0,
	},
	{
		.a = 1, .b = 2,
		.result = 2,
	},
	{
		.a = 2, .b = 2,
		.result = 2,
	},
	{
		.a = 3 * 5, .b = 3 * 7,
		.result = 3 * 5 * 7,
	},
};

KUNIT_ARRAY_PARAM(lcm, lcm_cases, NULL);

static void lcm_test(struct kunit *test)
{
	const char *message_fmt = "lcm(%lu, %lu)";
	const struct test_case *test_param = test->param_value;

	KUNIT_EXPECT_EQ_MSG(test, test_param->result,
			    lcm(test_param->a, test_param->b),
			    message_fmt, test_param->a,
			    test_param->b);

	if (test_param->a == test_param->b)
		return;

	/* lcm(a,b) == lcm(b,a) */
	KUNIT_EXPECT_EQ_MSG(test, test_param->result,
			    lcm(test_param->b, test_param->a),
			    message_fmt, test_param->b,
			    test_param->a);
}

static struct test_case int_sqrt_cases[] = {
	{
		.a = 0,
		.result = 0,
	},
	{
		.a = 1,
		.result = 1,
	},
	{
		.a = 4,
		.result = 2,
	},
	{
		.a = 5,
		.result = 2,
	},
	{
		.a = 8,
		.result = 2,
	},
	{
		.a = 1UL << 30,
		.result = 1UL << 15,
	},
};

KUNIT_ARRAY_PARAM(int_sqrt, int_sqrt_cases, NULL);

static void int_sqrt_test(struct kunit *test)
{
	const struct test_case *test_param = test->param_value;

	KUNIT_EXPECT_EQ_MSG(test, int_sqrt(test_param->a),
			    test_param->result, "sqrt(%lu)",
			    test_param->a);
}

struct reciprocal_test_case {
	u32 a, b;
	u32 result;
};

static struct reciprocal_test_case reciprocal_div_cases[] = {
	{
		.a = 0, .b = 1,
		.result = 0,
	},
	{
		.a = 42, .b = 20,
		.result = 2,
	},
	{
		.a = 42, .b = 9999,
		.result = 0,
	},
	{
		.a = (1 << 16), .b = (1 << 14),
		.result = 1 << 2,
	},
};

KUNIT_ARRAY_PARAM(reciprocal_div, reciprocal_div_cases, NULL);

static void reciprocal_div_test(struct kunit *test)
{
	const struct reciprocal_test_case *test_param = test->param_value;
	struct reciprocal_value rv = reciprocal_value(test_param->b);

	KUNIT_EXPECT_EQ_MSG(test, test_param->result,
			    reciprocal_divide(test_param->a, rv),
			    "reciprocal_divide(%u, %u)",
			    test_param->a, test_param->b);
}

static struct kunit_case math_test_cases[] = {
	KUNIT_CASE_PARAM(gcd_test, gcd_gen_params),
	KUNIT_CASE_PARAM(lcm_test, lcm_gen_params),
	KUNIT_CASE_PARAM(int_sqrt_test, int_sqrt_gen_params),
	KUNIT_CASE_PARAM(reciprocal_div_test, reciprocal_div_gen_params),
	{}
};

static struct kunit_suite math_test_suite = {
	.name = "lib-math",
	.test_cases = math_test_cases,
};

kunit_test_suites(&math_test_suite);

MODULE_LICENSE("GPL v2");
