// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kunit tests for IIO rescale conversions
 *
 * Copyright (c) 2021 Liam Beguin <liambeguin@gmail.com>
 */

#include <kunit/test.h>
#include <linux/gcd.h>
#include <linux/iio/afe/rescale.h>
#include <linux/iio/iio.h>
#include <linux/overflow.h>

struct rescale_tc_data {
	const char *name;

	const s32 numerator;
	const s32 denominator;
	const s32 offset;

	const int schan_val;
	const int schan_val2;
	const int schan_off;
	const int schan_scale_type;

	const char *expected;
	const char *expected_off;
};

const struct rescale_tc_data scale_cases[] = {
	/*
	 * Typical use cases
	 */
	{
		.name = "typical IIO_VAL_INT, positive",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT,
		.schan_val = 42,
		.expected = "5210.918114143\n",
	},
	{
		.name = "typical IIO_VAL_INT, negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT,
		.schan_val = 42,
		.expected = "-5210.918114143\n",
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL, positive",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 42,
		.schan_val2 = 20,
		.expected = "260.545905707\n",
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL, negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 42,
		.schan_val2 = 20,
		.expected = "-260.545905707\n",
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL_LOG2, positive",
		.numerator = 42,
		.denominator = 53,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 4096,
		.schan_val2 = 16,
		.expected = "0.049528301\n",
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL_LOG2, negative",
		.numerator = -42,
		.denominator = 53,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 4096,
		.schan_val2 = 16,
		.expected = "-0.049528301\n",
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_NANO, positive",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.expected = "1256.012008560\n",
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_NANO, negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.expected = "-1256.012008560\n",
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_MICRO, positive",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.expected = "16557.914267\n",
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_MICRO, negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.expected = "-16557.914267\n",
	},
	/*
	 * 32-bit overflow conditions
	 */
	{
		.name = "overflow IIO_VAL_FRACTIONAL, positive",
		.numerator = 2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 0x7FFFFFFF,
		.schan_val2 = 1,
		.expected = "214748364.700000000\n",
	},
	{
		.name = "overflow IIO_VAL_FRACTIONAL, negative",
		.numerator = -2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 0x7FFFFFFF,
		.schan_val2 = 1,
		.expected = "-214748364.700000000\n",
	},
	{
		.name = "overflow IIO_VAL_INT_PLUS_NANO, positive",
		.numerator = 2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 0x7fffffff,
		.expected = "1.214748364\n",
	},
	{
		.name = "overflow IIO_VAL_INT_PLUS_NANO, negative",
		.numerator = -2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 0x7fffffff,
		.expected = "-1.214748364\n",
	},
	{
		.name = "overflow IIO_VAL_INT_PLUS_MICRO, positive",
		.numerator = 2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 0x7fffffff,
		.expected = "215.748364\n",
	},
	{
		.name = "overflow IIO_VAL_INT_PLUS_MICRO, negative",
		.numerator = -2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 0x7fffffff,
		.expected = "-215.748364\n",
	},
};

const struct rescale_tc_data offset_cases[] = {
	/*
	 * Typical use cases
	 */
	{
		.name = "typical IIO_VAL_INT, positive",
		.offset = 1234,
		.schan_scale_type = IIO_VAL_INT,
		.schan_val = 123,
		.schan_val2 = 0,
		.schan_off = 14,
		.expected_off = "24\n", /* 23.872 */
	},
	{
		.name = "typical IIO_VAL_INT, negative",
		.offset = -1234,
		.schan_scale_type = IIO_VAL_INT,
		.schan_val = 12,
		.schan_val2 = 0,
		.schan_off = 14,
		.expected_off = "-88\n", /* -88.83333333333333 */
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL, positive",
		.offset = 1234,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 12,
		.schan_val2 = 34,
		.schan_off = 14,
		.expected_off = "3510\n", /* 3510.333333333333 */
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL, negative",
		.offset = -1234,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 12,
		.schan_val2 = 34,
		.schan_off = 14,
		.expected_off = "-3482\n", /* -3482.333333333333 */
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL_LOG2, positive",
		.offset = 1234,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 12,
		.schan_val2 = 16,
		.schan_off = 14,
		.expected_off = "6739299\n", /* 6739299.333333333 */
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL_LOG2, negative",
		.offset = -1234,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 12,
		.schan_val2 = 16,
		.schan_off = 14,
		.expected_off = "-6739271\n", /* -6739271.333333333 */
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_NANO, positive",
		.offset = 1234,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.schan_off = 14,
		.expected_off = "135\n", /* 135.8951219647469 */
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_NANO, negative",
		.offset = -1234,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.schan_off = 14,
		.expected_off = "-107\n", /* -107.89512196474689 */
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_MICRO, positive",
		.offset = 1234,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.schan_off = 14,
		.expected_off = "23\n", /* 23.246438560723952 */
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_MICRO, negative",
		.offset = -12345,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.schan_off = 14,
		.expected_off = "-78\n", /* -78.50185091745313 */
	},
};

static void case_to_desc(const struct rescale_tc_data *t, char *desc)
{
	strcpy(desc, t->name);
}

KUNIT_ARRAY_PARAM(iio_rescale_scale, scale_cases, case_to_desc);
KUNIT_ARRAY_PARAM(iio_rescale_offset, offset_cases, case_to_desc);

static void iio_rescale_test_scale(struct kunit *test)
{
	struct rescale_tc_data *t = (struct rescale_tc_data *)test->param_value;
	char *buf = kunit_kmalloc(test, PAGE_SIZE, GFP_KERNEL);
	struct rescale rescale;
	int values[2];
	int ret;

	rescale.numerator = t->numerator;
	rescale.denominator = t->denominator;
	rescale.offset = t->offset;
	values[0] = t->schan_val;
	values[1] = t->schan_val2;

	ret = rescale_process_scale(&rescale, t->schan_scale_type,
				&values[0], &values[1]);

	ret = iio_format_value(buf, ret, 2, values);

	KUNIT_EXPECT_EQ(test, (int)strlen(buf), ret);
	KUNIT_EXPECT_STREQ(test, buf, t->expected);
}

static void iio_rescale_test_offset(struct kunit *test)
{
	struct rescale_tc_data *t = (struct rescale_tc_data *)test->param_value;
	char *buf = kunit_kmalloc(test, PAGE_SIZE, GFP_KERNEL);
	struct rescale rescale;
	int values[2];
	int ret;

	rescale.numerator = t->numerator;
	rescale.denominator = t->denominator;
	rescale.offset = t->offset;
	values[0] = t->schan_val;
	values[1] = t->schan_val2;

	ret = rescale_process_offset(&rescale, t->schan_scale_type,
				     t->schan_val, t->schan_val2, t->schan_off,
				     &values[0], &values[1]);

	ret = iio_format_value(buf, ret, 2, values);

	KUNIT_EXPECT_EQ(test, (int)strlen(buf), ret);
	KUNIT_EXPECT_STREQ(test, buf, t->expected_off);
}

static struct kunit_case iio_rescale_test_cases[] = {
	KUNIT_CASE_PARAM(iio_rescale_test_scale, iio_rescale_scale_gen_params),
	KUNIT_CASE_PARAM(iio_rescale_test_offset, iio_rescale_offset_gen_params),
	{}
};

static struct kunit_suite iio_rescale_test_suite = {
	.name = "iio-rescale",
	.test_cases = iio_rescale_test_cases,
};
kunit_test_suite(iio_rescale_test_suite);
