// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Unit tests for lib/uuid.c module.
 *
 * Copyright 2016 Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 * Copyright 2021 André Almeida <andrealmeid@riseup.net>
 */
#include <kunit/test.h>
#include <linux/uuid.h>

struct test_data {
	const char *uuid;
	guid_t le;
	uuid_t be;
};

static const struct test_data correct_data[] = {
	{
		.uuid = "c33f4995-3701-450e-9fbf-206a2e98e576",
		.le = GUID_INIT(0xc33f4995, 0x3701, 0x450e, 0x9f, 0xbf, 0x20, 0x6a, 0x2e, 0x98, 0xe5, 0x76),
		.be = UUID_INIT(0xc33f4995, 0x3701, 0x450e, 0x9f, 0xbf, 0x20, 0x6a, 0x2e, 0x98, 0xe5, 0x76),
	},
	{
		.uuid = "64b4371c-77c1-48f9-8221-29f054fc023b",
		.le = GUID_INIT(0x64b4371c, 0x77c1, 0x48f9, 0x82, 0x21, 0x29, 0xf0, 0x54, 0xfc, 0x02, 0x3b),
		.be = UUID_INIT(0x64b4371c, 0x77c1, 0x48f9, 0x82, 0x21, 0x29, 0xf0, 0x54, 0xfc, 0x02, 0x3b),
	},
	{
		.uuid = "0cb4ddff-a545-4401-9d06-688af53e7f84",
		.le = GUID_INIT(0x0cb4ddff, 0xa545, 0x4401, 0x9d, 0x06, 0x68, 0x8a, 0xf5, 0x3e, 0x7f, 0x84),
		.be = UUID_INIT(0x0cb4ddff, 0xa545, 0x4401, 0x9d, 0x06, 0x68, 0x8a, 0xf5, 0x3e, 0x7f, 0x84),
	},
};

static const char * const wrong_data[] = {
	"c33f4995-3701-450e-9fbf206a2e98e576 ",	/* no hyphen(s) */
	"64b4371c-77c1-48f9-8221-29f054XX023b",	/* invalid character(s) */
	"0cb4ddff-a545-4401-9d06-688af53e",	/* not enough data */
};

static void uuid_correct_le(struct kunit *test)
{
	guid_t le;
	const struct test_data *data = test->param_value;

	KUNIT_ASSERT_EQ_MSG(test, guid_parse(data->uuid, &le), 0,
			    "failed to parse '%s'", data->uuid);
	KUNIT_EXPECT_TRUE_MSG(test, guid_equal(&data->le, &le),
			      "'%s' should be equal to %pUl", data->uuid, &le);
}

static void uuid_correct_be(struct kunit *test)
{
	uuid_t be;
	const struct test_data *data = test->param_value;

	KUNIT_ASSERT_EQ_MSG(test, uuid_parse(data->uuid, &be), 0,
			    "failed to parse '%s'", data->uuid);
	KUNIT_EXPECT_TRUE_MSG(test, uuid_equal(&data->be, &be),
			      "'%s' should be equal to %pUl", data->uuid, &be);
}

static void uuid_wrong_le(struct kunit *test)
{
	guid_t le;
	const char * const *data = test->param_value;

	KUNIT_ASSERT_NE_MSG(test, guid_parse(*data, &le), 0,
			    "parsing of '%s' should've failed", *data);
}

static void uuid_wrong_be(struct kunit *test)
{
	uuid_t be;
	const char * const *data = test->param_value;

	KUNIT_ASSERT_NE_MSG(test, uuid_parse(*data, &be), 0,
			    "parsing of '%s' should've failed", *data);
}

static void case_to_desc_correct(const struct test_data *t, char *desc)
{
	strcpy(desc, t->uuid);
}

KUNIT_ARRAY_PARAM(correct, correct_data, case_to_desc_correct);

static void case_to_desc_wrong(const char * const *s, char *desc)
{
	strcpy(desc, *s);
}

KUNIT_ARRAY_PARAM(wrong, wrong_data, case_to_desc_wrong);

static struct kunit_case uuid_test_cases[] = {
	KUNIT_CASE_PARAM(uuid_correct_be, correct_gen_params),
	KUNIT_CASE_PARAM(uuid_correct_le, correct_gen_params),
	KUNIT_CASE_PARAM(uuid_wrong_be, wrong_gen_params),
	KUNIT_CASE_PARAM(uuid_wrong_le, wrong_gen_params),
	{}
};

static struct kunit_suite uuid_test_suite = {
	.name = "uuid",
	.test_cases = uuid_test_cases,
};
kunit_test_suite(uuid_test_suite);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_LICENSE("Dual BSD/GPL");
