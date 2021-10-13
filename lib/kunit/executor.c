// SPDX-License-Identifier: GPL-2.0

#include <linux/reboot.h>
#include <kunit/test.h>
#include <linux/glob.h>
#include <linux/moduleparam.h>

/*
 * These symbols point to the .kunit_test_suites section and are defined in
 * include/asm-generic/vmlinux.lds.h, and consequently must be extern.
 */
extern struct kunit_suite * const * const __kunit_suites_start[];
extern struct kunit_suite * const * const __kunit_suites_end[];

#if IS_BUILTIN(CONFIG_KUNIT)

static char *filter_glob_param;
static char *action_param;

module_param_named(filter_glob, filter_glob_param, charp, 0);
MODULE_PARM_DESC(filter_glob,
		"Filter which KUnit test suites/tests run at boot-time, e.g. list* or list*.*del_test");
module_param_named(action, action_param, charp, 0);
MODULE_PARM_DESC(action,
		 "Changes KUnit executor behavior, valid values are:\n"
		 "<none>: run the tests like normal\n"
		 "'list' to list test names instead of running them.\n");

/* glob_match() needs NULL terminated strings, so we need a copy of filter_glob_param. */
struct kunit_test_filter {
	char *suite_glob;
	char *test_glob;
};

/* Split "suite_glob.test_glob" into two. Assumes filter_glob is not empty. */
static void kunit_parse_filter_glob(struct kunit_test_filter *parsed,
				    const char *filter_glob)
{
	const int len = strlen(filter_glob);
	const char *period = strchr(filter_glob, '.');

	if (!period) {
		parsed->suite_glob = kzalloc(len + 1, GFP_KERNEL);
		parsed->test_glob = NULL;
		strcpy(parsed->suite_glob, filter_glob);
		return;
	}

	parsed->suite_glob = kzalloc(period - filter_glob + 1, GFP_KERNEL);
	parsed->test_glob = kzalloc(len - (period - filter_glob) + 1, GFP_KERNEL);

	strncpy(parsed->suite_glob, filter_glob, period - filter_glob);
	strncpy(parsed->test_glob, period + 1, len - (period - filter_glob));
}

/* Create a copy of suite with only tests that match test_glob. */
static struct kunit_suite *
kunit_filter_tests(struct kunit_suite *const suite, const char *test_glob)
{
	int n = 0;
	struct kunit_case *filtered, *test_case;
	struct kunit_suite *copy;

	kunit_suite_for_each_test_case(suite, test_case) {
		if (!test_glob || glob_match(test_glob, test_case->name))
			++n;
	}

	if (n == 0)
		return NULL;

	/* Use memcpy to workaround copy->name being const. */
	copy = kmalloc(sizeof(*copy), GFP_KERNEL);
	memcpy(copy, suite, sizeof(*copy));

	filtered = kcalloc(n + 1, sizeof(*filtered), GFP_KERNEL);

	n = 0;
	kunit_suite_for_each_test_case(suite, test_case) {
		if (!test_glob || glob_match(test_glob, test_case->name))
			filtered[n++] = *test_case;
	}

	copy->test_cases = filtered;
	return copy;
}

static char *kunit_shutdown;
core_param(kunit_shutdown, kunit_shutdown, charp, 0644);

/* Stores a NULL-terminated array of suites. */
struct suite_set {
	struct kunit_suite * const *start;
	struct kunit_suite * const *end;
};

static void kunit_free_suite_set(struct suite_set suite_set)
{
	struct kunit_suite * const *suites;

	for (suites = suite_set.start; suites < suite_set.end; suites++)
		kfree(*suites);
	kfree(suite_set.start);
}

static struct suite_set kunit_filter_suites(const struct suite_set *suite_set,
					    const char *filter_glob)
{
	int i;
	struct kunit_suite **copy, *filtered_suite;
	struct suite_set filtered;
	struct kunit_test_filter filter;

	/* Note: this includes space for the terminating NULL. */
	const size_t max = suite_set->end - suite_set->start;

	copy = kmalloc_array(max, sizeof(*filtered.start), GFP_KERNEL);
	filtered.start = copy;
	if (!copy) { /* won't be able to run anything, return an empty set */
		filtered.end = copy;
		return filtered;
	}

	kunit_parse_filter_glob(&filter, filter_glob);

	for (i = 0; suite_set->start[i] != NULL; i++) {
		if (!glob_match(filter.suite_glob, suite_set->start[i]->name))
			continue;

		filtered_suite = kunit_filter_tests(suite_set->start[i], filter.test_glob);
		if (!filtered_suite)
			continue;

		*copy++ = filtered_suite;
	}
	*copy = NULL;
	filtered.end = copy;

	kfree(filter.suite_glob);
	kfree(filter.test_glob);
	return filtered;
}

static void kunit_handle_shutdown(void)
{
	if (!kunit_shutdown)
		return;

	if (!strcmp(kunit_shutdown, "poweroff"))
		kernel_power_off();
	else if (!strcmp(kunit_shutdown, "halt"))
		kernel_halt();
	else if (!strcmp(kunit_shutdown, "reboot"))
		kernel_restart(NULL);

}

static void kunit_exec_run_tests(struct suite_set *suite_set)
{
	pr_info("TAP version 14\n");
	pr_info("1..%zu\n", suite_set->end - suite_set->start);

	__kunit_test_suites_init(suite_set->start);
}

static void kunit_exec_list_tests(struct suite_set *suite_set)
{
	struct kunit_suite * const *suites;
	struct kunit_case *test_case;

	/* Hack: print a tap header so kunit.py can find the start of KUnit output. */
	pr_info("TAP version 14\n");

	for (suites = suite_set->start; suites < suite_set->end; suites++)
		kunit_suite_for_each_test_case((*suites), test_case) {
			pr_info("%s.%s\n", (*suites)->name, test_case->name);
		}
}

// TODO(dlatypov@google.com): delete this when we store suites in a single array.
static struct suite_set make_suite_set(void)
{
	struct suite_set flattened;
	size_t num_of_suites = 0;

	struct kunit_suite * const * const *suites, * const *subsuite;
	struct kunit_suite **end;

	for (suites = __kunit_suites_start; suites < __kunit_suites_end; suites++)
		for (subsuite = *suites; *subsuite != NULL; subsuite++)
			num_of_suites++;

	end = kcalloc(num_of_suites + 1, sizeof(*flattened.start), GFP_KERNEL);
	flattened.start = end;

	for (suites = __kunit_suites_start; suites < __kunit_suites_end; suites++)
		for (subsuite = *suites; *subsuite != NULL; subsuite++)
			*end++ = *subsuite;
	*end = NULL;
	flattened.end = end;
	return flattened;
}

int kunit_run_all_tests(void)
{
	struct suite_set suite_set = make_suite_set();
	struct kunit_suite * const *unfiltered = suite_set.start; /* need to free at end */

	if (filter_glob_param)
		suite_set = kunit_filter_suites(&suite_set, filter_glob_param);

	if (!action_param)
		kunit_exec_run_tests(&suite_set);
	else if (strcmp(action_param, "list") == 0)
		kunit_exec_list_tests(&suite_set);
	else
		pr_err("kunit executor: unknown action '%s'\n", action_param);

	if (filter_glob_param) { /* a copy was made of each suite */
		kunit_free_suite_set(suite_set);
	}
	kfree(unfiltered);

	kunit_handle_shutdown();

	return 0;
}

#if IS_BUILTIN(CONFIG_KUNIT_TEST)
#include "executor_test.c"
#endif

#endif /* IS_BUILTIN(CONFIG_KUNIT) */
