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
#include <sys/types.h>

#ifndef FAN_FS_ERROR
#define FAN_FS_ERROR		0x00008000
#define FAN_EVENT_INFO_TYPE_ERROR	4

struct fanotify_event_info_error {
	struct fanotify_event_info_header hdr;
	__s32 error;
	__u32 error_count;
};
#endif

#ifndef FILEID_INO32_GEN
#define FILEID_INO32_GEN	1
#endif

#ifndef FILEID_INVALID
#define	FILEID_INVALID		0xff
#endif

static void print_fh(struct file_handle *fh)
{
	int i;
	uint32_t *h = (uint32_t *) fh->f_handle;

	printf("\tfh: ");
	for (i = 0; i < fh->handle_bytes; i++)
		printf("%hhx", fh->f_handle[i]);
	printf("\n");

	printf("\tdecoded fh: ");
	if (fh->handle_type == FILEID_INO32_GEN)
		printf("inode=%u gen=%u\n", h[0], h[1]);
	else if (fh->handle_type == FILEID_INVALID && !fh->handle_bytes)
		printf("Type %d (Superblock error)\n", fh->handle_type);
	else
		printf("Type %d (Unknown)\n", fh->handle_type);

}

static void handle_notifications(char *buffer, int len)
{
	struct fanotify_event_metadata *metadata;
	struct fanotify_event_info_error *error;
	struct fanotify_event_info_fid *fid;
	char *next;

	for (metadata = (struct fanotify_event_metadata *) buffer;
	     FAN_EVENT_OK(metadata, len);
	     metadata = FAN_EVENT_NEXT(metadata, len)) {
		next = (char *)metadata + metadata->event_len;
		if (metadata->mask != FAN_FS_ERROR) {
			printf("unexpected FAN MARK: %llx\n", metadata->mask);
			goto next_event;
		} else if (metadata->fd != FAN_NOFD) {
			printf("Unexpected fd (!= FAN_NOFD)\n");
			goto next_event;
		}

		printf("FAN_FS_ERROR found len=%d\n", metadata->event_len);

		error = (struct fanotify_event_info_error *) (metadata+1);
		if (error->hdr.info_type != FAN_EVENT_INFO_TYPE_ERROR) {
			printf("unknown record: %d (Expecting TYPE_ERROR)\n",
			       error->hdr.info_type);
			goto next_event;
		}

		printf("\tGeneric Error Record: len=%d\n", error->hdr.len);
		printf("\terror: %d\n", error->error);
		printf("\terror_count: %d\n", error->error_count);

		fid = (struct fanotify_event_info_fid *) (error + 1);
		if ((char *) fid >= next) {
			printf("Event doesn't have FID\n");
			goto next_event;
		}
		printf("FID record found\n");

		if (fid->hdr.info_type != FAN_EVENT_INFO_TYPE_FID) {
			printf("unknown record: %d (Expecting TYPE_FID)\n",
			       fid->hdr.info_type);
			goto next_event;
		}
		printf("\tfsid: %x%x\n", fid->fsid.val[0], fid->fsid.val[1]);
		print_fh((struct file_handle *) &fid->handle);

next_event:
		printf("---\n\n");
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

	fd = fanotify_init(FAN_CLASS_NOTIF|FAN_REPORT_FID, O_RDONLY);
	if (fd < 0)
		errx(1, "fanotify_init");

	if (fanotify_mark(fd, FAN_MARK_ADD|FAN_MARK_FILESYSTEM,
			  FAN_FS_ERROR, AT_FDCWD, argv[1])) {
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
