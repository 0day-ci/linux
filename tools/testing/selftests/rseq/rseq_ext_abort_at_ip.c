// SPDX-License-Identifier: LGPL-2.1
/*
 * RSEQ abort-at-ip extension test.
 *
 * The test test_abort_at_ip_loop() implements an infinite loop which only exits when
 * aborted.  This rseq critical section is defined with the abort-at-ip
 * extension, which requires the userspace abort handler to reajust the stack pointer.
 * This test validates that the abort-at-ip value is within the address range of the
 * rseq critical section.
 *
 * The test test_abort_at_ip_undo() validates that when aborted between two
 * consecutive increments of two distinct variables, those variables are indeed one
 * value apart.  This validates that abort undo operations based on the abort-at-ip
 * work as expected.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "rseq.h"

const int nr_iter = 10;

#ifdef __x86_64__
static void test_abort_at_ip_loop(void)
{
	void *abort_ip_addr, *abort_ip_start, *abort_ip_end;

	printf("Testing abort_at_ip infinite loop\n");

	__asm__ __volatile__ goto (
		__RSEQ_ASM_DEFINE_TABLE(3, 0x0, ASM_RSEQ_CS_FLAG_ABORT_AT_IP, 1f, (2f - 1f), 4f) /* start, post_commit_offset, abort */
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, RSEQ_CS_OFFSET(%[rseq_abi]))
		"rep; nop\n\t"	/* cpu_relax for busy loop. */
		"jmp 1b\n\t"	/* infinite loop. */
		"2:\n\t"
		RSEQ_ASM_DEFINE_ABORT(4,
			/* abort-at-ip must be pop from the stack. */
			"popq %%rcx\n\t"
			"addq $128, %%rsp\n\t"	/* x86-64 redzone */
			"movq %%rcx, %[abort_ip_addr]\n\t"
			"lea 1b(%%rip), %%rcx\n\t"
			"movq %%rcx, %[abort_ip_start]\n\t"
			"lea 2b(%%rip), %%rcx\n\t"
			"movq %%rcx, %[abort_ip_end]\n\t",
			abort)
		: /* gcc asm goto does not allow outputs */
		: [rseq_abi]		"r" (&__rseq_abi),
		  [abort_ip_addr]	"m" (abort_ip_addr),
		  [abort_ip_start]	"m" (abort_ip_start),
		  [abort_ip_end]	"m" (abort_ip_end)
		: "memory", "cc", "rcx"
		: abort
	);
	fprintf(stderr, "Error: infinite loop should never exit gracefully.\n");
	abort();

abort:
	printf("Critical section aborted (as expected) at ip %p, within range [%p,%p[\n",
			abort_ip_addr, abort_ip_start, abort_ip_end);
	if (abort_ip_addr < abort_ip_start || abort_ip_addr >= abort_ip_end) {
		fprintf(stderr, "Error: abort-ip is outside of expected range\n");
		abort();
	}
}

static void test_abort_at_ip_undo(void)
{
	void *abort_ip_addr, *abort_ip_start, *abort_ip_end, *ip_after_first_inc, *ip_after_second_inc;
	unsigned long v[2] = { 0, 0 };

	printf("Testing abort_at_ip undo\n");

	__asm__ __volatile__ goto (
		__RSEQ_ASM_DEFINE_TABLE(3, 0x0, ASM_RSEQ_CS_FLAG_ABORT_AT_IP, 1f, (2f - 1f), 4f) /* start, post_commit_offset, abort */
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, RSEQ_CS_OFFSET(%[rseq_abi]))
		"incq %[v0]\n\t"
		"10:\n\t"
		"rep; nop\n\t"
		"incq %[v1]\n\t"
		"20:\n\t"
		"rep; nop\n\t"
		"jmp 1b\n\t"	/* infinite loop. */
		"2:\n\t"
		RSEQ_ASM_DEFINE_ABORT(4,
			/* abort-at-ip must be pop from the stack. */
			"popq %%rcx\n\t"
			"addq $128, %%rsp\n\t"	/* x86-64 redzone */
			"movq %%rcx, %[abort_ip_addr]\n\t"
			"lea 1b(%%rip), %%rcx\n\t"
			"movq %%rcx, %[abort_ip_start]\n\t"
			"lea 2b(%%rip), %%rcx\n\t"
			"movq %%rcx, %[abort_ip_end]\n\t"
			"lea 10b(%%rip), %%rcx\n\t"
			"movq %%rcx, %[ip_after_first_inc]\n\t"
			"lea 20b(%%rip), %%rcx\n\t"
			"movq %%rcx, %[ip_after_second_inc]\n\t",
			abort)
		: /* gcc asm goto does not allow outputs */
		: [rseq_abi]		"r" (&__rseq_abi),
		  [abort_ip_addr]	"m" (abort_ip_addr),
		  [abort_ip_start]	"m" (abort_ip_start),
		  [abort_ip_end]	"m" (abort_ip_end),
		  [ip_after_first_inc]	"m" (ip_after_first_inc),
		  [ip_after_second_inc]	"m" (ip_after_second_inc),
		  [v0]			"m" (v[0]),
		  [v1]			"m" (v[1])
		: "memory", "cc", "rcx"
		: abort
	);
	fprintf(stderr, "Error: infinite loop should never exit gracefully.\n");
	abort();

abort:
	printf("Critical section aborted (as expected) at ip %p, within range [%p,%p[\n",
			abort_ip_addr, abort_ip_start, abort_ip_end);
	if (abort_ip_addr < abort_ip_start || abort_ip_addr >= abort_ip_end) {
		fprintf(stderr, "Error: abort-ip is outside of expected range\n");
		abort();
	}
	printf("ip after first inc: %p, ip after second inc: %p\n",
			ip_after_first_inc, ip_after_second_inc);
	printf("Counter values: v0: %lu v1: %lu\n", v[0], v[1]);
	if (abort_ip_addr < ip_after_first_inc || abort_ip_addr >= ip_after_second_inc) {
		if (v[0] != v[1])
			abort();
	} else {
		if (v[0] != v[1] + 1)
			abort();
	}
}
#elif defined (__i386__)
static void test_abort_at_ip_loop(void)
{
	void *abort_ip_addr, *abort_ip_start, *abort_ip_end;

	printf("Testing abort_at_ip infinite loop\n");

	__asm__ __volatile__ goto (
		__RSEQ_ASM_DEFINE_TABLE(3, 0x0, ASM_RSEQ_CS_FLAG_ABORT_AT_IP, 1f, (2f - 1f), 4f) /* start, post_commit_offset, abort */
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, RSEQ_CS_OFFSET(%[rseq_abi]))
		"rep; nop\n\t"	/* cpu_relax for busy loop. */
		"jmp 1b\n\t"	/* infinite loop. */
		"2:\n\t"
		RSEQ_ASM_DEFINE_ABORT(4,
			/* abort-at-ip must be pop from the stack. */
			"popl %%ecx\n\t"
			"movl %%ecx, %[abort_ip_addr]\n\t"
			"movl $1b, %%ecx\n\t"
			"movl %%ecx, %[abort_ip_start]\n\t"
			"movl $2b, %%ecx\n\t"
			"movl %%ecx, %[abort_ip_end]\n\t",
			abort)
		: /* gcc asm goto does not allow outputs */
		: [rseq_abi]		"r" (&__rseq_abi),
		  [abort_ip_addr]	"m" (abort_ip_addr),
		  [abort_ip_start]	"m" (abort_ip_start),
		  [abort_ip_end]	"m" (abort_ip_end)
		: "memory", "cc", "ecx"
		: abort
	);
	fprintf(stderr, "Error: infinite loop should never exit gracefully.\n");
	abort();

abort:
	printf("Critical section aborted (as expected) at ip %p, within range [%p,%p[\n",
			abort_ip_addr, abort_ip_start, abort_ip_end);
	if (abort_ip_addr < abort_ip_start || abort_ip_addr >= abort_ip_end) {
		fprintf(stderr, "Error: abort-ip is outside of expected range\n");
		abort();
	}
}

static void test_abort_at_ip_undo(void)
{
	void *abort_ip_addr, *abort_ip_start, *abort_ip_end, *ip_after_first_inc, *ip_after_second_inc;
	unsigned long v[2] = { 0, 0 };

	printf("Testing abort_at_ip undo\n");

	__asm__ __volatile__ goto (
		__RSEQ_ASM_DEFINE_TABLE(3, 0x0, ASM_RSEQ_CS_FLAG_ABORT_AT_IP, 1f, (2f - 1f), 4f) /* start, post_commit_offset, abort */
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, RSEQ_CS_OFFSET(%[rseq_abi]))
		"incl %[v0]\n\t"
		"10:\n\t"
		"rep; nop\n\t"
		"incl %[v1]\n\t"
		"20:\n\t"
		"rep; nop\n\t"
		"jmp 1b\n\t"	/* infinite loop. */
		"2:\n\t"
		RSEQ_ASM_DEFINE_ABORT(4,
			/* abort-at-ip must be pop from the stack. */
			"popl %%ecx\n\t"
			"movl %%ecx, %[abort_ip_addr]\n\t"
			"movl $1b, %%ecx\n\t"
			"movl %%ecx, %[abort_ip_start]\n\t"
			"movl $2b, %%ecx\n\t"
			"movl %%ecx, %[abort_ip_end]\n\t"
			"movl $10b, %%ecx\n\t"
			"movl %%ecx, %[ip_after_first_inc]\n\t"
			"movl $20b, %%ecx\n\t"
			"movl %%ecx, %[ip_after_second_inc]\n\t",
			abort)
		: /* gcc asm goto does not allow outputs */
		: [rseq_abi]		"r" (&__rseq_abi),
		  [abort_ip_addr]	"m" (abort_ip_addr),
		  [abort_ip_start]	"m" (abort_ip_start),
		  [abort_ip_end]	"m" (abort_ip_end),
		  [ip_after_first_inc]	"m" (ip_after_first_inc),
		  [ip_after_second_inc]	"m" (ip_after_second_inc),
		  [v0]			"m" (v[0]),
		  [v1]			"m" (v[1])
		: "memory", "cc", "rcx"
		: abort
	);
	fprintf(stderr, "Error: infinite loop should never exit gracefully.\n");
	abort();

abort:
	printf("Critical section aborted (as expected) at ip %p, within range [%p,%p[\n",
			abort_ip_addr, abort_ip_start, abort_ip_end);
	if (abort_ip_addr < abort_ip_start || abort_ip_addr >= abort_ip_end) {
		fprintf(stderr, "Error: abort-ip is outside of expected range\n");
		abort();
	}
	printf("ip after first inc: %p, ip after second inc: %p\n",
			ip_after_first_inc, ip_after_second_inc);
	printf("Counter values: v0: %lu v1: %lu\n", v[0], v[1]);
	if (abort_ip_addr < ip_after_first_inc || abort_ip_addr >= ip_after_second_inc) {
		if (v[0] != v[1])
			abort();
	} else {
		if (v[0] != v[1] + 1)
			abort();
	}
}
#else
static void test_abort_at_ip_loop(void)
{
	abort();
}
static void test_abort_at_ip_undo(void)
{
	abort();
}
#endif

int main(int argc, char **argv)
{
	int i;

	if (rseq_register_current_thread()) {
		fprintf(stderr, "Error: rseq_register_current_thread(...) failed(%d): %s\n",
			errno, strerror(errno));
		goto init_thread_error;
	}
	printf("testing abort-at-ip extension\n");
	if (rseq_query_extension(RSEQ_EXT_ABORT_AT_IP) != 0) {
		fprintf(stderr, "RSEQ abort-at-ip extension is not supported, skipping test.\n");
		return 0;
	}
	for (i = 0; i < nr_iter; i++)
		test_abort_at_ip_loop();
	for (i = 0; i < nr_iter; i++)
		test_abort_at_ip_undo();
	if (rseq_unregister_current_thread()) {
		fprintf(stderr, "Error: rseq_unregister_current_thread(...) failed(%d): %s\n",
			errno, strerror(errno));
		goto init_thread_error;
	}
	return 0;

init_thread_error:
	return -1;
}
