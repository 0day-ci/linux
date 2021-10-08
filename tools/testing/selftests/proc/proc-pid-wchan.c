// SPDX-License-Identifier: GPL-2.0-only
/*
 * Make sure that wchan returns a reasonable symbol when blocked.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define perror_exit(str) do { perror(str); _exit(1); } while (0)

int main(void)
{
	char buf[64];
	pid_t child;
	int sync[2], fd;

	if (pipe(sync) < 0)
		perror_exit("pipe");

	child = fork();
	if (child < 0)
		perror_exit("fork");
	if (child == 0) {
		/* Child */
		if (close(sync[0]) < 0)
			perror_exit("child close sync[0]");
		if (close(sync[1]) < 0)
			perror_exit("child close sync[1]");
		sleep(10);
		_exit(0);
	}
	/* Parent */
	if (close(sync[1]) < 0)
		perror_exit("parent close sync[1]");
	if (read(sync[0], buf, 1) != 0)
		perror_exit("parent read sync[0]");

	snprintf(buf, sizeof(buf), "/proc/%d/wchan", child);
	fd = open(buf, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return 4;
		perror_exit(buf);
	}

	memset(buf, 0, sizeof(buf));
	if (read(fd, buf, sizeof(buf) - 1) < 1)
		perror_exit(buf);
	if (strstr(buf, "sleep") == NULL) {
		fprintf(stderr, "FAIL: did not find 'sleep' in wchan '%s'\n", buf);
		return 1;
	}
	printf("ok: found 'sleep' in wchan '%s'\n", buf);

	if (kill(child, SIGKILL) < 0)
		perror_exit("kill");
	if (waitpid(child, NULL, 0) != child) {
		fprintf(stderr, "waitpid: got the wrong child!?\n");
		return 1;
	}

	return 0;
}
