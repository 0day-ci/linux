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
	/* Treeid can be used to extend the inode number space.  Two inodes
	 * with different 'tree_id' are different, even if 'ino' is the same
	 * (though fs should make ino different as often as possible).
	 * When tree_id is requested and STATX_TREE_ID is set in result_mask,
	 * 'ino' MUST be unique across the filesystem.  Specifically, two
	 * open files that report the same dev, ino, and tree_id MUST be
	 * the same.
	 * If a directory and an object in that directory have the same dev
	 * and tree_id, they can be assumed to be in a meaningful tree, though
	 * the meaning is subject to local interpretation.  The set of inodes
	 * with a common tree_id is not required to be contiguous.
	 */
	u64		tree_id;
};

#endif
