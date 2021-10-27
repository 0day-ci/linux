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

#define pr_fmt(fmt) "test_dd: " fmt

#include <linux/module.h>

static int hit_ct;
static int test_ct;
static int errors;

static int __verbose;
module_param_named(verbose, __verbose, int, 0444);
MODULE_PARM_DESC(verbose, "enable print from trace (output verify)");

static int __bad_tracer;
module_param_named(use_bad_tracer, __bad_tracer, int, 0444);
MODULE_PARM_DESC(use_bad_tracer,
		 "use broken tracer, recursing with pr_debug\n"
		 "\tonly works at modprobe time\n");

static void (*my_tracer)(const char *lbl, struct va_format *vaf);

static void good_tracer(const char *lbl, struct va_format *vaf)
{
	hit_ct++;

	if (__verbose)
		pr_notice("%s%pV", lbl, vaf);
}

static void bad_tracer(const char *lbl, struct va_format *vaf)
{
	hit_ct++;
	if (__verbose)
		pr_notice("%s%pV", lbl, vaf);

	pr_debug("%s.%pV", lbl, vaf);
}

static void pick_tracer(void)
{
	if (__bad_tracer) {
		pr_notice("using bad tracer - fails hit count tests\n");
		my_tracer = bad_tracer;
	} else
		my_tracer = good_tracer;
}

static int expect_count(int want, const char *story)
{
	test_ct++;
	if (want != hit_ct) {
		pr_err("nok %d: want %d, got %d: %s\n", test_ct, want, hit_ct, story);
		errors++;
		hit_ct = 0;
		return 1;
	}
	pr_info("ok %d: hits %d, on <%s>\n", test_ct, want, story);
	hit_ct = 0;
	return 0;
}

/* call pr_debug (4 * reps) + 2 times, for tracer side-effects */
static void do_debugging(int reps)
{
	int i;

	pr_debug("Entry:\n");
	pr_info("%s %d time(s)\n", __func__, reps);
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
	/* todo: got <0 are errors, bubbled up. no test for that */
	test_ct++;
	if (got != want) {
		pr_warn("nok %d: want %d matches, got %d on <%s>\n", test_ct, want, got, story);
		errors++;
	} else
		pr_info("ok %d: %d matches on <%s>\n", test_ct, want, story);
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

/* these tests rely on register stuff having been done ?? */
struct exec_test exec_tests[] = {
	/*
	 * use original single string query style once, to test it.
	 * standard use is with separate module param, like:
	 * dynamic_debug_exec_queries("func do_debugging +_", "test_dynamic_debug");
	 */
	{ 6, 1, 0, NULL, "module test_dynamic_debug func do_debugging -T" },

	/* no modification probe */
	{ 6, 3, 0, KBUILD_MODNAME, "func do_debugging +_" },

	/* enable all prdbgs in DUT */
	{ 6, 4, 18, KBUILD_MODNAME, "func do_debugging +T" },

	/* disable hi call */
	{ 1, 4, 14, KBUILD_MODNAME, "format '^hi:' -T" },

	/* disable mid call */
	{ 1, 4, 10, KBUILD_MODNAME, "format '^mid:' -T" },

	/* repeat same disable */
	{ 1, 4, 10, KBUILD_MODNAME, "format '^mid:' -T" },

	/* repeat same disable, diff run ct */
	{ 1, 5, 12, KBUILD_MODNAME, "format '^mid:' -T" },

	/* include subclass */
	{ 2, 4, 2, KBUILD_MODNAME, "format '^low:' -T" },

	/* re-disable, exclude subclass */
	{ 1, 4, 2, KBUILD_MODNAME, "format '^low: ' -T" },

	/* enable, exclude subclass */
	{ 1, 4, 6, KBUILD_MODNAME, "format '^low: ' +T" },

	/* enable the subclass */
	{ 1, 4, 10, KBUILD_MODNAME, "format '^low:lower:' +T" },

	/* enable the subclass */
	{ 1, 6, 14, KBUILD_MODNAME, "format '^low:lower:' +T" },
};

static int __init test_dynamic_debug_init(void)
{
	int i;

	pick_tracer();

	pr_debug("Entry:\n");
	do_debugging(1);
	expect_count(0, "nothing on");

	//dynamic_debug_register_tracer(THIS_MODULE, my_tracer);
	/* 2nd time gets a complaint */
	//dynamic_debug_register_tracer(THIS_MODULE, my_tracer);

	for (i = 0; i < ARRAY_SIZE(exec_tests); i++)
		do_exec_test(&exec_tests[i]);

	//dynamic_debug_unregister_tracer(THIS_MODULE, my_tracer);

	/* this gets missing tracer warnings, cuz +T is still on */
	do_debugging(1);
	expect_count(0, "unregistered, but +T still on");

	/* reuse test 0 to turn off T */
	do_exec_test(&exec_tests[0]);

	/* this draws warning about failed deregistration */
	//dynamic_debug_unregister_tracer(THIS_MODULE, my_tracer);

	do_debugging(1);
	expect_count(0, "all off");

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
