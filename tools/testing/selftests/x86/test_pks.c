// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 *
 * User space tool to test PKS operations.  Accesses test code through
 * <debugfs>/x86/run_pks when CONFIG_PKS_TEST is enabled.
 */

#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>

#define PKS_TEST_FILE "/sys/kernel/debug/x86/run_pks"

#define RUN_SINGLE		"1"
#define ARM_CTX_SWITCH		"2"
#define CHECK_CTX_SWITCH	"3"

void print_help_and_exit(char *argv0)
{
	printf("Usage: %s [-h] <cpu>\n", argv0);
	printf("	--help,-h  This help\n");
	printf("\n");
	printf("	Run a context switch test on <cpu> (Default: 0)\n");
}

int check_context_switch(int cpu)
{
	int switch_done[2];
	int setup_done[2];
	cpu_set_t cpuset;
	char result[32];
	int rc = 0;
	pid_t pid;
	int fd;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);
	/*
	 * Ensure the two processes run on the same CPU so that they go through
	 * a context switch.
	 */
	sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset);

	if (pipe(setup_done)) {
		printf("ERROR: Failed to create pipe\n");
		return -1;
	}
	if (pipe(switch_done)) {
		printf("ERROR: Failed to create pipe\n");
		return -1;
	}

	pid = fork();
	if (pid == 0) {
		char done = 'y';

		fd = open(PKS_TEST_FILE, O_RDWR);
		if (fd < 0) {
			printf("ERROR: cannot open %s\n", PKS_TEST_FILE);
			return -1;
		}

		cpu = sched_getcpu();
		printf("Child running on cpu %d...\n", cpu);

		/* Allocate and run test. */
		write(fd, RUN_SINGLE, 1);

		/* Arm for context switch test */
		write(fd, ARM_CTX_SWITCH, 1);

		printf("   tell parent to go\n");
		write(setup_done[1], &done, sizeof(done));

		/* Context switch out... */
		printf("   Waiting for parent...\n");
		read(switch_done[0], &done, sizeof(done));

		/* Check msr restored */
		printf("Checking result\n");
		write(fd, CHECK_CTX_SWITCH, 1);

		read(fd, result, 10);
		printf("   #PF, context switch, pkey allocation and free tests: %s\n", result);
		if (!strncmp(result, "PASS", 10)) {
			rc = -1;
			done = 'F';
		}

		/* Signal result */
		write(setup_done[1], &done, sizeof(done));
	} else {
		char done = 'y';

		read(setup_done[0], &done, sizeof(done));
		cpu = sched_getcpu();
		printf("Parent running on cpu %d\n", cpu);

		fd = open(PKS_TEST_FILE, O_RDWR);
		if (fd < 0) {
			printf("ERROR: cannot open %s\n", PKS_TEST_FILE);
			return -1;
		}

		/* run test with the same pkey */
		write(fd, RUN_SINGLE, 1);

		printf("   Signaling child.\n");
		write(switch_done[1], &done, sizeof(done));

		/* Wait for result */
		read(setup_done[0], &done, sizeof(done));
		if (done == 'F')
			rc = -1;
	}

	close(fd);

	return rc;
}

int main(int argc, char *argv[])
{
	int cpu = 0;
	int rc;
	int c;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"help",	no_argument,	0,	'h' },
			{0,		0,		0,	0 }
		};

		c = getopt_long(argc, argv, "h", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_help_and_exit(argv[0]);
			break;
		}
	}

	if (optind < argc)
		cpu = strtoul(argv[optind], NULL, 0);

	if (cpu >= sysconf(_SC_NPROCESSORS_ONLN)) {
		printf("CPU %d is invalid\n", cpu);
		cpu = sysconf(_SC_NPROCESSORS_ONLN) - 1;
		printf("   running on max CPU: %d\n", cpu);
	}

	rc = check_context_switch(cpu);

	return rc;
}
