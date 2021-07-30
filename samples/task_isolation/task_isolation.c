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
	int ret, defmask;
	void *buf = malloc(4096);

	memset(buf, 1, 4096);
	ret = mlock(buf, 4096);
	if (ret) {
		perror("mlock");
		return EXIT_FAILURE;
	}

	ret = prctl(PR_ISOL_FEAT, 0, 0, 0, 0);
	if (ret == -1) {
		perror("prctl PR_ISOL_FEAT");
		return EXIT_FAILURE;
	}
	printf("supported features bitmask: 0x%x\n", ret);

	if (!(ret & ISOL_F_QUIESCE)) {
		printf("quiesce feature unsupported, quitting\n");
		return EXIT_FAILURE;
	}

	ret = prctl(PR_ISOL_FEAT, ISOL_F_QUIESCE, 0, 0, 0);
	if (ret == -1) {
		perror("prctl PR_ISOL_FEAT (ISOL_F_QUIESCE)");
		return EXIT_FAILURE;
	}
	printf("supported ISOL_F_QUIESCE bits: 0x%x\n", ret);

	ret = prctl(PR_ISOL_FEAT, ISOL_F_QUIESCE, ISOL_F_QUIESCE_DEFMASK,
		    0, 0);
	if (ret == -1) {
		perror("prctl PR_ISOL_FEAT (ISOL_F_QUIESCE, DEFMASK)");
		return EXIT_FAILURE;
	}

	defmask = ret;
	printf("default ISOL_F_QUIESCE bits: 0x%x\n", defmask);

	/*
	 * Application can either set the value from ISOL_F_QUIESCE_DEFMASK,
	 * which is configurable through
	 * /sys/kernel/task_isolation/default_quiesce, or specific values.
	 *
	 * Using ISOL_F_QUIESCE_DEFMASK allows for the application to
	 * take advantage of future quiescing capabilities without
	 * modification (provided default_quiesce is configured
	 * accordingly).
	 */
	defmask = defmask | ISOL_F_QUIESCE_VMSTATS;

	ret = prctl(PR_ISOL_SET, ISOL_F_QUIESCE, defmask,
		    0, 0);
	if (ret == -1) {
		perror("prctl PR_ISOL_SET");
		return EXIT_FAILURE;
	}

	ret = prctl(PR_ISOL_CTRL_SET, ISOL_F_QUIESCE, 0, 0, 0);
	if (ret == -1) {
		perror("prctl PR_ISOL_CTRL_SET (ISOL_F_QUIESCE)");
		return EXIT_FAILURE;
	}

#define NR_LOOPS 999999999
#define NR_PRINT 100000000
	/* busy loop */
	while (ret < NR_LOOPS)  {
		memset(buf, 0, 4096);
		ret = ret+1;
		if (!(ret % NR_PRINT))
			printf("loops=%d of %d\n", ret, NR_LOOPS);
	}

	ret = prctl(PR_ISOL_CTRL_SET, 0, 0, 0, 0);
	if (ret == -1) {
		perror("prctl PR_ISOL_CTRL_SET (0)");
		exit(0);
	}

	return EXIT_SUCCESS;
}

