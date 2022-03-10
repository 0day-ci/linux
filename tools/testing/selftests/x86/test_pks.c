// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

/**
 * DOC: PKS_TEST_USER
 *
 * To assist in executing the tests 'test_pks' can be built from the
 * tools/testing directory.  See the help output for details.
 *
 * .. code-block:: sh
 *
 *	$ cd tools/testing/selftests/x86
 *	$ make test_pks
 *	$ ./test_pks_64 -h
 *	...
 */
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#define DYN_DBG_CNT_FILE "/sys/kernel/debug/dynamic_debug/control"
#define PKS_TEST_FILE "/sys/kernel/debug/x86/run_pks"

/* Values from the kernel */
#define CHECK_DEFAULTS		"0"
#define RUN_SINGLE		"1"
#define ARM_CTX_SWITCH		"2"
#define CHECK_CTX_SWITCH	"3"
#define RUN_EXCEPTION		"4"
#define RUN_EXCEPTION_UPDATE	"5"
#define RUN_CRASH_TEST		"9"

time_t g_start_time;
int g_debug;
unsigned long g_cpu;

#define PRINT_DEBUG(fmt, ...) \
	do { \
		if (g_debug) \
			printf("%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define PRINT_ERROR(fmt, ...) \
	fprintf(stderr, "%s: " fmt, __func__, ##__VA_ARGS__)

static int do_simple_test(const char *debugfs_str);
static int do_context_switch(const char *debugfs_str);

/*
 * The crash test is a special case which is not included in the run all
 * option.  Do not add it here.
 */
enum {
	TEST_DEFAULTS = 0,
	TEST_SINGLE,
	TEST_CTX_SWITCH,
	TEST_EXCEPTION,
	TEST_FAULT_CALLBACK,
	MAX_TESTS,
} tests;

/* Special */
#define CREATE_FAULT_TEST_NAME "create_fault"

struct test_item {
	char *name;
	const char *debugfs_str;
	int (*test_fn)(const char *debugfs_str);
} test_list[] = {
	{ "check_defaults", CHECK_DEFAULTS, do_simple_test },
	{ "single", RUN_SINGLE, do_simple_test },
	{ "context_switch", ARM_CTX_SWITCH, do_context_switch },
	{ "exception", RUN_EXCEPTION, do_simple_test },
	{ "exception_update", RUN_EXCEPTION_UPDATE, do_simple_test }
};

static char *get_test_name(int test_num)
{
	if (test_num > MAX_TESTS)
		return "<UNKNOWN>";
	/* Special: not in run all */
	if (test_num == MAX_TESTS)
		return CREATE_FAULT_TEST_NAME;
	return test_list[test_num].name;
}

static int get_test_num(char *test_name)
{
	int i;

	/* Special: not in run all */
	if (strcmp(test_name, CREATE_FAULT_TEST_NAME) == 0)
		return MAX_TESTS;

	for (i = 0; i < MAX_TESTS; i++)
		if (strcmp(test_name, test_list[i].name) == 0)
			return i;
	return -1;
}

static void print_help_and_exit(char *argv0)
{
	int i;

	printf("Usage: %s [-h,-d] [test]\n", argv0);
	printf("	--help,-h   This help\n");
	printf("	--debug,-d  Output kernel debug via dynamic debug if available\n");
	printf("	--cpu,-c <cpu>  Use 'cpu' for context switch default 0\n");
	printf("\n");
	printf("        Run all PKS tests or the [test] specified.\n");
	printf("\n");
	printf("	[test] can be one of:\n");

	for (i = 0; i < MAX_TESTS; i++)
		printf("	       '%s'\n", get_test_name(i));

	/* Special: not in run all */
	printf("	       '%s' (Not included in run all)\n",
		CREATE_FAULT_TEST_NAME);

	printf("\n");
}

/*
 * debugfs_str is ignored for this test.
 */
static int do_context_switch(const char *debugfs_str)
{
	int switch_done[2];
	int setup_done[2];
	cpu_set_t cpuset;
	char result[32];
	char done = 'P';
	int rc = 0;
	pid_t pid;
	int fd;

	if (g_cpu >= sysconf(_SC_NPROCESSORS_ONLN)) {
		PRINT_ERROR("CPU %lu is invalid\n", g_cpu);
		g_cpu = sysconf(_SC_NPROCESSORS_ONLN) - 1;
		PRINT_ERROR("   running on max CPU: %lu\n", g_cpu);
	}

	CPU_ZERO(&cpuset);
	CPU_SET(g_cpu, &cpuset);
	/*
	 * Ensure the two processes run on the same CPU so that they go through
	 * a context switch.
	 */
	sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset);

	if (pipe(setup_done)) {
		PRINT_ERROR("ERROR: Failed to create pipe\n");
		return -EIO;
	}
	if (pipe(switch_done)) {
		PRINT_ERROR("ERROR: Failed to create pipe\n");
		return -EIO;
	}

	fd = open(PKS_TEST_FILE, O_RDWR);
	if (fd < 0) {
		PRINT_DEBUG("Failed to open test file : %s\n", PKS_TEST_FILE);
		return -ENOENT;
	}

	/* Avoid duplicated output after fork */
	fflush(stderr);
	fflush(stdout);

	pid = fork();
	if (pid == 0) {
		char done = 'P';

		g_cpu = sched_getcpu();
		PRINT_DEBUG("Child: running on cpu %lu...\n", g_cpu);

		/* Allocate and run test. */
		write(fd, RUN_SINGLE, 1);

		/* Arm for context switch test */
		write(fd, ARM_CTX_SWITCH, 1);

		PRINT_DEBUG("Child: Tell parent to go\n");
		write(setup_done[1], &done, sizeof(done));

		/* Context switch out... */
		PRINT_DEBUG("Child: Waiting for parent...\n");
		read(switch_done[0], &done, sizeof(done));

		/* Check msr restored */
		PRINT_DEBUG("Child: Checking result\n");
		rc = write(fd, CHECK_CTX_SWITCH, 1);
		if (rc < 0) {
			if (errno == ENOENT) {
				sprintf(result, "SKIP");
				done = 'S';
			} else {
				sprintf(result, "FAIL");
				done = 'F';
			}
			goto child_exit;
		}

		read(fd, result, 10);
		if (strncmp(result, "PASS", 4))
			done = 'F';

child_exit:
		PRINT_DEBUG("Child: Result (%c) %s\n", done, result);

		/* Signal result */
		write(setup_done[1], &done, sizeof(done));
		close(fd);

		exit(0);
	}

	PRINT_DEBUG("Parent: Waiting for child\n");
	read(setup_done[0], &done, sizeof(done));
	g_cpu = sched_getcpu();
	PRINT_DEBUG("Parent: running on cpu %lu\n", g_cpu);

	/* The parent needs a unique file context within the kernel */
	close(fd);
	fd = open(PKS_TEST_FILE, O_RDWR);
	if (fd < 0) {
		PRINT_ERROR("FATAL ERROR: cannot open %s\n", PKS_TEST_FILE);
		PRINT_DEBUG("Parent: Signaling child 'fail'\n");
		done = 'F';
		write(switch_done[1], &done, sizeof(done));
		return -ENOENT;
	}

	/* run test with the same pkey */
	rc = write(fd, RUN_SINGLE, 1);

	PRINT_DEBUG("Parent: Signaling child\n");
	write(switch_done[1], &done, sizeof(done));

	if (rc < 0) {
		rc = -errno;
		goto close_file;
	}
	rc = 0;

	/* Wait for result */
	read(setup_done[0], &done, sizeof(done));
	if (done == 'S')
		rc = -ENOENT;
	if (done == 'F')
		rc = -EFAULT;

	PRINT_DEBUG("Parent: exiting with rc (%c) %d\n", done, rc);

close_file:
	close(fd);
	return rc;
}

/*
 * Do a simple test of writing the debugfs value and reading back for 'PASS'
 */
static int do_simple_test(const char *debugfs_str)
{
	char str[16];
	int fd, rc = 0;

	fd = open(PKS_TEST_FILE, O_RDWR);
	if (fd < 0) {
		PRINT_DEBUG("Failed to open test file : %s\n", PKS_TEST_FILE);
		return -ENOENT;
	}

	rc = write(fd, debugfs_str, strlen(debugfs_str));
	if (rc < 0) {
		rc = -errno;
		goto close_file;
	}

	rc = read(fd, str, 16);
	if (rc < 0)
		goto close_file;

	str[15] = '\0';

	if (strncmp(str, "PASS", 4)) {
		PRINT_ERROR("result: %s\n", str);
		rc = -EFAULT;
		goto close_file;
	}

	rc = 0;

close_file:
	close(fd);
	return rc;
}

/*
 * This test is special in that it requires the option to be written 2 times.
 * In addition because it creates a fault it is not included in the run all
 * test suite.
 */
static int create_fault(void)
{
	char str[16];
	int fd, rc = 0;

	fd = open(PKS_TEST_FILE, O_RDWR);
	if (fd < 0) {
		PRINT_DEBUG("Failed to open test file : %s\n", PKS_TEST_FILE);
		return -ENOENT;
	}

	rc = write(fd, "9", 1);
	if (rc < 0) {
		rc = -errno;
		goto close_file;
	}

	rc = write(fd, "9", 1);
	if (rc < 0)
		goto close_file;

	rc = read(fd, str, 16);
	if (rc < 0)
		goto close_file;

	str[15] = '\0';

	if (strncmp(str, "PASS", 4)) {
		PRINT_ERROR("result: %s\n", str);
		rc = -EFAULT;
		goto close_file;
	}

	rc = 0;

close_file:
	close(fd);
	return rc;
}

static int run_one(int test_num)
{
	int ret;

	printf("[RUN]\t%s\n", get_test_name(test_num));

	if (test_num == MAX_TESTS)
		/* Special: not in run all */
		ret = create_fault();
	else
		ret = test_list[test_num].test_fn(test_list[test_num].debugfs_str);

	if (ret == -ENOENT) {
		printf("[SKIP] Test not supported\n");
		return 0;
	} else if (ret) {
		printf("[FAIL]\n");
		return 1;
	}

	printf("[OK]\n");
	return 0;
}

static int run_all(void)
{
	int i, rc = 0;

	for (i = 0; i < MAX_TESTS; i++) {
		int ret = run_one(i);

		/* sticky fail */
		if (ret)
			rc = ret;
	}

	return rc;
}

#define STR_LEN 256

/* Debug output in the kernel is through dynamic debug */
static void setup_debug(void)
{
	char str[STR_LEN];
	int fd, rc;

	g_start_time = time(NULL);

	fd = open(DYN_DBG_CNT_FILE, O_RDWR);
	if (fd < 0) {
		PRINT_ERROR("Dynamic debug not available: Failed to open: %s\n",
			DYN_DBG_CNT_FILE);
		return;
	}

	snprintf(str, STR_LEN, "file pks_test.c +pflm");

	rc = write(fd, str, strlen(str));
	if (rc != strlen(str))
		PRINT_ERROR("ERROR: Failed to set up dynamic debug...\n");

	close(fd);
}

static void print_debug(void)
{
	char str[STR_LEN];
	struct tm *tm;
	int fd, rc;

	fd = open(DYN_DBG_CNT_FILE, O_RDWR);
	if (fd < 0)
		return;

	snprintf(str, STR_LEN, "file pks_test.c -p");

	rc = write(fd, str, strlen(str));
	if (rc != strlen(str))
		PRINT_ERROR("ERROR: Failed to turn off dynamic debug...\n");

	close(fd);

	/*
	 * dmesg is not accurate with time stamps so back up the start time a
	 * bit to ensure all the output from this run is dumped.
	 */
	g_start_time -= 5;
	tm = localtime(&g_start_time);

	snprintf(str, STR_LEN,
		 "dmesg -H --since '%d-%d-%d %d:%d:%d' | grep pks_test",
		 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec);
	system(str);
	printf("\tDebug output command (approximate start time):\n\t\t%s\n",
		str);
}

int main(int argc, char *argv[])
{
	int flag_all = 1;
	int test_num = 0;
	int rc;

	while (1) {
		static struct option long_options[] = {
			{"help",	no_argument,		0,	'h' },
			{"debug",	no_argument,		0,	'd' },
			{"cpu",		required_argument,	0,	'c' },
			{0,		0,			0,	0 }
		};
		int option_index = 0;
		int c;

		c = getopt_long(argc, argv, "hd", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_help_and_exit(argv[0]);
			return 0;
		case 'd':
			g_debug++;
			break;
		case 'c':
			g_cpu = strtoul(optarg, NULL, 0);
			break;
		default:
			print_help_and_exit(argv[0]);
			exit(-1);
		}
	}

	if (optind < argc) {
		test_num = get_test_num(argv[optind]);
		if (test_num < 0) {
			printf("[RUN]\t'%s'\n[SKIP]\tInvalid test\n", argv[optind]);
			return 1;
		}

		flag_all = 0;
	}

	if (g_debug)
		setup_debug();

	if (flag_all)
		rc = run_all();
	else
		rc = run_one(test_num);

	if (g_debug)
		print_debug();

	return rc;
}
