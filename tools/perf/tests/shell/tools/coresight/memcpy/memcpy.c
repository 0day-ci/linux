// SPDX-License-Identifier: GPL-2.0
// Carsten Haitzler <carsten.haitzler@arm.com>, 2021
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv)
{
	unsigned long i, len, size;
	unsigned char *src, *dst;
	long long v;

	if (argc < 3) {
		printf("ERR: %s [copysize Kb] [numloops (hundreds)]\n", argv[0]);
		exit(1);
	}

	v = atoll(argv[1]);
	if ((v < 1) || (v > (1024 * 1024))) {
		printf("ERR: max memory 1GB (1048576 KB)\n");
		exit(1);
	}
	size = v;
	v = atoll(argv[2]);
	if ((v < 1) || (v > 40000000000ll)) {
		printf("ERR: loops 1-40000000000 (hundreds)\n");
		exit(1);
	}
	len = v * 100;
	src = malloc(size * 1024);
	dst = malloc(size * 1024);
	if ((!src) || (!dst)) {
		printf("ERR: Can't allocate memory\n");
		exit(1);
	}
	for (i = 0; i < len; i++)
		memcpy(dst, src, size * 1024);
	return 0;
}
