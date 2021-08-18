// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
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

	fd = open(self->dev, O_ACCMODE|O_NDELAY);
	EXPECT_EQ(0, ioctl(fd, FDGETDRVPRM, &params));
	params.flags |= FTD_MSG|FD_DEBUG;
	EXPECT_EQ(0, ioctl(fd, FDSETDRVPRM, &params));
	close(fd);
}

FIXTURE_TEARDOWN(floppy)
{
}

TEST_F(floppy, read)
{
	int fd, test;

	fd = open(self->dev, O_RDONLY);
	ASSERT_GT(fd, 0);
	ASSERT_EQ(read(fd, &test, sizeof(test)), sizeof(test));
	ASSERT_EQ(close(fd), 0);
}

TEST_F(floppy, open_write_fail)
{
	ASSERT_LT(open(self->dev, O_WRONLY), 0);
}

TEST_F(floppy, open_rdwr_fail)
{
	ASSERT_LT(open(self->dev, O_RDWR), 0);
}

TEST_F(floppy, ioctl_disk_writable)
{
	int fd;
	struct floppy_drive_struct drive;

	fd = open(self->dev, O_RDONLY|O_NDELAY);
	ASSERT_GT(fd, 0);
	ASSERT_EQ(0, ioctl(fd, FDGETDRVSTAT, &drive));
	ASSERT_FALSE(drive.flags & FD_DISK_WRITABLE);
	ASSERT_EQ(close(fd), 0);
}

TEST_F(floppy, mount)
{
	int fd;
	char test[5] = {};

	mount(self->dev, "/mnt", "vfat", MS_RDONLY, NULL);
	ASSERT_EQ(0, errno);

	fd = open("/mnt/test", O_RDONLY);
	read(fd, &test, sizeof(test));
	ASSERT_EQ(0, strncmp(test, "TEST", 4));
}

TEST_F(floppy, open_ndelay_write_fail)
{
#define TEST_DATA "TEST_FAIL_WRITE"
	int fd;
	char test[] = TEST_DATA;

	fd = open(self->dev, O_RDWR|O_NDELAY);
	ASSERT_GT(fd, 0);

	write(fd, test, sizeof(test));
	read(fd, test, sizeof(test));
	ASSERT_NE(0, strncmp(TEST_DATA, test, sizeof(TEST_DATA)));

	ASSERT_EQ(close(fd), 0);
#undef TEST_DATA
}

TEST_HARNESS_MAIN
