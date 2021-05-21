// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021, Collabora Ltd.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/fanotify.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef FAN_ERROR
#define FAN_ERROR		0x00008000
#define FAN_EVENT_INFO_TYPE_ERROR	5

struct fanotify_event_info_error {
	struct fanotify_event_info_header hdr;
	int error;
	__kernel_fsid_t fsid;
	unsigned long inode;
	__u32 error_count;
};
#endif

static void handle_notifications(char *buffer, int len)
{
	struct fanotify_event_metadata *metadata;
	struct fanotify_event_info_error *error;

	for (metadata = (struct fanotify_event_metadata *) buffer;
	     FAN_EVENT_OK(metadata, len); metadata = FAN_EVENT_NEXT(metadata, len)) {
		if (!(metadata->mask == FAN_ERROR)) {
			printf("unexpected FAN MARK: %llx\n", metadata->mask);
			continue;
		} else if (metadata->fd != FAN_NOFD) {
			printf("Unexpected fd (!= FAN_NOFD)\n");
			continue;
		}

		printf("FAN_ERROR found len=%d\n", metadata->event_len);

		error = (struct fanotify_event_info_error *) (metadata+1);
		if (error->hdr.info_type == FAN_EVENT_INFO_TYPE_ERROR) {
			printf("unknown record: %d\n", error->hdr.info_type);
			continue;
		}

		printf("  Generic Error Record: len=%d\n", error->hdr.len);
		printf("      fsid: %llx\n", error->fsid);
		printf("      error: %d\n", error->error);
		printf("      inode: %lu\n", error->inode);
		printf("      error_count: %d\n", error->error_count);
	}
}

int main(int argc, char **argv)
{
	int fd;
	char buffer[BUFSIZ];

	if (argc < 2) {
		printf("Missing path argument\n");
		return 1;
	}

	fd = fanotify_init(FAN_CLASS_NOTIF, O_RDONLY);
	if (fd < 0)
		errx(1, "fanotify_init");

	if (fanotify_mark(fd, FAN_MARK_ADD|FAN_MARK_FILESYSTEM,
			  FAN_ERROR, AT_FDCWD, argv[1])) {
		errx(1, "fanotify_mark");
	}

	while (1) {
		int n = read(fd, buffer, BUFSIZ);

		if (n < 0)
			errx(1, "read");

		handle_notifications(buffer, n);
	}

	return 0;
}
