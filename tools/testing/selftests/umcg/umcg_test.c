// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include "libumcg.h"

#include <pthread.h>
#include <stdatomic.h>

#include "../kselftest_harness.h"

#define CHECK_CONFIG()						\
{								\
	int ret = sys_umcg_api_version(1, 0);	\
								\
	if (ret == -1 && errno == ENOSYS)			\
		SKIP(return, "CONFIG_UMCG not set");	\
}

struct worker_args {
	umcg_t		group;  /* Which group the worker should join. */
	umcg_tid	utid;   /* This worker's utid. */
	void *(*thread_fn)(void *);  /* Function to run. */
	void		*thread_arg;
	intptr_t	tag;
};

static void validate_state(umcg_tid utid, u32 expected, const char *ctx)
{
	u32 state = umcg_get_task_state(utid);

	if (state == expected)
		return;

	fprintf(stderr, "BAD state for %ld: expected: %u; got: %u; ctx :%s\n",
			utid, expected, state, ctx);
	exit(1);
}

static void *worker_fn(void *arg)
{
	void *result;
	umcg_tid utid;
	struct worker_args *args = (struct worker_args *)arg;

	validate_state(umcg_get_utid(), UMCG_TASK_NONE, "worker_fn start");

	atomic_thread_fence(memory_order_acquire);
	atomic_store_explicit(&args->utid, umcg_get_utid(),
			memory_order_seq_cst);

	utid = umcg_register_worker(args->group, args->tag);
	if (args->utid != utid) {
		fprintf(stderr, "umcg_register_worker failed.\n");
		exit(1);
	}
	validate_state(umcg_get_utid(), UMCG_TASK_RUNNING, "worker_fn in");

	/* Fence args->thread_arg */
	atomic_thread_fence(memory_order_acquire);

	result = args->thread_fn(args->thread_arg);
	validate_state(umcg_get_utid(), UMCG_TASK_RUNNING, "worker_fn out");

	if (umcg_unregister_task()) {
		fprintf(stderr, "umcg_unregister_task failed.\n");
		exit(1);
	}
	validate_state(umcg_get_utid(), UMCG_TASK_NONE, "worker_fn finish");

	return result;
}

static void *simple_running_worker(void *arg)
{
	bool *checkpoint = (bool *)arg;

	atomic_store_explicit(checkpoint, true, memory_order_relaxed);
	return NULL;
}

TEST(umcg_poll_run_test) {
	pthread_t worker;
	bool checkpoint = false;
	struct worker_args worker_args;

	CHECK_CONFIG();

	worker_args.utid = UMCG_NONE;
	worker_args.group = umcg_create_group(0);
	ASSERT_NE(UMCG_NONE, worker_args.group);

	worker_args.thread_fn = &simple_running_worker;
	worker_args.thread_arg = &checkpoint;
	worker_args.tag = 0;

	ASSERT_EQ(0, pthread_create(&worker, NULL, &worker_fn, &worker_args));

	/* Wait for the worker to start. */
	while (UMCG_NONE == atomic_load_explicit(&worker_args.utid,
				memory_order_relaxed))
		;

	/*
	 * Make sure that the worker does not checkpoint until the server
	 * runs it.
	 */
	usleep(1000);
	ASSERT_FALSE(atomic_load_explicit(&checkpoint, memory_order_relaxed));

	ASSERT_NE(0, umcg_register_server(worker_args.group, 0));

	/*
	 * Run the worker until it exits. Need to loop because the worker
	 * may pagefault and wake the server.
	 */
	do {
		u32 state;

		/* Poll the worker. */
		ASSERT_EQ(worker_args.utid, umcg_poll_worker());
		validate_state(worker_args.utid, UMCG_TASK_RUNNABLE, "wns poll");

		umcg_tid utid = umcg_run_worker(worker_args.utid);
		if (utid == UMCG_NONE) {
			ASSERT_EQ(0, errno);
			break;
		}

		ASSERT_EQ(utid, worker_args.utid);

		state = umcg_get_task_state(utid);
		ASSERT_TRUE(state == UMCG_TASK_BLOCKED || UMCG_TASK_UNBLOCKED);
	} while (true);

	ASSERT_TRUE(atomic_load_explicit(&checkpoint, memory_order_relaxed));

	/* Can't destroy group while this thread still belongs to it. */
	ASSERT_NE(0, umcg_destroy_group(worker_args.group));
	ASSERT_EQ(0, umcg_unregister_task());
	ASSERT_EQ(0, umcg_destroy_group(worker_args.group));
	ASSERT_EQ(0, pthread_join(worker, NULL));
}

static void *sleeping_worker(void *arg)
{
	int *checkpoint = (int *)arg;

	atomic_store_explicit(checkpoint, 1, memory_order_relaxed);
	usleep(2000);
	atomic_store_explicit(checkpoint, 2, memory_order_relaxed);

	return NULL;
}

TEST(umcg_sleep_test) {
	pthread_t worker;
	u32 state;
	int checkpoint = 0;
	struct worker_args worker_args;

	CHECK_CONFIG();

	worker_args.utid = UMCG_NONE;
	worker_args.group = umcg_create_group(0);
	ASSERT_NE(UMCG_NONE, worker_args.group);

	worker_args.thread_fn = &sleeping_worker;
	worker_args.thread_arg = &checkpoint;
	worker_args.tag = 0;

	ASSERT_EQ(0, pthread_create(&worker, NULL, &worker_fn, &worker_args));

	/* Wait for the worker to start. */
	while (UMCG_NONE == atomic_load_explicit(&worker_args.utid,
				memory_order_relaxed))
		;

	/*
	 * Make sure that the worker does not checkpoint until the server
	 * runs it.
	 */
	usleep(1000);
	ASSERT_EQ(0, atomic_load_explicit(&checkpoint, memory_order_relaxed));

	validate_state(umcg_get_utid(), UMCG_TASK_NONE, "sws prereg");

	ASSERT_NE(0, umcg_register_server(worker_args.group, 0));

	validate_state(umcg_get_utid(), UMCG_TASK_PROCESSING, "sws postreg");

	/*
	 * Run the worker until it checkpoints 1. Need to loop because
	 * the worker may pagefault and wake the server.
	 */
	do {
		ASSERT_EQ(worker_args.utid, umcg_poll_worker());
		validate_state(worker_args.utid, UMCG_TASK_RUNNABLE,
				"sws poll");

		umcg_tid utid = umcg_run_worker(worker_args.utid);
		ASSERT_EQ(utid, worker_args.utid);
	} while (1 != atomic_load_explicit(&checkpoint, memory_order_relaxed));

	state = umcg_get_task_state(worker_args.utid);
	ASSERT_TRUE(state == UMCG_TASK_BLOCKED || UMCG_TASK_UNBLOCKED);
	validate_state(umcg_get_utid(), UMCG_TASK_PROCESSING, "sws mid");

	/* The worker cannot reach checkpoint 2 without the server running it. */
	usleep(2000);
	ASSERT_EQ(1, atomic_load_explicit(&checkpoint, memory_order_relaxed));

	state = umcg_get_task_state(worker_args.utid);
	ASSERT_TRUE(state == UMCG_TASK_BLOCKED || UMCG_TASK_UNBLOCKED);

	/* Run the worker until it exits. */
	do {
		ASSERT_EQ(worker_args.utid, umcg_poll_worker());
		umcg_tid utid = umcg_run_worker(worker_args.utid);
		if (utid == UMCG_NONE) {
			ASSERT_EQ(0, errno);
			break;
		}

		ASSERT_EQ(utid, worker_args.utid);
	} while (true);

	/* The final check and cleanup. */
	ASSERT_EQ(2, atomic_load_explicit(&checkpoint, memory_order_relaxed));
	validate_state(umcg_get_utid(), UMCG_TASK_PROCESSING, "sws preunreg");
	ASSERT_EQ(0, pthread_join(worker, NULL));
	ASSERT_EQ(0, umcg_unregister_task());
	validate_state(umcg_get_utid(), UMCG_TASK_NONE, "sws postunreg");
	ASSERT_EQ(0, umcg_destroy_group(worker_args.group));
}

static void *waiting_worker(void *arg)
{
	int *checkpoint = (int *)arg;

	atomic_store_explicit(checkpoint, 1, memory_order_relaxed);
	if (umcg_wait(NULL)) {
		fprintf(stderr, "umcg_wait() failed.\n");
		exit(1);
	}
	atomic_store_explicit(checkpoint, 2, memory_order_relaxed);

	return NULL;
}

TEST(umcg_wait_wake_test) {
	pthread_t worker;
	int checkpoint = 0;
	struct worker_args worker_args;

	CHECK_CONFIG();

	worker_args.utid = UMCG_NONE;
	worker_args.group = umcg_create_group(0);
	ASSERT_NE(UMCG_NONE, worker_args.group);

	worker_args.thread_fn = &waiting_worker;
	worker_args.thread_arg = &checkpoint;
	worker_args.tag = 0;

	ASSERT_EQ(0, pthread_create(&worker, NULL, &worker_fn, &worker_args));

	/* Wait for the worker to start. */
	while (UMCG_NONE == atomic_load_explicit(&worker_args.utid,
				memory_order_relaxed))
		;

	/*
	 * Make sure that the worker does not checkpoint until the server
	 * runs it.
	 */
	usleep(1000);
	ASSERT_EQ(0, atomic_load_explicit(&checkpoint, memory_order_relaxed));

	ASSERT_NE(0, umcg_register_server(worker_args.group, 0));

	/*
	 * Run the worker until it checkpoints 1. Need to loop because
	 * the worker may pagefault and wake the server.
	 */
	do {
		ASSERT_EQ(worker_args.utid, umcg_poll_worker());
		ASSERT_EQ(worker_args.utid, umcg_run_worker(worker_args.utid));
	} while (1 != atomic_load_explicit(&checkpoint, memory_order_relaxed));

	validate_state(worker_args.utid, UMCG_TASK_RUNNABLE, "wait_wake wait");

	/* The worker cannot reach checkpoint 2 without the server waking it. */
	usleep(2000);
	ASSERT_EQ(1, atomic_load_explicit(&checkpoint, memory_order_relaxed));
	validate_state(worker_args.utid, UMCG_TASK_RUNNABLE, "wait_wake wait");


	ASSERT_EQ(0, umcg_wake(worker_args.utid));

	/*
	 * umcg_wake() above marks the worker as RUNNING; it will become
	 * UNBLOCKED upon wakeup as it does not have a server. But this may
	 * be delayed.
	 */
	while (umcg_get_task_state(worker_args.utid) != UMCG_TASK_UNBLOCKED)
		;

	/* The worker cannot reach checkpoint 2 without the server running it. */
	usleep(2000);
	ASSERT_EQ(1, atomic_load_explicit(&checkpoint, memory_order_relaxed));

	/* Run the worker until it exits. */
	do {
		ASSERT_EQ(worker_args.utid, umcg_poll_worker());
		umcg_tid utid = umcg_run_worker(worker_args.utid);
		if (utid == UMCG_NONE) {
			ASSERT_EQ(0, errno);
			break;
		}

		ASSERT_EQ(utid, worker_args.utid);
	} while (true);

	/* The final check and cleanup. */
	ASSERT_EQ(2, atomic_load_explicit(&checkpoint, memory_order_relaxed));
	ASSERT_EQ(0, pthread_join(worker, NULL));
	ASSERT_EQ(0, umcg_unregister_task());
	ASSERT_EQ(0, umcg_destroy_group(worker_args.group));
}

static void *swapping_worker(void *arg)
{
	umcg_tid next;

	atomic_thread_fence(memory_order_acquire);
	next = (umcg_tid)arg;

	if (next == UMCG_NONE) {
		if (0 != umcg_wait(NULL)) {
			fprintf(stderr, "swapping_worker: umcg_wait failed\n");
			exit(1);
		}
	} else {
		if (0 != umcg_swap(next, NULL)) {
			fprintf(stderr, "swapping_worker: umcg_swap failed\n");
			exit(1);
		}
	}

	return NULL;
}

TEST(umcg_swap_test) {
	const int n_workers = 10;
	struct worker_args *worker_args;
	int swap_chain_wakeups = 0;
	umcg_tid utid = UMCG_NONE;
	bool *workers_polled;
	pthread_t *workers;
	umcg_t group_id;
	int idx;

	CHECK_CONFIG();

	group_id = umcg_create_group(0);
	ASSERT_NE(UMCG_NONE, group_id);

	workers = malloc(n_workers * sizeof(pthread_t));
	worker_args = malloc(n_workers * sizeof(struct worker_args));
	workers_polled = malloc(n_workers * sizeof(bool));
	if (!workers || !worker_args || !workers_polled) {
		fprintf(stderr, "malloc failed\n");
		exit(1);
	}

	memset(worker_args, 0, n_workers * sizeof(struct worker_args));

	/* Start workers. All will block in umcg_register_worker(). */
	for (idx = 0; idx < n_workers; ++idx) {
		workers_polled[idx] = false;

		worker_args[idx].group = group_id;
		worker_args[idx].thread_fn = &swapping_worker;
		worker_args[idx].tag = idx;
		atomic_thread_fence(memory_order_release);

		ASSERT_EQ(0, pthread_create(&workers[idx], NULL, &worker_fn,
					&worker_args[idx]));
	}

	/* Wait for all workers to update their utids. */
	for (idx = 0; idx < n_workers; ++idx) {
		uint64_t counter = 0;
		while (UMCG_NONE == atomic_load_explicit(&worker_args[idx].utid,
					memory_order_seq_cst)) {
			++counter;
			if (!(counter % 1000000))
				fprintf(stderr, "looping for utid: %d %lu\n",
						idx, counter);
		}
	}

	/* Update worker args. */
	for (idx = 0; idx < (n_workers - 1); ++idx) {
		worker_args[idx].thread_arg = (void *)worker_args[idx + 1].utid;
	}
	atomic_thread_fence(memory_order_release);

	ASSERT_NE(0, umcg_register_server(group_id, 0));

	/* Poll workers. */
	for (idx = 0; idx < n_workers; ++idx) {
		utid = umcg_poll_worker();

		ASSERT_NE(UMCG_NONE, utid);
		workers_polled[umcg_get_task_tag(utid)] = true;

		validate_state(utid, UMCG_TASK_RUNNABLE, "swap poll");
	}

	/* Check that all workers have been polled. */
	for (idx = 0; idx < n_workers; ++idx) {
		ASSERT_TRUE(workers_polled[idx]);
	}

	/* Run the first worker; the swap chain will lead to the last worker. */
	utid = worker_args[0].utid;
	idx = 0;
	do {
		uint32_t state;

		utid = umcg_run_worker(utid);
		if (utid == worker_args[n_workers - 1].utid &&
				umcg_get_task_state(utid) == UMCG_TASK_RUNNABLE)
			break;

		/* There can be an occasional mid-swap wakeup due to pagefault. */
		++swap_chain_wakeups;

		/* Validate progression. */
		ASSERT_GE(umcg_get_task_tag(utid), idx);
		idx = umcg_get_task_tag(utid);

		/* Validate state. */
		state = umcg_get_task_state(utid);
		ASSERT_TRUE(state == UMCG_TASK_BLOCKED ||
				state == UMCG_TASK_UNBLOCKED);

		ASSERT_EQ(utid, umcg_poll_worker());
	} while (true);

	ASSERT_LT(swap_chain_wakeups, 4);
	if (swap_chain_wakeups)
		fprintf(stderr, "WARNING: %d swap chain wakeups\n",
				swap_chain_wakeups);

	/* Finally run/release all workers. */
	for (idx = 0; idx < n_workers; ++idx) {
		utid = worker_args[idx].utid;
		do {
			utid = umcg_run_worker(utid);
			if (utid) {
				ASSERT_EQ(utid, worker_args[idx].utid);
				ASSERT_EQ(utid, umcg_poll_worker());
			}
		} while (utid != UMCG_NONE);
	}

	/* Cleanup. */
	for (idx = 0; idx < n_workers; ++idx)
		ASSERT_EQ(0, pthread_join(workers[idx], NULL));
	ASSERT_EQ(0, umcg_unregister_task());
	ASSERT_EQ(0, umcg_destroy_group(group_id));
}

TEST_HARNESS_MAIN
