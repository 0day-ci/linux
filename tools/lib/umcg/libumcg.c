// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include "libumcg.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

/* UMCG API version supported by this library. */
static const uint32_t umcg_api_version = 1;

struct umcg_group {
	uint32_t group_id;
};

/**
 * struct umcg_task_tls - per thread struct used to identify/manage UMCG tasks
 *
 * Each UMCG task requires an instance of struct umcg_task passed to
 * sys_umcg_register. This struct contains it, as well as several additional
 * fields.
 */
struct umcg_task_tls {
	struct umcg_task	umcg_task;
	umcg_tid		self;
	intptr_t		tag;
	pid_t			tid;

} __attribute((aligned(4 * sizeof(uint64_t))));

static thread_local struct umcg_task_tls *umcg_task_tls;

umcg_tid umcg_get_utid(void)
{
	return (umcg_tid)&umcg_task_tls;
}

static umcg_tid umcg_task_to_utid(struct umcg_task *ut)
{
	if (!ut)
		return UMCG_NONE;

	return ((struct umcg_task_tls *)ut)->self;
}

static struct umcg_task_tls *utid_to_utls(umcg_tid utid)
{
	if (!utid || !*(struct umcg_task_tls **)utid) {
		fprintf(stderr, "utid_to_utls: NULL\n");
		/* Kill the process rather than corrupt memory. */
		raise(SIGKILL);
		return NULL;
	}
	return *(struct umcg_task_tls **)utid;
}

void umcg_set_task_tag(umcg_tid utid, intptr_t tag)
{
	utid_to_utls(utid)->tag = tag;
}

intptr_t umcg_get_task_tag(umcg_tid utid)
{
	return utid_to_utls(utid)->tag;
}

umcg_tid umcg_register_core_task(intptr_t tag)
{
	int ret;

	if (umcg_task_tls != NULL) {
		errno = EINVAL;
		return UMCG_NONE;
	}

	umcg_task_tls = malloc(sizeof(struct umcg_task_tls));
	if (!umcg_task_tls) {
		errno = ENOMEM;
		return UMCG_NONE;
	}

	umcg_task_tls->umcg_task.state = UMCG_TASK_NONE;
	umcg_task_tls->self = (umcg_tid)&umcg_task_tls;
	umcg_task_tls->tag = tag;
	umcg_task_tls->tid = gettid();

	ret = sys_umcg_register_task(umcg_api_version, UMCG_REGISTER_CORE_TASK,
			UMCG_NOID, &umcg_task_tls->umcg_task);
	if (ret) {
		free(umcg_task_tls);
		umcg_task_tls = NULL;
		errno = ret;
		return UMCG_NONE;
	}

	return umcg_task_tls->self;
}

umcg_tid umcg_register_worker(umcg_t group_id, intptr_t tag)
{
	int ret;
	struct umcg_group *group;

	if (group_id == UMCG_NONE) {
		errno = EINVAL;
		return UMCG_NONE;
	}

	if (umcg_task_tls != NULL) {
		errno = EINVAL;
		return UMCG_NONE;
	}

	group = (struct umcg_group *)group_id;

	umcg_task_tls = malloc(sizeof(struct umcg_task_tls));
	if (!umcg_task_tls) {
		errno = ENOMEM;
		return UMCG_NONE;
	}

	umcg_task_tls->umcg_task.state = UMCG_TASK_NONE;
	umcg_task_tls->self = (umcg_tid)&umcg_task_tls;
	umcg_task_tls->tag = tag;
	umcg_task_tls->tid = gettid();

	ret = sys_umcg_register_task(umcg_api_version, UMCG_REGISTER_WORKER,
			group->group_id, &umcg_task_tls->umcg_task);
	if (ret) {
		free(umcg_task_tls);
		umcg_task_tls = NULL;
		errno = ret;
		return UMCG_NONE;
	}

	return umcg_task_tls->self;
}

umcg_tid umcg_register_server(umcg_t group_id, intptr_t tag)
{
	int ret;
	struct umcg_group *group;

	if (group_id == UMCG_NONE) {
		errno = EINVAL;
		return UMCG_NONE;
	}

	if (umcg_task_tls != NULL) {
		errno = EINVAL;
		return UMCG_NONE;
	}

	group = (struct umcg_group *)group_id;

	umcg_task_tls = malloc(sizeof(struct umcg_task_tls));
	if (!umcg_task_tls) {
		errno = ENOMEM;
		return UMCG_NONE;
	}

	umcg_task_tls->umcg_task.state = UMCG_TASK_NONE;
	umcg_task_tls->self = (umcg_tid)&umcg_task_tls;
	umcg_task_tls->tag = tag;
	umcg_task_tls->tid = gettid();

	ret = sys_umcg_register_task(umcg_api_version, UMCG_REGISTER_SERVER,
			group->group_id, &umcg_task_tls->umcg_task);
	if (ret) {
		free(umcg_task_tls);
		umcg_task_tls = NULL;
		errno = ret;
		return UMCG_NONE;
	}

	return umcg_task_tls->self;
}

int umcg_unregister_task(void)
{
	int ret;

	if (!umcg_task_tls) {
		errno = EINVAL;
		return -1;
	}

	ret = sys_umcg_unregister_task(0);
	if (ret) {
		errno = ret;
		return -1;
	}

	free(umcg_task_tls);
	atomic_store_explicit(&umcg_task_tls, NULL, memory_order_seq_cst);
	return 0;
}

/* Helper return codes. */
enum umcg_prepare_op_result {
	UMCG_OP_DONE,
	UMCG_OP_SYS,
	UMCG_OP_AGAIN,
	UMCG_OP_ERROR
};

static enum umcg_prepare_op_result umcg_prepare_wait(void)
{
	struct umcg_task *ut;
	uint32_t umcg_state;
	int ret;

	if (!umcg_task_tls) {
		errno = EINVAL;
		return UMCG_OP_ERROR;
	}

	ut = &umcg_task_tls->umcg_task;

	umcg_state = UMCG_TASK_RUNNING;
	if (atomic_compare_exchange_strong_explicit(&ut->state,
			&umcg_state, UMCG_TASK_RUNNABLE,
			memory_order_seq_cst, memory_order_seq_cst))
		return UMCG_OP_SYS;

	if (umcg_state != (UMCG_TASK_RUNNING | UMCG_TF_WAKEUP_QUEUED)) {
		fprintf(stderr, "libumcg: unexpected state before wait: %u\n",
				umcg_state);
		errno = EINVAL;
		return UMCG_OP_ERROR;
	}

	if (atomic_compare_exchange_strong_explicit(&ut->state,
			&umcg_state, UMCG_TASK_RUNNING,
			memory_order_seq_cst, memory_order_seq_cst)) {
		return UMCG_OP_DONE;
	}

	/* Raced with another wait/wake? This is not supported. */
	fprintf(stderr, "libumcg: failed to remove the wakeup flag: %u\n",
			umcg_state);
	errno = EINVAL;
	return UMCG_OP_ERROR;
}

static int umcg_do_wait(const struct timespec *timeout)
{
	uint32_t umcg_state;
	int ret;

	do {
		ret = sys_umcg_wait(0, timeout);
		if (ret != 0 && errno != EAGAIN)
			return ret;

		umcg_state = atomic_load_explicit(
				&umcg_task_tls->umcg_task.state,
				memory_order_acquire);
	} while (umcg_state == UMCG_TASK_RUNNABLE);

	return 0;
}

int umcg_wait(const struct timespec *timeout)
{
	switch (umcg_prepare_wait()) {
	case UMCG_OP_DONE:
		return 0;
	case UMCG_OP_SYS:
		break;
	case UMCG_OP_ERROR:
		return -1;
	default:
		fprintf(stderr, "Unknown pre_op result.\n");
		exit(1);
		return -1;
	}

	return umcg_do_wait(timeout);
}

static enum umcg_prepare_op_result umcg_prepare_wake(struct umcg_task_tls *utls)
{
	struct umcg_task *ut = &utls->umcg_task;
	uint32_t umcg_state, next_state;

	next_state = UMCG_TASK_RUNNING;
	umcg_state = UMCG_TASK_RUNNABLE;
	if (atomic_compare_exchange_strong_explicit(&ut->state,
			&umcg_state, next_state,
			memory_order_seq_cst, memory_order_seq_cst))
		return UMCG_OP_SYS;

	if (umcg_state != UMCG_TASK_RUNNING) {
		if (umcg_state == (UMCG_TASK_RUNNING | UMCG_TF_WAKEUP_QUEUED)) {
			/*
			 * With ping-pong mutual swapping using wake/wait
			 * without synchronization this can happen.
			 */
			return UMCG_OP_AGAIN;
		}
		fprintf(stderr, "libumcg: unexpected state in umcg_wake(): %u\n",
				umcg_state);
		errno = EINVAL;
		return UMCG_OP_ERROR;
	}

	if (atomic_compare_exchange_strong_explicit(&ut->state,
			&umcg_state, UMCG_TASK_RUNNING | UMCG_TF_WAKEUP_QUEUED,
			memory_order_seq_cst, memory_order_seq_cst)) {
		return UMCG_OP_DONE;
	}

	if (umcg_state != UMCG_TASK_RUNNABLE) {
		fprintf(stderr, "libumcg: unexpected state in umcg_wake (1): %u\n",
				umcg_state);
		errno = EINVAL;
		return UMCG_OP_ERROR;
	}

	return UMCG_OP_AGAIN;
}

static int umcg_do_wake_or_swap(struct umcg_task_tls *next_utls,
				uint64_t prev_wait_counter, bool should_wait,
				const struct timespec *timeout)
{
	int ret;

again:

	if (should_wait)
		ret = sys_umcg_swap(0, next_utls->tid, 0, timeout);
	else
		ret = sys_umcg_wake(0, next_utls->tid);

	if (ret && errno == EAGAIN)
		goto again;

	return ret;
}

int umcg_wake(umcg_tid next)
{
	struct umcg_task_tls *utls = *(struct umcg_task_tls **)next;
	uint64_t prev_wait_counter;

	if (!utls) {
		errno = EINVAL;
		return -1;
	}

again:
	switch (umcg_prepare_wake(utls)) {
	case UMCG_OP_DONE:
		return 0;
	case UMCG_OP_SYS:
		break;
	case UMCG_OP_ERROR:
		return -1;
	case UMCG_OP_AGAIN:
		goto again;
	default:
		fprintf(stderr, "libumcg: unknown pre_op result.\n");
		exit(1);
		return -1;
	}

	return umcg_do_wake_or_swap(utls, prev_wait_counter, false, NULL);
}

int umcg_swap(umcg_tid next, const struct timespec *timeout)
{
	struct umcg_task_tls *utls = *(struct umcg_task_tls **)next;
	bool should_wake, should_wait;
	uint64_t prev_wait_counter;
	int ret;

	if (!utls) {
		errno = EINVAL;
		return -1;
	}

again:
	switch (umcg_prepare_wake(utls)) {
	case UMCG_OP_DONE:
		should_wake = false;
		break;
	case UMCG_OP_SYS:
		should_wake = true;
		break;
	case UMCG_OP_ERROR:
		return -1;
	case UMCG_OP_AGAIN:
		goto again;
	default:
		fprintf(stderr, "lubumcg: unknown pre_op result.\n");
		exit(1);
		return -1;
	}

	switch (umcg_prepare_wait()) {
	case UMCG_OP_DONE:
		should_wait = false;
		break;
	case UMCG_OP_SYS:
		should_wait = true;
		break;
	case UMCG_OP_ERROR:
		return -1;
	default:
		fprintf(stderr, "lubumcg: unknown pre_op result.\n");
		exit(1);
		return -1;
	}

	if (should_wake)
		return umcg_do_wake_or_swap(utls, prev_wait_counter,
				should_wait, timeout);

	if (should_wait)
		return umcg_do_wait(timeout);

	return 0;
}

umcg_t umcg_create_group(uint32_t flags)
{
	int res = sys_umcg_create_group(umcg_api_version, flags);
	struct umcg_group *group;

	if (res < 0) {
		errno = -res;
		return -1;
	}

	group = malloc(sizeof(struct umcg_group));
	if (!group) {
		errno = ENOMEM;
		return UMCG_NONE;
	}

	group->group_id = res;
	return (intptr_t)group;
}

int umcg_destroy_group(umcg_t umcg)
{
	int res;
	struct umcg_group *group = (struct umcg_group *)umcg;

	res = sys_umcg_destroy_group(group->group_id);
	if (res) {
		errno = -res;
		return -1;
	}

	free(group);
	return 0;
}

umcg_tid umcg_poll_worker(void)
{
	struct umcg_task *server_ut = &umcg_task_tls->umcg_task;
	struct umcg_task *worker_ut;
	uint32_t expected_state;
	int ret;

	expected_state = UMCG_TASK_PROCESSING;
	if (!atomic_compare_exchange_strong_explicit(&server_ut->state,
			&expected_state, UMCG_TASK_POLLING,
			memory_order_seq_cst, memory_order_seq_cst)) {
		fprintf(stderr, "umcg_poll_worker: wrong server state before: %u\n",
				expected_state);
		exit(1);
		return UMCG_NONE;
	}
	ret = sys_umcg_poll_worker(0, &worker_ut);

	expected_state = UMCG_TASK_POLLING;
	if (!atomic_compare_exchange_strong_explicit(&server_ut->state,
			&expected_state, UMCG_TASK_PROCESSING,
			memory_order_seq_cst, memory_order_seq_cst)) {
		fprintf(stderr, "umcg_poll_worker: wrong server state after: %u\n",
				expected_state);
		exit(1);
		return UMCG_NONE;
	}

	if (ret) {
		fprintf(stderr, "sys_umcg_poll_worker: unexpected result %d\n",
				errno);
		exit(1);
		return UMCG_NONE;
	}

	return umcg_task_to_utid(worker_ut);
}

umcg_tid umcg_run_worker(umcg_tid worker)
{
	struct umcg_task_tls *worker_utls;
	struct umcg_task *server_ut = &umcg_task_tls->umcg_task;
	struct umcg_task *worker_ut;
	uint32_t expected_state;
	int ret;

	worker_utls = atomic_load_explicit((struct umcg_task_tls **)worker,
			memory_order_seq_cst);
	if (!worker_utls)
		return UMCG_NONE;

	worker_ut = &worker_utls->umcg_task;

	expected_state = UMCG_TASK_RUNNABLE;
	if (!atomic_compare_exchange_strong_explicit(&worker_ut->state,
			&expected_state, UMCG_TASK_RUNNING,
			memory_order_seq_cst, memory_order_seq_cst)) {
		fprintf(stderr, "umcg_run_worker: wrong worker state: %u\n",
				expected_state);
		exit(1);
		return UMCG_NONE;
	}

	expected_state = UMCG_TASK_PROCESSING;
	if (!atomic_compare_exchange_strong_explicit(&server_ut->state,
			&expected_state, UMCG_TASK_SERVING,
			memory_order_seq_cst, memory_order_seq_cst)) {
		fprintf(stderr, "umcg_run_worker: wrong server state: %u\n",
				expected_state);
		exit(1);
		return UMCG_NONE;
	}

again:
	ret = sys_umcg_run_worker(0, worker_utls->tid, &worker_ut);
	if (ret && errno == EAGAIN)
		goto again;

	if (ret) {
		fprintf(stderr, "umcg_run_worker failed: %d %d\n", ret, errno);
		return UMCG_NONE;
	}

	expected_state = UMCG_TASK_SERVING;
	if (!atomic_compare_exchange_strong_explicit(&server_ut->state,
			&expected_state, UMCG_TASK_PROCESSING,
			memory_order_seq_cst, memory_order_seq_cst)) {
		fprintf(stderr, "umcg_run_worker: wrong server state: %u\n",
				expected_state);
		exit(1);
		return UMCG_NONE;
	}

	return umcg_task_to_utid(worker_ut);
}

uint32_t umcg_get_task_state(umcg_tid task)
{
	struct umcg_task_tls *utls = atomic_load_explicit(
			(struct umcg_task_tls **)task, memory_order_seq_cst);

	if (!utls)
		return UMCG_TASK_NONE;

	return atomic_load_explicit(&utls->umcg_task.state, memory_order_relaxed);
}
