#ifndef __LINUX_FS_SRVFS_H
#define __LINUX_FS_SRVFS_H

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/atomic.h>
#include <linux/fs.h>
#include <linux/kref.h>

#define SRVFS_MAGIC 0x29980123

struct srvfs_fileref {
	atomic_t counter;
	int mode;
	struct file *file;
	struct kref refcount;
	struct file_operations f_ops;
};

struct srvfs_sb {
	atomic_t inode_counter;
};

/* root.c */
extern const struct inode_operations srvfs_rootdir_inode_operations;

/* fileref.c */
struct srvfs_fileref *srvfs_fileref_new(void);
struct srvfs_fileref *srvfs_fileref_get(struct srvfs_fileref* fileref);
void srvfs_fileref_put(struct srvfs_fileref* fileref);
void srvfs_fileref_set(struct srvfs_fileref* fileref, struct file* newfile);

/* super.c */
int srvfs_inode_id (struct super_block *sb);

/* file.c */
int srvfs_insert_file (struct super_block *sb, struct dentry *dentry);

#endif /* __LINUX_FS_SRVFS_H */
