/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wait until an inotify event on the given cgroup file.
 */
#include <linux/limits.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char usage[] = "Usage: %s <cgroup_file>\n";
static char *file;

static inline void fail_message(char *msg)
{
	fprintf(stderr, msg, file);
	exit(1);
}

int main(int argc, char *argv[])
{
	char *cmd = argv[0];
	int fd;
	struct pollfd fds = { .events = POLLIN, };

	if (argc != 2) {
		fprintf(stderr, usage, cmd);
		return -1;
	}
	file = argv[1];
	fd = open(file, O_RDONLY);
	if (fd < 0)
		fail_message("Cgroup file %s not found!\n");
	close(fd);

	fd = inotify_init();
	if (fd < 0)
		fail_message("inotify_init() fails on %s!\n");
	if (inotify_add_watch(fd, file, IN_MODIFY) < 0)
		fail_message("inotify_init() fails on %s!\n");
	fds.fd = fd;

	/*
	 * poll waiting loop
	 */
	for (;;) {
		int ret = poll(&fds, 1, 10000);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			exit(1);
		}
		if ((ret > 0) && (fds.revents & POLLIN))
			break;
	}
	close(fd);
	return 0;
}
