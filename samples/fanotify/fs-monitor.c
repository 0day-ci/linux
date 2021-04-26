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

#define FAN_ERROR		0x00100000
#define FAN_PREALLOC_QUEUE      0x00000080

#define FAN_EVENT_INFO_TYPE_LOCATION	4
#define FAN_EVENT_INFO_TYPE_ERROR	5
#define FAN_EVENT_INFO_TYPE_FSDATA	6

struct fanotify_event_info_error {
	struct fanotify_event_info_header hdr;
	int version;
	int error;
	long long unsigned fsid;
};

struct fanotify_event_info_location {
	struct fanotify_event_info_header hdr;
	int line;
	char function[0];
};

struct fanotify_event_info_fsdata {
	struct fanotify_event_info_header hdr;
	char data[0];
};

struct ext4_error_inode_report {
	unsigned long long inode;
	unsigned long long block;
	char desc[40];
};
#endif

static void handle_notifications(char *buffer, int len)
{
	struct fanotify_event_metadata *metadata;
	struct fanotify_event_info_header *hdr = 0;
	char *off, *next;

	for (metadata = (struct fanotify_event_metadata *) buffer;
	     FAN_EVENT_OK(metadata, len); metadata = FAN_EVENT_NEXT(metadata, len)) {
		next = (char*)metadata  + metadata->event_len;
		if (!(metadata->mask == FAN_ERROR)) {
			printf("unexpected FAN MARK: %llx\n", metadata->mask);
			continue;
		}
		if (metadata->fd != FAN_NOFD) {
			printf("bizar fd != FAN_NOFD\n");
			continue;;
		}

		printf("FAN_ERROR found len=%d\n", metadata->event_len);

		for (off = (char*)(metadata+1); off < next; off = off + hdr->len) {
			hdr = (struct fanotify_event_info_header*)(off);

			if (hdr->info_type == FAN_EVENT_INFO_TYPE_ERROR) {
				struct fanotify_event_info_error *error =
					(struct fanotify_event_info_error*) hdr;

				printf("  Generic Error Record: len=%d\n", hdr->len);
				printf("      version: %d\n", error->version);
				printf("      error: %d\n", error->error);
				printf("      fsid: %llx\n", error->fsid);

			} else if(hdr->info_type == FAN_EVENT_INFO_TYPE_LOCATION) {
				struct fanotify_event_info_location *loc =
					(struct fanotify_event_info_location*) hdr;

				printf("  Location Record Size = %d\n", loc->hdr.len);
				printf("      loc=%s:%d\n", loc->function, loc->line);

			} else if(hdr->info_type == FAN_EVENT_INFO_TYPE_FSDATA) {
				struct fanotify_event_info_fsdata *data =
					(struct fanotify_event_info_fsdata *)hdr;
				struct ext4_error_inode_report *fsdata =
					(struct ext4_error_inode_report*) ((char*)data->data);

				printf("  Fsdata Record: len=%d\n", hdr->len);
				printf("      inode=%llu\n", fsdata->inode);
				if (fsdata->block != -1L)
					printf("      block=%llu\n", fsdata->block);
				printf("      desc=%s\n", fsdata->desc);
			}
		}
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

	fd = fanotify_init(FAN_CLASS_NOTIF|FAN_PREALLOC_QUEUE, O_RDONLY);
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
