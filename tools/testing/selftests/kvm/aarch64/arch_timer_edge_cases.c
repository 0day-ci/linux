// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch_timer_edge_cases.c - Tests the aarch64 timer IRQ functionality.
 *
 * The test validates some edge cases related to the virtual arch-timer:
 * - timers above the max TVAL value.
 * - timers in the past, including TVALs that rollover from 0.
 * - timers across counter roll-overs.
 * - masking a pending timer across counter roll-overs.
 * - moving counters ahead and behind pending timers.
 * - reprograming timers.
 * - the same timer condition firing multiple times.
 * - masking/unmasking using the timer control mask.
 *
 * Copyright (c) 2021, Google LLC.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <pthread.h>
#include <linux/kvm.h>
#include <linux/sizes.h>
#include <linux/bitmap.h>
#include <sched.h>
#include <sys/sysinfo.h>

#include "kvm_util.h"
#include "processor.h"
#include "delay.h"
#include "arch_timer.h"
#include "gic.h"
#include "vgic.h"

#define VCPUID				0

#define msecs_to_usecs(msec)		((msec) * 1000LL)

#define CVAL_MAX			~0ULL
/* tval is a signed 32-bit int. */
#define TVAL_MAX			INT_MAX
#define TVAL_MIN			INT_MIN

#define GICD_BASE_GPA			0x8000000ULL
#define GICR_BASE_GPA			0x80A0000ULL

/* After how much time we say there is no IRQ. */
#define TIMEOUT_NO_IRQ_US		msecs_to_usecs(50)

/* 1ms sounds a bit excessive, but QEMU-TCG is slow. */
#define TEST_MARGIN_US			1000ULL

/* A nice counter value to use as the starting one for most tests. */
#define DEF_CNT				(CVAL_MAX / 2)

/* Number of runs. */
#define NR_TEST_ITERS_DEF		5

/* Default "long" wait test time in ms. */
#define LONG_WAIT_TEST_MS		100

/* Shared with IRQ handler. */
volatile struct test_vcpu_shared_data {
	int handled;
	int spurious;
} shared_data;

struct test_args {
	/* Virtual or physical timer and counter tests. */
	enum arch_timer timer;
	/* Delay used in the test_long_timer_delays test. */
	uint64_t long_wait_ms;
	/* Number of iterations. */
	int iterations;
};

struct test_args test_args = {
	/* Only testing VIRTUAL timers for now. */
	.timer = VIRTUAL,
	.long_wait_ms = LONG_WAIT_TEST_MS,
	.iterations = NR_TEST_ITERS_DEF,
};

static int vtimer_irq, ptimer_irq;

typedef enum sync_cmd {
	SET_REG_KVM_REG_ARM_TIMER_CNT = 100001,
	USERSPACE_USLEEP,
	USERSPACE_SCHED_YIELD,
	USERSPACE_MIGRATE_SELF,
} sync_cmd_t;

typedef void (*sleep_method_t)(uint64_t usec);

static void sleep_poll(uint64_t usec);
static void sleep_sched_poll(uint64_t usec);
static void sleep_in_userspace(uint64_t usec);
static void sleep_migrate(uint64_t usec);

sleep_method_t sleep_method[] = {
	sleep_poll,
	sleep_sched_poll,
	sleep_migrate,
	sleep_in_userspace,
};

typedef void (*wfi_method_t)(void);

static void wait_for_non_spurious_irq(void);
static void wait_poll_for_irq(void);
static void wait_sched_poll_for_irq(void);
static void wait_migrate_poll_for_irq(void);

wfi_method_t wfi_method[] = {
	wait_for_non_spurious_irq,
	wait_poll_for_irq,
	wait_sched_poll_for_irq,
	wait_migrate_poll_for_irq,
};

#define for_each_wfi_method(i)							\
	for ((i) = 0; (i) < ARRAY_SIZE(wfi_method); (i)++)

#define for_each_sleep_method(i)						\
	for ((i) = 0; (i) < ARRAY_SIZE(sleep_method); (i)++)

typedef enum timer_view {
	TIMER_CVAL = 1,
	TIMER_TVAL,
} timer_view_t;

/* Pair of pcpus for the test to alternate between. */
static int pcpus[2] = {-1, -1};
static int pcpus_idx;

static uint32_t next_pcpu(void)
{
	pcpus_idx = 1 - pcpus_idx;
	return pcpus[pcpus_idx];
}

#define ASSERT_IRQS_HANDLED_2(__nr, arg1, arg2) do {				\
	int __h = shared_data.handled;						\
	GUEST_ASSERT_4(__h == (__nr), __h, __nr, arg1, arg2);			\
} while (0)

#define ASSERT_IRQS_HANDLED_1(__nr, arg1)					\
	ASSERT_IRQS_HANDLED_2((__nr), arg1, 0)

#define ASSERT_IRQS_HANDLED(__nr)						\
	ASSERT_IRQS_HANDLED_2((__nr), 0, 0)

#define TIMER_GET_CTL()								\
	timer_get_ctl(test_args.timer)

#define TIMER_SET_CTL(__ctl)							\
	timer_set_ctl(test_args.timer, (__ctl))

#define TIMER_SET_CVAL(__cval)							\
	timer_set_cval(test_args.timer, (__cval))

#define TIMER_SET_TVAL(__tval)							\
	timer_set_tval(test_args.timer, (__tval))

#define TIMER_GET_CVAL()							\
	timer_get_cval(test_args.timer)

#define TIMER_GET_TVAL()							\
	timer_get_tval(test_args.timer)

#define TIMER_GET_CNTCT()							\
	timer_get_cntct(test_args.timer)

#define GUEST_SYNC_CLOCK(__cmd, __val)						\
	GUEST_SYNC_ARGS(__cmd, __val, 0, 0, 0)

#define __SET_COUNTER(__ctr, __t)						\
	GUEST_SYNC_ARGS(SET_REG_KVM_REG_ARM_TIMER_CNT, (__ctr), (__t), 0, 0)

#define SET_COUNTER(__ctr)							\
	__SET_COUNTER((__ctr), test_args.timer)

#define USERSPACE_CMD(__cmd)							\
	GUEST_SYNC_ARGS(__cmd, 0, 0, 0, 0)

#define USERSPACE_SCHEDULE()							\
	USERSPACE_CMD(USERSPACE_SCHED_YIELD)

#define USERSPACE_MIGRATE_VCPU()						\
	USERSPACE_CMD(USERSPACE_MIGRATE_SELF)

#define SLEEP_IN_USERSPACE(__usecs)						\
	GUEST_SYNC_ARGS(USERSPACE_USLEEP, (__usecs), 0, 0, 0)

#define IAR_SPURIOUS		1023

static void guest_irq_handler(struct ex_regs *regs)
{
	unsigned int intid = gic_get_and_ack_irq();
	uint64_t cnt, cval;
	uint32_t ctl;

	GUEST_ASSERT(gic_irq_get_pending(intid));

	if (intid == IAR_SPURIOUS) {
		shared_data.spurious++;
		return;
	}

	ctl = TIMER_GET_CTL();
	cnt = TIMER_GET_CNTCT();
	cval = TIMER_GET_CVAL();

	GUEST_ASSERT_1(ctl & CTL_ISTATUS, ctl);

	/* Disable and mask the timer. */
	TIMER_SET_CTL(CTL_IMASK);
	GUEST_ASSERT(!gic_irq_get_pending(intid));

	shared_data.handled++;

	GUEST_ASSERT_2(cnt >= cval, cnt, cval);

	gic_set_eoi(intid);
}

static void set_cval_irq(uint64_t cval_cycles, uint32_t ctl)
{
	shared_data.handled = 0;
	shared_data.spurious = 0;
	TIMER_SET_CVAL(cval_cycles);
	TIMER_SET_CTL(ctl);
}

static void set_tval_irq(uint64_t tval_cycles, uint32_t ctl)
{
	shared_data.handled = 0;
	shared_data.spurious = 0;
	TIMER_SET_TVAL(tval_cycles);
	TIMER_SET_CTL(ctl);
}

static void set_xval_irq(uint64_t xval, uint32_t ctl, timer_view_t tv)
{
	switch (tv) {
	case TIMER_CVAL:
		set_cval_irq(xval, ctl);
		break;
	case TIMER_TVAL:
		set_tval_irq(xval, ctl);
		break;
	default:
		GUEST_ASSERT(0);
	}
}

/*
 * Should be called with IRQs masked.
 *
 * Note that this can hang forever, so we rely on having a timeout mechanism in
 * the "runner", like: tools/testing/selftests/kselftest/runner.sh.
 */
static void wait_for_non_spurious_irq(void)
{
	int h;

	for (h = shared_data.handled; h == shared_data.handled;) {
		asm volatile("wfi\n"
			     "msr daifclr, #2\n"
			     /* handle IRQ */
			     "msr daifset, #2\n"
			     : : : "memory");
	}
}

/*
 * Wait for an non-spurious IRQ by polling in the guest (userspace=0) or in
 * userspace (e.g., userspace=1 and userspace_cmd=USERSPACE_SCHED_YIELD).
 *
 * Should be called with IRQs masked. Not really needed like the wfi above, but
 * it should match the others.
 *
 * Note that this can hang forever, so we rely on having a timeout mechanism in
 * the "runner", like: tools/testing/selftests/kselftest/runner.sh.
 */
static void poll_for_non_spurious_irq(bool userspace, sync_cmd_t userspace_cmd)
{
	int h;

	h = shared_data.handled;

	local_irq_enable();
	while (h == shared_data.handled) {
		if (userspace)
			USERSPACE_CMD(userspace_cmd);
		else
			cpu_relax();
	}
	local_irq_disable();
}

static void wait_poll_for_irq(void)
{
	poll_for_non_spurious_irq(false, -1);
}

static void wait_sched_poll_for_irq(void)
{
	poll_for_non_spurious_irq(true, USERSPACE_SCHED_YIELD);
}

static void wait_migrate_poll_for_irq(void)
{
	poll_for_non_spurious_irq(true, USERSPACE_MIGRATE_SELF);
}

/*
 * Sleep for usec microseconds by polling in the guest (userspace=0) or in
 * userspace (e.g., userspace=1 and userspace_cmd=USERSPACE_SCHEDULE).
 */
static void guest_poll(enum arch_timer timer, uint64_t usec,
		bool userspace, sync_cmd_t userspace_cmd)
{
	uint64_t cycles = usec_to_cycles(usec);
	uint64_t start = timer_get_cntct(timer);

	/*
	 * TODO: Take care of roll-overs. Right now, we are fine as we use the
	 * virtual timer/counter for all of our roll-over tests, and so we can use
	 * the physical counter for this function. Assert this (temporarily):
	 */
	GUEST_ASSERT(test_args.timer == VIRTUAL && timer == PHYSICAL);

	while ((timer_get_cntct(timer) - start) < cycles) {
		if (userspace)
			USERSPACE_CMD(userspace_cmd);
		else
			cpu_relax();
	}
}

static void sleep_poll(uint64_t usec)
{
	if (test_args.timer == VIRTUAL)
		guest_poll(PHYSICAL, usec, false, -1);
	else
		GUEST_ASSERT(0); /* Not implemented. */
}

static void sleep_sched_poll(uint64_t usec)
{
	if (test_args.timer == VIRTUAL)
		guest_poll(PHYSICAL, usec, true, USERSPACE_SCHED_YIELD);
	else
		GUEST_ASSERT(0); /* Not implemented. */
}

static void sleep_migrate(uint64_t usec)
{
	if (test_args.timer == VIRTUAL)
		guest_poll(PHYSICAL, usec, true, USERSPACE_MIGRATE_SELF);
	else
		GUEST_ASSERT(0); /* Not implemented. */
}

static void sleep_in_userspace(uint64_t usec)
{
	SLEEP_IN_USERSPACE(usec);
}

/*
 * Reset the timer state to some nice values like the counter not being close
 * to the edge, and the control register masked and disabled.
 */
static void reset_timer_state(uint64_t cnt)
{
	SET_COUNTER(cnt);
	TIMER_SET_CTL(CTL_IMASK);
}

static void test_timer_xval(uint64_t xval, timer_view_t tv, wfi_method_t wm,
		bool reset_state, uint64_t reset_cnt)
{
	local_irq_disable();

	if (reset_state)
		reset_timer_state(reset_cnt);

	set_xval_irq(xval, CTL_ENABLE, tv);
	wm();

	ASSERT_IRQS_HANDLED_2(1, tv, wm);
	local_irq_enable();
}

/*
 * The test_timer_* functions will program the timer, wait for it, and assert
 * the firing of the correct IRQ.
 *
 * These functions don't have a timeout and return as soon as they receive an
 * IRQ. They can hang (forever), so we rely on having a timeout mechanism in
 * the "runner", like: tools/testing/selftests/kselftest/runner.sh.
 */

static void test_timer_cval(uint64_t cval, wfi_method_t wm, bool reset_state,
		uint64_t reset_cnt)
{
	test_timer_xval(cval, TIMER_CVAL, wm, reset_state, reset_cnt);
}

static void test_timer_tval(int32_t tval, wfi_method_t wm, bool reset_state,
		uint64_t reset_cnt)
{
	test_timer_xval((uint64_t)tval, TIMER_TVAL, wm, reset_state, reset_cnt);
}

static void test_xval_check_no_irq(uint64_t xval, uint64_t usec,
				timer_view_t timer_view,
				sleep_method_t guest_sleep)
{
	local_irq_disable();

	set_xval_irq(xval, CTL_ENABLE, timer_view);
	guest_sleep(usec);

	local_irq_enable();
	isb();

	/* Assume success (no IRQ) after waiting usec microseconds */
	ASSERT_IRQS_HANDLED(0);
	TIMER_SET_CTL(CTL_IMASK);
}

static void test_cval_no_irq(uint64_t cval, uint64_t usec, sleep_method_t wm)
{
	test_xval_check_no_irq(cval, usec, TIMER_CVAL, wm);
}

static void test_tval_no_irq(int32_t tval, uint64_t usec, sleep_method_t wm)
{
	/* tval will be cast to an int32_t in test_xval_check_no_irq */
	test_xval_check_no_irq((uint64_t)tval, usec, TIMER_TVAL, wm);
}

/*
 * Set the counter to just below the edge (CVAL_MAX) and set a timer that
 * crosses it over.
 */
static void test_timers_across_rollovers(void)
{
	uint64_t edge_minus_5ms = CVAL_MAX - msec_to_cycles(5);
	int i;

	for_each_wfi_method(i) {
		wfi_method_t wm = wfi_method[i];

		reset_timer_state(edge_minus_5ms);
		test_timer_tval(msec_to_cycles(10), wm, false, -1);

		reset_timer_state(edge_minus_5ms);
		test_timer_tval(TVAL_MAX, wm, false, -1);

		reset_timer_state(edge_minus_5ms);
		test_timer_tval(TVAL_MIN, wm, false, -1);
	}
}

/* Test masking/unmasking a timer using the timer mask (not the IRQ mask). */
static void test_timer_control_mask_then_unmask(void)
{
	reset_timer_state(DEF_CNT);
	set_tval_irq(-1, CTL_ENABLE | CTL_IMASK);

	/* No IRQs because the timer is still masked. */
	ASSERT_IRQS_HANDLED(0);

	/* Unmask the timer, and then get an IRQ. */
	local_irq_disable();
	TIMER_SET_CTL(CTL_ENABLE);
	wait_for_non_spurious_irq();

	ASSERT_IRQS_HANDLED(1);
	local_irq_enable();
}

/* Check that timer control masks actually mask a timer being fired. */
static void test_timer_control_masks(void)
{
	reset_timer_state(DEF_CNT);

	/* Local IRQs are not masked at this point. */

	set_tval_irq(-1, CTL_ENABLE | CTL_IMASK);

	/* Assume no IRQ after waiting TIMEOUT_NO_IRQ_US microseconds */
	sleep_poll(TIMEOUT_NO_IRQ_US);

	ASSERT_IRQS_HANDLED(0);
	TIMER_SET_CTL(CTL_IMASK);
}

static void test_fire_a_timer_multiple_times(wfi_method_t wm, int num)
{
	int i;

	local_irq_disable();
	reset_timer_state(DEF_CNT);

	set_tval_irq(0, CTL_ENABLE);

	for (i = 1; i <= num; i++) {
		wm();

		/*
		 * The IRQ handler masked and disabled the timer.
		 * Enable and unmmask it again.
		 */
		TIMER_SET_CTL(CTL_ENABLE);

		ASSERT_IRQS_HANDLED(i);
	}

	local_irq_enable();
}

static void test_timers_fired_multiple_times(void)
{
	int i;

	for_each_wfi_method(i)
		test_fire_a_timer_multiple_times(wfi_method[i], 10);
}

/*
 * Set a timer for tval=d_1_ms then reprogram it to tval=d_2_ms. Check that we
 * get the timer fired. There is no timeout for the wait: we use the wfi
 * instruction.
 */
static void test_reprogramming_timer(wfi_method_t wm,
		int32_t d_1_ms, int32_t d_2_ms)
{
	local_irq_disable();
	reset_timer_state(DEF_CNT);

	/* Program the timer to DEF_CNT + d_1_ms. */
	set_tval_irq(msec_to_cycles(d_1_ms), CTL_ENABLE);

	/* Reprogram the timer to DEF_CNT + d_2_ms. */
	TIMER_SET_TVAL(msec_to_cycles(d_2_ms));

	wm();

	/* The IRQ should arrive at DEF_CNT + d_2_ms (or after). */
	GUEST_ASSERT(TIMER_GET_CNTCT() >= DEF_CNT + msec_to_cycles(d_2_ms));

	local_irq_enable();
	ASSERT_IRQS_HANDLED_1(1, wm);
};

/*
 * Set a timer for tval=d_1_ms then reprogram it to tval=d_2_ms. Check
 * that we get the timer fired in d_2_ms.
 */
static void test_reprogramming_timer_with_timeout(sleep_method_t guest_sleep,
		int32_t d_1_ms, int32_t d_2_ms)
{
	local_irq_disable();
	reset_timer_state(DEF_CNT);

	set_tval_irq(msec_to_cycles(d_1_ms), CTL_ENABLE);

	/* Reprogram the timer. */
	TIMER_SET_TVAL(msec_to_cycles(d_2_ms));

	guest_sleep(msecs_to_usecs(d_2_ms) + TEST_MARGIN_US);

	local_irq_enable();
	isb();
	ASSERT_IRQS_HANDLED(1);
};

static void test_reprogram_timers(void)
{
	int i;

	for_each_wfi_method(i) {
		test_reprogramming_timer(wfi_method[i], 20, 5);
		test_reprogramming_timer(wfi_method[i], 5, 20);
	}

	for_each_sleep_method(i) {
		test_reprogramming_timer_with_timeout(sleep_method[i], 20, 5);
		test_reprogramming_timer_with_timeout(sleep_method[i], 5, 20);
	}
}

/*
 * Mask local IRQs, set the counter to MAX-5ms and a timer to fire
 * immediately, wait for 10ms to roll-over, and then unmask. The timer should
 * not fire as the timer condition is not valid anymore.
 */
static void test_irq_masked_timer_across_rollover(sleep_method_t guest_sleep)
{
	local_irq_disable();
	reset_timer_state(CVAL_MAX - msec_to_cycles(5));

	set_tval_irq(-1, CTL_ENABLE);

	GUEST_ASSERT(TIMER_GET_CTL() & CTL_ISTATUS);
	guest_sleep(msecs_to_usecs(10));
	GUEST_ASSERT((TIMER_GET_CTL() & CTL_ISTATUS) == 0);

	local_irq_enable();
	isb();

	ASSERT_IRQS_HANDLED(0);
}

/*
 * Set a timer at the edge, and wait with irqs masked for so long that the
 * counter rolls over and the "Timer Condition" doesn't apply anymore.
 * We should still get an IRQ.
 */
static void test_irq_masked_timers_across_rollovers(void)
{
	int i;

	for_each_sleep_method(i)
		test_irq_masked_timer_across_rollover(sleep_method[i]);
}

static void test_basic_functionality(void)
{
	int32_t tval = (int32_t)msec_to_cycles(10);
	uint64_t cval;
	int i;

	for_each_wfi_method(i) {
		wfi_method_t wm = wfi_method[i];

		cval = DEF_CNT + msec_to_cycles(10);

		test_timer_cval(cval, wm, true, DEF_CNT);
		test_timer_tval(tval, wm, true, DEF_CNT);
	}
}

/*
 * This test checks basic timer behavior without actually firing timers, things
 * like: the relationship between cval and tval, tval down-counting.
 */
static void timers_sanity_checks(bool use_sched)
{
	reset_timer_state(DEF_CNT);

	local_irq_disable();

	/* cval in the past */
	TIMER_SET_CVAL(TIMER_GET_CNTCT() - msec_to_cycles(10));
	if (use_sched)
		USERSPACE_SCHEDULE();
	GUEST_ASSERT(TIMER_GET_TVAL() < 0);

	/* tval in the past */
	TIMER_SET_TVAL(-1);
	if (use_sched)
		USERSPACE_SCHEDULE();
	GUEST_ASSERT(TIMER_GET_CVAL() < TIMER_GET_CNTCT());

	/* tval larger than TVAL_MAX. */
	TIMER_SET_CVAL(TIMER_GET_CNTCT() + TVAL_MAX + msec_to_cycles(10));
	if (use_sched)
		USERSPACE_SCHEDULE();
	GUEST_ASSERT(TIMER_GET_TVAL() <= 0);

	/*
	 * tval larger than 2 * TVAL_MAX.
	 * Twice the TVAL_MAX completely loops around the TVAL.
	 */
	TIMER_SET_CVAL(TIMER_GET_CNTCT() + 2ULL * TVAL_MAX + msec_to_cycles(10));
	if (use_sched)
		USERSPACE_SCHEDULE();
	GUEST_ASSERT_1(TIMER_GET_TVAL() <= msec_to_cycles(10), TIMER_GET_TVAL());

	/* negative tval that rollovers from 0. */
	SET_COUNTER(msec_to_cycles(1));
	TIMER_SET_TVAL(-1 * msec_to_cycles(10));
	if (use_sched)
		USERSPACE_SCHEDULE();
	GUEST_ASSERT(TIMER_GET_CVAL() >= (CVAL_MAX - msec_to_cycles(9)));

	/* tval should keep down-counting from 0 to -1. */
	TIMER_SET_TVAL(0);
	sleep_poll(1);
	GUEST_ASSERT(TIMER_GET_TVAL() < 0);

	local_irq_enable();

	/* Mask and disable any pending timer. */
	TIMER_SET_CTL(CTL_IMASK);
}

static void test_timers_sanity_checks(void)
{
	timers_sanity_checks(false);
	/* Check how KVM saves/restores these edge-case values. */
	timers_sanity_checks(true);
}

static void test_set_cnt_after_tval_max(wfi_method_t wm)
{
	local_irq_disable();
	reset_timer_state(DEF_CNT);

	set_cval_irq((uint64_t)TVAL_MAX + msec_to_cycles(5), CTL_ENABLE);

	SET_COUNTER(TVAL_MAX);
	wm();

	ASSERT_IRQS_HANDLED_1(1, wm);
	local_irq_enable();
}

/* Test timers set for: cval = now + TVAL_MAX + 5ms */
static void test_timers_above_tval_max(void)
{
	uint64_t cval;
	int i;

	/*
	 * Test that the system is not implementing cval in terms of tval.  If
	 * that was the case, setting a cval to "cval = now + TVAL_MAX + 5ms"
	 * would wrap to "cval = now + 5ms", and the timer would fire
	 * immediately. Test that it doesn't.
	 */
	for_each_sleep_method(i) {
		reset_timer_state(DEF_CNT);
		cval = TIMER_GET_CNTCT() + TVAL_MAX + msec_to_cycles(5);
		test_cval_no_irq(cval, msecs_to_usecs(5) + TEST_MARGIN_US,
				sleep_method[i]);
	}

	for_each_wfi_method(i) {
		/* Get the IRQ by moving the counter forward. */
		test_set_cnt_after_tval_max(wfi_method[i]);
	}
}

/*
 * Template function to be used by the test_move_counter_ahead_* tests.  It
 * sets the counter to cnt_1, the [c|t]val, the counter to cnt_2, and
 * then waits for an IRQ.
 */
static void test_set_cnt_after_xval(uint64_t cnt_1, uint64_t xval,
		uint64_t cnt_2, wfi_method_t wm, timer_view_t tv)
{
	local_irq_disable();

	SET_COUNTER(cnt_1);
	TIMER_SET_CTL(CTL_IMASK);

	set_xval_irq(xval, CTL_ENABLE, tv);
	SET_COUNTER(cnt_2);
	wm();

	ASSERT_IRQS_HANDLED(1);
	local_irq_enable();
}

/*
 * Template function to be used by the test_move_counter_ahead_* tests.  It
 * sets the counter to cnt_1, the [c|t]val, the counter to cnt_2, and
 * then waits for an IRQ.
 */
static void test_set_cnt_after_xval_no_irq(uint64_t cnt_1, uint64_t xval,
		uint64_t cnt_2, sleep_method_t guest_sleep, timer_view_t tv)
{
	local_irq_disable();

	SET_COUNTER(cnt_1);
	TIMER_SET_CTL(CTL_IMASK);

	set_xval_irq(xval, CTL_ENABLE, tv);
	SET_COUNTER(cnt_2);
	guest_sleep(TIMEOUT_NO_IRQ_US);

	local_irq_enable();
	isb();

	/* Assume no IRQ after waiting TIMEOUT_NO_IRQ_US microseconds */
	ASSERT_IRQS_HANDLED(0);
	TIMER_SET_CTL(CTL_IMASK);
}

static void test_set_cnt_after_tval(uint64_t cnt_1, int32_t tval,
		uint64_t cnt_2, wfi_method_t wm)
{
	test_set_cnt_after_xval(cnt_1, tval, cnt_2, wm, TIMER_TVAL);
}

static void test_set_cnt_after_cval(uint64_t cnt_1, uint64_t cval,
		uint64_t cnt_2, wfi_method_t wm)
{
	test_set_cnt_after_xval(cnt_1, cval, cnt_2, wm, TIMER_CVAL);
}

static void test_set_cnt_after_tval_no_irq(uint64_t cnt_1, int32_t tval,
		uint64_t cnt_2, sleep_method_t wm)
{
	test_set_cnt_after_xval_no_irq(cnt_1, tval, cnt_2, wm, TIMER_TVAL);
}

static void test_set_cnt_after_cval_no_irq(uint64_t cnt_1, uint64_t cval,
		uint64_t cnt_2, sleep_method_t wm)
{
	test_set_cnt_after_xval_no_irq(cnt_1, cval, cnt_2, wm, TIMER_CVAL);
}

/* Set a timer and then move the counter ahead of it. */
static void test_move_counters_ahead_of_timers(void)
{
	int i;
	int32_t tval;

	for_each_wfi_method(i) {
		wfi_method_t wm = wfi_method[i];

		test_set_cnt_after_cval(0, DEF_CNT, DEF_CNT + 1, wm);
		test_set_cnt_after_cval(CVAL_MAX, 1, 2, wm);

		/* Move counter ahead of negative tval. */
		test_set_cnt_after_tval(0, -1, DEF_CNT + 1, wm);
		test_set_cnt_after_tval(0, -1, TVAL_MAX, wm);
		tval = TVAL_MAX;
		test_set_cnt_after_tval(0, tval, (uint64_t)tval + 1, wm);
	}

	for_each_sleep_method(i) {
		sleep_method_t sm = sleep_method[i];

		test_set_cnt_after_cval_no_irq(0, DEF_CNT, CVAL_MAX, sm);
		test_set_cnt_after_cval_no_irq(0, CVAL_MAX - 1, CVAL_MAX, sm);
	}
}

/*
 * Program a timer, mask it, and then change the tval or counter to cancel it.
 * Unmask it and check that nothing fires.
 */
static void test_move_counters_behind_timers(void)
{
	int i;

	for_each_sleep_method(i) {
		sleep_method_t sm = sleep_method[i];

		test_set_cnt_after_cval_no_irq(DEF_CNT, DEF_CNT - 1, 0, sm);
		test_set_cnt_after_tval_no_irq(DEF_CNT, -1, 0, sm);
	}
}

static void test_timers_in_the_past(void)
{
	int32_t tval = -1 * (int32_t)msec_to_cycles(10);
	uint64_t cval;
	int i;

	for_each_wfi_method(i) {
		wfi_method_t wm = wfi_method[i];

		/* set a timer 10ms the past. */
		cval = DEF_CNT - msec_to_cycles(10);
		test_timer_cval(cval, wm, true, DEF_CNT);
		test_timer_tval(tval, wm, true, DEF_CNT);

		/* Set a timer to counter=0 (in the past) */
		test_timer_cval(0, wm, true, DEF_CNT);

		/* Set a time for tval=0 (now) */
		test_timer_tval(0, wm, true, DEF_CNT);

		/* Set a timer to as far in the past as possible */
		test_timer_tval(TVAL_MIN, wm, true, DEF_CNT);
	}

	/*
	 * Set the counter to 5ms, and a tval to -10ms. There should be no
	 * timer as that tval means cval=CVAL_MAX-5ms.
	 */
	for_each_sleep_method(i) {
		sleep_method_t sm = sleep_method[i];

		SET_COUNTER(msec_to_cycles(5));
		test_tval_no_irq(tval, TIMEOUT_NO_IRQ_US, sm);
	}
}

static void test_long_timer_delays(void)
{
	uint64_t wait_ms = test_args.long_wait_ms;
	int32_t tval = (int32_t)msec_to_cycles(wait_ms);
	uint64_t cval;
	int i;

	for_each_wfi_method(i) {
		wfi_method_t wm = wfi_method[i];

		cval = DEF_CNT + msec_to_cycles(wait_ms);
		test_timer_cval(cval, wm, true, DEF_CNT);
		test_timer_tval(tval, wm, true, DEF_CNT);
	}
}

static void guest_run_iteration(void)
{
	test_basic_functionality();
	test_timers_sanity_checks();

	test_timers_above_tval_max();
	test_timers_in_the_past();

	test_timers_across_rollovers();
	test_irq_masked_timers_across_rollovers();

	test_move_counters_ahead_of_timers();
	test_move_counters_behind_timers();
	test_reprogram_timers();

	test_timers_fired_multiple_times();

	test_timer_control_mask_then_unmask();
	test_timer_control_masks();
}

static void guest_code(void)
{
	int i;

	local_irq_disable();

	gic_init(GIC_V3, 1, (void *)GICD_BASE_GPA, (void *)GICR_BASE_GPA);

	TIMER_SET_CTL(CTL_IMASK);
	timer_set_ctl(PHYSICAL, CTL_IMASK);

	gic_irq_enable(vtimer_irq);
	gic_irq_enable(ptimer_irq);
	local_irq_enable();

	for (i = 0; i < test_args.iterations; i++) {
		GUEST_SYNC(i);
		guest_run_iteration();
	}

	test_long_timer_delays();
	GUEST_DONE();
}

static void migrate_self(uint32_t new_pcpu)
{
	int ret;
	cpu_set_t cpuset;
	pthread_t thread;

	thread = pthread_self();

	CPU_ZERO(&cpuset);
	CPU_SET(new_pcpu, &cpuset);

	pr_debug("Migrating from %u to %u\n", sched_getcpu(), new_pcpu);

	ret = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);

	TEST_ASSERT(ret == 0, "Failed to migrate to pCPU: %u; ret: %d\n",
			new_pcpu, ret);
}

/*
 * Set the two pcpus that the test will use to alternate between. Default to
 * use the current cpu as pcpus[0] and the one right after in the affinity set
 * as pcpus[1].
 */
static void set_default_pcpus(void)
{
	int max	= get_nprocs();
	int curr = sched_getcpu();
	cpu_set_t cpuset;
	long i;

	TEST_ASSERT(max > 1, "Need at least 2 online pcpus.");

	pcpus[0] = curr;

	sched_getaffinity(getpid(), sizeof(cpu_set_t), &cpuset);
	for (i = (curr + 1) % CPU_SETSIZE; i != curr; i = (i + 1) % CPU_SETSIZE) {
		if (CPU_ISSET(i, &cpuset)) {
			pcpus[1] = i;
			break;
		}
	}

	TEST_ASSERT(pcpus[1] != -1, "Couldn't find a second pcpu.");
	pr_debug("pcpus: %d %d\n", pcpus[0], pcpus[1]);
}

static void kvm_set_cntxct(struct kvm_vm *vm, uint64_t cnt, enum arch_timer timer)
{
	TEST_ASSERT(timer == VIRTUAL,
		"Only supports setting the virtual counter for now.");

	struct kvm_one_reg reg = {
		.id = KVM_REG_ARM_TIMER_CNT,
		.addr = (uint64_t)&cnt,
	};
	vcpu_set_reg(vm, 0, &reg);
}

static void handle_sync(struct kvm_vm *vm, struct ucall *uc)
{
	sync_cmd_t cmd = uc->args[1];
	uint64_t val = uc->args[2];
	enum arch_timer timer = uc->args[3];

	switch (cmd) {
	case SET_REG_KVM_REG_ARM_TIMER_CNT:
		kvm_set_cntxct(vm, val, timer);
		break;
	case USERSPACE_USLEEP:
		usleep(val);
		break;
	case USERSPACE_SCHED_YIELD:
		sched_yield();
		break;
	case USERSPACE_MIGRATE_SELF:
		migrate_self(next_pcpu());
		break;
	default:
		break;
	}
}

static void test_run(struct kvm_vm *vm)
{
	struct ucall uc;
	int stage = 0;

	/* Start on the first pcpu. */
	migrate_self(pcpus[0]);

	sync_global_to_guest(vm, test_args);

	for (stage = 0; ; stage++) {
		vcpu_run(vm, VCPUID);
		switch (get_ucall(vm, VCPUID, &uc)) {
		case UCALL_SYNC:
			handle_sync(vm, &uc);
			break;
		case UCALL_DONE:
			goto out;
		case UCALL_ABORT:
			TEST_FAIL("%s at %s:%ld\n\tvalues: %lu, %lu; %lu",
				(const char *)uc.args[0], __FILE__, uc.args[1],
				uc.args[2], uc.args[3], uc.args[4]);
			goto out;
		default:
			TEST_FAIL("Unexpected guest exit\n");
		}
	}

out:
	return;
}

static void test_init_timer_irq(struct kvm_vm *vm)
{
	int vcpu_fd = vcpu_get_fd(vm, VCPUID);

	kvm_device_access(vcpu_fd, KVM_ARM_VCPU_TIMER_CTRL,
			KVM_ARM_VCPU_TIMER_IRQ_PTIMER, &ptimer_irq, false);
	kvm_device_access(vcpu_fd, KVM_ARM_VCPU_TIMER_CTRL,
			KVM_ARM_VCPU_TIMER_IRQ_VTIMER, &vtimer_irq, false);

	sync_global_to_guest(vm, ptimer_irq);
	sync_global_to_guest(vm, vtimer_irq);

	pr_debug("ptimer_irq: %d; vtimer_irq: %d\n", ptimer_irq, vtimer_irq);
}

static struct kvm_vm *test_vm_create(void)
{
	struct kvm_vm *vm;

	vm = vm_create_default(VCPUID, 0, guest_code);

	vm_init_descriptor_tables(vm);
	vm_install_exception_handler(vm, VECTOR_IRQ_CURRENT, guest_irq_handler);

	vcpu_init_descriptor_tables(vm, 0);

	ucall_init(vm, NULL);
	test_init_timer_irq(vm);
	vgic_v3_setup(vm, 1, 64, GICD_BASE_GPA, GICR_BASE_GPA);

	return vm;
}

static void test_print_help(char *name)
{
	pr_info("Usage: %s [-h] [-i iterations] [-p pcpu1,pcpu2] [-l long_wait_ms]\n",
		name);
	pr_info("\t-i: Number of iterations (default: %u)\n",
		NR_TEST_ITERS_DEF);
	pr_info("\t-p: Pair of pcpus for the vcpus to alternate between. "
		"Defaults to use the current cpu and the one right after "
		"in the affinity set.\n");
	pr_info("\t-l: Delta (in ms) used for long wait time test (default: %u)\n",
		LONG_WAIT_TEST_MS);
	pr_info("\t-h: Print this help message\n");
}

static bool parse_args(int argc, char *argv[])
{
	int opt, ret;

	while ((opt = getopt(argc, argv, "hi:p:l:")) != -1) {
		switch (opt) {
		case 'i':
			test_args.iterations = atoi(optarg);
			if (test_args.iterations <= 0) {
				pr_info("Positive value needed for -i\n");
				goto err;
			}
			break;
		case 'p':
			ret = sscanf(optarg, "%u,%u", &pcpus[0], &pcpus[1]);
			if (ret != 2) {
				pr_info("Invalid pcpus pair");
				goto err;
			}
			break;
		case 'l':
			test_args.long_wait_ms = atoi(optarg);
			if (test_args.long_wait_ms <= 0) {
				pr_info("Positive value needed for -l\n");
				goto err;
			}
			break;
		case 'h':
		default:
			goto err;
		}
	}

	return true;

err:
	test_print_help(argv[0]);
	return false;
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	if (!parse_args(argc, argv))
		exit(KSFT_SKIP);

	if (pcpus[0] == -1 || pcpus[1] == -1)
		set_default_pcpus();

	vm = test_vm_create();
	test_run(vm);
	kvm_vm_free(vm);

	return 0;
}
