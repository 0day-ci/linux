// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 ARM Limited.
 * Original author: Mark Brown <broonie@kernel.org>
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <asm/sigcontext.h>
#include <asm/ptrace.h>

#include "../../kselftest.h"
#include "rdvl.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define MIN_VL 16

struct vec_data {
	const char *name;
	unsigned long hwcap_type;
	unsigned long hwcap;
	const char *rdvl_binary;
	int (*rdvl)(void);

	int prctl_get;
	int prctl_set;
	const char *default_vl_file;

	int default_vl;
	int min_vl;
	int max_vl;
} vec_data[] = {
	{
		.name = "SVE",
		.hwcap_type = AT_HWCAP,
		.hwcap = HWCAP_SVE,
		.rdvl = rdvl_sve,
		.rdvl_binary = "./rdvl-sve",
		.prctl_get = PR_SVE_GET_VL,
		.prctl_set = PR_SVE_SET_VL,
		.default_vl_file = "/proc/sys/abi/sve_default_vector_length",
	},
};

/* Start a new process and return the vector length it sees */
int get_child_rdvl(struct vec_data *data)
{
	char buf[10];
	int pipefd[2];
	pid_t pid, child;
	int read_vl, ret;

	ret = pipe(pipefd);
	if (ret == -1) {
		ksft_print_msg("pipe() failed: %d (%s)\n",
			       errno, strerror(errno));
		return -1;
	}

	child = fork();
	if (child == -1) {
		ksft_print_msg("fork() failed: %d (%s)\n",
			       errno, strerror(errno));
		return -1;
	}

	/* Child: put vector length on the pipe */
	if (child == 0) {
		/*
		 * Replace stdout with the pipe, errors to stderr from
		 * here as kselftest prints to stdout.
		 */
		ret = dup2(pipefd[1], 1);
		if (ret == -1) {
			fprintf(stderr, "dup2() %d\n", errno);
			exit(-1);
		}

		/* exec() a new binary which puts the VL on stdout */
		ret = execl(data->rdvl_binary, data->rdvl_binary, NULL);
		fprintf(stderr, "execl(%s) failed: %d\n",
			data->rdvl_binary, errno, strerror(errno));

		exit(-1);
	}

	close(pipefd[1]);

	/* Parent; wait for the exit status from the child & verify it */
	while (1) {
		pid = wait(&ret);
		if (pid == -1) {
			ksft_print_msg("wait() failed: %d (%s)\n",
				       errno, strerror(errno));
			return -1;
		}

		if (pid != child)
			continue;

		if (!WIFEXITED(ret)) {
			ksft_print_msg("child exited abnormally\n");
			return -1;
		}

		if (WEXITSTATUS(ret) != 0) {
			ksft_print_msg("child returned error %d\n",
				       WEXITSTATUS(ret));
			return -1;
		}

		memset(buf, 0, sizeof(buf));
		ret = read(pipefd[0], buf, sizeof(buf) - 1);
		if (ret <= 0) {
			ksft_print_msg("read() failed: %d (%s)\n",
				       errno, strerror(errno));
			return -1;
		}
		close(pipefd[0]);

		ret = sscanf(buf, "%d", &read_vl);
		if (ret != 1) {
			ksft_print_msg("failed to parse VL from '%s'\n",
				       buf);
			return -1;
		}

		return read_vl;
	}
}

int file_read_integer(const char *name, int *val)
{
	char buf[40];
	int f, ret;

	f = open(name, O_RDONLY);
	if (f < 0) {
		ksft_test_result_fail("Unable to open %s: %d (%s)\n",
				      name, errno,
				      strerror(errno));
		return -1;
	}

	memset(buf, 0, sizeof(buf));
	ret = read(f, buf, sizeof(buf) - 1);
	if (ret < 0) {
		ksft_test_result_fail("Error reading %s: %d (%s)\n",
				      name, errno, strerror(errno));
		return -1;
	}
	close(f);

	ret = sscanf(buf, "%d", val);
	if (ret != 1) {
		ksft_test_result_fail("Failed to parse %s\n", name);
		return -1;
	}

	return 0;
}

int file_write_integer(const char *name, int val)
{
	char buf[40];
	int f, ret;

	f = open(name, O_WRONLY);
	if (f < 0) {
		ksft_test_result_fail("Unable to open %s: %d (%s)\n",
				      name, errno,
				      strerror(errno));
		return -1;
	}

	snprintf(buf, sizeof(buf), "%d", val);
	ret = write(f, buf, strlen(buf));
	if (ret < 0) {
		ksft_test_result_fail("Error writing %d to %s: %d (%s)\n",
				      val, name, errno, strerror(errno));
		return -1;
	}
	close(f);

	return 0;
}

/*
 * Verify that we can read the default VL via proc, checking that it
 * is set in a freshly spawned child.
 */
void proc_read_default(struct vec_data *data)
{
	int default_vl, child_vl, ret;

	ret = file_read_integer(data->default_vl_file, &default_vl);
	if (ret != 0)
		return;

	/* Is this the actual default seen by new processes? */
	child_vl = get_child_rdvl(data);
	if (child_vl != default_vl) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      default_vl, child_vl);
		return;
	}

	ksft_test_result_pass("%s default vector length %d\n", data->name,
			      default_vl);
	data->default_vl = default_vl;
}

/* Verify that we can write a minimum value and have it take effect */
void proc_write_min(struct vec_data *data)
{
	int ret, new_default, child_vl;

	ret = file_write_integer(data->default_vl_file, MIN_VL);
	if (ret != 0)
		return;

	/* What was the new value? */
	ret = file_read_integer(data->default_vl_file, &new_default);
	if (ret != 0)
		return;

	/* Did it take effect in a new process? */
	child_vl = get_child_rdvl(data);
	if (child_vl != new_default) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      new_default, child_vl);
		return;
	}

	ksft_test_result_pass("%s minimum vector length %d\n", data->name,
			      new_default);
	data->min_vl = new_default;

	file_write_integer(data->default_vl_file, data->default_vl);
}

/* Verify that we can write a maximum value and have it take effect */
void proc_write_max(struct vec_data *data)
{
	int ret, new_default, child_vl;

	/* -1 is accepted by the /proc interface as the maximum VL */
	ret = file_write_integer(data->default_vl_file, -1);
	if (ret != 0)
		return;

	/* What was the new value? */
	ret = file_read_integer(data->default_vl_file, &new_default);
	if (ret != 0)
		return;

	/* Did it take effect in a new process? */
	child_vl = get_child_rdvl(data);
	if (child_vl != new_default) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      new_default, child_vl);
		return;
	}

	ksft_test_result_pass("%s maximum vector length %d\n", data->name,
			      new_default);
	data->max_vl = new_default;

	file_write_integer(data->default_vl_file, data->default_vl);
}

/* Can we read back a VL from prctl? */
void prctl_get(struct vec_data *data)
{
	int ret;

	ret = prctl(data->prctl_get);
	if (ret == -1) {
		ksft_test_result_fail("%s prctl() read failed: %d (%s)\n",
				      data->name, errno, strerror(errno));
		return;
	}

	/* Mask out any flags */
	ret &= PR_SVE_VL_LEN_MASK;

	/* Is that what we can read back directly? */
	if (ret == data->rdvl())
		ksft_test_result_pass("%s current VL is %d\n",
				      data->name, ret);
	else
		ksft_test_result_fail("%s prctl() VL %d but RDVL is %d\n",
				      data->name, ret, data->rdvl());
}

/* Does the prctl let us set the VL we already have? */
void prctl_set_same(struct vec_data *data)
{
	int cur_vl = data->rdvl();
	int ret;

	ret = prctl(data->prctl_set, cur_vl);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed: %d (%s)\n",
				      data->name, errno, strerror(errno));
		return;
	}

	if (cur_vl != data->rdvl())
		ksft_test_result_pass("%s current VL is %d\n",
				      data->name, ret);
	else
		ksft_test_result_fail("%s prctl() VL %d but RDVL is %d\n",
				      data->name, ret, data->rdvl());
}

/* Can we set a new VL for this process? */
void prctl_set(struct vec_data *data)
{
	int ret;

	if (data->min_vl == data->max_vl) {
		ksft_test_result_skip("%s only one VL supported\n",
				      data->name);
		return;
	}

	/* Try to set the minimum VL */
	ret = prctl(data->prctl_set, data->min_vl);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->min_vl,
				      errno, strerror(errno));
		return;
	}

	if ((ret & PR_SVE_VL_LEN_MASK) != data->min_vl) {
		ksft_test_result_fail("%s prctl set %d but return value is %d\n",
				      data->name, data->min_vl, data->rdvl());
		return;
	}

	if (data->rdvl() != data->min_vl) {
		ksft_test_result_fail("%s set %d but RDVL is %d\n",
				      data->name, data->min_vl, data->rdvl());
		return;
	}

	/* Try to set the maximum VL */
	ret = prctl(data->prctl_set, data->max_vl);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->max_vl,
				      errno, strerror(errno));
		return;
	}

	if ((ret & PR_SVE_VL_LEN_MASK) != data->max_vl) {
		ksft_test_result_fail("%s prctl() set %d but return value is %d\n",
				      data->name, data->max_vl, data->rdvl());
		return;
	}

	/* The _INHERIT flag should not be present when we read the VL */
	ret = prctl(data->prctl_get);
	if (ret == -1) {
		ksft_test_result_fail("%s prctl() read failed: %d (%s)\n",
				      data->name, errno, strerror(errno));
		return;
	}

	if (ret & PR_SVE_VL_INHERIT) {
		ksft_test_result_fail("%s prctl() reports _INHERIT\n",
				      data->name);
		return;
	}

	ksft_test_result_pass("%s prctl() set min/max\n", data->name);
}

/* If we didn't request it a new VL shouldn't affect the child */
void prctl_set_no_child(struct vec_data *data)
{
	int ret, child_vl;

	if (data->min_vl == data->max_vl) {
		ksft_test_result_skip("%s only one VL supported\n",
				      data->name);
		return;
	}

	ret = prctl(data->prctl_set, data->min_vl);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->min_vl,
				      errno, strerror(errno));
		return;
	}

	/* Ensure the default VL is different */
	ret = file_write_integer(data->default_vl_file, data->max_vl);
	if (ret != 0)
		return;

	/* Check that the child has the default we just set */
	child_vl = get_child_rdvl(data);
	if (child_vl != data->max_vl) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      data->max_vl, child_vl);
		return;
	}

	ksft_test_result_pass("%s vector length used default\n", data->name);

	file_write_integer(data->default_vl_file, data->default_vl);
}

/* If we didn't request it a new VL shouldn't affect the child */
void prctl_set_for_child(struct vec_data *data)
{
	int ret, child_vl;

	if (data->min_vl == data->max_vl) {
		ksft_test_result_skip("%s only one VL supported\n",
				      data->name);
		return;
	}

	ret = prctl(data->prctl_set, data->min_vl | PR_SVE_VL_INHERIT);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->min_vl,
				      errno, strerror(errno));
		return;
	}

	/* The _INHERIT flag should be present when we read the VL */
	ret = prctl(data->prctl_get);
	if (ret == -1) {
		ksft_test_result_fail("%s prctl() read failed: %d (%s)\n",
				      data->name, errno, strerror(errno));
		return;
	}
	if (!(ret & PR_SVE_VL_INHERIT)) {
		ksft_test_result_fail("%s prctl() does not report _INHERIT\n",
				      data->name);
		return;
	}

	/* Ensure the default VL is different */
	ret = file_write_integer(data->default_vl_file, data->max_vl);
	if (ret != 0)
		return;

	/* Check that the child inherited our VL */
	child_vl = get_child_rdvl(data);
	if (child_vl != data->min_vl) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      data->min_vl, child_vl);
		return;
	}

	ksft_test_result_pass("%s vector length was inherited\n", data->name);

	file_write_integer(data->default_vl_file, data->default_vl);
}

/* _ONEXEC takes effect only in the child process */
void prctl_set_onexec(struct vec_data *data)
{
	int ret, child_vl;

	if (data->min_vl == data->max_vl) {
		ksft_test_result_skip("%s only one VL supported\n",
				      data->name);
		return;
	}

	/* Set a known value for the default and our current VL */
	ret = file_write_integer(data->default_vl_file, data->max_vl);
	if (ret != 0)
		return;

	ret = prctl(data->prctl_set, data->max_vl);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->min_vl,
				      errno, strerror(errno));
		return;
	}

	/* Set a different value for the child to have on exec */
	ret = prctl(data->prctl_set, data->min_vl | PR_SVE_SET_VL_ONEXEC);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->min_vl,
				      errno, strerror(errno));
		return;
	}

	/* Our current VL should stay the same */
	if (data->rdvl() != data->max_vl) {
		ksft_test_result_fail("%s VL changed by _ONEXEC prctl()\n",
				      data->name);
		return;
	}

	/* Check that the child inherited our VL */
	child_vl = get_child_rdvl(data);
	if (child_vl != data->min_vl) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      data->min_vl, child_vl);
		return;
	}

	ksft_test_result_pass("%s vector length set on exec\n", data->name);

	file_write_integer(data->default_vl_file, data->default_vl);
}

typedef void (*test_type)(struct vec_data *);

test_type tests[] = {
	/*
	 * The default/min/max tests must be first to provide data for
	 * other tests.
	 */
	proc_read_default,
	proc_write_min,
	proc_write_max,

	prctl_get,
	prctl_set,
	prctl_set_no_child,
	prctl_set_for_child,
	prctl_set_onexec,
};

int main(void)
{
	int i, j;

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(tests) * ARRAY_SIZE(vec_data));

	for (i = 0; i < ARRAY_SIZE(vec_data); i++) {
		struct vec_data *data = &vec_data[i];
		int supported = getauxval(data->hwcap_type) & data->hwcap;

		for (j = 0; j < ARRAY_SIZE(tests); j++) {
			if (supported)
				tests[j](data);
			else
				ksft_test_result_skip("%s not supported\n",
						      data->name);
		}
	}

	ksft_exit_pass();
}
