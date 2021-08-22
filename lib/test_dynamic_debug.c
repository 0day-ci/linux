// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel module for testing dynamic_debug
 *
 * Author:
 *      Jim Cromie	<jim.cromie@gmail.com>
 */

/*
 * test-setup:
 * - use register_tracer to attach a test harness
 * - a custom trace_printer which counts invocations of enabled pr_debugs
 * - a DUT (device under test), calling categorized pr_debugs
 * test-run:
 * - manipulate the pr_debugs' enablements -> match_ct
 * - call event generator with loop-ct
 *   its pr_debug()s are Traced: trace_ct++
 * - count and compare: mainly trace_ct, but also match_ct
 */

#include <linux/module.h>

static int trace_ct;	/* the state var */
static int test_ct;
static int errors;

static int verbose;
module_param_named(verbose, verbose, int, 0444);
MODULE_PARM_DESC(verbose, "notice on tracer");

static int __test_resv;
module_param_named(test_reserve, __test_resv, int, 0444);
MODULE_PARM_DESC(test_reserve, "test 'reservation' against 'poaching'\n");

static int __broken_tracer;
module_param_named(broken_tracer, __broken_tracer, int, 0444);
MODULE_PARM_DESC(broken_tracer,
		 "use broken tracer, recursing with pr_debug\n"
		 "\tonly works at modprobe time\n");

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

static int (*my_tracer)
	(const char *decorator, char *prefix, char *label, struct va_format *vaf);

static void pick_tracer(void)
{
	if (__broken_tracer)
		my_tracer = bad_tracer;
	else
		my_tracer = good_tracer;
}

/* call pr_debug (4 * reps) + 2 times, for tracer side-effects */
static void DUT(int reps)
{
	int i;

	pr_debug("Entry:\n");
	pr_info(" DUT %d time(s)\n", reps);
	for (i = 0; i < reps; i++) {
		pr_debug("hi: %d\n", i);
		pr_debug("mid: %d\n", i);
		pr_debug("low: %d\n", i);
		pr_debug("low:lower: %d subcategory test\n", i);
	}
	pr_debug("Exit:\n");
}

struct test_spec {
	int matches;	/* expected rc from applying qry */
	int loops;	/* passed to DUT */
	int hits;	/* should match trace_ct after gen */
	const char *mod;	/* Anyof: wildcarded-string, null, &mod.name */
	const char *qry;	/* echo $qry > control */
	const char *story;	/* test purpose explanation progress */
};

static void expect_count(struct test_spec *t)
{
	test_ct++;
	if (trace_ct != t->hits) {
		pr_err("fail#%d: got %d != %d by \"%s\"\tfor \"%s\"\n",
		       test_ct, trace_ct, t->hits, t->qry, t->story);
		errors++;
	} else
		pr_info("pass#%d, hits %d, on \"%s\"\n", test_ct, t->hits, t->story);

	trace_ct = 0;
}

static void expect_matches(int got, struct test_spec *t)
{
	if (got != t->matches) {
		pr_warn(" nok: got %d != %d on \"%s\"\n", got, t->matches, t->qry);
		errors++;
	} else
		pr_info(" ok: %d matches by \"%s\"\t for \"%s\"\n", got, t->qry, t->story);
}

static void do_test_spec(struct test_spec *t)
{
	int match_count;

	match_count = dynamic_debug_exec_queries(t->qry, t->mod);
	if (match_count < 0) {
		pr_err("exec-queries fail rc:%d\n", match_count);
		return;
	}
	expect_matches(match_count, t);
	DUT(t->loops);
	expect_count(t);
}

static const char modnm[] = "test_dynamic_debug";

static void do_register_test(struct test_spec *t, int deep)
{
	int match_count;

	pr_debug("enter: %s\n", t->story);
	if (deep)
		pr_debug("register good tracer\n");

	match_count = dynamic_debug_register_tracer(t->qry, t->mod, good_tracer);
	if (match_count < 0) {
		pr_err("exec-queries fail rc:%d\n", match_count);
		return;
	}
	expect_matches(match_count, t);
	DUT(t->loops);
	expect_count(t);

	if (!deep)
		return;

	pr_debug("unregister bad tracer\n");
	match_count = dynamic_debug_unregister_tracer(t->qry, t->mod, bad_tracer);
	if (match_count < 0) {
		pr_err("exec-queries fail rc:%d\n", match_count);
		return;
	}
	expect_matches(match_count, t);
	DUT(t->loops);
	expect_count(t);

	pr_debug("unregister good tracer\n");
	match_count = dynamic_debug_unregister_tracer(t->qry, t->mod, good_tracer);
	if (match_count < 0) {
		pr_err("exec-queries fail rc:%d\n", match_count);
		return;
	}
	expect_matches(match_count, t);
	DUT(t->loops);
	expect_count(t);
}

struct test_spec registry_tests[] = {

	/* matches, loops, hits, modname, query, story */
	{ 6, 1, 0,   modnm, "func DUT +_",	"probe: DUT 1" },
	{ 6, 1, 2+4, modnm, "func DUT +T",	"enable (T)" },
	{ 6, 2, 10,  modnm, "func DUT -_",	"probe: DUT 2" },
	{ 6, 3, 14,  modnm, "func DUT +T",	"over-enable, ok" },
	{ 6, 2, 10,  modnm, "func DUT -_",	"probe: DUT 3" },
	{ 6, 3, 0,   modnm, "func DUT -T",	"disable" },

	/* 5 other callsites here */
	{ 11, 1, 0,  modnm, "+_",	"probe: whole module" },
	{ 11, 5, 22, modnm, "+T",	"enable (T) whole module" },
	{ 11, 1, 7, modnm, "+T",	"re-enable whole module" },
	{ 11, 3, 1,  modnm, "-T",	"disable whole module" },

	{ 3, 2, 0,  modnm, "func test_* +_",	"probe: count test_*" },
	{ 3, 2, 0,  modnm, "func test_* +_",	"probe: count test_*" },

	/* terminate registry tests in a known T state */
	{ 6, 3, 14, modnm, "func DUT +T",	"enable just function" },
};

/* these tests rely on register stuff having been done ?? */
struct test_spec exec_tests[] = {
	/*
	 * preferred use of exec_queries(qry, modnm) is with true
	 * modnm, since that removes 'module $modnm' from the query
	 * string. (supports modprobe $modname dyndbg=+p).
	 *
	 * But start the old way. with Ts enabled.
	 */
	{ 6, 1, 6, NULL, "module test_dynamic_debug func DUT +p",
			 "Hello! using original module-in-query style" },

	{ 11, 1, 6, modnm, "+p",	"enable (p) whole module, run DUT 1x" },
	{ 11, 1, 0, modnm, "-p",	"disable (p) whole module, run DUT(1x)" },

	{ 6, 4, 18, modnm, "func DUT +T", "enable (T) DUT(4)" },

	{ 1, 4, 14, modnm, "format '^hi:' -T",		"disable 1 callsite" },
	{ 1, 4, 10, modnm, "format '^mid:' -T",		"disable 1 callsite" },
	{ 1, 4, 10, modnm, "format '^mid:' -T",		"re-disable" },
	{ 1, 5, 12, modnm, "format '^mid:' -T",		"re-disable with more loops" },
	{ 2, 4, 2, modnm, "format '^low:' -T",		"disable with subclass" },
	{ 1, 4, 2, modnm, "format '^low: ' -T",		"re-disable, exclude subclass" },
	{ 1, 4, 6, modnm, "format '^low: ' +T",		"enable, exclude subclass" },
	{ 1, 4, 10, modnm, "format '^low:lower:' +T",	"enable the subclass" },
	{ 1, 6, 14, modnm, "format '^low:lower:' +T",	"re-enable the subclass" },
};

struct test_spec ratelimit_tests[] = {
	{ 6, 3000, 0, modnm, "func DUT +Tr" }
};

static void do_test_vec(struct test_spec *t, int len)
{
	int i;

	for (i = 0; i < len; i++, t++)
		do_test_spec(t);
}

static void test_all(void)
{
	int i;

	pr_debug("Entry:\n");
	pick_tracer();

	for (i = 0; i < ARRAY_SIZE(registry_tests); i++)
		; //do_register_test(&registry_tests[i], __test_resv);

	for (i = 0; i < ARRAY_SIZE(registry_tests); i++)
		do_register_test(&registry_tests[i], 0);

	do_test_vec(exec_tests, ARRAY_SIZE(exec_tests));
	do_test_vec(ratelimit_tests, ARRAY_SIZE(ratelimit_tests));
}

static void report(char *who)
{
	if (errors)
		pr_err("%s: failed %d of %d tests\n", who, errors, test_ct);
	else
		pr_notice("%s: passed %d tests\n", who, test_ct);
}

static int __init test_dynamic_debug_init(void)
{
	pr_debug("Init:\n");

	test_all();
	report("complete");

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
