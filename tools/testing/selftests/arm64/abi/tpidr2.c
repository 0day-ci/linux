// SPDX-License-Identifier: GPL-2.0-only

#define SYS_TPIDR2 "S3_3_C13_C0_5"

#define EXPECTED_TESTS 4

static void putstr(const char *str)
{
	write(1, str, strlen(str));
}

static void putnum(unsigned int num)
{
	char c;

	if (num / 10)
		putnum(num / 10);

	c = '0' + (num % 10);
	write(1, &c, 1);
}

static int tests_run;
static int tests_passed;
static int tests_failed;
static int tests_skipped;

static void set_tpidr2(uint64_t val)
{
	asm volatile (
		"msr	" SYS_TPIDR2 ", %0\n"
		:
		: "r"(val)
		: "cc");
}

static uint64_t get_tpidr2(void)
{
	uint64_t val;

	asm volatile (
		"mrs	%0, " SYS_TPIDR2 "\n"
		: "=r"(val)
		:
		: "cc");

	return val;
}

static void print_summary(void)
{
	if (tests_passed + tests_failed + tests_skipped != EXPECTED_TESTS)
		putstr("# UNEXPECTED TEST COUNT: ");

	putstr("# Totals: pass:");
	putnum(tests_passed);
	putstr(" fail:");
	putnum(tests_failed);
	putstr(" xfail:0 xpass:0 skip:");
	putnum(tests_skipped);
	putstr(" error:0\n");
}

/* Processes should start with TPIDR2 == 0 */
static int default_value(void)
{
	return get_tpidr2() == 0;
}

/* If we set TPIDR2 we should read that value */
static int write_read(void)
{
	set_tpidr2(getpid());

	return getpid() == get_tpidr2();
}

/* If we set a value we should read the same value after scheduling out */
static int write_sleep_read(void)
{
	set_tpidr2(getpid());

	msleep(100);

	return getpid() == get_tpidr2();
}

/*
 * If we fork the value in the parent should be unchanged and the
 * child should start with 0 and be able to set its own value.
 */
static int write_fork_read(void)
{
	pid_t newpid, waiting;
	int status;

	set_tpidr2(getpid());

	newpid = fork();
	if (newpid == 0) {
		/* In child */
		if (get_tpidr2() != 0) {
			putstr("# TPIDR2 non-zero in child: ");
			putnum(get_tpidr2());
			putstr("\n");
			exit(0);
		}

		set_tpidr2(getpid());
		if (get_tpidr2() == getpid()) {
			exit(1);
		} else {
			putstr("# Failed to set TPIDR2 in child\n");
			exit(0);
		}
	}
	if (newpid < 0) {
		putstr("# fork() failed: -");
		putnum(-newpid);
		putstr("\n");
		return 0;
	}

	for (;;) {
		waiting = waitpid(newpid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			putstr("# waitpid() failed: ");
			putnum(errno);
			putstr("\n");
			return 0;
		}
		if (waiting != newpid) {
			putstr("# waitpid() returned wrong PID\n");
			return 0;
		}

		if (!WIFEXITED(status)) {
			putstr("# child did not exit\n");
			return 0;
		}

		if (getpid() != get_tpidr2()) {
			putstr("# TPIDR2 corrupted in parent\n");
			return 0;
		}

		return WEXITSTATUS(status);
	}
}

#define run_test(name)			     \
	if (name()) {			     \
		tests_passed++;		     \
	} else {			     \
		tests_failed++;		     \
		putstr("not ");		     \
	}				     \
	putstr("ok ");			     \
	putnum(++tests_run);		     \
	putstr(" " #name "\n");

int main(int argc, char **argv)
{
	int ret, i;

	putstr("TAP version 13\n");
	putstr("1..");
	putnum(EXPECTED_TESTS);
	putstr("\n");

	putstr("# PID: ");
	putnum(getpid());
	putstr("\n");

	/*
	 * This test is run with nolibc which doesn't support hwcap and
	 * it's probably disproportionate to implement so instead check
	 * for the default vector length configuration in /proc.
	 */
	ret = open("/proc/sys/abi/sme_default_vector_length", O_RDONLY, 0);
	if (ret >= 0) {
		run_test(default_value);
		run_test(write_read);
		run_test(write_sleep_read);
		run_test(write_fork_read);
	} else {
		putstr("# SME support not present\n");

		for (i = 0; i < EXPECTED_TESTS; i++) {
			putstr("ok ");
			putnum(i);
			putstr(" skipped, TPIDR2 not supported\n");
		}

		tests_skipped += EXPECTED_TESTS;
	}

	print_summary();

	return 0;
}
