
#include "srvfs.h"

#include <asm/atomic.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

static void srvfs_sb_evict_inode(struct inode *inode)
{
	struct srvfs_fileref *fileref = inode->i_private;

	pr_info("srvfs_evict_inode(): %ld\n", inode->i_ino);
	clear_inode(inode);
	if (fileref)
		srvfs_fileref_put(fileref);
	else
		pr_info("evicting root/dir inode\n");
}

static void srvfs_sb_put_super(struct super_block *sb)
{
	pr_info("srvfs: freeing superblock");
	if (sb->s_fs_info) {
		kfree(sb->s_fs_info);
		sb->s_fs_info = NULL;
	}
}

static const struct super_operations srvfs_super_operations = {
	.statfs		= simple_statfs,
	.evict_inode	= srvfs_sb_evict_inode,
	.put_super	= srvfs_sb_put_super,
};

int srvfs_inode_id (struct super_block *sb)
{
	struct srvfs_sb *priv = sb->s_fs_info;
	return atomic_inc_return(&priv->inode_counter);
}

static int srvfs_fill_super (struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct dentry *root;
	struct srvfs_sb* sbpriv;

	sbpriv = kmalloc(sizeof(struct srvfs_sb), GFP_KERNEL);
	if (sbpriv == NULL)
		goto err_sbpriv;

	atomic_set(&sbpriv->inode_counter, 1);

	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = SRVFS_MAGIC;
	sb->s_op = &srvfs_super_operations;
	sb->s_time_gran = 1;
	sb->s_fs_info = sbpriv;

	inode = new_inode(sb);
	if (!inode)
		goto err_inode;

	/*
	 * because the root inode is 1, the files array must not contain an
	 * entry at index 1
	 */
	inode->i_ino = srvfs_inode_id(sb);
	inode->i_mode = S_IFDIR | 0755;
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_op = &srvfs_rootdir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	set_nlink(inode, 2);
	root = d_make_root(inode);
	if (!root)
		goto err_root;

	sb->s_root = root;

	return 0;

err_root:
	iput(inode);

err_inode:
	kfree(sbpriv);

err_sbpriv:
	return -ENOMEM;
}

struct dentry *srvfs_mount(struct file_system_type *fs_type,
			   int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, srvfs_fill_super);
}

static struct file_system_type srvfs_type = {
	.owner		= THIS_MODULE,
	.name		= "srvfs",
	.mount		= srvfs_mount,
	.kill_sb	= kill_litter_super,
};

static int __init srvfs_init(void)
{
	return register_filesystem(&srvfs_type);
}

static void __exit srvfs_exit(void)
{
	unregister_filesystem(&srvfs_type);
}

module_init(srvfs_init);
module_exit(srvfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Enrico Weigelt, metux IT consult <info@metux.net>");
