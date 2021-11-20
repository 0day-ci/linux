// SPDX-License-Identifier: GPL-2.0
/*
 * This program faults memory in tmpfs
 */

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* Global definitions. */

/* Global variables. */
static const char *self;
static char *shmaddr;
static int shmid;

/*
 * Show usage and exit.
 */
static void exit_usage(void)
{
	printf("Usage: %s -p <path to tmpfs file> -s <size to map>\n", self);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	int fd = 0;
	int key = 0;
	int *ptr = NULL;
	int c = 0;
	int size = 0;
	char path[256] = "";
	int want_sleep = 0, private = 0;
	int populate = 0;
	int write = 0;
	int reserve = 1;

	/* Parse command-line arguments. */
	setvbuf(stdout, NULL, _IONBF, 0);
	self = argv[0];

	while ((c = getopt(argc, argv, ":s:p:")) != -1) {
		switch (c) {
		case 's':
			size = atoi(optarg);
			break;
		case 'p':
			strncpy(path, optarg, sizeof(path));
			break;
		default:
			errno = EINVAL;
			perror("Invalid arg");
			exit_usage();
		}
	}

	printf("%s\n", path);
	if (strncmp(path, "", sizeof(path)) != 0) {
		printf("Writing to this path: %s\n", path);
	} else {
		errno = EINVAL;
		perror("path not found");
		exit_usage();
	}

	if (size != 0) {
		printf("Writing this size: %d\n", size);
	} else {
		errno = EINVAL;
		perror("size not found");
		exit_usage();
	}

	fd = open(path, O_CREAT | O_RDWR, 0777);
	if (fd == -1)
		err(1, "Failed to open file.");

	if (ftruncate(fd, size))
		err(1, "failed to ftruncate %s", path);

	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		close(fd);
		err(1, "Error mapping the file");
	}

	printf("Writing to memory.\n");
	memset(ptr, 1, size);
	printf("Done writing to memory.\n");
	close(fd);

	return 0;
}
