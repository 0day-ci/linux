// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel module for testing dynamic_debug
 *
 * Authors:
 *      Jim Cromie	<jim.cromie@gmail.com>
 */

/*
 * test-setup: use trace_print attachment interface as a test harness,
 * define a custom trace_printer which counts invocations, and a
 * pr_debug event generator function which calls a set of categorized
 * pr_debugs.
 * test-run: manipulate the pr_debug's enablement, run the event
 * generator, and check for the expected side effects.
 */

#include <linux/module.h>

static int __bad_tracer;

static int trace_ct = 0;
static int test_ct = 0;
static int errors = 0;
static int verbose = 0;


module_param_named(use_bad_tracer, __bad_tracer, int, 0644);
MODULE_PARM_DESC(use_bad_tracer,
		 "use broken tracer, recursing with pr_debug\n"
		 "\tonly works at modprobe time\n");

static int (*my_tracer)(const char *decorator, char *prefix, char *label, struct va_format *vaf);

static int good_tracer(const char *decorator, char *prefix, char *label, struct va_format *vaf)
{
	trace_ct++;
	if (verbose)
		pr_notice("my_tracer: %pV", vaf);
	return 0;
}

static int bad_tracer(const char *decorator, char *prefix, char *label, struct va_format *vaf)
{
	/* dont try pr_debug, it recurses back here */
	pr_debug("oops! recursion, crash?\n");
	return 0;
}

static void pick_tracer(void)
{
	if (__bad_tracer)
		my_tracer = bad_tracer;
	else
		my_tracer = good_tracer;
}

static int expect_count(int want, const char *story)
{
	test_ct++;
	if (want != trace_ct) {
		pr_err("expect_count: want %d, got %d: %s\n", want, trace_ct, story);
		errors++;
		trace_ct = 0;
		return 1;
	}
	pr_info("pass %d, hits %d, on \"%s\"\n", test_ct, want, story);
	trace_ct = 0;
	return 0;
}

/* call pr_debug (4 * reps) + 2 times, for tracer side-effects */
static void do_debugging(int reps)
{
	int i;

	pr_debug("Entry:\n");
	pr_info(" do_debugging %d time(s)\n", reps);
	for (i = 0; i < reps; i++) {
		pr_debug("hi: %d\n", i);
		pr_debug("mid: %d\n", i);
		pr_debug("low: %d\n", i);
		pr_debug("low:lower: %d subcategory test\n", i);
	}
	pr_debug("Exit:\n");
}

static void expect_matches(int want, int got, const char *story)
{
	// todo: got <0 are errors, bubbled up
	if (got != want)
		pr_warn(" match_count wrong: want %d got %d on \"%s\"\n", want, got, story);
	else
		pr_info(" ok: %d matches by \"%s\"\n", want, story);

	// ? count errs ? separately ?
}

static int report(char *who)
{
	if (errors)
		pr_err("%s failed %d of %d tests\n", who, errors, test_ct);
	else
		pr_info("%s passed %d tests\n", who, test_ct);
	return errors;
}

struct exec_test {
	int matches;
	int loops;
	int hits;
	const char *mod;
	const char *qry;
};

static void do_exec_test(struct exec_test *tst)
{
	int match_count;

	match_count = dynamic_debug_exec_queries(tst->qry, tst->mod);
	expect_matches(tst->matches, match_count, tst->qry);
	do_debugging(tst->loops);
	expect_count(tst->hits, tst->qry);
}

static const char my_mod[] = "test_dynamic_debug";

/* these tests rely on register stuff having been done ?? */
struct exec_test exec_tests[] = {

	/* standard use is my_mod, for `modprobe $module dyndbg=+p` */

	/* no modification probe */
	{ 6, 2, 0, my_mod, "func do_debugging +_" },

	/* use original single string query style */
	{ 6, 3, 0, NULL, "module test_dynamic_debug func do_debugging -T" },

	/* this is mildly preferred */
	{ 6, 3, 0, my_mod, "func do_debugging -T" },

	/* enable all DUT */
	{ 6, 4, 18, my_mod, "func do_debugging +T" },

	/* disable 1 call */
	{ 1, 4, 14, my_mod, "format '^hi:' -T" },

	/* disable 1 call */
	{ 1, 4, 10, my_mod, "format '^mid:' -T" },

	/* repeat same disable */
	{ 1, 4, 10, my_mod, "format '^mid:' -T" },

	/* repeat same disable, diff run ct */
	{ 1, 5, 12, my_mod, "format '^mid:' -T" },

	/* include subclass */
	{ 2, 4, 2, my_mod, "format '^low:' -T" },

	/* re-disable, exclude subclass */
	{ 1, 4, 2, my_mod, "format '^low: ' -T" },

	/* enable, exclude subclass */
	{ 1, 4, 6, my_mod, "format '^low: ' +T" },

	/* enable the subclass */
	{ 1, 4, 10, my_mod, "format '^low:lower:' +T" },

	/* enable the subclass */
	{ 1, 6, 14, my_mod, "format '^low:lower:' +T" },
};

struct register_test {
	int matches;
	int loops;
	int hits;
	const char *mod;
	const char *qry;
};

static void do_register_test(struct register_test *tst)
{
	int match_count;

	match_count = dynamic_debug_register_tracer(tst->qry, tst->mod, my_tracer);
	expect_matches(tst->matches, match_count, tst->qry);
	do_debugging(tst->loops);
	expect_count(tst->hits, tst->qry);
}

struct register_test register_tests[] = {

	{ 6, 3, 14, my_mod, "func do_debugging +T" },

	{ 10, 3, 0, my_mod, "+_" }, //, my_tracer },
	{ 11, 3, 0, my_mod, "+T" }, //, my_tracer },
};

static int __init test_dynamic_debug_init(void)
{
	int match_count; /* rc from ddebug_exec_queries. meh. */
	int i;

	pick_tracer();

	pr_debug("Entry:\n");
	do_debugging(3);
	expect_count(0, "nothing unless dyndbg=+T at modprobe");

	for (i = 0; i < ARRAY_SIZE(register_tests); i++)
		do_register_test(&register_tests[i]);

	do_debugging(2);
	expect_count(10, "do_debugging 2 times after +T");

	for (i = 0; i < ARRAY_SIZE(exec_tests); i++)
		do_exec_test(&exec_tests[i]);

	match_count = dynamic_debug_unregister_tracer(
		"func do_debugging -T", "test_dynamic_debug", my_tracer);

	expect_matches(6, match_count,
		       "unregister do_debugging()s tracers");
	do_debugging(4);
	expect_count(0, "everything is off");

	match_count = dynamic_debug_unregister_tracer(
		"func do_debugging -T", "test_dynamic_debug", my_tracer);

	expect_matches(6, match_count,
		       "re-unregister, same count, not a change count");
	report("init");
	pr_debug("Exit:\n");
	return 0;
}

static void __exit test_dynamic_debug_exit(void)
{
	report("exit");
	pr_debug("Exit:");
}

module_init(test_dynamic_debug_init);
module_exit(test_dynamic_debug_exit);

MODULE_AUTHOR("Jim Cromie <jim.cromie@gmail.com>");
MODULE_LICENSE("GPL");
