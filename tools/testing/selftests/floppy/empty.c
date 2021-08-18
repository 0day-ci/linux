// SPDX-License-Identifier: GPL-2.0

#include <fcntl.h>
#include <unistd.h>
#include "../kselftest_harness.h"

FIXTURE(floppy) {
	const char *dev;
	int fd;
};

FIXTURE_VARIANT(floppy) {
	int flags;
};

/*
 * See ff06db1efb2a ("floppy: fix open(O_ACCMODE) for ioctl-only open")
 * fdutils use O_ACCMODE for probing and ioctl-only open
 */
FIXTURE_VARIANT_ADD(floppy, ACCMODE) {
	.flags = O_ACCMODE,
};

FIXTURE_VARIANT_ADD(floppy, NACCMODE) {
	.flags = O_ACCMODE|O_NDELAY,
};

FIXTURE_VARIANT_ADD(floppy, NRD) {
	.flags = O_RDONLY|O_NDELAY,
};

FIXTURE_VARIANT_ADD(floppy, NWR) {
	.flags = O_WRONLY|O_NDELAY,
};

FIXTURE_VARIANT_ADD(floppy, NRDWR) {
	.flags = O_RDWR|O_NDELAY,
};

FIXTURE_SETUP(floppy)
{
	self->dev = "/dev/fd0";
	if (access(self->dev, F_OK))
		ksft_exit_skip("No floppy device found\n");
};

FIXTURE_TEARDOWN(floppy)
{
	ASSERT_EQ(close(self->fd), 0);
}

TEST_F(floppy, open)
{
	self->fd = open(self->dev, variant->flags);
	ASSERT_GT(self->fd, 0);
}

TEST_HARNESS_MAIN
