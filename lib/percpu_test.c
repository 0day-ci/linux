// SPDX-License-Identifier: GPL-2.0-only
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/cpu.h>
#include <linux/module.h>

/* validate @native and @pcp counter values match @expected */
#define CHECK(native, pcp, expected)                                    \
	do {                                                            \
		WARN((native) != (expected),                            \
		     "raw %ld (0x%lx) != expected %lld (0x%llx)",	\
		     (native), (native),				\
		     (long long)(expected), (long long)(expected));	\
		WARN(__this_cpu_read(pcp) != (expected),                \
		     "pcp %ld (0x%lx) != expected %lld (0x%llx)",	\
		     __this_cpu_read(pcp), __this_cpu_read(pcp),	\
		     (long long)(expected), (long long)(expected));	\
	} while (0)

/* upto max alloc size tests for percpu var */
static char __percpu *counters[1 << PAGE_SHIFT];
static struct task_struct *percpu_stressd_thread;

/* let's not trigger OOM */
int percpu_alloc_max_size_shift = PAGE_SHIFT - 3;
module_param(percpu_alloc_max_size_shift, int, 0644);
MODULE_PARM_DESC(percpu_alloc_max_size_shift, "max size of allocation in stress test will be upto 1 << percpu_alloc_max_size_shift");

static long percpu_stressd_interval = 1 * 10 * HZ;
module_param(percpu_stressd_interval, long, 0644);
MODULE_PARM_DESC(percpu_stressd_interval, "percpu_stressd internal");

/* keep the default test same */
static int percpu_test_num;
module_param(percpu_test_num, int, 0644);
MODULE_PARM_DESC(percpu_test_num, "Test number percpu_test_num");

static int percpu_test_verify(void)
{
	/*
	 * volatile prevents compiler from optimizing it uses, otherwise the
	 * +ul_one/-ul_one below would replace with inc/dec instructions.
	 */
	volatile unsigned int ui_one = 1;
	long l = 0;
	unsigned long ul = 0;
	long __percpu *long_counter = alloc_percpu(long);
	unsigned long __percpu *ulong_counter = alloc_percpu(unsigned long);

	if (!long_counter || !ulong_counter)
		goto out;

	pr_debug("percpu_test: %s start cpu: %d\n", __func__, smp_processor_id());

	preempt_disable();

	l += -1;
	__this_cpu_add(*long_counter, -1);
	CHECK(l, *long_counter, -1);

	l += 1;
	__this_cpu_add(*long_counter, 1);
	CHECK(l, *long_counter, 0);

	ul = 0;
	__this_cpu_write(*ulong_counter, 0);

	ul += 1UL;
	__this_cpu_add(*ulong_counter, 1UL);
	CHECK(ul, *ulong_counter, 1);

	ul += -1UL;
	__this_cpu_add(*ulong_counter, -1UL);
	CHECK(ul, *ulong_counter, 0);

	ul += -(unsigned long)1;
	__this_cpu_add(*ulong_counter, -(unsigned long)1);
	CHECK(ul, *ulong_counter, -1);

	ul = 0;
	__this_cpu_write(*ulong_counter, 0);

	ul -= 1;
	__this_cpu_dec(*ulong_counter);
	CHECK(ul, *ulong_counter, -1);
	CHECK(ul, *ulong_counter, ULONG_MAX);

	l += -ui_one;
	__this_cpu_add(*long_counter, -ui_one);
	CHECK(l, *long_counter, 0xffffffff);

	l += ui_one;
	__this_cpu_add(*long_counter, ui_one);
	CHECK(l, *long_counter, (long)0x100000000LL);


	l = 0;
	__this_cpu_write(*long_counter, 0);

	l -= ui_one;
	__this_cpu_sub(*long_counter, ui_one);
	CHECK(l, *long_counter, -1);

	l = 0;
	__this_cpu_write(*long_counter, 0);

	l += ui_one;
	__this_cpu_add(*long_counter, ui_one);
	CHECK(l, *long_counter, 1);

	l += -ui_one;
	__this_cpu_add(*long_counter, -ui_one);
	CHECK(l, *long_counter, (long)0x100000000LL);

	l = 0;
	__this_cpu_write(*long_counter, 0);

	l -= ui_one;
	this_cpu_sub(*long_counter, ui_one);
	CHECK(l, *long_counter, -1);
	CHECK(l, *long_counter, ULONG_MAX);

	ul = 0;
	__this_cpu_write(*ulong_counter, 0);

	ul += ui_one;
	__this_cpu_add(*ulong_counter, ui_one);
	CHECK(ul, *ulong_counter, 1);

	ul = 0;
	__this_cpu_write(*ulong_counter, 0);

	ul -= ui_one;
	__this_cpu_sub(*ulong_counter, ui_one);
	CHECK(ul, *ulong_counter, -1);
	CHECK(ul, *ulong_counter, ULONG_MAX);

	ul = 3;
	__this_cpu_write(*ulong_counter, 3);

	ul = this_cpu_sub_return(*ulong_counter, ui_one);
	CHECK(ul, *ulong_counter, 2);

	ul = __this_cpu_sub_return(*ulong_counter, ui_one);
	CHECK(ul, *ulong_counter, 1);

	preempt_enable();

out:
	free_percpu(long_counter);
	free_percpu(ulong_counter);
	pr_debug("percpu_test: %s done cpu: %d\n", __func__, smp_processor_id());

	/*
	 * Keep the default functionality same.
	 * Fail will directly unload this module.
	 */
	return -EAGAIN;
}

void percpu_test_verify_work(struct work_struct *work)
{
	percpu_test_verify();
}

static int percpu_test_stress(void)
{
	int i;

	for (i = 1; i < (1 << percpu_alloc_max_size_shift); i++) {
		size_t size = i;

		if (size > PCPU_MIN_ALLOC_SIZE)
			break;
		counters[i] = (char __percpu *)__alloc_percpu(size, __alignof__(char));
		if (!counters[i])
			break;
		cond_resched();
	}

	schedule_on_each_cpu(percpu_test_verify_work);

	for (i = 0; i < (1 << percpu_alloc_max_size_shift); i++) {
		free_percpu(counters[i]);
		cond_resched();
	}
	return -EAGAIN;
}

static int percpu_stressd(void *v)
{
	int iter = 0;

	pr_info("DAEMON: starts %s\n", __func__);
	do {
		if (kthread_should_stop())
			break;
		iter++;
		pr_info("TEST Starts: %s: iter (%d)\n", __func__, iter);
		percpu_test_stress();
		pr_info("TEST Completed: %s: iter (%d)\n", __func__, iter);

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(percpu_stressd_interval);
	} while (1);

	return 0;
}

static int percpu_test_stressd(void)
{
	percpu_stressd_thread = kthread_run(percpu_stressd, NULL, "percpu_stressd");
	if (IS_ERR(percpu_stressd_thread))
		percpu_stressd_thread = NULL;
	return 0;
}

enum test_type {
	PERCPU_VERIFY,
	PERCPU_STRESS,
	PERCPU_STRESSD,
	NR_TESTS,
};

const char *test_names[NR_TESTS] = {
	[PERCPU_VERIFY] = "percpu_verify",
	[PERCPU_STRESS] = "percpu_stress",
	[PERCPU_STRESSD] = "percpu_stressd",
};

static int __init percpu_test_init(void)
{
	int i, ret = 0;
	typedef int (*percpu_tests)(void);
	const percpu_tests test_funcs[NR_TESTS] = {
		[PERCPU_VERIFY] = percpu_test_verify,
		[PERCPU_STRESS] = percpu_test_stress,
		[PERCPU_STRESSD] = percpu_test_stressd,
	};

	/* sanity checks */
	if (percpu_alloc_max_size_shift > PAGE_SHIFT)
		percpu_alloc_max_size_shift = PAGE_SHIFT;
	if (percpu_test_num > NR_TESTS)
		percpu_test_num = NR_TESTS;

	pr_info("percpu_test: INIT, interval: %ld, max_shift: %d, run_tests: %s\n",
			percpu_stressd_interval, percpu_alloc_max_size_shift,
			percpu_test_num == NR_TESTS ? "run all tests" :
			test_names[percpu_test_num]);

	/* run a given test */
	if (percpu_test_num < NR_TESTS) {
		pr_info("TEST Starts: %s\n", test_names[percpu_test_num]);
		ret = test_funcs[percpu_test_num]();
		pr_info("TEST Completed: %s\n", test_names[percpu_test_num]);
		goto out;
	}

	for (i = 0; i < NR_TESTS; i++) {
		pr_info("TEST Starts: %s\n", test_names[i]);
		test_funcs[i]();
		pr_info("TEST Completed: %s\n", test_names[i]);
	}
out:
	return ret;
}

static void __exit percpu_test_exit(void)
{
	if (percpu_stressd_thread)
		kthread_stop(percpu_stressd_thread);
	pr_info("percpu_test: EXIT\n");
}

module_init(percpu_test_init)
module_exit(percpu_test_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Thelen");
MODULE_DESCRIPTION("percpu operations test");
