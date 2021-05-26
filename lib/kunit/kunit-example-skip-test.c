// SPDX-License-Identifier: GPL-2.0
/*
 * Example KUnit test which is always skipped.
 *
 * Copyright (C) 2021, Google LLC.
 * Author: David Gow <davidgow@google.com>
 */

#include <kunit/test.h>

/*
 * This test should always be skipped.
 */

static void example_skip_test(struct kunit *test)
{
	/* This line should run */
	kunit_log(KERN_INFO, test, "You should not see a line below.");

	/* Skip (and abort) the test */
	kunit_skip(test, "this test should be skipped");

	/* This line should not execute */
	kunit_log(KERN_INFO, test, "You should not see this line.");
}

static void example_mark_skipped_test(struct kunit *test)
{
	/* This line should run */
	kunit_log(KERN_INFO, test, "You should see a line below.");

	/* Skip (but do not abort) the test */
	kunit_mark_skipped(test, "this test should be skipped");

	/* This line should run */
	kunit_log(KERN_INFO, test, "You should see this line.");
}

static struct kunit_case example_skip_test_cases[] = {
	KUNIT_CASE(example_skip_test),
	KUNIT_CASE(example_mark_skipped_test),
	{}
};

static struct kunit_suite example_skip_test_suite = {
	.name = "example_skip",
	.test_cases = example_skip_test_cases,
};

kunit_test_suites(&example_skip_test_suite);

MODULE_LICENSE("GPL v2");
