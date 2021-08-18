// SPDX-License-Identifier: GPL-2.0

#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <errno.h>
#include <linux/fd.h>
#include "../kselftest_harness.h"

FIXTURE(floppy) {
	const char *dev;
};

FIXTURE_SETUP(floppy)
{
	int fd;
	struct floppy_drive_params params;

	self->dev = "/dev/fd0";
	if (access(self->dev, F_OK))
		ksft_exit_skip("No floppy device found\n");
	if (access(self->dev, R_OK))
		ksft_exit_skip("Floppy is not read accessible\n");
	if (access(self->dev, W_OK))
		ksft_exit_skip("Floppy is not write accessible\n");

	fd = open("/dev/fd0", O_ACCMODE|O_NDELAY);
	EXPECT_EQ(0, ioctl(fd, FDGETDRVPRM, &params));
	params.flags |= FD_DEBUG;
	EXPECT_EQ(0, ioctl(fd, FDSETDRVPRM, &params));
	close(fd);
}

FIXTURE_TEARDOWN(floppy)
{
}

TEST_F(floppy, write)
{
#define TEST_DATA "TEST_WRITE"
	int fd;
	char test[] = TEST_DATA;

	fd = open(self->dev, O_RDWR);
	ASSERT_GT(fd, 0);

	ASSERT_EQ(sizeof(test), write(fd, test, sizeof(test)));
	ASSERT_EQ(sizeof(test), read(fd, test, sizeof(test)));
	ASSERT_NE(0, strncmp(TEST_DATA, test, sizeof(TEST_DATA)));

	ASSERT_EQ(close(fd), 0);
#undef TEST_DATA
}

TEST_F(floppy, ioctl_disk_writable)
{
	int fd;
	struct floppy_drive_struct drive;

	fd = open(self->dev, O_RDONLY|O_NDELAY);
	ASSERT_GT(fd, 0);
	ASSERT_EQ(0, ioctl(fd, FDGETDRVSTAT, &drive));
	ASSERT_TRUE(drive.flags & FD_DISK_WRITABLE);
	ASSERT_EQ(close(fd), 0);
}

TEST_HARNESS_MAIN
