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
		.expected = "5210.918114143",
	},
	{
		.name = "typical IIO_VAL_INT, negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT,
		.schan_val = 42,
		.expected = "-5210.918114143",
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL, positive",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 42,
		.schan_val2 = 20,
		.expected = "260.545905707",
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL, negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 42,
		.schan_val2 = 20,
		.expected = "-260.545905707",
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL_LOG2, positive",
		.numerator = 42,
		.denominator = 53,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 4096,
		.schan_val2 = 16,
		.expected = "0.049528301",
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL_LOG2, negative",
		.numerator = -42,
		.denominator = 53,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 4096,
		.schan_val2 = 16,
		.expected = "-0.049528301",
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_NANO, positive",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 123456,
		.expected = "1240.710106203",
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_NANO, negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 123456,
		.expected = "-1240.710106203",
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_MICRO, positive",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 1234,
		.expected = "1240.84789",
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_MICRO, negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 1234,
		.expected = "-1240.84789",
	},
	/*
	 * Use cases with small scales involving divisions
	 */
	{
		.name = "small IIO_VAL_FRACTIONAL, 0 < scale < 1",
		.numerator = 6,
		.denominator = 6,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 1,
		.schan_val2 = 3,
		.expected = "0.3333333333333333",
	},
	{
		.name = "small IIO_VAL_FRACTIONAL, -1 < scale < 0",
		.numerator = -6,
		.denominator = 6,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 1,
		.schan_val2 = 3,
		.expected = "-0.3333333333333333",
	},
	{
		.name = "small IIO_VAL_FRACTIONAL, 0 < scale < 2",
		.numerator = 8,
		.denominator = 2,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 1,
		.schan_val2 = 3,
		.expected = "1.3333333333333333",
	},
	{
		.name = "small IIO_VAL_FRACTIONAL, -2 < scale < 0",
		.numerator = -8,
		.denominator = 2,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 1,
		.schan_val2 = 3,
		.expected = "-1.3333333333333333",
	},
	{
		.name = "small IIO_VAL_FRACTIONAL_LOG2, 0 < scale < 1",
		.numerator = 16,
		.denominator = 3,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 1,
		.schan_val2 = 4,
		.expected = "0.3333333333333333",
	},
	{
		.name = "small IIO_VAL_FRACTIONAL_LOG2, -1 < scale < 0",
		.numerator = -16,
		.denominator = 3,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 1,
		.schan_val2 = 4,
		.expected = "-0.3333333333333333",
	},
	{
		.name = "small IIO_VAL_FRACTIONAL_LOG2, 0 < scale < 2",
		.numerator = 8,
		.denominator = 3,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 1,
		.schan_val2 = 1,
		.expected = "1.3333333333333333",
	},
	{
		.name = "small IIO_VAL_FRACTIONAL_LOG2, -2 < scale < 0",
		.numerator = -8,
		.denominator = 3,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 1,
		.schan_val2 = 1,
		.expected = "-1.3333333333333333",
	},
	{
		.name = "small IIO_VAL_INT_PLUS_MICRO, positive",
		.numerator = 1,
		.denominator = 2,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 5,
		.schan_val2 = 1234,
		.expected = "2.500617",
	},
	{
		.name = "small IIO_VAL_INT_PLUS_MICRO, negative",
		.numerator = -1,
		.denominator = 2,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 5,
		.schan_val2 = 1234,
		.expected = "-2.500617",
	},
	/*
	 * INT_PLUS_{MICRO,NANO} positive/negative corner cases
	 */
	{
		.name = "negative IIO_VAL_INT_PLUS_NANO, negative schan",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = -10,
		.schan_val2 = 123456,
		.expected = "-1240.710106203",
	},
	{
		.name = "negative IIO_VAL_INT_PLUS_NANO, both negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = -10,
		.schan_val2 = 123456,
		.expected = "1240.710106203",
	},
	{
		.name = "negative IIO_VAL_INT_PLUS_NANO, 3 negative",
		.numerator = -1000000,
		.denominator = -8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = -10,
		.schan_val2 = 123456,
		.expected = "-1240.710106203",
	},
	{
		.name = "negative IIO_VAL_INT_PLUS_NANO, 4 negative",
		.numerator = -1000000,
		.denominator = -8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = -10,
		.schan_val2 = -123456,
		.expected = "-1240.710106203",
	},
	{
		.name = "negative IIO_VAL_INT_PLUS_NANO, negative, *val = 0",
		.numerator = 1,
		.denominator = -10,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 0,
		.schan_val2 = 123456789,
		.expected = "-0.012345678",
	},
	/*
	 * INT_PLUS_{MICRO,NANO} decimal part overflow
	 */
	{
		.name = "decimal overflow IIO_VAL_INT_PLUS_NANO, positive",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.expected = "1256.01200856",
	},
	{
		.name = "decimal overflow IIO_VAL_INT_PLUS_NANO, negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.expected = "-1256.01200856",
	},
	{
		.name = "decimal overflow IIO_VAL_INT_PLUS_NANO, negative schan",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = -10,
		.schan_val2 = 123456789,
		.expected = "-1256.01200856",
	},
	{
		.name = "decimal overflow IIO_VAL_INT_PLUS_MICRO, positive",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.expected = "16557.914267",
	},
	{
		.name = "decimal overflow IIO_VAL_INT_PLUS_MICRO, negative",
		.numerator = -1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.expected = "-16557.914267",
	},
	{
		.name = "decimal overflow IIO_VAL_INT_PLUS_MICRO, negative schan",
		.numerator = 1000000,
		.denominator = 8060,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = -10,
		.schan_val2 = 123456789,
		.expected = "-16557.914267",
	},
	/*
	 * 32-bit overflow conditions
	 */
	{
		.name = "overflow IIO_VAL_FRACTIONAL, positive",
		.numerator = 2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = S32_MAX,
		.schan_val2 = 1,
		.expected = "214748364.7",
	},
	{
		.name = "overflow IIO_VAL_FRACTIONAL, negative",
		.numerator = -2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = S32_MAX,
		.schan_val2 = 1,
		.expected = "-214748364.7",
	},
	{
		.name = "overflow IIO_VAL_FRACTIONAL_LOG2, positive",
		.numerator = S32_MAX,
		.denominator = 4096,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 4096,
		.schan_val2 = 16,
		.expected = "32767.99998474121",
	},
	{
		.name = "overflow IIO_VAL_FRACTIONAL_LOG2, negative",
		.numerator = S32_MAX,
		.denominator = 4096,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = -4096,
		.schan_val2 = 16,
		.expected = "-32767.99998474121",
	},
	{
		.name = "overflow IIO_VAL_INT_PLUS_NANO, positive",
		.numerator = 2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = S32_MAX,
		.expected = "1.214748364",
	},
	{
		.name = "overflow IIO_VAL_INT_PLUS_NANO, negative",
		.numerator = -2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = S32_MAX,
		.expected = "-1.214748364",
	},
	{
		.name = "overflow IIO_VAL_INT_PLUS_NANO, negative schan",
		.numerator = 2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = -10,
		.schan_val2 = S32_MAX,
		.expected = "-1.214748364",
	},
	{
		.name = "overflow IIO_VAL_INT_PLUS_MICRO, positive",
		.numerator = 2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = S32_MAX,
		.expected = "215.748364",
	},
	{
		.name = "overflow IIO_VAL_INT_PLUS_MICRO, negative",
		.numerator = -2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = S32_MAX,
		.expected = "-215.748364",
	},
	{
		.name = "overflow IIO_VAL_INT_PLUS_MICRO, negative schan",
		.numerator = 2,
		.denominator = 20,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = -10,
		.schan_val2 = S32_MAX,
		.expected = "-215.748364",
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
		.expected_off = "24", /* 23.872 */
	},
	{
		.name = "typical IIO_VAL_INT, negative",
		.offset = -1234,
		.schan_scale_type = IIO_VAL_INT,
		.schan_val = 12,
		.schan_val2 = 0,
		.schan_off = 14,
		.expected_off = "-88", /* -88.83333333333333 */
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL, positive",
		.offset = 1234,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 12,
		.schan_val2 = 34,
		.schan_off = 14,
		.expected_off = "3510", /* 3510.333333333333 */
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL, negative",
		.offset = -1234,
		.schan_scale_type = IIO_VAL_FRACTIONAL,
		.schan_val = 12,
		.schan_val2 = 34,
		.schan_off = 14,
		.expected_off = "-3482", /* -3482.333333333333 */
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL_LOG2, positive",
		.offset = 1234,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 12,
		.schan_val2 = 16,
		.schan_off = 14,
		.expected_off = "6739299", /* 6739299.333333333 */
	},
	{
		.name = "typical IIO_VAL_FRACTIONAL_LOG2, negative",
		.offset = -1234,
		.schan_scale_type = IIO_VAL_FRACTIONAL_LOG2,
		.schan_val = 12,
		.schan_val2 = 16,
		.schan_off = 14,
		.expected_off = "-6739271", /* -6739271.333333333 */
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_NANO, positive",
		.offset = 1234,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.schan_off = 14,
		.expected_off = "135", /* 135.8951219647469 */
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_NANO, negative",
		.offset = -1234,
		.schan_scale_type = IIO_VAL_INT_PLUS_NANO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.schan_off = 14,
		.expected_off = "-107", /* -107.89512196474689 */
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_MICRO, positive",
		.offset = 1234,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.schan_off = 14,
		.expected_off = "23", /* 23.246438560723952 */
	},
	{
		.name = "typical IIO_VAL_INT_PLUS_MICRO, negative",
		.offset = -12345,
		.schan_scale_type = IIO_VAL_INT_PLUS_MICRO,
		.schan_val = 10,
		.schan_val2 = 123456789,
		.schan_off = 14,
		.expected_off = "-78", /* -78.50185091745313 */
	},
};

static void case_to_desc(const struct rescale_tc_data *t, char *desc)
{
	strcpy(desc, t->name);
}

KUNIT_ARRAY_PARAM(iio_rescale_scale, scale_cases, case_to_desc);
KUNIT_ARRAY_PARAM(iio_rescale_offset, offset_cases, case_to_desc);

/**
 * iio_str_to_micro() - Parse a fixed-point string to get an
 *                      IIO_VAL_INT_PLUS_MICRO value
 * @str: The string to parse
 * @micro: The number as an integer
 *
 * Returns 0 on success, or a negative error code if the string cound not be
 * parsed.
 */
static int iio_str_to_micro(const char *str, s64 *micro)
{
	int fract_mult = 100000LL;
	int tmp, tmp2;
	int ret = 0;

	ret = iio_str_to_fixpoint(str, fract_mult, &tmp, &tmp2);
	if (ret < 0)
		return ret;

	if (tmp < 0)
		tmp2 *= -1;

	*micro = (s64)tmp * 10 * fract_mult + tmp2;

	return ret;
}

/**
 * iio_test_relative_error_ppm() - Compute relative error (in ppm) between two
 *                                 fixed-point strings
 * @real_str: The real value as a string
 * @exp_str: The expected value as a string
 *
 * Returns a negative error code if the strings cound not be parsed, or the
 * relative error in ppm.
 */
static int iio_test_relative_error_ppm(const char *real_str, const char *exp_str)
{
	s64 real, exp, err;
	int ret;

	ret = iio_str_to_micro(real_str, &real);
	if (ret < 0)
		return ret;

	ret = iio_str_to_micro(exp_str, &exp);
	if (ret < 0)
		return ret;

	err = 1000000 * abs(exp - real);
	err = div64_u64(err, abs(exp));
	return (int)err;
}

static void iio_rescale_test_scale(struct kunit *test)
{
	struct rescale_tc_data *t = (struct rescale_tc_data *)test->param_value;
	char *buff = kunit_kmalloc(test, PAGE_SIZE, GFP_KERNEL);
	struct rescale rescale;
	int values[2];
	int rel_ppm;
	int ret;

	rescale.numerator = t->numerator;
	rescale.denominator = t->denominator;
	rescale.offset = t->offset;
	values[0] = t->schan_val;
	values[1] = t->schan_val2;

	ret = rescale_process_scale(&rescale, t->schan_scale_type,
				    &values[0], &values[1]);

	ret = iio_format_value(buff, ret, 2, values);
	KUNIT_EXPECT_EQ(test, (int)strlen(buff), ret);

	rel_ppm = iio_test_relative_error_ppm(buff, t->expected);
	KUNIT_EXPECT_GE_MSG(test, rel_ppm, 0, "failed to compute ppm\n");

	KUNIT_EXPECT_LT_MSG(test, rel_ppm, 500,
			    "\t    real=%s"
			    "\texpected=%s\n",
			    buff, t->expected);
}

static void iio_rescale_test_offset(struct kunit *test)
{
	struct rescale_tc_data *t = (struct rescale_tc_data *)test->param_value;
	char *buff_off = kunit_kmalloc(test, PAGE_SIZE, GFP_KERNEL);
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

	ret = iio_format_value(buff_off, ret, 2, values);
	KUNIT_EXPECT_EQ(test, (int)strlen(buff_off), ret);

	KUNIT_EXPECT_STREQ(test, strim(buff_off), t->expected_off);
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
