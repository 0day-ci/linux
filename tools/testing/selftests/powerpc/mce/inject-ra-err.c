// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <asm/vas-api.h>

int main(void)
{
	int fd, ret;
	int *paste_addr;
	struct vas_tx_win_open_attr attr;
	char *devname = "/dev/crypto/nx-gzip";

	memset(&attr, 0, sizeof(attr));
	attr.version = 1;
	attr.vas_id = 0;

	fd = open(devname, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Failed to open device %s\n", devname);
		return -errno;
	}
	ret = ioctl(fd, VAS_TX_WIN_OPEN, &attr);
	if (ret < 0) {
		fprintf(stderr, "ioctl() n %d, error %d\n", ret, errno);
		ret = -errno;
		goto out;
	}
	paste_addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0ULL);
	/* The following assignment triggers exception */
	*paste_addr = 1;
	ret = 0;
out:
	close(fd);
	return ret;
}
