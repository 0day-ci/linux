// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021 HiSilicon Limited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <stdbool.h>

#include "ptr_ring_test.h"
#include "../../../../include/linux/ptr_ring.h"

#define MIN_RING_SIZE	2
#define MAX_RING_SIZE	10000000

static struct ptr_ring ring ____cacheline_aligned_in_smp;

struct worker_info {
	pthread_t tid;
	int test_count;
	bool error;
};

static void *produce_worker(void *arg)
{
	struct worker_info *info = arg;
	unsigned long i = 0;
	int ret;

	while (++i <= info->test_count) {
		while (__ptr_ring_full(&ring))
			cpu_relax();

		ret = __ptr_ring_produce(&ring, (void *)i);
		if (ret) {
			fprintf(stderr, "produce failed: %d\n", ret);
			info->error = true;
			return NULL;
		}
	}

	info->error = false;

	return NULL;
}

static void *consume_worker(void *arg)
{
	struct worker_info *info = arg;
	unsigned long i = 0;
	int *ptr;

	while (++i <= info->test_count) {
		while (__ptr_ring_empty(&ring))
			cpu_relax();

		ptr = __ptr_ring_consume(&ring);
		if ((unsigned long)ptr != i) {
			fprintf(stderr, "consumer failed, ptr: %lu, i: %lu\n",
				(unsigned long)ptr, i);
			info->error = true;
			return NULL;
		}
	}

	if (!__ptr_ring_empty(&ring)) {
		fprintf(stderr, "ring should be empty, test failed\n");
		info->error = true;
		return NULL;
	}

	info->error = false;
	return NULL;
}

/* test case for single producer single consumer */
static void spsc_test(int size, int count)
{
	struct worker_info producer, consumer;
	pthread_attr_t attr;
	void *res;
	int ret;

	ret = ptr_ring_init(&ring, size, 0);
	if (ret) {
		fprintf(stderr, "init failed: %d\n", ret);
		return;
	}

	producer.test_count = count;
	consumer.test_count = count;

	ret = pthread_attr_init(&attr);
	if (ret) {
		fprintf(stderr, "pthread attr init failed: %d\n", ret);
		goto out;
	}

	ret = pthread_create(&producer.tid, &attr,
			     produce_worker, &producer);
	if (ret) {
		fprintf(stderr, "create producer thread failed: %d\n", ret);
		goto out;
	}

	ret = pthread_create(&consumer.tid, &attr,
			     consume_worker, &consumer);
	if (ret) {
		fprintf(stderr, "create consumer thread failed: %d\n", ret);
		goto out;
	}

	ret = pthread_join(producer.tid, &res);
	if (ret) {
		fprintf(stderr, "join producer thread failed: %d\n", ret);
		goto out;
	}

	ret = pthread_join(consumer.tid, &res);
	if (ret) {
		fprintf(stderr, "join consumer thread failed: %d\n", ret);
		goto out;
	}

	if (producer.error || consumer.error) {
		fprintf(stderr, "spsc test failed\n");
		goto out;
	}

	printf("ptr_ring(size:%d) perf spsc test produced/comsumed %d items, finished\n",
	       size, count);
out:
	ptr_ring_cleanup(&ring, NULL);
}

static void simple_test(int size, int count)
{
	struct timeval start, end;
	int i = 0;
	int *ptr;
	int ret;

	ret = ptr_ring_init(&ring, size, 0);
	if (ret) {
		fprintf(stderr, "init failed: %d\n", ret);
		return;
	}

	while (++i <= count) {
		ret = __ptr_ring_produce(&ring, &count);
		if (ret) {
			fprintf(stderr, "produce failed: %d\n", ret);
			goto out;
		}

		ptr = __ptr_ring_consume(&ring);
		if (ptr != &count)  {
			fprintf(stderr, "consume failed: %p\n", ptr);
			goto out;
		}
	}

	printf("ptr_ring(size:%d) perf simple test produced/consumed %d items, finished\n",
	       size, count);

out:
	ptr_ring_cleanup(&ring, NULL);
}

int main(int argc, char *argv[])
{
	int count = 1000000;
	int size = 1000;
	int mode = 0;
	int opt;

	while ((opt = getopt(argc, argv, "N:s:m:h")) != -1) {
		switch (opt) {
		case 'N':
			count = atoi(optarg);
			break;
		case 's':
			size = atoi(optarg);
			break;
		case 'm':
			mode = atoi(optarg);
			break;
		case 'h':
			printf("usage: ptr_ring_test [-N COUNT] [-s RING_SIZE] [-m TEST_MODE]\n");
			return 0;
		default:
			return -1;
		}
	}

	if (count <= 0) {
		fprintf(stderr, "invalid test count, must be > 0\n");
		return -1;
	}

	if (size < MIN_RING_SIZE || size > MAX_RING_SIZE) {
		fprintf(stderr, "invalid ring size, must be in %d-%d\n",
			MIN_RING_SIZE, MAX_RING_SIZE);
		return -1;
	}

	switch (mode) {
	case 0:
		simple_test(size, count);
		break;
	case 1:
		spsc_test(size, count);
		break;
	default:
		fprintf(stderr, "invalid test mode\n");
		return -1;
	}

	return 0;
}
