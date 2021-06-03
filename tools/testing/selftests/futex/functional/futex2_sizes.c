// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *   Copyright Collabora Ltd., 2021
 *
 * DESCRIPTION
 *	Test wait/wake mechanism of futex2, using 32bit sized futexes.
 *
 * AUTHOR
 *	André Almeida <andrealmeid@collabora.com>
 *
 * HISTORY
 *      2021-Feb-5: Initial version by André <andrealmeid@collabora.com>
 *
 *****************************************************************************/

#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include "futex2test.h"
#include "logging.h"

#define TEST_NAME "futex2-sizes"

#define futex8  uint8_t
#define futex16 uint16_t
#define futex32 uint32_t
#define futex64 uint64_t

// edge case values, to test sizes
#define VALUE16 257        // 2^8  + 1
#define VALUE32 65537      // 2^16 + 1
#define VALUE64 4294967297 // 2^32 + 1

#define WAKE_WAIT_US 100000

void *futex;

void usage(char *prog)
{
	printf("Usage: %s\n", prog);
	printf("  -c	Use color\n");
	printf("  -h	Display this help message\n");
	printf("  -v L	Verbosity level: %d=QUIET %d=CRITICAL %d=INFO\n",
	       VQUIET, VCRITICAL, VINFO);
}

struct futex {
	unsigned long flags;
	unsigned long val;
};

void *waiterfn(void *arg)
{
	int ret;
	unsigned int *flags = (unsigned int *) arg;

	ret = futex2_wait(futex, 0, *flags, NULL);
	if (ret == ERROR)
		error("waiter failed %d errno %d\n", ret, errno);

	return NULL;
}

/*
 * create a thread to wait, then wake it
 */
void test_single_waiter(unsigned int flags, int *ret)
{
	pthread_t waiter;
	int res;

	pthread_create(&waiter, NULL, waiterfn, &flags);

	usleep(WAKE_WAIT_US);

	info("Calling futex2_wake at addr %p flags %u\n", futex, flags);
	res = futex2_wake(futex, 1, flags);
	if (res == 1) {
		ksft_test_result_pass("futex2_sizes\n");
	} else {
		ksft_test_result_fail("futex2_sizes returned: %d %s\n",
				      errno, strerror(errno));
		*ret = RET_FAIL;
	}
}

int main(int argc, char *argv[])
{
	int res, ret = RET_PASS, fd, c, shm_id;
	u_int32_t f_private = 0;
	pthread_t waiter;

	futex8  f8 = 0;
	futex16 f16 = 0;
	futex32 f32 = 0;
	futex64 f64 = 0;
	unsigned int flags = 0;

	while ((c = getopt(argc, argv, "cht:v:")) != -1) {
		switch (c) {
		case 'c':
			log_color(1);
			break;
		case 'h':
			usage(basename(argv[0]));
			exit(0);
		case 'v':
			log_verbosity(atoi(optarg));
			break;
		default:
			usage(basename(argv[0]));
			exit(1);
		}
	}

	ksft_print_header();
	ksft_set_plan(4);
	ksft_print_msg("%s: Test FUTEX2_SIZES\n", basename(argv[0]));

	info("Calling futex2_wait futex: %p\n", futex);
	futex = &f8;
	flags = FUTEX_8;
	test_single_waiter(flags, &ret);

	futex = &f16;
	flags = FUTEX_16;
	test_single_waiter(flags, &ret);

	futex = &f32;
	flags = FUTEX_32;
	test_single_waiter(flags, &ret);

	futex = &f64;
	flags = FUTEX_64;
	test_single_waiter(flags, &ret);

	ksft_print_cnts();
	return ret;
}
