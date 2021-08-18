// SPDX-License-Identifier: GPL-2.0

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/reboot.h>

__attribute__((noreturn)) static void poweroff(void)
{
	fflush(stdout);
	fflush(stderr);
	reboot(RB_POWER_OFF);
	sleep(10);
	fprintf(stderr, "\nFailed to power off\n");
	exit(1);
}

static void panic(const char *what)
{
	fprintf(stderr, "\nPANIC %s: %s\n", what, strerror(errno));
	poweroff();
}

int main(int argc, char *argv[])
{
	pid_t pid;

	pid = fork();
	if (pid == -1)
		panic("fork");
	else if (pid == 0) {
		execl("/test", "test", NULL);
		panic("exec");
	}
	if (waitpid(pid, NULL, 0) < 0)
		panic("waitpid");

	poweroff();
	return 1;
}
