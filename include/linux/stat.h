/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STAT_H
#define _LINUX_STAT_H


#include <asm/stat.h>
#include <uapi/linux/stat.h>

#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO		(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO		(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)

#define UTIME_NOW	((1l << 30) - 1l)
#define UTIME_OMIT	((1l << 30) - 2l)

#include <linux/types.h>
#include <linux/time.h>
#include <linux/uidgid.h>

struct kstat {
	u32		result_mask;	/* What fields the user got */
	umode_t		mode;
	unsigned int	nlink;
	uint32_t	blksize;	/* Preferred I/O size */
	u64		attributes;
	u64		attributes_mask;
#define KSTAT_ATTR_FS_IOC_FLAGS				\
	(STATX_ATTR_COMPRESSED |			\
	 STATX_ATTR_IMMUTABLE |				\
	 STATX_ATTR_APPEND |				\
	 STATX_ATTR_NODUMP |				\
	 STATX_ATTR_ENCRYPTED |				\
	 STATX_ATTR_VERITY				\
	 )/* Attrs corresponding to FS_*_FL flags */
	u64		ino;
	dev_t		dev;
	dev_t		rdev;
	kuid_t		uid;
	kgid_t		gid;
	loff_t		size;
	struct timespec64 atime;
	struct timespec64 mtime;
	struct timespec64 ctime;
	struct timespec64 btime;			/* File creation time */
	u64		blocks;
	u64		mnt_id;
	/*
	 * BTRFS does not provide unique inode numbers within a filesystem,
	 * depending on a synthetic 'dev' to provide uniqueness.
	 * NFSd cannot make use of this 'dev' number so clients often see
	 * duplicate inode numbers.
	 * For BTRFS, 'ino' is unlikely to use the high bits.  It puts
	 * another number in ino_uniquifier which:
	 * - has most entropy in the high bits
	 * - is different precisely when 'dev' is different
	 * - is stable across unmount/remount
	 * NFSd can xor this with 'ino' to get a substantially more unique
	 * number for reporting to the client.
	 * The ino_uniquifier for a directory can reasonably be applied
	 * to inode numbers reported by the readdir filldir callback.
	 * It is NOT currently exported to user-space.
	 */
	u64		ino_uniquifier;
};

#endif
