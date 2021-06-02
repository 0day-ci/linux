/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/major.h>
#include <linux/root_dev.h>
#include <linux/init_syscalls.h>

extern int root_mountflags;

void  mount_block_root(char *name, int flags);
void  mount_root(void);
bool  ramdisk_exec_exist(void);

#ifdef CONFIG_INITRAMFS_MOUNT

int   prepare_mount_rootfs(void);
void  finish_mount_rootfs(bool success);

#else

static inline int   prepare_mount_rootfs(void) { return 0; }
static inline void  finish_mount_rootfs(bool success) { }

#endif

static inline __init int create_dev(char *name, dev_t dev)
{
	init_unlink(name);
	return init_mknod(name, S_IFBLK | 0600, new_encode_dev(dev));
}

#ifdef CONFIG_BLK_DEV_RAM

int __init rd_load_disk(int n);
int __init rd_load_image(char *from);

#else

static inline int rd_load_disk(int n) { return 0; }
static inline int rd_load_image(char *from) { return 0; }

#endif

#ifdef CONFIG_BLK_DEV_INITRD

bool __init initrd_load(void);

#else

static inline bool initrd_load(void) { return false; }

#endif
