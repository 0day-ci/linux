// SPDX-License-Identifier: GPL-2.0
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <linux/prctl.h>

int main(void)
{
	int ret;
	void *buf = malloc(4096);

	memset(buf, 1, 4096);
	ret = mlock(buf, 4096);
	if (ret) {
		perror("mlock");
		return EXIT_FAILURE;
	}

	ret = prctl(PR_ISOL_SET, PR_ISOL_MODE, PR_ISOL_MODE_NORMAL, 0, 0);
	if (ret == -1) {
		perror("prctl PR_ISOL_SET");
		return EXIT_FAILURE;
	}

	ret = prctl(PR_ISOL_ENTER, 0, 0, 0, 0);
	if (ret == -1) {
		perror("prctl PR_ISOL_ENTER");
		exit(0);
	}

	/* busy loop */
	while (ret < 99999999) {
		memset(buf, 0, 10);
		ret = ret+1;
	}

	ret = prctl(PR_ISOL_EXIT, 0, 0, 0, 0);
	if (ret == -1) {
		perror("prctl PR_ISOL_EXIT");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

